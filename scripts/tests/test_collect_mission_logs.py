#!/usr/bin/env python3
"""Local artifact collector tests; no vehicle, network, or live mission."""

import hashlib
import importlib.util
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any, cast

SCRIPT = Path(__file__).resolve().parents[1] / "collect_mission_logs.py"
sys.path.insert(0, str(SCRIPT.parent))
spec = importlib.util.spec_from_file_location("collector", SCRIPT)
assert spec is not None and spec.loader is not None
collector = cast(Any, importlib.util.module_from_spec(spec))
sys.modules[spec.name] = collector
spec.loader.exec_module(collector)


class PixhawkPeer:
    protocol = "tcp"

    def __init__(self):
        self.queue = bytearray()
        self.reader = collector.mav.Reader()
        self.params = [("P_INT", 4.0, 6), ("P_FLOAT", 0.25, 9)]
        self.logs = {7: b"previous", 8: bytes(range(256)) * 3}
        self.sent = []

    def frame(self, message_id, payload):
        return collector.mav.encode(message_id, payload, system=1, component=1)

    def state(self):
        heartbeat = struct.pack("<IBBBBB", 0, 2, 3, 0, 4, 3)
        return self.frame(0, heartbeat) + self.frame(245, bytes((0, 1)))

    def recv(self, _timeout):
        queued = bytes(self.queue)
        self.queue.clear()
        return self.state() + queued

    def send(self, packet):
        for message_id, _, _, payload in self.reader.feed(packet):
            self.sent.append(message_id)
            if message_id == 21:
                for index, (name, value, parameter_type) in enumerate(self.params):
                    body = struct.pack("<fHH16sB", value, len(self.params), index,
                                       name.encode(), parameter_type)
                    self.queue += self.frame(22, body)
            elif message_id == 20:
                index = struct.unpack_from("<h", payload)[0]
                name, value, parameter_type = self.params[index]
                body = struct.pack("<fHH16sB", value, len(self.params), index,
                                   name.encode(), parameter_type)
                self.queue += self.frame(22, body)
            elif message_id == 117:
                for log_id, data in self.logs.items():
                    body = struct.pack("<IIHHH", 1_800_000_000 + log_id, len(data),
                                       log_id, len(self.logs), max(self.logs))
                    self.queue += self.frame(118, body)
            elif message_id == 119:
                offset, count, log_id = struct.unpack_from("<IIH", payload)
                data = self.logs[log_id]
                for packet_offset in range(offset, min(offset + count, len(data)), 90):
                    block = data[packet_offset:packet_offset + 90]
                    body = struct.pack("<IHB90s", packet_offset, log_id, len(block), block)
                    self.queue += self.frame(120, body)
            elif message_id in (76, 122):
                pass
            else:
                raise AssertionError(f"unexpected request {message_id}")


class CollectorTest(unittest.TestCase):
    def make_log(self, mission, prefix, body="", stamp="10_10_2026_____12_00_00"):
        directory = mission / f"{prefix}{stamp}"
        directory.mkdir()
        (directory / f"{directory.name}.alog").write_text(body or "0 X source value\n")
        (directory / f"{directory.name}._moos").write_text("LOGSTART 1\n")
        return directory

    def validate_sums(self, artifact):
        for line in (artifact / "SHA256SUMS").read_text().splitlines():
            wanted, name = line.split("  ", 1)
            self.assertEqual(hashlib.sha256((artifact / name).read_bytes()).hexdigest(), wanted)

    def test_read_only_pixhawk_parameter_and_latest_log_download(self):
        peer = PixhawkPeer()
        client = collector.ReadOnlyPixhawk(peer, response_timeout=0.1)
        client.admit()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            count = client.download_parameters(root / "postflight.params", timeout=1)
            self.assertEqual(count, 2)
            text = (root / "postflight.params").read_text()
            self.assertIn("1\t1\tP_INT\t4\t6", text)
            self.assertIn("1\t1\tP_FLOAT\t0.25\t9", text)
            entries = client.get_log_entries()
            self.assertEqual([entry.id for entry in entries], [7, 8])
            destination = root / "latest.bin"
            client.download_log(entries[-1], destination)
            self.assertEqual(destination.read_bytes(), peer.logs[8])
        self.assertTrue(all(message in (20, 21, 76, 117, 119, 122)
                            for message in peer.sent))
        self.assertNotIn(23, peer.sent)  # PARAM_SET
        self.assertNotIn(121, peer.sent)  # LOG_ERASE

    def test_shoreside_collects_latest_log_and_checksums(self):
        with tempfile.TemporaryDirectory() as directory:
            mission = Path(directory) / "mission"
            mission.mkdir()
            older = self.make_log(mission, "LOG_SHORESIDE_",
                                  stamp="10_10_2026_____12_00_00")
            latest = self.make_log(mission, "LOG_SHORESIDE_",
                                   stamp="11_10_2026_____12_00_00")
            older.touch()
            latest.touch()
            output = mission / "artifacts"
            result = collector.main(["--mission-dir", str(mission), "--node", "shoreside",
                                     "--output-root", str(output)])
            self.assertEqual(result, 0)
            artifacts = list(output.iterdir())
            self.assertEqual(len(artifacts), 1)
            artifact = artifacts[0]
            manifest = json.loads((artifact / "MANIFEST.json").read_text())
            self.assertTrue(manifest["complete"])
            self.assertEqual(Path(manifest["moos_log_source"]).name, latest.name)
            self.assertTrue((artifact / "moos" / latest.name).is_dir())
            self.validate_sums(artifact)

    def test_real_pearl_preserves_moos_and_reports_jetson_gap(self):
        with tempfile.TemporaryDirectory() as directory:
            mission = Path(directory) / "mission"
            mission.mkdir()
            log = self.make_log(mission, "LOG_pearl_")
            output = mission / "artifacts"
            result = collector.main(["--mission-dir", str(mission), "--node", "pearl-real",
                                     "--output-root", str(output)])
            self.assertEqual(result, 2)
            artifact = next(output.iterdir())
            manifest = json.loads((artifact / "MANIFEST.json").read_text())
            self.assertFalse(manifest["complete"])
            self.assertIn("recording disabled", manifest["error"])
            self.assertTrue((artifact / "moos" / log.name).is_dir())
            self.assertTrue((artifact / "jetson-video" / "UNAVAILABLE.txt").is_file())
            self.validate_sums(artifact)

    def test_drone_video_requires_stopped_two_stream_recording(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mission = root / "mission"
            mission.mkdir()
            video = root / "video" / "LOG_uav_test" / "task-1"
            for camera in ("fpv", "nadir"):
                (video / camera).mkdir(parents=True)
                (video / camera / "segment.mp4").write_bytes(camera.encode())
            alog = ("1 VIDEO_RECORD_DIRECTORY pVideoRecorder " + str(video) + "\n"
                    "2 VIDEO_RECORD_ID pVideoRecorder task-1\n"
                    "3 VIDEO_RECORD_STATE pVideoRecorder STOPPED\n")
            log = self.make_log(mission, "LOG_uav_", alog)
            destination = root / "copy"
            destination.mkdir()
            metadata = collector.copy_drone_video(log, root / "video", destination)
            self.assertEqual(metadata["streams"], ["fpv", "nadir"])
            self.assertEqual((destination / "fpv" / "segment.mp4").read_bytes(), b"fpv")
            (video / "nadir" / "segment.mp4").unlink()
            empty_copy = root / "empty-copy"
            empty_copy.mkdir()
            with self.assertRaises(collector.CollectionError):
                collector.copy_drone_video(log, root / "video", empty_copy)


if __name__ == "__main__":
    unittest.main(verbosity=2)
