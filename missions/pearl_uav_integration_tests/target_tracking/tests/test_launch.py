#!/usr/bin/env python3
"""Configuration-only checks; never connect to a drone or launch MOOS."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

MISSION = Path(__file__).resolve().parents[1]


class LaunchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="target-tracking-config-")
        self.directory = Path(self.temp.name)
        for name in ("launch.sh", "clean.sh", "meta_target_tracking.moos"):
            shutil.copy2(MISSION / name, self.directory / name)

    def tearDown(self):
        self.temp.cleanup()

    def generate(self, *flags):
        return subprocess.run(
            [str(self.directory / "launch.sh"), "--just_make", *flags],
            cwd=self.directory,
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )

    def config(self):
        return (self.directory / "targ_target_tracking.moos").read_text()

    def test_default_is_single_community_and_passive(self):
        result = self.generate()
        self.assertEqual(result.returncode, 0, result.stderr)
        config = self.config()
        self.assertIn("Community  = target_tracking", config)
        self.assertEqual(config.count("ProcessConfig = MOOSDB"), 0)
        self.assertIn("Run = MOOSDB", config)
        self.assertIn("Run = pArduBridge", config)
        self.assertIn("Run = pVideoRecorder", config)
        self.assertIn("Run = pLandingTargetViewer", config)
        self.assertIn("Run = pWebSocketServer", config)
        self.assertIn("WSPort = 9090", config)
        self.assertIn("AllowSubmissions = false", config)
        self.assertIn("passive = true", config)
        self.assertIn("projection_mode = relative", config)
        self.assertIn("relative_anchor_x = 0", config)
        self.assertIn("relative_anchor_y = 0", config)
        self.assertIn("url          = 100.112.119.114:14550", config)
        self.assertIn("url_protocol = udp", config)
        self.assertIn("QGCParams = --endpoint=udp:100.112.119.114:14550", config)
        self.assertIn("Run = qgc-vnc-session", config)
        self.assertIn("scope = UAV_RANGEFINDER_DISTANCE", config)
        self.assertIn("scope = UAV_RANGEFINDER_ORIENTATION_PITCH", config)
        self.assertIn("Run = moos-vnc-session", config)
        self.assertIn("api_url = 100.112.119.114:9997", config)
        self.assertIn(
            "record_root = /var/lib/landmark-recorder/mission-video", config
        )
        self.assertIn("recording_id_prefix = recording", config)
        self.assertIn("segment_duration = 1m", config)
        self.assertIn("path = fpv:oak", config)
        self.assertIn("path = nadir:main", config)
        self.assertNotIn("path = nadir:lores", config)
        self.assertIn("record_proxy = fpv:pearl-recorder-fpv", config)
        self.assertIn("record_proxy = nadir:pearl-recorder-nadir", config)
        self.assertNotIn("activation =", config)
        self.assertNotIn("api_user", config)
        self.assertNotIn("api_password_file", config)
        self.assertIn(
            "button_one = RECORD-START # VIDEO_RECORD_COMMAND=START", config
        )
        self.assertIn(
            "button_two = RECORD-STOP  # VIDEO_RECORD_COMMAND=STOP", config
        )
        self.assertIn("scope = VIDEO_RECORD_ID", config)
        self.assertIn("scope = VIDEO_RECORD_LOCATION", config)
        self.assertIn("null_tiff = true", config)
        self.assertNotIn("tiff_file =", config)
        self.assertIn("hash_viewable = true", config)
        self.assertIn("hash_delta = 10", config)
        self.assertIn("center_view = average", config)
        self.assertIn("vehicles_name_mode = names", config)
        self.assertFalse(any(self.directory.glob("*.bhv")))

        for forbidden in (
            "pHelmIvP",
            "pMavlinkLandingTarget",
            "pShare",
            "uFldNodeBroker",
            "ARM_UAV",
            "ARDU_COMMAND",
            "DO_TAKEOFF",
            "AUTOLAND",
            "FLY_WAYPOINT",
        ):
            self.assertNotIn(forbidden, config)

    def test_overrides_and_headless_generation(self):
        result = self.generate(
            "--nogui",
            "--mport=9100",
            "--udp_bind=192.168.10.20",
            "--udp_port=14600",
            "--lat_origin=42.1",
            "--lon_origin=-71.2",
            "--target_size=0.75",
            "--observation_timeout=1.25",
            "--video-api=192.168.10.30:19997",
            "--video-record-root=/srv/target-recordings",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        config = self.config()
        self.assertIn("ServerPort = 9100", config)
        self.assertIn("url          = 192.168.10.20:14600", config)
        self.assertIn("LatOrigin    = 42.1", config)
        self.assertIn("LongOrigin   = -71.2", config)
        self.assertIn("target_size = 0.75", config)
        self.assertIn("observation_timeout = 1.25", config)
        self.assertIn("api_url = 192.168.10.30:19997", config)
        self.assertIn("record_root = /srv/target-recordings", config)
        self.assertNotIn("Run = moos-vnc-session", config)
        self.assertNotIn("Run = qgc-vnc-session", config)
        self.assertFalse((self.directory / "LOG_antler").exists())
        self.assertTrue(os.access(self.directory / "launch.sh", os.X_OK))

    def test_rejects_invalid_arguments(self):
        invalid = (
            ("--udp_bind=localhost",),
            ("--udp_bind=300.1.1.1",),
            ("--udp_port=0",),
            ("--udp_port=70000",),
            ("--mport=abc",),
            ("--lat_origin=91",),
            ("--lon_origin=-181",),
            ("--target_size=0",),
            ("--observation_timeout=-1",),
            ("--video-api=http://100.112.119.114:9997",),
            ("--video-api=100.112.119.114:0",),
            ("--video-record-root=relative/path",),
            ("--typo",),
            ("1",),
        )
        for flags in invalid:
            with self.subTest(flags=flags):
                self.assertNotEqual(self.generate(*flags).returncode, 0)


if __name__ == "__main__":
    unittest.main()
