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
from unittest import mock
from typing import Any, cast

SCRIPT = Path(__file__).resolve().parents[1] / "collect_mission_logs.py"
sys.path.insert(0, str(SCRIPT.parent))
spec = importlib.util.spec_from_file_location("collector", SCRIPT)
assert spec is not None and spec.loader is not None
collector = cast(Any, importlib.util.module_from_spec(spec))
sys.modules[spec.name] = collector
spec.loader.exec_module(collector)


BASE_TIME = 1_800_000_000


def native_log(start, end):
    fmt = b"\xa3\x95\x80" + struct.pack(
        "<BB4s16s64s", 129, 19, b"RTC", b"QQ", b"TimeUS,TimeUTC")
    def rtc(boot, utc):
        return b"\xa3\x95\x81" + struct.pack("<QQ", int(boot * 1e6), int(utc * 1e6))
    return fmt + rtc(1, start) + rtc(1 + end - start, end)


class PixhawkPeer:
    protocol = "tcp"

    def __init__(self):
        self.queue = bytearray()
        self.reader = collector.mav.Reader()
        self.params = [("P_INT", 4.0, 6), ("P_FLOAT", 0.25, 9)]
        self.logs = {7: native_log(BASE_TIME + 10, BASE_TIME + 20),
                     8: native_log(BASE_TIME + 30, BASE_TIME + 40)}
        self.log_times = {7: BASE_TIME + 20, 8: BASE_TIME + 40}
        self.requests = []
        self.sent = []

    def close(self):
        self.closed = True

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
                    body = struct.pack("<IIHHH", self.log_times.get(log_id, BASE_TIME + 100), len(data),
                                       log_id, len(self.logs), max(self.logs))
                    self.queue += self.frame(118, body)
            elif message_id == 119:
                offset, count, log_id = struct.unpack_from("<IIH", payload)
                self.requests.append((log_id, offset, count))
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

    def native_mission(self, root, start=BASE_TIME, duration=100):
        return self.make_log(root, "LOG_uav_", f"% LOGSTART {start}\n0 X src start\n{duration} X src end\n")

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

    def test_drone_collection_refreshes_stop_evidence_for_selected_log(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mission = root / "mission"
            mission.mkdir()
            recording = root / "video" / "recording"
            log = self.make_log(
                mission, "LOG_uav_",
                "1 VIDEO_RECORD_STATE pVideoRecorder RECORDING\n"
                f"2 VIDEO_RECORD_DIRECTORY pVideoRecorder {recording}\n")
            evidence = mission / ".video-stop-evidence.json"
            evidence.write_text('{"log_directory": "stale-log"}\n')
            with mock.patch.object(
                    collector, "stop_recording_after_mission",
                    return_value=evidence) as stop:
                result = collector.ensure_drone_recording_stopped(
                    mission, log, "http://video-api", evidence)
            self.assertEqual(result, evidence)
            stop.assert_called_once_with(
                mission, "http://video-api", evidence, log_directory=log)

            (log / f"{log.name}.alog").write_text(
                "1 VIDEO_RECORD_STATE pVideoRecorder STOPPED\n")
            with mock.patch.object(collector, "stop_recording_after_mission") as stop:
                result = collector.ensure_drone_recording_stopped(
                    mission, log, "http://video-api", evidence)
            self.assertIsNone(result)
            stop.assert_not_called()

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

    def test_multiple_mission_native_logs_are_archived_and_catalog_checked(self):
        peer = PixhawkPeer()
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(collector, "assert_drone_apps_stopped"), \
                mock.patch.object(collector.mav, "Link", return_value=peer):
            root = Path(directory) / "native"
            mission = self.native_mission(Path(directory))
            metadata = collector.collect_ardupilot(root, "tcp", "fixture", 1, 1, 0.1, mission)
            self.assertTrue(metadata["complete"])
            self.assertEqual([row["id"] for row in metadata["logs"]], [8, 7])
            self.assertEqual(metadata["catalog_before"], metadata["catalog_after"])
            for row in metadata["logs"]:
                self.assertEqual((root / "dataflash" / row["file"]).read_bytes(), peer.logs[row["id"]])
            self.assertEqual(json.loads((root / "NATIVE_INVENTORY.json").read_text()), metadata)
        self.assertTrue(peer.closed)
        self.assertNotIn(23, peer.sent)
        self.assertNotIn(121, peer.sent)

    def test_native_partial_download_does_not_block_other_safe_downloads(self):
        peer = PixhawkPeer()
        peer.logs[9] = native_log(BASE_TIME + 45, BASE_TIME + 50)
        original = collector.ReadOnlyPixhawk.download_log
        def fail_older(client, entry, destination):
            if entry.id == 7:
                destination.write_bytes(b"partial")
                raise collector.CollectionError("interrupted old log")
            return original(client, entry, destination)
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(collector, "assert_drone_apps_stopped"), \
                mock.patch.object(collector.mav, "Link", return_value=peer), \
                mock.patch.object(collector.ReadOnlyPixhawk, "download_log", autospec=True,
                                  side_effect=fail_older):
            root = Path(directory) / "native"
            mission = self.native_mission(Path(directory))
            with self.assertRaises(collector.CollectionError):
                collector.collect_ardupilot(root, "tcp", "fixture", 1, 1, 0.1, mission)
            inventory = json.loads((root / "NATIVE_INVENTORY.json").read_text())
            self.assertFalse(inventory["complete"])
            self.assertTrue(inventory["logs"][0]["complete"])
            self.assertFalse(inventory["logs"][1]["complete"])
            self.assertTrue(inventory["logs"][2]["complete"])
            self.assertEqual([row["id"] for row in inventory["logs"]], [9, 7, 8])
            self.assertEqual(len(list((root / "dataflash").glob("*.bin"))), 2)
            self.assertEqual(len(list((root / "dataflash").glob("*.partial"))), 1)

    def test_native_unsafe_state_aborts_remaining_downloads(self):
        peer = PixhawkPeer()
        def unsafe(client, _entry, _destination):
            client.unsafe_seen = True
            raise collector.CollectionError("armed during download")
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(collector, "assert_drone_apps_stopped"), \
                mock.patch.object(collector.mav, "Link", return_value=peer), \
                mock.patch.object(collector.ReadOnlyPixhawk, "download_log", autospec=True,
                                  side_effect=unsafe) as download:
            mission = self.native_mission(Path(directory))
            def unsafe_after_probe(client, entry, destination):
                peer.sent.clear()
                return unsafe(client, entry, destination)
            download.side_effect = unsafe_after_probe
            with self.assertRaisesRegex(collector.CollectionError, "armed or became airborne"):
                collector.collect_ardupilot(Path(directory), "tcp", "fixture", 1, 1, 0.1, mission)
            download.assert_called_once()
            self.assertNotIn(122, peer.sent)  # no further request after unsafe evidence
            self.assertFalse(json.loads((Path(directory) / "NATIVE_INVENTORY.json").read_text())["complete"])

    def test_native_catalog_changes_fail_closed(self):
        peer = PixhawkPeer()
        entries = [collector.LogEntry(key, len(value), 1_800_000_000 + key, 2, 8)
                   for key, value in peer.logs.items()]
        changed = [entries[0], collector.LogEntry(8, 999, entries[1].time_utc, 2, 8)]
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(collector, "assert_drone_apps_stopped"), \
                mock.patch.object(collector.mav, "Link", return_value=peer), \
                mock.patch.object(collector.ReadOnlyPixhawk, "get_log_entries",
                                  side_effect=[entries, changed]):
            mission = self.native_mission(Path(directory))
            with self.assertRaisesRegex(collector.CollectionError, "catalog changed"):
                collector.collect_ardupilot(Path(directory), "tcp", "fixture", 1, 1, 0.1, mission)
            self.assertFalse(json.loads((Path(directory) / "NATIVE_INVENTORY.json").read_text())["complete"])

    def test_native_scope_excludes_old_and_later_logs(self):
        peer = PixhawkPeer()
        peer.logs[9] = native_log(BASE_TIME + 70, BASE_TIME + 80)
        original = collector.ReadOnlyPixhawk.download_log
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(collector, "assert_drone_apps_stopped"), \
                mock.patch.object(collector.mav, "Link", return_value=peer), \
                mock.patch.object(collector.ReadOnlyPixhawk, "download_log", autospec=True,
                                  side_effect=original) as download:
            root = Path(directory)
            mission = self.native_mission(root, BASE_TIME + 25, 20)
            meta = collector.collect_ardupilot(root / "native", "tcp", "fixture", 1, 1, 0.1, mission)
            self.assertEqual(meta["selected_log_ids"], [8])
            self.assertEqual([row["id"] for row in meta["logs"]], [8])
            download.assert_called_once()
            self.assertFalse(any(request[0] == 7 for request in peer.requests))
            self.assertNotIn("dataflash_id", meta)  # newest catalog ID was not selected

    def test_native_scope_includes_log_straddling_mission(self):
        peer = PixhawkPeer()
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(collector, "assert_drone_apps_stopped"), \
                mock.patch.object(collector.mav, "Link", return_value=peer):
            root = Path(directory)
            mission = self.native_mission(root, BASE_TIME + 35, 1)
            meta = collector.collect_ardupilot(root / "native", "tcp", "fixture", 1, 1, 0.1, mission)
            self.assertEqual(meta["selected_log_ids"], [8])
            self.assertGreater(peer.log_times[8], meta["mission_window"]["until_unix"])

    def test_native_explicit_older_mission_does_not_collect_latest(self):
        peer = PixhawkPeer()
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(collector, "assert_drone_apps_stopped"), \
                mock.patch.object(collector.mav, "Link", return_value=peer):
            root = Path(directory)
            mission = self.native_mission(root, BASE_TIME + 12, 4)
            meta = collector.collect_ardupilot(root / "native", "tcp", "fixture", 1, 1, 0.1, mission)
            self.assertEqual(meta["selected_log_ids"], [7])
            self.assertNotIn("dataflash_id", meta)

    def test_native_unknown_timing_never_falls_back_to_all(self):
        peer = PixhawkPeer()
        peer.log_times[8] = 0  # native UTC still permits matching this file
        peer.logs[9] = b"no native clock"
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(collector, "assert_drone_apps_stopped"), \
                mock.patch.object(collector.mav, "Link", return_value=peer):
            root = Path(directory)
            mission = self.native_mission(root, BASE_TIME + 25, 20)
            with self.assertRaisesRegex(collector.CollectionError, "selection"):
                collector.collect_ardupilot(root / "native", "tcp", "fixture", 1, 1, 0.1, mission)
            meta = json.loads((root / "native/NATIVE_INVENTORY.json").read_text())
            self.assertFalse(meta["complete"])
            self.assertEqual(meta["selected_log_ids"], [8])
            self.assertEqual([row["id"] for row in meta["logs"]], [8])
            self.assertEqual(meta["selection_results"][0]["decision"], "unknown")

    def test_native_no_match_does_not_download_unrelated_files(self):
        peer = PixhawkPeer()
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(collector, "assert_drone_apps_stopped"), \
                mock.patch.object(collector.mav, "Link", return_value=peer):
            root = Path(directory)
            mission = self.native_mission(root, BASE_TIME + 200, 10)
            with self.assertRaisesRegex(collector.CollectionError, "No DataFlash log"):
                collector.collect_ardupilot(root / "native", "tcp", "fixture", 1, 1, 0.1, mission)
            self.assertEqual(peer.requests, [])
            self.assertEqual(list((root / "native/dataflash").iterdir()), [])

    def test_probe_range_alignment_and_bytes(self):
        peer = PixhawkPeer()
        peer.logs[8] = bytes(range(256)) * 3
        client = collector.ReadOnlyPixhawk(peer, response_timeout=0.1)
        client.admit()
        entry = client.get_log_entries()[-1]
        self.assertEqual(client.read_log_range(entry, 90, 270), peer.logs[8][90:270])
        self.assertEqual(client.read_log_range(entry, 630, entry.size), peer.logs[8][630:])
        self.assertEqual(peer.requests, [(8, 90, 180), (8, 630, 138)])
        for start, end in ((1, 90), (0, 91), (0, 999), (90, 90)):
            with self.assertRaises(collector.CollectionError):
                client.read_log_range(entry, start, end)

    def test_unsafe_state_during_probe_aborts_selection(self):
        peer = PixhawkPeer()
        def unsafe(client, *_args):
            peer.sent.clear()
            client.unsafe_seen = True
            raise collector.CollectionError("armed during probe")
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(collector, "assert_drone_apps_stopped"), \
                mock.patch.object(collector.mav, "Link", return_value=peer), \
                mock.patch.object(collector.ReadOnlyPixhawk, "read_log_range", autospec=True,
                                  side_effect=unsafe) as probe:
            root = Path(directory)
            mission = self.native_mission(root)
            with self.assertRaisesRegex(collector.CollectionError, "armed or became airborne"):
                collector.collect_ardupilot(root / "native", "tcp", "fixture", 1, 1, 0.1, mission)
            probe.assert_called_once()
            self.assertEqual(peer.sent, [])

    def test_native_guard_still_refuses_armed_stale_and_airborne(self):
        peer = PixhawkPeer()
        client = collector.ReadOnlyPixhawk(peer, response_timeout=0.1)
        client.admit()
        now = collector.time.monotonic()
        for heartbeat, ground in (((True, now), (True, now)),
                                  ((False, now - 10), (True, now)),
                                  ((False, now), (False, now))):
            client.heartbeat, client.ground = heartbeat, ground
            with self.assertRaises(collector.CollectionError):
                client.guard()

    def test_two_task_video_keeps_both_and_deduplicates_publications(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mission = root / "mission"
            mission.mkdir()
            video_root = root / "video"
            tasks = [video_root / "session" / f"task-{index}" for index in (1, 2)]
            for task in tasks:
                for camera in ("fpv", "nadir"):
                    (task / camera).mkdir(parents=True)
                    (task / camera / "clip.mp4").write_bytes(f"{task.name}/{camera}".encode())
            log = self.make_log(mission, "LOG_uav_", "".join(
                f"{index} VIDEO_RECORD_DIRECTORY pVideoRecorder {task}\n"
                for index, task in enumerate([tasks[0], tasks[0], tasks[1]])) +
                "4 VIDEO_RECORD_STATE pVideoRecorder STOPPED\n")
            destination = root / "copy"
            destination.mkdir()
            metadata = collector.copy_drone_video(log, video_root, destination)
            self.assertEqual(len(metadata["recordings"]), 2)
            for index, task in enumerate(tasks, 1):
                for camera in ("fpv", "nadir"):
                    self.assertEqual((destination / f"tasks/{index:04d}" / camera / "clip.mp4").read_bytes(),
                                     (task / camera / "clip.mp4").read_bytes())
            (tasks[0] / "fpv" / "clip.mp4").unlink()
            partial = root / "partial"
            partial.mkdir()
            with self.assertRaises(collector.CollectionError):
                collector.copy_drone_video(log, video_root, partial)
            self.assertTrue((partial / "tasks/0002/fpv/clip.mp4").is_file())
            self.assertTrue((partial / "tasks/0001/nadir/clip.mp4").is_file())
            self.assertFalse(json.loads((partial / "VIDEO_INVENTORY.json").read_text())["complete"])

    def test_video_open_files_and_outside_root_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            mission = root / "mission"
            mission.mkdir()
            task = root / "video" / "task"
            for camera in ("fpv", "nadir"):
                (task / camera).mkdir(parents=True)
                (task / camera / "clip.mp4").write_bytes(b"fixture")
            log = self.make_log(mission, "LOG_uav_",
                                f"1 VIDEO_RECORD_DIRECTORY recorder {task}\n"
                                "2 VIDEO_RECORD_STATE recorder STOPPED\n")
            for video_root, opened in ((root / "different-root", []), (root / "video", ["open"])):
                destination = Path(tempfile.mkdtemp(dir=root))
                with mock.patch.object(collector, "open_files_below", return_value=opened):
                    with self.assertRaises(collector.CollectionError):
                        collector.copy_drone_video(log, video_root, destination)
                self.assertEqual(list(destination.rglob("*.mp4")), [])

    def test_missing_video_does_not_prevent_native_collection(self):
        with tempfile.TemporaryDirectory() as directory:
            mission = Path(directory)
            self.make_log(mission, "LOG_uav_")
            output = mission / "artifacts"
            def native(destination, *_args):
                destination.mkdir()
                (destination / "native.bin").write_bytes(b"native evidence")
                return {"complete": True}
            with mock.patch.object(collector, "assert_drone_apps_stopped"), \
                    mock.patch.object(collector, "collect_wifi_evidence", return_value={"warnings": []}) as wifi, \
                    mock.patch.object(collector, "ensure_drone_recording_stopped",
                                      side_effect=collector.CollectionError("missing video")), \
                    mock.patch.object(collector, "collect_ardupilot", side_effect=native) as capture:
                result = collector.main(["--mission-dir", str(mission), "--node", "drone",
                                         "--output-root", str(output)])
            self.assertEqual(result, 1)
            capture.assert_called_once()
            wifi.assert_called_once()
            artifact = next(output.iterdir())
            manifest = json.loads((artifact / "MANIFEST.json").read_text())
            self.assertFalse(manifest["complete"])
            self.assertTrue(manifest["ardupilot"]["complete"])
            self.assertEqual((artifact / "ardupilot/native.bin").read_bytes(), b"native evidence")
            self.validate_sums(artifact)

    def test_log_selection_uses_native_start_not_directory_mtime(self):
        with tempfile.TemporaryDirectory() as directory:
            mission = Path(directory)
            older = self.make_log(mission, "LOG_SHORESIDE_", stamp="10_10_2026_____12_00_00")
            newer = self.make_log(mission, "LOG_SHORESIDE_", stamp="11_10_2026_____12_00_00")
            (older / f"{older.name}.alog").write_text("% LOGSTART 100\n0 X src old\n")
            (newer / f"{newer.name}.alog").write_text("% LOGSTART 200\n0 X src new\n")
            collector.os.utime(older, (9999999999, 9999999999))
            self.assertEqual(collector.latest_log_directory(mission, "shoreside", None), newer)
            self.assertEqual(collector.latest_log_directory(mission, "shoreside", older.name), older)


if __name__ == "__main__":
    unittest.main(verbosity=2)
