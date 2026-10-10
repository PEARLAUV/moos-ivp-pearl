#!/usr/bin/env python3
"""Native MOOS integration coverage for named and aggregate checks."""
from __future__ import annotations

import json
import os
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MOOS_BIN = Path(os.environ.get("MOOS_BIN_DIR", ROOT.parent / "moos-ivp" / "bin"))
CHECK_BINARY = Path(os.environ.get("UCHECKS_BINARY", ROOT / "bin" / "uChecks"))


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


class ChecksIntegrationTest(unittest.TestCase):
    def setUp(self) -> None:
        for binary in (MOOS_BIN / "MOOSDB", MOOS_BIN / "pAntler", MOOS_BIN / "uPokeDB",
                       MOOS_BIN / "uQueryDB", CHECK_BINARY):
            if not binary.is_file():
                raise RuntimeError(f"Missing dependency: {binary}")
        self.temp = tempfile.TemporaryDirectory(prefix="uchecks-")
        self.path = Path(self.temp.name)
        self.mission = self.path / "checks.moos"
        lease = self._testMethodName.startswith("test_result_lease_")
        extra_apps = (
            f"Run = {MOOS_BIN / 'pLogger'} @ NewConsole=false\n"
            f"  Run = {MOOS_BIN / 'uTimerScript'} @ NewConsole=false ~ uLeaseInputs"
            if lease else ""
        )
        self.mission.write_text(f"""
ServerHost=127.0.0.1
ServerPort={free_port()}
Community=uchecks_test
ProcessConfig=ANTLER
{{
  MSBetweenLaunches=100
  Run = {MOOS_BIN / 'MOOSDB'} @ NewConsole=false
  Run = {CHECK_BINARY} @ NewConsole=false
  Run = {CHECK_BINARY} @ NewConsole=false ~ uChecksSession
  {extra_apps}
}}
ProcessConfig=uChecksSession
{{
  AppTick=20
  CommsTick=20
  condition=SESSION_ENABLE=true
  session_var=CHECK_SESSION
  result_var=SESSION_ALL
  result_prefix=SESSION_
  check=name=FIX,timeout=5
  require=check=FIX,var=GPS_QUALITY,type=string,one_of=1|2|4|5,on_mismatch=fail
}}
ProcessConfig=uChecks
{{
  AppTick=20
  CommsTick=20
  condition=CHECK_ENABLE=true
  result_var=TEST_ALL
  result_prefix=TEST_
  {"result_interval=0.2" if lease else ""}
  check=name=GPS,timeout=12,max_age=3,success_hold=1
  require=check=GPS,var=GPS_FIX,type=double,min=3,on_mismatch=wait
  check=name=FLIGHT_PLAN,timeout=12
  require=check=FLIGHT_PLAN,var=ROUTE_READY,type=string,equals=true,on_mismatch=wait
  check=name=BATTERY,timeout=12,max_age=30
  require=check=BATTERY,var=BATTERY_VALID,type=double,equals=1,on_mismatch=fail
}}
ProcessConfig=uLeaseInputs
{{
  AppTick=20
  CommsTick=20
  condition=REFRESH_GPS=true
  paused=false
  reset_max=unlimited
  reset_time=0.1
  event=var=GPS_FIX,val=3,time=0
}}
ProcessConfig=pLogger
{{
  AppTick=20
  CommsTick=20
  File=LOG
  PATH=./
  AsyncLog=true
  FileTimeStamp=false
  WildCardLogging=true
}}
""", encoding="utf-8")
        self.log = (self.path / "antler.log").open("w", encoding="utf-8")
        self.antler = subprocess.Popen([str(MOOS_BIN / "pAntler"), str(self.mission)],
                                       cwd=self.path, stdout=self.log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
        time.sleep(1.5)
        self.assertIsNone(self.antler.poll(), "pAntler exited")

    def tearDown(self) -> None:
        if self.antler.poll() is None:
            os.killpg(self.antler.pid, signal.SIGTERM)
            try:
                self.antler.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(self.antler.pid, signal.SIGKILL)
                self.antler.wait(timeout=3)
        self.log.close()
        self.temp.cleanup()

    def poke(self, *pairs: str) -> None:
        result = subprocess.run([str(MOOS_BIN / "uPokeDB"), str(self.mission),
                                 "--quiet", *pairs], capture_output=True,
                                text=True, timeout=12)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def query(self, *conditions: str, wait: int = 5) -> None:
        result = subprocess.run([str(MOOS_BIN / "uQueryDB"), str(self.mission),
                                 *(f"--condition={condition}" for condition in conditions),
                                 f"--wait={wait}"], capture_output=True, text=True,
                                timeout=wait + 12)
        if result.returncode:
            self.log.flush()
            self.fail(result.stdout + result.stderr +
                      (self.path / "antler.log").read_text(encoding="utf-8"))

    def snapshot(self, variable: str, condition: str) -> dict:
        result = subprocess.run(
            [str(MOOS_BIN / "uQueryDB"), str(self.mission),
             f"--condition={condition}", f"--check_var={variable}",
             "--vo", "--wait=5"],
            cwd=self.path, capture_output=True, text=True, timeout=17,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        raw = (self.path / ".checkvars").read_text(encoding="utf-8").strip()
        self.assertTrue(raw.startswith(variable + "="), raw[:300])
        return json.loads(raw[len(variable) + 1:])

    def result_records(self, variable: str) -> list[tuple[float, str]]:
        records = []
        for path in self.path.glob("**/*.alog"):
            for line in path.read_text().splitlines():
                fields = line.split(None, 3)
                if len(fields) != 4 or fields[1] != variable:
                    continue
                try:
                    records.append((float(fields[0]), fields[3].strip()))
                except ValueError:
                    continue
        return records

    def test_result_lease_refreshes_active_evaluation_not_retained_success(self) -> None:
        self.poke("CHECK_ENABLE:=true", "REFRESH_GPS:=true",
                  "ROUTE_READY:=true", "BATTERY_VALID=1")
        self.query("TEST_ALL==success")
        time.sleep(0.8)
        for variable in ("TEST_ALL", "TEST_GPS", "TEST_ALL_STATE", "TEST_GPS_STATE"):
            with self.subTest(variable=variable):
                records = self.result_records(variable)
                successes = [r for r in records if r[1] == "success" or
                             r[1].startswith("status=success,")]
                self.assertGreaterEqual(len(successes), 3, records)
                self.assertGreater(successes[-1][0], successes[0][0] + 0.4)

        self.poke("CHECK_ENABLE:=false")
        state = self.snapshot("TEST_ALL_SNAPSHOT", "CHECK_ENABLE==false")
        self.assertFalse(state["active"])
        time.sleep(0.3)
        frozen = {key: len(self.result_records(key))
                  for key in ("TEST_ALL", "TEST_GPS")}
        time.sleep(0.8)
        self.assertEqual(frozen, {key: len(self.result_records(key)) for key in frozen})

        # Re-enable, then stop telemetry. Heartbeats must publish the newly
        # evaluated non-success, never renew success using stale GPS input.
        self.poke("CHECK_ENABLE:=true", "REFRESH_GPS:=false")
        time.sleep(3.5)
        self.query("TEST_ALL!=success")
        state = self.snapshot("TEST_ALL_SNAPSHOT", "TEST_ALL!=success")
        self.assertFalse(state["checks"]["GPS"]["requirements"][0]["fresh"])
        self.assertNotEqual(state["aggregate"]["status"], "success")
        time.sleep(0.3)
        records = self.result_records("TEST_ALL")
        last_non_success = max(t for t, v in records if v != "success")
        self.assertFalse(any(t > last_non_success and v == "success" for t, v in records))

    def test_snapshot_periodic_and_final_inactive(self) -> None:
        self.poke("CHECK_ENABLE=true", "ROUTE_READY:=true", "BATTERY_VALID=1")
        self.query("TEST_ALL==pending")
        time.sleep(0.2)
        first = self.snapshot("TEST_ALL_SNAPSHOT", "TEST_ALL==pending")
        self.assertEqual(first["schema_version"], 1)
        self.assertEqual(first["instance"], "TEST_ALL")
        self.assertTrue(first["active"])
        self.assertEqual(first["aggregate"],
                         {"variable": "TEST_ALL", "status": "pending"})
        self.assertEqual(first["activation"]["inputs"]["CHECK_ENABLE"]["value"], "true")
        self.assertEqual(first["inputs"]["BATTERY_VALID"]["value"], 1)
        self.assertEqual(first["inputs"]["GPS_FIX"]["value"], None)
        self.assertEqual(first["checks"]["GPS"]["requirements"][0]["matched"], None)
        self.assertEqual(first["checks"]["FLIGHT_PLAN"]["requirements"][0]["max_age_s"], None)
        self.assertEqual(first["checks"]["BATTERY"]["requirements"][0]["expected"],
                         {"equals": 1})
        time.sleep(1.2)
        second = self.snapshot("TEST_ALL_SNAPSHOT", "TEST_ALL==pending")
        self.assertEqual(second["epoch"], first["epoch"])
        self.assertGreater(second["sequence"], first["sequence"])
        self.assertGreater(second["published_at_moos"], first["published_at_moos"])
        self.poke("CHECK_ENABLE=false")
        final = second
        for _ in range(20):
            final = self.snapshot("TEST_ALL_SNAPSHOT", "CHECK_ENABLE==false")
            if not final["active"]:
                break
            time.sleep(0.15)
        self.assertFalse(final["active"])
        self.assertGreater(final["sequence"], second["sequence"])
        time.sleep(1.2)
        self.assertEqual(self.snapshot("TEST_ALL_SNAPSHOT", "CHECK_ENABLE==false")["sequence"],
                         final["sequence"])

    def test_snapshot_session_resets_latch(self) -> None:
        self.poke("SESSION_ENABLE:=true", "CHECK_SESSION:=alpha", "GPS_QUALITY:=6")
        self.query("SESSION_ALL==fail")
        time.sleep(0.15)
        failed = self.snapshot("SESSION_ALL_SNAPSHOT", "SESSION_ALL==fail")
        self.assertEqual(failed["session"],
                         {"variable": "CHECK_SESSION", "id": "alpha"})
        self.assertTrue(failed["checks"]["FIX"]["failure_latched"])
        self.assertEqual(failed["checks"]["FIX"]["requirements"][0]["expected"],
                         {"one_of": ["1", "2", "4", "5"]})
        self.assertEqual(failed["checks"]["FIX"]["requirements"][0]["reason"],
                         "mismatch")
        self.poke("CHECK_SESSION:=beta", "GPS_QUALITY:=2")
        self.query("SESSION_ALL==success")
        time.sleep(0.15)
        passed = self.snapshot("SESSION_ALL_SNAPSHOT", "SESSION_ALL==success")
        self.assertEqual(passed["session"]["id"], "beta")
        self.assertFalse(passed["checks"]["FIX"]["failure_latched"])
        self.assertGreater(passed["sequence"], failed["sequence"])
        self.poke("CHECK_SESSION:=none")
        self.query("SESSION_ALL==inactive")
        inactive = passed
        for _ in range(20):
            inactive = self.snapshot("SESSION_ALL_SNAPSHOT", "SESSION_ALL==inactive")
            if not inactive["active"]:
                break
            time.sleep(0.15)
        self.assertFalse(inactive["active"])
        self.assertIsNone(inactive["session"]["id"])

    def test_all_success_and_one_failure(self) -> None:
        self.query("TEST_ALL==inactive")
        self.poke("CHECK_ENABLE=true", "ROUTE_READY:=true", "BATTERY_VALID=1")
        self.query("TEST_FLIGHT_PLAN==success", "TEST_BATTERY==success",
                   "TEST_ALL==pending")
        self.poke("GPS_FIX=3")
        self.query("TEST_GPS==success", "TEST_ALL==success")
        self.poke("BATTERY_VALID=0")
        self.query("TEST_BATTERY==fail", "TEST_ALL==fail")
        self.poke("BATTERY_VALID=1")
        self.query("TEST_ALL==fail")  # Failure latch.

    def test_per_check_age_does_not_expire_flight_plan(self) -> None:
        self.poke("CHECK_ENABLE=true", "ROUTE_READY:=true")
        self.query("TEST_FLIGHT_PLAN==success")
        time.sleep(3.5)
        self.query("TEST_FLIGHT_PLAN==success", "TEST_ALL==pending")
        state = self.snapshot("TEST_ALL_SNAPSHOT", "TEST_ALL==pending")
        self.assertGreater(state["inputs"]["ROUTE_READY"]["age_s"], 3)
        self.assertTrue(state["checks"]["FLIGHT_PLAN"]["requirements"][0]["fresh"])
        self.poke("CHECK_ENABLE=false")
        time.sleep(3.5)
        self.query("TEST_FLIGHT_PLAN==success", "TEST_ALL==expired")
        self.poke("CHECK_ENABLE=true", "ROUTE_READY:=false")
        self.query("TEST_FLIGHT_PLAN==pending")

    def test_session_resets_latches_and_string_one_of(self) -> None:
        self.poke("SESSION_ENABLE:=true", "CHECK_SESSION:=alpha", "GPS_QUALITY:=2")
        self.query("SESSION_FIX==success", "SESSION_ALL==success")
        self.poke("GPS_QUALITY:=6")
        self.query("SESSION_ALL==fail")
        self.poke("CHECK_SESSION:=beta", "GPS_QUALITY:=1")
        self.query("SESSION_ALL==success")
        result = subprocess.run([str(MOOS_BIN / "uQueryDB"), str(self.mission),
                                 "--condition=SESSION_ALL==success",
                                 "--check_var=SESSION_ALL_STATE", "--vo", "--wait=5"],
                                cwd=self.path, capture_output=True, text=True, timeout=17)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("session=beta", (self.path / ".checkvars").read_text())
        self.poke("CHECK_SESSION:=gamma", "GPS_QUALITY:=4")
        self.query("SESSION_ALL==success")
        self.poke("CHECK_SESSION:=none")
        self.query("SESSION_ALL==inactive")

    def test_mismatched_type_fails_aggregate(self) -> None:
        self.poke("CHECK_ENABLE=true", "BATTERY_VALID:=1")
        self.query("TEST_BATTERY==fail", "TEST_ALL==fail")
        time.sleep(0.15)
        state = self.snapshot("TEST_ALL_SNAPSHOT", "TEST_ALL==fail")
        self.assertEqual(state["inputs"]["BATTERY_VALID"]["type"], "string")
        self.assertEqual(state["checks"]["BATTERY"]["requirements"][0]["reason"],
                         "wrong_type")
        self.assertTrue(state["checks"]["BATTERY"]["failure_latched"])

    def test_one_check_timeout_fails_aggregate(self) -> None:
        self.poke("CHECK_ENABLE=true", "ROUTE_READY:=true", "BATTERY_VALID=1")
        self.query("TEST_GPS==fail", "TEST_ALL==fail", wait=16)


if __name__ == "__main__":
    unittest.main()
