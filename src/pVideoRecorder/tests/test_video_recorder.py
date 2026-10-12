#!/usr/bin/env python3
"""End-to-end MOOS and HTTP contract test for pVideoRecorder."""

import base64
import http.server
import json
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MOOS_BIN = ROOT.parent / "moos-ivp/bin"
APP = ROOT / "bin/pVideoRecorder"


class ApiHandler(http.server.BaseHTTPRequestHandler):
    requests = []
    fail_start_path: str | None = None
    lock = threading.Lock()

    def record_request(self, method, missing=False):
        length = int(self.headers.get("Content-Length", "0"))
        raw_body = self.rfile.read(length) if length else b""
        parsed_body = json.loads(raw_body.decode()) if raw_body else None
        with self.lock:
            self.requests.append(
                {
                    "method": method,
                    "path": self.path,
                    "body": parsed_body,
                    "authorization": self.headers.get("Authorization"),
                }
            )
        fail = (
            self.path == self.fail_start_path
            and parsed_body is not None
            and parsed_body.get("record") is True
        )
        payload = b'{"status":"error"}' if fail else b'{"status":"ok"}'
        status = 500 if fail else (404 if missing else 200)
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(payload)

    def do_PATCH(self):  # noqa: N802 - BaseHTTPRequestHandler API
        self.record_request("PATCH")

    def do_POST(self):  # noqa: N802 - BaseHTTPRequestHandler API
        self.record_request("POST")

    def do_DELETE(self):  # noqa: N802 - BaseHTTPRequestHandler API
        self.record_request("DELETE", missing=True)

    def log_message(self, format, *args):  # noqa: A002 - base-class signature
        del format, args


class VideoRecorderTest(unittest.TestCase):
    def setUp(self):
        if not APP.exists():
            self.skipTest(f"{APP} is not built")

        ApiHandler.requests = []
        ApiHandler.fail_start_path = None
        self.directory = Path(tempfile.mkdtemp(prefix="video-recorder-"))
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), ApiHandler)
        self.server_thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.server_thread.start()

        password = self.directory / "api.password"
        password.write_text("test-secret\n")
        self.mission = self.directory / "test.moos"
        self.mission.write_text(
            f"""ServerHost = 127.0.0.1
ServerPort = 0
Community = video_test

ProcessConfig = pVideoRecorder
{{
  AppTick = 20
  CommsTick = 20
  api_url = 127.0.0.1:{self.server.server_port}
  api_user = recorder
  api_password_file = {password}
  record_root = /data/mission-video
  segment_duration = 1m
  recording_id_var = UAV_ROUTE_ACTIVE_ID
  path = nadir:drone_nadir
  path = fpv:drone_fpv
  activation = nadir:/usr/local/bin/start-nadir
  http_timeout = 1
}}

ProcessConfig = pVideoAutoRecorder
{{
  AppTick = 20
  CommsTick = 20
  api_url = 127.0.0.1:{self.server.server_port}
  record_root = /data/manual-video
  command_var = VIDEO_AUTO_COMMAND
  logger_directory_var = AUTO_LOGGER_DIRECTORY
  status_prefix = VIDEO_AUTO
  recording_id_prefix = recording
  segment_duration = 1m
  path = nadir:drone_nadir
  path = fpv:drone_fpv
  record_proxy = nadir:recorder_nadir
  record_proxy = fpv:recorder_fpv
  http_timeout = 1
}}

ProcessConfig = uVideoStart
{{
  AppTick = 20
  CommsTick = 20
  paused = false
  reset_max = 0
  event = var=LOGGER_DIRECTORY, val="./LOG_uav_10_10_2026_____15_50_23", time=0
  event = var=UAV_ROUTE_ACTIVE_ID, val=task-001, time=0.1
  event = var=VIDEO_RECORD_COMMAND, val=START, time=0.5
}}

ProcessConfig = uVideoDuplicate
{{
  AppTick = 20
  CommsTick = 20
  paused = false
  reset_max = 0
  event = var=LOGGER_DIRECTORY, val="./LOG_uav_restarted", time=0
  event = var=UAV_ROUTE_ACTIVE_ID, val=task-002, time=0.1
  event = var=VIDEO_RECORD_COMMAND, val=START, time=0.5
}}

ProcessConfig = uVideoSecondStart
{{
  AppTick = 20
  CommsTick = 20
  paused = false
  reset_max = 0
  event = var=LOGGER_DIRECTORY, val="./LOG_uav_10_10_2026_____15_50_23", time=0
  event = var=UAV_ROUTE_ACTIVE_ID, val=task-002, time=0.1
  event = var=VIDEO_RECORD_COMMAND, val=START, time=0.5
}}

ProcessConfig = uVideoStop
{{
  AppTick = 20
  CommsTick = 20
  paused = false
  reset_max = 0
  event = var=VIDEO_RECORD_COMMAND, val=STOP, time=0
}}

ProcessConfig = uAutoStart
{{
  AppTick = 20
  CommsTick = 20
  paused = false
  reset_max = 0
  event = var=AUTO_LOGGER_DIRECTORY, val=./LOG_manual_session, time=0
  event = var=VIDEO_AUTO_COMMAND, val=START, time=0.2
}}

ProcessConfig = uAutoStop
{{
  AppTick = 20
  CommsTick = 20
  paused = false
  reset_max = 0
  event = var=VIDEO_AUTO_COMMAND, val=STOP, time=0
}}
"""
        )

        # Replace port zero with a kernel-selected available TCP port.
        import socket

        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            db_port = sock.getsockname()[1]
        self.mission.write_text(self.mission.read_text().replace("ServerPort = 0", f"ServerPort = {db_port}"))

        self.output = (self.directory / "apps.log").open("w")
        self.processes = [
            subprocess.Popen(
                [str(MOOS_BIN / "MOOSDB"), str(self.mission)],
                stdout=self.output,
                stderr=subprocess.STDOUT,
            )
        ]
        time.sleep(0.4)
        self.processes.append(
            subprocess.Popen(
                [str(APP), str(self.mission), "pVideoRecorder"],
                stdout=self.output,
                stderr=subprocess.STDOUT,
            )
        )
        self.query("VIDEO_RECORD_STATE=IDLE")

    def tearDown(self):
        for process in reversed(getattr(self, "processes", [])):
            if process.poll() is None:
                process.terminate()
        for process in reversed(getattr(self, "processes", [])):
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        if hasattr(self, "output"):
            self.output.close()
        if hasattr(self, "server"):
            self.server.shutdown()
            self.server.server_close()

    def poke(self, *assignments):
        subprocess.run(
            [str(MOOS_BIN / "uPokeDB"), str(self.mission), "--quiet", *assignments],
            check=True,
            capture_output=True,
            text=True,
            timeout=10,
        )

    def query(self, *conditions, wait=6):
        result = subprocess.run(
            [
                str(MOOS_BIN / "uQueryDB"),
                str(self.mission),
                f"--wait={wait}",
                *(f"--condition={condition}" for condition in conditions),
                "--check_var=VIDEO_RECORD_COMMAND_RESULT",
                "--check_var=VIDEO_RECORD_DIRECTORY",
                "--check_var=VIDEO_RECORD_ID",
                "--check_var=VIDEO_RECORD_HOST",
                "--check_var=VIDEO_RECORD_LOCATION",
                "--esv",
            ],
            capture_output=True,
            text=True,
            timeout=wait + 10,
        )
        self.assertEqual(
            result.returncode,
            0,
            result.stdout
            + result.stderr
            + "\nHTTP requests: "
            + repr(self.requests())
            + "\nApp output:\n"
            + (self.directory / "apps.log").read_text(),
        )

    def requests(self):
        with ApiHandler.lock:
            return list(ApiHandler.requests)

    def launch_script(self, alias):
        process = subprocess.Popen(
            [str(MOOS_BIN / "uTimerScript"), str(self.mission), alias],
            stdout=self.output,
            stderr=subprocess.STDOUT,
        )
        self.processes.append(process)
        return process

    def test_explicit_start_uses_logger_directory_then_stop(self):
        time.sleep(0.3)
        self.assertEqual(self.requests(), [])

        self.launch_script("uVideoStart")
        self.query(
            "VIDEO_RECORD_STATE=RECORDING",
            "VIDEO_RECORDING=1",
            "VIDEO_RECORD_LOG_DIRECTORY=LOG_uav_10_10_2026_____15_50_23",
            "VIDEO_RECORD_ID=task-001",
            "VIDEO_RECORD_HOST=127.0.0.1",
        )

        requests = self.requests()
        self.assertEqual(len(requests), 2)
        self.assertEqual(
            {request["path"] for request in requests},
            {
                "/v3/config/paths/patch/drone_nadir",
                "/v3/config/paths/patch/drone_fpv",
            },
        )
        record_root = (
            "/data/mission-video/LOG_uav_10_10_2026_____15_50_23/task-001"
        )
        expected_auth = "Basic " + base64.b64encode(b"recorder:test-secret").decode()
        for request in requests:
            camera = "nadir" if request["path"].endswith("/drone_nadir") else "fpv"
            expected_body = {
                "recordPath": f"{record_root}/{camera}/%path_%Y-%m-%d_%H-%M-%S-%f",
                "record": True,
                "recordSegmentDuration": "1m",
            }
            if request["path"].endswith("/drone_nadir"):
                expected_body.update(
                    {
                        "runOnInit": "/usr/local/bin/start-nadir",
                        "runOnInitRestart": True,
                    }
                )
            self.assertEqual(request["body"], expected_body)
            self.assertEqual(request["authorization"], expected_auth)

        # A logger restart does not change the active recording directory.
        self.launch_script("uVideoDuplicate")
        time.sleep(1)
        self.assertEqual(len(self.requests()), 2)

        self.launch_script("uVideoStop")
        self.query("VIDEO_RECORD_STATE=STOPPED", "VIDEO_RECORDING=0")
        requests = self.requests()
        self.assertEqual(len(requests), 4)
        for request in requests[2:]:
            expected_body: dict[str, object] = {"record": False}
            if request["path"].endswith("/drone_nadir"):
                expected_body["runOnInit"] = ""
            self.assertEqual(request["body"], expected_body)

        # A second accepted task in the same logger run gets its own directory.
        self.launch_script("uVideoSecondStart")
        self.query("VIDEO_RECORD_STATE=RECORDING", "VIDEO_RECORD_ID=task-002")
        second_start = self.requests()[4:6]
        self.assertEqual(len(second_start), 2)
        self.assertTrue(
            all(
                "/LOG_uav_10_10_2026_____15_50_23/task-002/" in request["body"]["recordPath"]
                for request in second_start
            )
        )
        self.launch_script("uVideoStop")
        self.query("VIDEO_RECORD_STATE=STOPPED", "VIDEO_RECORDING=0")

    def test_auto_ids_separate_multiple_manual_recordings(self):
        auto_recorder = subprocess.Popen(
            [str(APP), str(self.mission), "pVideoAutoRecorder"],
            stdout=self.output,
            stderr=subprocess.STDOUT,
        )
        self.processes.append(auto_recorder)
        self.query("VIDEO_AUTO_STATE=IDLE")

        self.launch_script("uAutoStart")
        self.query("VIDEO_AUTO_STATE=RECORDING", "VIDEO_AUTO_ID=recording-001")
        first_start = self.requests()
        self.assertEqual(len(first_start), 4, first_start)
        self.assertEqual([request["method"] for request in first_start],
                         ["DELETE", "POST", "DELETE", "POST"])
        posts = [request for request in first_start if request["method"] == "POST"]
        self.assertEqual(
            {request["path"] for request in posts},
            {
                "/v3/config/paths/add/recorder_nadir",
                "/v3/config/paths/add/recorder_fpv",
            },
        )
        expected_sources = {
            "/v3/config/paths/add/recorder_nadir": "rtsp://127.0.0.1:8554/drone_nadir",
            "/v3/config/paths/add/recorder_fpv": "rtsp://127.0.0.1:8554/drone_fpv",
        }
        for request in posts:
            self.assertEqual(request["body"]["source"], expected_sources[request["path"]])
            self.assertFalse(request["body"]["sourceOnDemand"])
            self.assertTrue(request["body"]["record"])
            self.assertEqual(request["body"]["recordSegmentDuration"], "1m")
            self.assertIn("/%path_%Y-%m-%d_%H-%M-%S-%f", request["body"]["recordPath"])
            self.assertIn(
                "/LOG_manual_session/recording-001/", request["body"]["recordPath"]
            )
        self.assertTrue(all(request["authorization"] is None for request in first_start))

        self.launch_script("uAutoStop")
        self.query("VIDEO_AUTO_STATE=STOPPED")
        stop_requests = self.requests()[4:6]
        self.assertEqual(len(stop_requests), 2, self.requests())
        self.assertTrue(all(request["method"] == "DELETE" for request in stop_requests))

        self.launch_script("uAutoStart")
        self.query("VIDEO_AUTO_STATE=RECORDING", "VIDEO_AUTO_ID=recording-002")
        second_start = self.requests()[6:10]
        self.assertEqual(len(second_start), 4, self.requests())
        second_posts = [request for request in second_start if request["method"] == "POST"]
        self.assertEqual(len(second_posts), 2, self.requests())
        self.assertTrue(
            all(
                "/LOG_manual_session/recording-002/" in request["body"]["recordPath"]
                for request in second_posts
            )
        )

    def test_stop_from_idle_enforces_remote_stop(self):
        self.launch_script("uVideoStop")
        self.query("VIDEO_RECORD_STATE=STOPPED", "VIDEO_RECORDING=0")
        requests = self.requests()
        self.assertEqual(len(requests), 2, requests)
        self.assertTrue(all(request["body"].get("record") is False for request in requests))
        nadir = next(request for request in requests if request["path"].endswith("/drone_nadir"))
        self.assertEqual(nadir["body"]["runOnInit"], "")

    def test_partial_start_failure_is_rolled_back(self):
        ApiHandler.fail_start_path = "/v3/config/paths/patch/drone_fpv"
        self.launch_script("uVideoStart")
        self.query("VIDEO_RECORD_STATE=ERROR", "VIDEO_RECORDING=0")

        requests = self.requests()
        start_requests = [request for request in requests if request["body"].get("record") is True]
        stop_requests = [
            request for request in requests if request["body"].get("record") is False
        ]
        self.assertGreaterEqual(len(start_requests), 1, requests)
        self.assertEqual(start_requests[-1]["path"], ApiHandler.fail_start_path)
        self.assertEqual(
            {request["path"] for request in stop_requests},
            {
                "/v3/config/paths/patch/drone_nadir",
                "/v3/config/paths/patch/drone_fpv",
            },
        )


if __name__ == "__main__":
    unittest.main()
