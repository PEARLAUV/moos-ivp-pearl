#!/usr/bin/env python3
"""Real isolated MOOS yaw gate: FC ACK is not measured alignment."""

import math
import os
import re
import shlex
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

import test_landing_checks as fixture


class YawTests(fixture.LandingChecksTest):
    @classmethod
    def setUpClass(cls):
        cls.helper_temp = tempfile.TemporaryDirectory(prefix="native-pose-fixture-")
        cls.addClassCleanup(cls.helper_temp.cleanup)
        build = fixture.ROOT / "build/src/pLandingCoordinator/CMakeFiles/pLandingCoordinator.dir"
        flags = (build / "flags.make").read_text().split("CXX_INCLUDES = ", 1)[1].splitlines()[0]
        link = shlex.split((build / "link.txt").read_text())
        library = next(x for x in link if x.endswith("/libMOOS.a"))
        cls.pose_helper = Path(cls.helper_temp.name) / "mock_pose"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", *shlex.split(flags),
                        str(Path(__file__).with_name("mock_pose.cpp")), library,
                        "-pthread", "-o", str(cls.pose_helper)], check=True, timeout=30)

    def native_pose(self, *values):
        port = re.search(r"ServerPort = (\d+)", self.mission.read_text())
        assert port is not None
        subprocess.run([str(self.pose_helper), port[1], *values], check=True,
                       capture_output=True, timeout=10)

    def setUp(self):
        self.extra_config = ""
        self.extra_apps = f"Run = {fixture.MOOS_BIN / 'uTimerScript'} @ NewConsole = false ~ uAttitudeAge"
        # Only this isolated fixture emits mock native ages. Static poses remain
        # under explicit test control; SAMPLE_TIME is never renewed by this timer.
        self.extra_process_config = """ProcessConfig = uAttitudeAge
{
  AppTick = 20
  CommsTick = 20
  condition = TEST_REFRESH_ATTITUDE=true
  paused = false
  reset_max = unlimited
  reset_time = 0.05
  event = var=UAV_ATTITUDE_AGE,val=0.1,time=0
}
"""
        if self._testMethodName.startswith("test_yaw"):
            timeout = 8 if self._testMethodName == "test_yaw_timeout" else 60
            self.extra_config = f"align_yaw=true\nyaw_tolerance=5\nyaw_timeout={timeout}"
            if "_direct_" in self._testMethodName:
                self.extra_config += "\nobservation_pose_samples_var=TEST_POSES"
        super().setUp()

    def prepare(self, yaw=359, relative=2):
        self.poke("ARDU_COMMAND:=NONE", "RECOVER:=true")
        session = self.query("LANDING_OWN_PHASE=REQUESTED", variable="LANDING_OWN_SESSION")
        started = time.time() - 1
        self.peer(session, started, "AGREED", "IDLE")
        self.query("LANDING_OWN_PHASE=RENDEZVOUS")
        self.poke("RENDEZVOUS_OWN_SESSION:=new-rv")
        self.poke("RENDEZVOUS_OWN_PHASE:=COMPLETE")
        self.peer(session, started, "CLEARED")
        self.poke("UAV_HEALTH_ALL_OK=1", "UAV_HEALTH_AGE=0.1", "UAV_IS_ARMED=1",
                  "UAV_LANDED_STATE_AVAILABLE=1", "UAV_LANDED_STATE:=IN_AIR",
                  "UAV_LANDED_STATE_AGE=0.1", "UAV_GUIDED_MODE_ACTIVE=1",
                  "UAV_LANDING_TARGET_AVAILABLE=1",
                  "UAV_LANDING_TARGET_POSITION_VALID=1", "UAV_LANDING_TARGET_FRAME=12",
                  "TEST_REFRESH_TARGET:=true")
        self.pose(yaw, relative)
        request = self.query("LANDING_OWN_PHASE=RENDEZVOUS", "ARDU_COMMAND=NONE",
                             variable="YAW_ALIGN_REQUEST")
        fields = dict(part.split("=", 1) for part in request.split("#"))
        return fields

    def pose(self, yaw, relative):
        half = math.radians(relative) / 2
        stamp = int(time.time() * 1e6)
        self.poke("TEST_REFRESH_ATTITUDE:=true")
        direct = ([f"TEST_POSES:={stamp},0,0,{yaw},42,-71,8,@MOOSTIME,{stamp}"]
                  if "_direct_" in self._testMethodName else [])
        self.native_pose(*direct, f"UAV_YAW={yaw}", "UAV_ROLL=0", "UAV_PITCH=0",
                  "UAV_ATTITUDE_AVAILABLE=1",
                  "UAV_ATTITUDE_SAMPLE_TIME=@MOOSTIME",
                  f"UAV_ATTITUDE_TIME_USEC={stamp}", f"UAV_LANDING_TARGET_TIME_USEC={stamp}",
                  f"UAV_LANDING_TARGET_Q_W={math.cos(half):.12f}", "UAV_LANDING_TARGET_Q_X=0",
                  "UAV_LANDING_TARGET_Q_Y=0", f"UAV_LANDING_TARGET_Q_Z={math.sin(half):.12f}",
                  "UAV_ATTITUDE_AGE=0.1")

    def ack(self, fields, status="ACCEPTED", identifier=None):
        self.poke(f"UAV_YAW_ALIGN_RESULT:=id={identifier or fields['id']}"
                  f"#heading={fields['heading']}#status={status}#detail=FC_ACK")

    def test_yaw_requires_matching_ack_then_new_fresh_measured_pose(self):
        fields = self.prepare()
        self.assertAlmostEqual(float(fields["heading"]), 1, delta=0.001)
        self.ack(fields, identifier="old-session")
        self.query("LANDING_OWN_PHASE=RENDEZVOUS", "ARDU_COMMAND=NONE")
        self.ack(fields)
        self.query("LANDING_OWN_PHASE=RENDEZVOUS", "ARDU_COMMAND=NONE")
        # A fresh receive timestamp with an old native age cannot satisfy alignment.
        self.poke("TEST_REFRESH_ATTITUDE:=false")
        self.poke("UAV_YAW=1", "UAV_ATTITUDE_AGE=10")
        self.query("LANDING_OWN_YAW_STATE=WAITING_FOR_ORIENTATION", "ARDU_COMMAND=NONE")
        self.pose(1, 0)
        self.query("LANDING_OWN_YAW_STATE=ALIGNED", "LANDING_OWN_PHASE=COMMANDING",
                   "ARDU_COMMAND=AUTOLAND")

    def test_yaw_rejection_fails_closed(self):
        fields = self.prepare(yaw=100, relative=180)
        self.ack(fields, status="REJECTED")
        self.query("LANDING_OWN_PHASE=ABORTED", "LANDING_OWN_REASON=yaw_alignment_rejected",
                   "ARDU_COMMAND=NONE")

    def test_yaw_changed_target_fails_closed(self):
        fields = self.prepare(yaw=100, relative=180)
        self.ack(fields)
        self.pose(100, 160)
        self.query("LANDING_OWN_PHASE=ABORTED", "LANDING_OWN_REASON=yaw_target_heading_changed",
                   "ARDU_COMMAND=NONE")

    def test_yaw_cancel_does_not_land_on_late_ack(self):
        fields = self.prepare()
        self.poke("LANDING_CANCEL:=true")
        self.ack(fields)
        self.pose(1, 0)
        self.query("LANDING_OWN_PHASE=ABORTED", "ARDU_COMMAND=NONE")

    def test_yaw_native_window_matches_exposure_not_latest_scalar(self):
        fields = self.prepare()
        self.ack(fields)
        stamp = int(time.time() * 1e6)
        self.native_pose(f"UAV_ATTITUDE_SAMPLES={stamp},0,0,1,@MOOSTIME;"
                         f"{stamp+1},0,0,1,@MOOSTIME",
                         f"UAV_LANDING_TARGET_TIME_USEC={stamp}",
                         "UAV_LANDING_TARGET_Q_W=1", "UAV_LANDING_TARGET_Q_Z=0",
                         "UAV_YAW=200", f"UAV_ATTITUDE_TIME_USEC={stamp+1}")
        self.query("LANDING_OWN_YAW_STATE=ALIGNED", "ARDU_COMMAND=AUTOLAND")

    def test_yaw_unknown_window_timestamp_is_not_guessed(self):
        self.prepare()
        stamp = int(time.time() * 1e6)
        self.native_pose(f"UAV_ATTITUDE_SAMPLES={stamp},0,0,1,@MOOSTIME",
                         f"UAV_LANDING_TARGET_TIME_USEC={stamp-1}")
        # A slow query may also see the deliberately single mock pose age out.
        # Neither missing exact timestamp nor subsequent staleness may land.
        self.query("LANDING_OWN_YAW_STATE=WAITING_FOR_ORIENTATION", "ARDU_COMMAND=NONE")

    def test_yaw_direct_pose_matches_exposure_without_borrowing_measured_yaw(self):
        fields = self.prepare()
        self.ack(fields)
        stamp = int(time.time()*1e6)
        half = math.radians(-89)/2
        # Exposure yaw90 + quaternion -89 => target true yaw1. Measured post-ACK
        # yaw remains1 from the independent bridge, not exposure90.
        self.native_pose(f"TEST_POSES:={stamp},0,0,90,42,-71,8,@MOOSTIME,{stamp}",
                         f"UAV_LANDING_TARGET_TIME_USEC={stamp}", "UAV_YAW=1",
                         "UAV_ATTITUDE_SAMPLE_TIME=@MOOSTIME",
                         f"UAV_ATTITUDE_TIME_USEC={stamp+1}",
                         f"UAV_LANDING_TARGET_Q_W={math.cos(half)}",
                         f"UAV_LANDING_TARGET_Q_Z={math.sin(half)}")
        self.query("LANDING_OWN_YAW_STATE=ALIGNED", "ARDU_COMMAND=AUTOLAND")

    def test_yaw_direct_missing_pose_does_not_fallback_to_bridge_history(self):
        self.prepare()
        stamp = int(time.time()*1e6)
        self.native_pose("TEST_POSES:=invalid",
                         f"UAV_ATTITUDE_SAMPLES={stamp},0,0,359,@MOOSTIME",
                         f"UAV_LANDING_TARGET_TIME_USEC={stamp}")
        self.query("LANDING_OWN_YAW_STATE=WAITING_FOR_ORIENTATION", "ARDU_COMMAND=NONE")

    def test_yaw_timeout(self):
        self.prepare(yaw=100, relative=180)
        time.sleep(8)
        self.query("LANDING_OWN_PHASE=ABORTED", "LANDING_OWN_REASON=yaw_alignment_timeout",
                   "ARDU_COMMAND=NONE")


if __name__ == "__main__":
    unittest.main()
