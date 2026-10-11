#!/usr/bin/env python3
"""Shoreside fetch tests; no SSH connection or remote mutation."""

import hashlib
import importlib.util
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any, cast
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / "fetch_collected_logs.py"
sys.path.insert(0, str(SCRIPT.parent))
spec = importlib.util.spec_from_file_location("fetch_collected_logs", SCRIPT)
assert spec is not None and spec.loader is not None
fetcher = cast(Any, importlib.util.module_from_spec(spec))
sys.modules[spec.name] = fetcher
spec.loader.exec_module(fetcher)


def make_artifact(root: Path, name: str, node: str, complete: bool) -> Path:
    artifact = root / name
    artifact.mkdir(parents=True)
    (artifact / "payload.bin").write_bytes((node + " evidence").encode())
    (artifact / "MANIFEST.json").write_text(json.dumps({"node": node, "complete": complete}) + "\n")
    rows = []
    for path in sorted((artifact / item) for item in ("MANIFEST.json", "payload.bin")):
        rows.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n")
    (artifact / "SHA256SUMS").write_text("".join(rows))
    return artifact


class FetchCollectedLogsTest(unittest.TestCase):
    def test_verifies_manifest_node_and_every_checksum(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifact = make_artifact(root, "20260101T000000Z_drone_LOG_uav_test",
                                     "drone", True)
            result = fetcher.verify_artifact(artifact, {"drone"})
            self.assertTrue(result["source_complete"])
            self.assertEqual(result["verified_files"], 2)
            (artifact / "payload.bin").write_bytes(b"corrupt")
            with self.assertRaises(fetcher.FetchError):
                fetcher.verify_artifact(artifact, {"drone"})
            artifact = make_artifact(root, "20260101T000001Z_pearl-real_LOG_pearl_test",
                                     "pearl-real", False)
            with self.assertRaises(fetcher.FetchError):
                fetcher.verify_artifact(artifact, {"pearl-grass"})

    def test_rejects_checksum_path_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifact = make_artifact(root, "artifact", "drone", True)
            outside = root / "outside"
            outside.write_text("outside")
            digest = hashlib.sha256(outside.read_bytes()).hexdigest()
            (artifact / "SHA256SUMS").write_text(f"{digest}  ../outside\n")
            with self.assertRaises(fetcher.FetchError):
                fetcher.verify_artifact(artifact, {"drone"})

    def test_fetches_latest_drone_and_site_pearl_into_one_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            remote = root / "remote"
            output = root / "output"
            drone_name = "20260101T000000Z_drone_LOG_uav_test"
            pearl_name = "20260101T000001Z_pearl-real_LOG_pearl_test"
            sources = {
                drone_name: make_artifact(remote, drone_name, "drone", True),
                pearl_name: make_artifact(remote, pearl_name, "pearl-real", False),
            }
            discoveries = []

            def latest(host, _remote_root, pattern, _options):
                discoveries.append((host, pattern))
                return drone_name if "drone" in pattern else pearl_name

            def transfer(_host, _remote_root, name, destination, _options):
                shutil.copytree(sources[name], destination)

            with mock.patch.object(fetcher, "remote_latest", side_effect=latest), \
                    mock.patch.object(fetcher, "rsync_artifact", side_effect=transfer):
                result = fetcher.main([
                    "--allow-non-shoreside", "--site", "charles",
                    "--output-root", str(output), "--drone-host", "drone.test",
                    "--pearl-real-host", "pearl-real.test",
                ])
            self.assertEqual(result, 0)
            self.assertEqual(discoveries, [
                ("drone.test", "*_drone_LOG_uav_*"),
                ("pearl-real.test", "*_pearl-real_LOG_pearl_*"),
            ])
            fetched = next(output.iterdir())
            manifest = json.loads((fetched / "FETCH_MANIFEST.json").read_text())
            self.assertTrue(manifest["transfer_complete"])
            self.assertTrue(manifest["sources"]["drone"]["source_complete"])
            self.assertFalse(manifest["sources"]["pearl"]["source_complete"])
            self.assertTrue((fetched / "drone" / drone_name / "payload.bin").is_file())
            self.assertTrue((fetched / "pearl" / pearl_name / "payload.bin").is_file())


if __name__ == "__main__":
    unittest.main(verbosity=2)
