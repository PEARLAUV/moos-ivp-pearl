#!/usr/bin/env python3
"""Exercise target-relative yaw alignment with the same contract used on REAL."""

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
        cls.helper_temp = tempfile.TemporaryDirectory(prefix="relative-yaw-fixture-")
        cls.addClassCleanup(cls.helper_temp.cleanup)
        build = fixture.ROOT / "build/src/pLandingCoordinator/CMakeFiles/pLandingCoordinator.dir"
        flags = (build / "flags.make").read_text().split("CXX_INCLUDES = ", 1)[1].splitlines()[0]
        link = shlex.split((build / "link.txt").read_text())
        library = next(item for item in link if item.endswith("/libMOOS.a"))
        cls.pose_helper = Path(cls.helper_temp.name) / "mock_pose"
        subprocess.run(
            [os.environ.get("CXX", "c++"), "-std=c++11", *shlex.split(flags),
             str(Path(__file__).with_name("mock_pose.cpp")), library, "-pthread",
             "-o", str(cls.pose_helper)],
            check=True,
            timeout=30,
        )

    def native_pose(self, *values):
        port = re.search(r"ServerPort = (\d+)", self.mission.read_text())
        assert port is not None
        subprocess.run(
            [str(self.pose_helper), port[1], *values],
            check=True,
            capture_output=True,
            timeout=10,
        )

    def setUp(self):
        self.extra_apps = (
            f"Run = {fixture.MOOS_BIN / 'uTimerScript'} @ NewConsole = false ~ uAttitudeAge"
        )
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
        self.extra_config = ""
        if self._testMethodName.startswith("test_yaw_"):
            timeout = 2 if self._testMethodName == "test_yaw_timeout" else 60
            self.extra_config = (
                "align_yaw=true\n"
                "yaw_tolerance=5\n"
                "yaw_stability_time=0.1\n"
                "yaw_stability_tolerance=3\n"
                "yaw_hold_time=0.1\n"
                "yaw_max_tilt=15\n"
                f"yaw_timeout={timeout}"
            )
        self._stamp = int(time.time() * 1e6)
        super().setUp()

    def start(self):
        self.poke("ARDU_COMMAND:=NONE", "RECOVER:=true")
        session = self.query("LANDING_OWN_PHASE=REQUESTED", variable="LANDING_OWN_SESSION")
        started = time.time() - 1
        self.peer(session, started, "AGREED", "IDLE")
        self.query("LANDING_OWN_PHASE=RENDEZVOUS")
        self.poke("RENDEZVOUS_OWN_SESSION:=new-rv")
        self.poke("RENDEZVOUS_OWN_PHASE:=COMPLETE")
        self.peer(session, started, "CLEARED")
        self.poke(
            "UAV_HEALTH_ALL_OK=1",
            "UAV_HEALTH_AGE=0.1",
            "UAV_IS_ARMED=1",
            "UAV_LANDED_STATE_AVAILABLE=1",
            "UAV_LANDED_STATE:=IN_AIR",
            "UAV_LANDED_STATE_AGE=0.1",
            "UAV_GUIDED_MODE_ACTIVE=1",
            "UAV_LANDING_TARGET_AVAILABLE=1",
            "UAV_LANDING_TARGET_POSITION_VALID=1",
            "UAV_LANDING_TARGET_FRAME=12",
            "UAV_LANDING_TARGET_TARGET_NUM=0",
            "TEST_REFRESH_TARGET:=true",
            "TEST_REFRESH_ATTITUDE:=true",
        )

    def pose(self, relative, *, roll=0, pitch=0, stamp=None):
        half = math.radians(relative) / 2
        if stamp is None:
            self._stamp += 1
            stamp = self._stamp
        self.native_pose(
            f"UAV_ROLL={roll}",
            f"UAV_PITCH={pitch}",
            "UAV_ATTITUDE_AVAILABLE=1",
            "UAV_ATTITUDE_AGE=0.1",
            f"UAV_LANDING_TARGET_TIME_USEC={stamp}",
            f"UAV_LANDING_TARGET_Q_W={math.cos(half):.12f}",
            "UAV_LANDING_TARGET_Q_X=0",
            "UAV_LANDING_TARGET_Q_Y=0",
            f"UAV_LANDING_TARGET_Q_Z={math.sin(half):.12f}",
        )
        return stamp

    def observations(self, relative, count=4, **kwargs):
        for _ in range(count):
            self.pose(relative, **kwargs)
            time.sleep(0.06)

    def prepare(self, relative=20):
        self.start()
        self.observations(relative)
        request = self.query(
            "LANDING_OWN_PHASE=RENDEZVOUS",
            "ARDU_COMMAND=NONE",
            variable="YAW_ALIGN_REQUEST",
        )
        assert request is not None
        return dict(part.split("=", 1) for part in request.split("#"))

    def ack(self, fields, status="ACCEPTED", identifier=None):
        self.poke(
            f"UAV_YAW_ALIGN_RESULT:=id={identifier or fields['id']}"
            f"#angle={fields['angle']}#status={status}#detail=FC_ACK"
        )

    def confirm(self, fields):
        self.ack(fields)
        self.observations(0)
        self.query(
            "LANDING_OWN_YAW_STATE=ALIGNED",
            "LANDING_OWN_PHASE=COMMANDING",
            "ARDU_COMMAND=AUTOLAND",
        )

    def test_yaw_requires_matching_ack_then_fresh_target_confirmation(self):
        fields = self.prepare()
        self.assertAlmostEqual(float(fields["angle"]), 20, delta=0.001)
        self.ack(fields, identifier="old-session")
        self.query("LANDING_OWN_PHASE=RENDEZVOUS", "ARDU_COMMAND=NONE")
        self.ack(fields)
        self.query("LANDING_OWN_YAW_STATE=WAITING_FOR_POST_ACK_TARGET", "ARDU_COMMAND=NONE")
        self.observations(0)
        self.query(
            "LANDING_OWN_YAW_STATE=ALIGNED",
            "LANDING_OWN_PHASE=COMMANDING",
            "ARDU_COMMAND=AUTOLAND",
        )

    def test_yaw_rejection_fails_closed(self):
        fields = self.prepare(relative=170)
        self.ack(fields, status="REJECTED")
        self.query(
            "LANDING_OWN_PHASE=ABORTED",
            "LANDING_OWN_REASON=yaw_alignment_rejected",
            "ARDU_COMMAND=NONE",
        )

    def test_yaw_live_relative_error_must_converge_after_ack(self):
        fields = self.prepare(relative=90)
        self.ack(fields)
        self.observations(30)
        self.query("LANDING_OWN_YAW_STATE=ALIGNING", "ARDU_COMMAND=NONE")
        self.observations(0)
        self.query("LANDING_OWN_YAW_STATE=ALIGNED", "ARDU_COMMAND=AUTOLAND")

    def test_yaw_cancel_does_not_land_on_late_ack(self):
        fields = self.prepare()
        self.poke("LANDING_CANCEL:=true")
        self.ack(fields)
        self.observations(0)
        self.query("LANDING_OWN_PHASE=ABORTED", "ARDU_COMMAND=NONE")

    def test_yaw_nonadvancing_observation_cannot_confirm_alignment(self):
        fields = self.prepare()
        self.ack(fields)
        repeated = self.pose(0)
        self.observations(0, stamp=repeated)
        self.query("LANDING_OWN_YAW_STATE=CONFIRMING_ALIGNMENT", "ARDU_COMMAND=NONE")
        self.observations(0)
        self.query("LANDING_OWN_YAW_STATE=ALIGNED", "ARDU_COMMAND=AUTOLAND")

    def test_yaw_unstable_target_orientation_is_not_commanded(self):
        self.start()
        for relative in (40, 55, 35, 50):
            self.pose(relative)
            time.sleep(0.06)
        self.query("LANDING_OWN_YAW_STATE=STABILIZING_TARGET_ORIENTATION", "ARDU_COMMAND=NONE")
        self.observations(40)
        request = self.query(variable="YAW_ALIGN_REQUEST")
        assert request is not None
        fields = dict(part.split("=", 1) for part in request.split("#"))
        self.assertAlmostEqual(float(fields["angle"]), 40, delta=0.001)

    def test_yaw_already_aligned_requires_observation_hold_but_no_fc_command(self):
        self.start()
        self.observations(0)
        self.query(
            "LANDING_OWN_YAW_STATE=ALIGNED",
            "LANDING_OWN_PHASE=COMMANDING",
            "ARDU_COMMAND=AUTOLAND",
        )

    def test_yaw_excessive_tilt_blocks_relative_yaw(self):
        self.start()
        self.observations(30, roll=12, pitch=12)
        self.query(
            "LANDING_OWN_YAW_STATE=WAITING_FOR_ORIENTATION",
            "LANDING_OWN_YAW_INPUT_REASON=aircraft_tilt_exceeds_relative_yaw_limit",
            "ARDU_COMMAND=NONE",
        )

    def test_yaw_timeout(self):
        self.start()
        self.pose(170)
        time.sleep(2)
        self.query(
            "LANDING_OWN_PHASE=ABORTED",
            "LANDING_OWN_REASON=yaw_alignment_timeout",
            "ARDU_COMMAND=NONE",
        )


if __name__ == "__main__":
    unittest.main()
