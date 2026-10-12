#!/usr/bin/env python3
"""Protect task-scoped UAV video recording and mode policy."""

import os
import subprocess
import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]
TARGET = MISSION / "targ_uav.moos"


class VideoRecordingContract(unittest.TestCase):
    def launch(self, *arguments, success=True):
        environment = os.environ.copy()
        environment.pop("XDG_CONFIG_HOME", None)
        result = subprocess.run(
            [str(MISSION / "launch_uav.sh"), *arguments, "--just_make"],
            cwd=MISSION,
            env=environment,
            capture_output=True,
            text=True,
            timeout=90,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return TARGET.read_text()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def test_real_always_installs_task_driven_recorder(self):
        config = self.launch("--mode=REAL")
        self.assertIn("Run = pVideoRecorder", config)
        recorder = config.split("ProcessConfig = pVideoRecorder", 1)[1].split("}", 1)[0]
        self.assertIn("api_url           = 100.112.119.114:9997", recorder)
        self.assertNotIn("api_user", recorder)
        self.assertNotIn("api_password_file", recorder)
        self.assertIn(
            "record_root       = /var/lib/landmark-recorder/mission-video", recorder
        )
        self.assertIn("recording_id_var  = UAV_ROUTE_ACTIVE_ID", recorder)
        self.assertIn("segment_duration  = 1m", recorder)
        self.assertIn("path = fpv:oak", recorder)
        self.assertNotIn("oak-record", recorder)
        self.assertIn("path = nadir:main", recorder)
        self.assertNotIn("path = nadir:lores", recorder)
        self.assertIn("record_proxy = fpv:pearl-recorder-fpv", recorder)
        self.assertIn("record_proxy = nadir:pearl-recorder-nadir", recorder)
        self.assertNotIn("activation =", recorder)
        self.assertIn(
            "effect = machine=operation, state=TAKEOFF_OPERATIONS, phase=entry, "
            "var=VIDEO_RECORD_COMMAND, type=string, value=START",
            config,
        )
        self.assertIn(
            "effect = machine=operation, state=IDLE, phase=entry, "
            "var=VIDEO_RECORD_COMMAND, type=string, value=STOP",
            config,
        )
        self.assertIn("bridge = src=VIDEO_RECORD_HOST", config)
        self.assertIn("bridge = src=VIDEO_RECORD_DIRECTORY", config)
        self.assertIn("bridge = src=VIDEO_RECORD_LOCATION", config)
        self.assertIn("bridge = src=VIDEO_RECORD_ID", config)
        self.assertIn("bridge = src=VIDEO_RECORD_SEGMENT_DURATION", config)
        self.assertNotIn(
            "state=OFFNOMINAL, phase=entry, var=VIDEO_RECORD_COMMAND",
            config,
        )

    def test_real_cannot_disable_recorder(self):
        output = self.launch("--mode=REAL", "--no-video-recording", success=False)
        self.assertIn("required in REAL mode", output)

    def test_sitl_is_disabled_by_default_and_can_opt_in(self):
        config = self.launch("--mode=SITL")
        self.assertNotIn("Run = pVideoRecorder", config)
        self.assertNotIn("ProcessConfig = pVideoRecorder", config)

        config = self.launch("--mode=SITL", "--video-recording")
        self.assertIn("Run = pVideoRecorder", config)
        self.assertIn("api_url           = 100.112.119.114:9997", config)
        self.assertNotIn("api_user", config)
        self.assertNotIn("api_password_file", config)

    def test_shoreside_keeps_manual_recovery_command_route(self):
        shore = (MISSION / "meta_shoreside.moos").read_text()
        self.assertIn(
            "output = src_name=VIDEO_RECORD_COMMAND, route=$(UAV_IP):$(UAV_PSHARE)",
            shore,
        )
        self.assertNotIn("VIDEO_RECORD_COMMAND=START", shore)
        self.assertIn("Normal START/STOP is generated", shore)


if __name__ == "__main__":
    unittest.main()
