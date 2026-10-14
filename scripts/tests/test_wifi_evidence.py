#!/usr/bin/env python3
"""Wi-Fi forensic collection tests; no actual journals, radios, or services."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import wifi_evidence as wifi


class WifiEvidenceTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.log = self.root / "LOG_uav_test"
        self.log.mkdir()
        (self.log / "test.alog").write_text("% LOGSTART 1800000000\n0 X src a\n12.5 X src b\n")
        self.sys_net = self.root / "net"
        (self.sys_net / "wlan1/wireless").mkdir(parents=True)
        self.proc = self.root / "proc"
        for name, body in (("net/wireless", "wlan1: 0000 60. -50. -256\n"),
                           ("uptime", "100.0 200.0\n"),
                           ("sys/kernel/random/boot_id", "boot-id\n")):
            path = self.proc / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(body)
        self.var_log = self.root / "varlog"
        self.var_log.mkdir()
        self.commands = []

    def run_command(self, argv, *, stdout, stderr, **kwargs):
        self.commands.append(argv)
        if argv[0] == "journalctl" and "NetworkManager.service" in argv:
            stdout.write((json.dumps({"__REALTIME_TIMESTAMP": "1800000003000000",
                                     "__MONOTONIC_TIMESTAMP": "3000000", "_BOOT_ID": "old-boot",
                                     "MESSAGE": "wlan1: CTRL-EVENT-SIGNAL-CHANGE above=0 signal=-72 noise=9999"}) + "\n").encode())
        return subprocess.CompletedProcess(argv, 0)

    def collect(self):
        return wifi.collect_wifi_evidence(self.log, self.root / "evidence", 1800000000,
                                          sys_net=self.sys_net, proc_root=self.proc,
                                          log_root=self.var_log)

    def test_window_uses_native_logstart_and_elapsed_not_directory_name(self):
        window = wifi.mission_window(self.log, 1800000000)
        self.assertEqual(window["end_unix"], 1800000012.5)
        self.assertEqual(window["since_unix"], 1799999880)
        self.assertEqual(window["until_unix"], 1800000132.5)

    def test_collects_historical_records_and_keeps_current_snapshots_separate(self):
        (self.var_log / "kern.log.1.gz").write_bytes(b"verbatim archived bytes")
        with mock.patch.object(wifi.subprocess, "run", side_effect=self.run_command):
            result = self.collect()
        self.assertEqual(result["journal_records"], 1)
        self.assertEqual(result["journal_rssi_candidates"], 1)
        candidates = json.loads((self.root / "evidence/journal-rssi-candidates.json").read_text())
        self.assertEqual(candidates[0]["value_text"], "-72")
        self.assertEqual(candidates[0]["interface"], "wlan1")
        self.assertEqual(candidates[0]["boot_id"], "old-boot")
        self.assertEqual(candidates[0]["units"], "dBm")
        self.assertNotIn("-50", json.dumps(candidates))
        self.assertEqual((self.root / "evidence/rotated/kern.log.1.gz").read_bytes(), b"verbatim archived bytes")
        self.assertTrue(any("_TRANSPORT=kernel" in argv for argv in self.commands))
        self.assertFalse(any("-k" in argv or "--boot" in argv for argv in self.commands))
        self.assertFalse(any(token in argv for argv in self.commands
                             for token in ("ssh", "sudo", "scan", "set", "delete", "restart")))

    def test_dedicated_board_sampler_journal_is_collected(self):
        def sampler(argv, *, stdout, stderr, **kwargs):
            if "SYSLOG_IDENTIFIER=pHostInfo-wifi" in argv:
                stdout.write((json.dumps({"__REALTIME_TIMESTAMP": "1800000005000000",
                    "_BOOT_ID": "flight-boot", "SYSLOG_IDENTIFIER": "pHostInfo-wifi",
                    "MESSAGE": "interface=wlan1 status_valid=1 link_up=1 signal_valid=1 signal=-61.0 dBm sample_time=1800000005.0"}) + "\n").encode())
            return subprocess.CompletedProcess(argv, 0)
        with mock.patch.object(wifi.subprocess, "run", side_effect=sampler):
            result = self.collect()
        self.assertEqual(result["journal_rssi_candidates"], 1)
        sample = json.loads((self.root / "evidence/journal-rssi-candidates.json").read_text())[0]
        self.assertEqual(sample["source"], "journal-wifi-samples.stdout")
        self.assertEqual(sample["units"], "dBm")
        self.assertEqual(sample["interface"], "wlan1")

    def test_missing_journals_do_not_turn_current_rssi_into_history(self):
        with mock.patch.object(wifi.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)):
            result = self.collect()
        self.assertEqual(result["journal_records"], 0)
        self.assertEqual(result["journal_rssi_candidates"], 0)
        self.assertEqual(result["rssi_history_status"], "not_found_in_exported_journals")
        self.assertIn("-50", (self.root / "evidence/current-proc-net-wireless").read_text())
        self.assertTrue(result["warnings"])

    def test_unavailable_commands_retain_errors_without_preventing_other_evidence(self):
        def failure(argv, *, stdout, stderr, **kwargs):
            if "NetworkManager.service" in argv:
                stderr.write(b"permission denied")
                return subprocess.CompletedProcess(argv, 1)
            return self.run_command(argv, stdout=stdout, stderr=stderr, **kwargs)
        with mock.patch.object(wifi.subprocess, "run", side_effect=failure):
            result = self.collect()
        self.assertEqual(result["commands"]["journal-network"]["exit_code"], 1)
        self.assertIn("permission denied", (self.root / "evidence/journal-network.stderr").read_text())
        self.assertTrue((self.root / "evidence/current-proc-net-wireless").is_file())
        self.assertTrue(any("journal-network" in warning for warning in result["warnings"]))

    def test_timeout_is_bounded_and_preserves_partial_output(self):
        destination = self.root / "command"
        destination.mkdir()
        def timeout(argv, *, stdout, **kwargs):
            stdout.write(b"partial")
            raise subprocess.TimeoutExpired(argv, 20)
        with mock.patch.object(wifi.subprocess, "run", side_effect=timeout):
            result = wifi.capture_command(destination, "test", ["journalctl"])
        self.assertIsNone(result["exit_code"])
        self.assertEqual((destination / "test.stdout").read_bytes(), b"partial")

    def test_rotated_symlinks_are_not_followed(self):
        (self.var_log / "syslog").symlink_to(self.log / "test.alog")
        with mock.patch.object(wifi.subprocess, "run", side_effect=self.run_command):
            result = self.collect()
        self.assertIn("error", result["rotated_logs"][0])
        self.assertFalse((self.root / "evidence/rotated/syslog").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
