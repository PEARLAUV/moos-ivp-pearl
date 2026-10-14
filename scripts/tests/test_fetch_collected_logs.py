#!/usr/bin/env python3
"""Shoreside fetch tests: local fixtures only, no SSH connection or mutation."""

import copy
import hashlib
import importlib.util
import json
import shlex
import shutil
import subprocess
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


def make_artifact(root: Path, name: str, node: str, complete: bool = True) -> Path:
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
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.remote = self.root / "remote"
        self.output = self.root / "output"
        self.drone1 = make_artifact(self.remote, "20260101T000000Z_drone_LOG_uav_first", "drone")
        self.drone2 = make_artifact(self.remote, "20260101T000001Z_drone_LOG_uav_second", "drone")
        self.pearl = make_artifact(self.remote, "20260101T000002Z_pearl-grass_LOG_pearl_first",
                                   "pearl-grass", False)
        self.transfers = []
        self.discoveries = []

    def inventory(self, host, remote_root, pattern, _options):
        self.discoveries.append((host, remote_root, pattern))
        return {"root": str(self.remote), "artifacts": [
            {"name": path.name, "sha256sums_sha256": fetcher.sha256(path / "SHA256SUMS")}
            for path in sorted(self.remote.glob(pattern))]}

    def transfer(self, _host, _remote_root, name, destination, _options):
        self.transfers.append(name)
        shutil.copytree(self.remote / name, destination)

    def run_fetch(self, inventory=None, transfer=None, extra=()):
        with mock.patch.object(fetcher, "remote_inventory", side_effect=inventory or self.inventory), \
                mock.patch.object(fetcher, "rsync_artifact", side_effect=transfer or self.transfer):
            return fetcher.main([
                "--allow-non-shoreside", "--site", "briggs", "--output-root", str(self.output),
                "--drone-host", "drone.test", "--pearl-grass-host", "pearl.test", *extra])

    def manifests(self):
        return [json.loads(path.read_text()) for path in
                sorted(self.output.glob("*/FETCH_MANIFEST.json"))]

    def test_verifies_manifest_node_and_every_checksum(self):
        result = fetcher.verify_artifact(self.drone1, {"drone"})
        self.assertTrue(result["source_complete"])
        self.assertEqual(result["verified_files"], 2)
        (self.drone1 / "payload.bin").write_bytes(b"corrupt")
        with self.assertRaises(fetcher.FetchError):
            fetcher.verify_artifact(self.drone1, {"drone"})
        with self.assertRaises(fetcher.FetchError):
            fetcher.verify_artifact(self.pearl, {"pearl-real"})

    def test_malformed_manifest_does_not_block_other_bundles(self):
        (self.drone1 / "MANIFEST.json").write_text('{"node": [], "complete": true}\n')
        (self.drone1 / "SHA256SUMS").write_text("".join(
            f"{fetcher.sha256(self.drone1 / name)}  {name}\n"
            for name in ("MANIFEST.json", "payload.bin")))
        self.assertEqual(self.run_fetch(), 1)
        manifest = self.manifests()[0]
        self.assertEqual(manifest["sources"]["drone"]["artifacts"][0]["status"], "failed")
        self.assertEqual(manifest["sources"]["drone"]["artifacts"][1]["status"], "fetched")
        self.assertTrue(manifest["sources"]["pearl"]["transfer_complete"])

    def test_rejects_escape_unlisted_files_missing_manifest_hash_and_symlinks(self):
        sums = self.drone1 / "SHA256SUMS"
        original = sums.read_text()
        outside = self.root / "outside"
        outside.write_text("outside")
        for bad in (f"{fetcher.sha256(outside)}  ../outside\n",
                    original.splitlines(keepends=True)[1], original + original):
            sums.write_text(bad)
            with self.assertRaises(fetcher.FetchError):
                fetcher.verify_artifact(self.drone1, {"drone"})
        sums.write_text(original)
        extra = self.drone1 / "extra"
        extra.write_text("not covered")
        with self.assertRaises(fetcher.FetchError):
            fetcher.verify_artifact(self.drone1, {"drone"})
        extra.unlink()
        extra.symlink_to(outside)
        with self.assertRaises(fetcher.FetchError):
            fetcher.verify_artifact(self.drone1, {"drone"})

    def test_fetches_all_bundles_and_reuses_only_reverified_copies(self):
        self.assertEqual(self.run_fetch(), 0)
        self.assertCountEqual(self.transfers, [self.drone1.name, self.drone2.name, self.pearl.name])
        manifest = self.manifests()[0]
        self.assertTrue(manifest["transfer_complete"])
        self.assertEqual(len(manifest["sources"]["drone"]["artifacts"]), 2)
        self.assertFalse(manifest["sources"]["pearl"]["artifacts"][0]["source_complete"])
        self.transfers.clear()
        self.assertEqual(self.run_fetch(), 0)
        self.assertEqual(self.transfers, [])
        manifest = self.manifests()[-1]
        for source in manifest["sources"].values():
            for row in source["artifacts"]:
                self.assertEqual(row["status"], "reused")
                self.assertTrue((self.output / row["local_path"] / "payload.bin").is_file())
        row = manifest["sources"]["drone"]["artifacts"][0]
        damaged = self.output / row["local_path"] / "payload.bin"
        damaged.write_bytes(b"damaged")
        self.assertEqual(self.run_fetch(), 0)
        self.assertEqual(self.transfers, [self.drone1.name])
        self.assertEqual(damaged.read_bytes(), b"damaged")  # no overwriting originals

    def test_new_bundle_is_fetched_on_next_run_without_recopying_old_bundles(self):
        self.assertEqual(self.run_fetch(), 0)
        self.transfers.clear()
        new = make_artifact(self.remote, "20260101T000003Z_drone_LOG_uav_third", "drone")
        self.assertEqual(self.run_fetch(), 0)
        self.assertEqual(self.transfers, [new.name])

    def test_transfer_failure_does_not_block_other_bundles_or_host_and_is_retryable(self):
        def transfer(host, root, name, destination, options):
            if name == self.drone1.name:
                raise fetcher.FetchError("simulated interrupted transfer")
            self.transfer(host, root, name, destination, options)
        self.assertEqual(self.run_fetch(transfer=transfer), 1)
        self.assertCountEqual(self.transfers, [self.drone2.name, self.pearl.name])
        self.assertFalse(self.manifests()[0]["transfer_complete"])
        self.transfers.clear()
        self.assertEqual(self.run_fetch(), 0)
        self.assertEqual(self.transfers, [self.drone1.name])

    def test_unreachable_host_does_not_block_other_host(self):
        def inventory(host, *args):
            if host == "drone.test":
                raise fetcher.FetchError("offline")
            return self.inventory(host, *args)
        self.assertEqual(self.run_fetch(inventory=inventory), 1)
        self.assertEqual(self.transfers, [self.pearl.name])

    def test_changed_remote_inventory_cannot_be_reported_complete(self):
        counts = {}
        def inventory(host, *args):
            value = self.inventory(host, *args)
            counts[host] = counts.get(host, 0) + 1
            if host == "drone.test" and counts[host] == 2:
                value = copy.deepcopy(value)
                value["artifacts"][0]["sha256sums_sha256"] = "0" * 64
            return value
        self.assertEqual(self.run_fetch(inventory=inventory), 1)
        manifest = self.manifests()[0]
        self.assertEqual(manifest["sources"]["drone"]["artifacts"][0]["status"], "failed")

    def test_new_bundle_during_transfer_is_recorded_pending(self):
        created = []
        def transfer(*args):
            self.transfer(*args)
            if not created:
                created.append(make_artifact(self.remote, "20260101T000003Z_drone_LOG_uav_new", "drone"))
        self.assertEqual(self.run_fetch(transfer=transfer), 1)
        manifest = self.manifests()[0]
        self.assertEqual(manifest["sources"]["drone"]["pending_new_bundles"], [created[0].name])
        self.transfers.clear()
        self.assertEqual(self.run_fetch(), 0)
        self.assertEqual(self.transfers, [created[0].name])

    def test_changed_manifest_during_transfer_is_not_verified(self):
        def transfer(*args):
            self.transfer(*args)
            destination = args[3]
            if args[2] == self.drone1.name:
                (destination / "SHA256SUMS").write_text("changed")
        self.assertEqual(self.run_fetch(transfer=transfer), 1)
        self.assertFalse(self.manifests()[0]["transfer_complete"])

    def test_inventory_script_is_read_only_and_returns_every_finalized_bundle(self):
        # Run the remote Python payload LOCALLY against fixtures, never SSH.
        (self.remote / ".collecting-drone").mkdir()
        actual_run = subprocess.run
        def local_run(command, **kwargs):
            self.assertEqual(command[0], "ssh")
            remote_command = shlex.split(command[-1])
            self.assertEqual(remote_command[0], "python3")
            return actual_run(remote_command, **kwargs)
        before = {path: path.read_bytes() for path in self.remote.rglob("*") if path.is_file()}
        with mock.patch.object(fetcher.subprocess, "run", side_effect=local_run):
            result = fetcher.remote_inventory("unused", str(self.remote), "*_drone_LOG_uav_*", [])
            self.assertEqual([row["name"] for row in result["artifacts"]],
                             [self.drone1.name, self.drone2.name])
            with self.assertRaises(fetcher.FetchError):
                fetcher.remote_inventory("unused", str(self.root / "missing"), "*_drone_LOG_uav_*", [])
        self.assertEqual(before, {path: path.read_bytes() for path in self.remote.rglob("*") if path.is_file()})

    def test_root_defaults_and_per_host_overrides(self):
        self.assertEqual(fetcher.parse_args(["--output-root", "out"]).remote_root,
                         "/home/pearl/moos-ivp-pearl/missions/briggs_field/2026_10_09_water_ops_prep/field_logs")
        self.assertEqual(self.run_fetch(extra=("--drone-remote-root", "/drone", "--pearl-remote-root", "/pearl")), 0)
        self.assertIn(("drone.test", "/drone", "*_drone_LOG_uav_*"), self.discoveries)
        self.assertIn(("pearl.test", "/pearl", "*_pearl-grass_LOG_pearl_*"), self.discoveries)

    def test_charles_selects_only_real_pearl(self):
        real = make_artifact(self.remote, "20260101T000003Z_pearl-real_LOG_pearl_real", "pearl-real", False)
        self.assertEqual(self.run_fetch(extra=("--site", "charles", "--pearl-real-host", "real.test")), 0)
        self.assertIn(real.name, self.transfers)
        self.assertNotIn(self.pearl.name, self.transfers)


if __name__ == "__main__":
    unittest.main(verbosity=2)
