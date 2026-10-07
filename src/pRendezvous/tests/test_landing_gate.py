#!/usr/bin/env python3

"""End-to-end fail-closed landing-gate checks for pRendezvous."""

from __future__ import annotations

import os
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
MOOS_BIN_DIR = Path(
    os.environ.get("MOOS_BIN_DIR", REPO_ROOT.parent / "moos-ivp" / "bin")
)
APP_BINARY = Path(
    os.environ.get("PRENDEZVOUS_BINARY", REPO_ROOT / "bin" / "pRendezvous")
)
PMEDIATOR_BINARY = Path(
    os.environ.get(
        "PMEDIATOR_BINARY", REPO_ROOT.parent / "moos-ivp-swarm" / "bin" / "pMediator"
    )
)


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def free_udp_port() -> int:
    with socket.socket(type=socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


class LandingGateIntegrationTest(unittest.TestCase):
    def setUp(self) -> None:
        required = [
            MOOS_BIN_DIR / "MOOSDB",
            MOOS_BIN_DIR / "pAntler",
            MOOS_BIN_DIR / "uPokeDB",
            MOOS_BIN_DIR / "uQueryDB",
            APP_BINARY,
        ]
        missing = [str(path) for path in required if not path.is_file()]
        if missing:
            raise RuntimeError("Build dependencies are missing: " + ", ".join(missing))

        self.moos_port = free_port()
        self.temp_dir = tempfile.TemporaryDirectory(prefix="rendezvous-gate-")
        self.temp_path = Path(self.temp_dir.name)
        self.mission_path = self.temp_path / "landing_gate.moos"
        self.antler_log_path = self.temp_path / "pAntler.log"
        required_check_tests = {
            "test_required_check_pending_waits",
            "test_required_check_failure_aborts",
            "test_required_check_success_proceeds",
            "test_required_check_success_still_requires_other_gates",
        }
        pearl_role = self._testMethodName in {
            "test_pearl_rejects_unready_platform",
            "test_pearl_separation_is_continuous",
            "test_pearl_reconciles_completion_and_restores_controls",
        } | required_check_tests
        role = "pearl" if pearl_role else "uav"
        ownship = "pearl" if pearl_role else "uav"
        peer_node = "uav" if pearl_role else "pearl"
        arrival_dwell = 4 if pearl_role else 0
        acquire_timeout = 5 if self._testMethodName == "test_target_timeout" else 60
        battery_required = self._testMethodName in {
            "test_battery_priority_thresholds",
            "test_invalid_battery_blocks_start",
            "test_low_battery_prioritizes_recovery",
            "test_pearl_separation_is_continuous",
            "test_stale_battery_blocks_start",
        }
        platform_required = self._testMethodName in {
            "test_pearl_rejects_unready_platform",
            "test_platform_gate_blocks_commit",
            "test_platform_health_blocks_start",
            "test_stale_platform_data_blocks_start",
        }
        battery_max_age = (
            1 if self._testMethodName == "test_stale_battery_blocks_start" else 30
        )
        platform_max_age = (
            1
            if self._testMethodName == "test_stale_platform_data_blocks_start"
            else 30
        )
        secondary_enabled = self._testMethodName == "test_secondary_recovery_lands"
        require_check = (
            "  require_check = TEST_RECOVERY_CHECKS_STATE\n"
            "  require_check = TEST_PLATFORM_CHECKS_STATE\n"
            if self._testMethodName in required_check_tests
            else ""
        )
        self.mission_path.write_text(
            f"""ServerHost = 127.0.0.1
ServerPort = {self.moos_port}
Community  = landing_gate_test

ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = {MOOS_BIN_DIR / 'MOOSDB'} @ NewConsole = false
  Run = {APP_BINARY} @ NewConsole = false
}}

ProcessConfig = pRendezvous
{{
  AppTick   = 10
  CommsTick = 10
  role      = {role}
  ownship   = {ownship}
  peer_node = {peer_node}
  require_health = false
  require_battery = {str(battery_required).lower()}
{require_check}  battery_max_age = {battery_max_age}
  min_battery = 25
  critical_battery = 15
  require_flight_state = false
  require_landing_target = true
  require_platform_ready = {str(platform_required).lower()}
  require_platform_health = {str(platform_required).lower()}
  min_pearl_battery = 15
  max_landing_wind_speed = 4
  platform_data_max_age = {platform_max_age}
  nav_stale_thresh = 30
  request_timeout = 30
  route_timeout = 10
  transit_timeout = 30
  arrival_radius = 5
  arrival_dwell = {arrival_dwell}
  max_peer_separation = 3
  landing_target_max_age = 5
  landing_target_lock_dwell = 4
  landing_target_acquire_timeout = {acquire_timeout}
  landing_target_max_offset = 1.5
  landing_target_max_angle = 0.2
  expected_target_num = 0
  reacquire_update_interval = 0.2
  reacquire_update_distance = 0.1
  state_post_interval = 0.1
  secondary_landing_enabled = {str(secondary_enabled).lower()}
  secondary_landing_x = 10
  secondary_landing_y = 15
  secondary_arrival_radius = 2
  secondary_arrival_dwell = 0.5
  secondary_landing_delay = 1
}}
""",
            encoding="utf-8",
        )
        self.antler_log = self.antler_log_path.open("w", encoding="utf-8")
        self.antler = subprocess.Popen(
            [str(MOOS_BIN_DIR / "pAntler"), str(self.mission_path)],
            cwd=self.temp_path,
            stdout=self.antler_log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        self.wait_for_moosdb()

    def tearDown(self) -> None:
        if self.antler.poll() is None:
            os.killpg(self.antler.pid, signal.SIGTERM)
            try:
                self.antler.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(self.antler.pid, signal.SIGKILL)
                self.antler.wait(timeout=3)
        self.antler_log.close()
        self.temp_dir.cleanup()

    def wait_for_moosdb(self) -> None:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if self.antler.poll() is not None:
                self.fail("pAntler exited before MOOSDB became available")
            try:
                with socket.create_connection(
                    ("127.0.0.1", self.moos_port), timeout=0.2
                ):
                    return
            except OSError:
                time.sleep(0.1)
        self.fail("MOOSDB did not become available within five seconds")

    def poke(self, *pairs: str) -> None:
        result = subprocess.run(
            [str(MOOS_BIN_DIR / "uPokeDB"), str(self.mission_path), "--quiet", *pairs],
            cwd=self.temp_path,
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
        if result.returncode != 0:
            self.fail(f"uPokeDB failed: {pairs}\n{result.stdout}\n{result.stderr}")

    def assert_query(self, *conditions: str, wait: float = 3) -> None:
        result = subprocess.run(
            [
                str(MOOS_BIN_DIR / "uQueryDB"),
                str(self.mission_path),
                *(f"--condition={condition}" for condition in conditions),
                f"--wait={wait}",
            ],
            cwd=self.temp_path,
            capture_output=True,
            text=True,
            timeout=wait + 3,
            check=False,
        )
        if result.returncode != 0:
            output = self.antler_log_path.read_text(encoding="utf-8", errors="replace")
            self.fail(
                f"MOOS conditions did not become true: {conditions}\n"
                f"{result.stdout}\n{result.stderr}\npAntler:\n{output}"
            )

    def query_value(self, variable: str, condition: str) -> str:
        checkvars = self.temp_path / ".checkvars"
        checkvars.unlink(missing_ok=True)
        result = subprocess.run(
            [
                str(MOOS_BIN_DIR / "uQueryDB"),
                str(self.mission_path),
                f"--condition={condition}",
                f"--check_var={variable}",
                "--csv",
                "--wait=3",
            ],
            cwd=self.temp_path,
            capture_output=True,
            text=True,
            timeout=6,
            check=False,
        )
        if result.returncode != 0 or not checkvars.is_file():
            output = self.antler_log_path.read_text(encoding="utf-8", errors="replace")
            self.fail(
                f"Could not query {variable} under {condition}\n"
                f"{result.stdout}\n{result.stderr}\npAntler:\n{output}"
            )
        line = checkvars.read_text(encoding="utf-8").strip()
        return line.split(",", 1)[1]

    def enter_acquisition(self) -> str:
        self.poke("NAV_X=0", "NAV_Y=0")
        time.sleep(0.2)
        self.poke("RENDEZVOUS_START=true")
        session = self.query_value("UAV_RENDEZVOUS_SESSION", "DB_UPTIME>1")
        self.assertTrue(session, "rendezvous request did not create a session")
        self.poke(f"RENDEZVOUS_PROPOSAL:=id={session}#x=0#y=0")
        time.sleep(0.2)
        self.poke("ROUTE_BUFFER_VEHICLE_STATE:=DEPLOY_ACCEPTED")
        phase = self.query_value(
            "UAV_RENDEZVOUS_PHASE", "UAV_RENDEZVOUS_PHASE!=__missing__"
        )
        self.assertEqual(phase, "RENDEZVOUS")
        self.assert_query("UAV_RENDEZVOUS_ARRIVED=1")
        self.poke(
            "NODE_REPORT:=NAME=pearl,X=0.5,Y=0",
            f"LANDING_CLEARANCE:=id={session}#status=cleared#x=0.5#y=0",
        )
        self.assert_query("UAV_RENDEZVOUS_PHASE=ACQUIRING_TARGET")
        return session

    def post_target(self, *, available: int = 1, target_num: int = 0) -> None:
        self.poke(
            f"UAV_LANDING_TARGET_AVAILABLE={available}",
            "UAV_LANDING_TARGET_AGE=0.1",
            f"UAV_LANDING_TARGET_TARGET_NUM={target_num}",
            "UAV_LANDING_TARGET_POSITION_VALID=1",
            "UAV_LANDING_TARGET_X=0.2",
            "UAV_LANDING_TARGET_Y=0.1",
            "UAV_LANDING_TARGET_ANGLE_X=0.01",
            "UAV_LANDING_TARGET_ANGLE_Y=0.01",
        )

    def test_wrong_target_and_lock_loss_cannot_commit(self) -> None:
        self.enter_acquisition()

        self.poke("UAV_PREC_LAND_REQUEST=true")
        result = self.query_value(
            "UAV_PREC_LAND_RESULT", "UAV_PREC_LAND_RESULT!=__missing__"
        )
        self.assertEqual(
            result,
            "status=rejected#reason=coordinated_clearance_required",
        )
        self.assert_query("UAV_RENDEZVOUS_PHASE=ACQUIRING_TARGET")

        self.post_target(target_num=0)
        self.poke("UAV_LANDING_TARGET_AGE=6")
        self.assert_query(
            "UAV_LANDING_GATE_REASON=landing_target_too_old",
            "UAV_RENDEZVOUS_PHASE=ACQUIRING_TARGET",
        )

        self.post_target(target_num=1)
        self.assert_query(
            "UAV_LANDING_GATE_REASON=unexpected_landing_target",
            "UAV_RENDEZVOUS_PHASE=ACQUIRING_TARGET",
        )

        self.post_target(target_num=0)
        self.post_target(available=0)
        self.assert_query(
            "UAV_LANDING_GATE_REASON=landing_target_unavailable",
            "UAV_RENDEZVOUS_PHASE=ACQUIRING_TARGET",
        )

        self.post_target(target_num=0)
        self.post_target(target_num=0)
        self.post_target(target_num=0)
        self.post_target(target_num=0)
        self.assert_query(
            "UAV_LANDING_GATE_READY=1",
            "UAV_RENDEZVOUS_PHASE=LANDING",
            "UAV_PREC_LAND_COMMIT=true",
        )

    def test_target_timeout(self) -> None:
        self.enter_acquisition()
        self.assert_query(
            "UAV_RENDEZVOUS_PHASE=ABORT",
            "UAV_LANDING_GATE_REASON=landing_target_timeout",
            wait=7,
        )

    def test_secondary_recovery_lands(self) -> None:
        self.poke(
            "NAV_X=0",
            "NAV_Y=0",
            "UAV_IS_ARMED=true",
            "UAV_LANDED_STATE:=IN_AIR",
        )
        time.sleep(0.2)
        self.poke("RENDEZVOUS_START=true")
        self.assert_query("UAV_RENDEZVOUS_PHASE=REQUESTING")
        self.poke("RENDEZVOUS_ABORT=true")
        self.assert_query(
            "UAV_RENDEZVOUS_PHASE=ABORT",
            "UAV_SECONDARY_RECOVERY_STATE=TRANSITING",
            "UAV_SECONDARY_RECOVERY_POINT={10,15}",
        )

        self.poke("NAV_X=10", "NAV_Y=15")
        self.assert_query("UAV_SECONDARY_RECOVERY_STATE=ARRIVED")
        self.assert_query(
            "UAV_SECONDARY_RECOVERY_STATE=LANDING",
            "UAV_PREC_LAND_COMMIT=true",
        )

        self.poke("UAV_LANDED_STATE:=ON_GROUND", "UAV_IS_ARMED=false")
        self.assert_query("UAV_SECONDARY_RECOVERY_STATE=LANDED")

    def test_low_battery_prioritizes_recovery(self) -> None:
        self.poke(
            "NAV_X=0",
            "NAV_Y=0",
            "UAV_BATTERY_SOC=10",
            "UAV_BATTERY_DATA_VALID=true",
        )
        time.sleep(0.2)
        self.poke("RENDEZVOUS_START=true")
        self.assert_query(
            "UAV_RENDEZVOUS_PHASE=REQUESTING",
            "UAV_RECOVERY_PRIORITY=EMERGENCY",
        )

    def test_battery_priority_thresholds(self) -> None:
        self.poke("UAV_BATTERY_DATA_VALID=true", "UAV_BATTERY_SOC=25")
        self.assert_query("UAV_RECOVERY_PRIORITY=NORMAL")
        self.poke("UAV_BATTERY_SOC=20")
        self.assert_query("UAV_RECOVERY_PRIORITY=URGENT")
        self.poke("UAV_BATTERY_SOC=14.9")
        self.assert_query("UAV_RECOVERY_PRIORITY=EMERGENCY")

    def test_invalid_battery_blocks_start(self) -> None:
        self.poke(
            "NAV_X=0",
            "NAV_Y=0",
            "UAV_BATTERY_SOC=80",
            "UAV_BATTERY_DATA_VALID=false",
        )
        time.sleep(0.2)
        self.poke("RENDEZVOUS_START=true")
        self.assert_query(
            "UAV_RENDEZVOUS_PHASE=ABORT",
            "UAV_RECOVERY_PRIORITY=UNKNOWN",
        )

    def test_stale_battery_blocks_start(self) -> None:
        self.poke("UAV_BATTERY_SOC=80", "UAV_BATTERY_DATA_VALID=true")
        time.sleep(1.5)
        self.poke("NAV_X=0", "NAV_Y=0")
        time.sleep(0.2)
        self.poke("RENDEZVOUS_START=true")
        self.assert_query(
            "UAV_RENDEZVOUS_PHASE=ABORT",
            "UAV_RECOVERY_PRIORITY=UNKNOWN",
        )

    def test_platform_health_blocks_start(self) -> None:
        self.poke(
            "NAV_X=0",
            "NAV_Y=0",
            "PEARL_BATTERY_SOC=80",
            "PEARL_BATTERY_DATA_VALID=true",
            "PEARL_WIND_SPEED=2",
            "PEARL_WIND_DATA_VALID=true",
            "PEARL_PROC_WATCH_ALL_OK=false",
        )
        time.sleep(0.2)
        self.poke("RENDEZVOUS_START=true")
        state = self.query_value(
            "UAV_RENDEZVOUS_STATE", "UAV_RENDEZVOUS_PHASE=ABORT"
        )
        self.assertIn("reason=pearl_health_not_ready", state)

    def test_stale_platform_data_blocks_start(self) -> None:
        self.poke(
            "PEARL_BATTERY_SOC=80",
            "PEARL_BATTERY_DATA_VALID=true",
            "PEARL_WIND_SPEED=2",
            "PEARL_WIND_DATA_VALID=true",
            "PEARL_PROC_WATCH_ALL_OK=true",
        )
        time.sleep(1.5)
        self.poke("NAV_X=0", "NAV_Y=0")
        time.sleep(0.2)
        self.poke("RENDEZVOUS_START=true")
        state = self.query_value(
            "UAV_RENDEZVOUS_STATE", "UAV_RENDEZVOUS_PHASE=ABORT"
        )
        self.assertIn("reason=pearl_battery_stale", state)

    def test_platform_gate_blocks_commit(self) -> None:
        self.poke(
            "PEARL_BATTERY_SOC=80",
            "PEARL_BATTERY_DATA_VALID=true",
            "PEARL_WIND_SPEED=2",
            "PEARL_WIND_DATA_VALID=true",
            "PEARL_PROC_WATCH_ALL_OK=true",
        )
        self.enter_acquisition()

        self.poke("PEARL_WIND_SPEED=6")
        self.post_target(target_num=0)
        self.assert_query(
            "UAV_LANDING_GATE_READY=0",
            "UAV_LANDING_GATE_REASON=landing_wind_high",
            "UAV_RENDEZVOUS_PHASE=ACQUIRING_TARGET",
        )

        self.poke("PEARL_WIND_SPEED=2")
        self.post_target(target_num=0)
        self.assert_query(
            "UAV_LANDING_GATE_READY=1",
            "UAV_RENDEZVOUS_PHASE=LANDING",
            "UAV_PREC_LAND_COMMIT=true",
            wait=7,
        )

    def post_required_check(self, session: str, status: str) -> None:
        self.poke(
            f"TEST_RECOVERY_CHECKS_STATE:={status}",
            f"TEST_RECOVERY_CHECKS_STATE_STATE:=status={status},session={session}",
            f"TEST_PLATFORM_CHECKS_STATE:={status}",
            f"TEST_PLATFORM_CHECKS_STATE_STATE:=status={status},session={session}",
        )

    def request_pearl_rendezvous(self, session: str, *, with_nav: bool = True) -> None:
        if with_nav:
            self.poke("NAV_X=0", "NAV_Y=0")
            time.sleep(0.2)
        self.poke(
            f"RENDEZVOUS_REQUEST:=id={session}#x=20#y=0#speed=3"
            "#battery=80#priority=normal#health=ok"
        )

    def test_required_check_pending_waits(self) -> None:
        self.request_pearl_rendezvous("check-pending")
        state = self.query_value(
            "PEARL_RENDEZVOUS_STATE", "PEARL_RENDEZVOUS_PHASE=REQUESTING"
        )
        self.assertIn("reason=required_checks_pending", state)

        # Neither a scalar success nor a stale diagnostic can authorize.
        self.poke("TEST_RECOVERY_CHECKS_STATE:=success")
        self.poke(
            "TEST_RECOVERY_CHECKS_STATE_STATE:="
            "status=success,session=old-session"
        )
        time.sleep(0.3)
        state = self.query_value(
            "PEARL_RENDEZVOUS_STATE", "PEARL_RENDEZVOUS_PHASE=REQUESTING"
        )
        self.assertIn("reason=required_checks_pending", state)

    def test_required_check_failure_aborts(self) -> None:
        self.request_pearl_rendezvous("check-fail")
        # One confirmed failure aborts without waiting for the other check.
        self.poke(
            "TEST_RECOVERY_CHECKS_STATE:=fail",
            "TEST_RECOVERY_CHECKS_STATE_STATE:="
            "status=fail,session=check-fail",
        )
        state = self.query_value(
            "PEARL_RENDEZVOUS_STATE", "PEARL_RENDEZVOUS_PHASE=ABORT"
        )
        self.assertIn(
            "reason=required_check_fail:TEST_RECOVERY_CHECKS_STATE", state
        )

    def test_required_check_success_proceeds(self) -> None:
        self.request_pearl_rendezvous("check-success")
        self.post_required_check("check-success", "success")
        time.sleep(0.3)
        state = self.query_value(
            "PEARL_RENDEZVOUS_STATE", "PEARL_RENDEZVOUS_PHASE=REQUESTING"
        )
        self.assertIn("reason=proposal_sent", state)
        self.assertIn("id=check-success", state)

    def test_required_check_success_still_requires_other_gates(self) -> None:
        self.request_pearl_rendezvous("check-no-nav", with_nav=False)
        self.post_required_check("check-no-nav", "success")
        state = self.query_value(
            "PEARL_RENDEZVOUS_STATE", "PEARL_RENDEZVOUS_PHASE=ABORT"
        )
        self.assertIn("reason=pearl_navigation_stale", state)

    def test_pearl_rejects_unready_platform(self) -> None:
        self.poke(
            "NAV_X=0",
            "NAV_Y=0",
            "PEARL_BATTERY_SOC=10",
            "PEARL_BATTERY_DATA_VALID=true",
            "PEARL_WIND_SPEED=2",
            "PEARL_WIND_DATA_VALID=true",
            "PROC_WATCH_ALL_OK=true",
        )
        time.sleep(0.2)
        self.poke(
            "RENDEZVOUS_REQUEST:=id=blocked#x=20#y=0#speed=3#battery=10"
            "#priority=emergency#health=ok"
        )
        state = self.query_value(
            "PEARL_RENDEZVOUS_STATE", "PEARL_RENDEZVOUS_PHASE=ABORT"
        )
        self.assertIn("reason=pearl_battery_below_reserve", state)

        self.poke("PEARL_BATTERY_SOC=80")
        time.sleep(0.2)
        self.poke(
            "RENDEZVOUS_REQUEST:=id=accepted#x=20#y=0#speed=3#battery=10"
            "#priority=emergency#health=ok"
        )
        self.assert_query("PEARL_RENDEZVOUS_PHASE=REQUESTING")

    def test_pearl_reconciles_completion_and_restores_controls(self) -> None:
        self.poke(
            "NAV_X=0",
            "NAV_Y=0",
            "DEPLOY=true",
            "RETURN=true",
            "STATION_KEEP=false",
            "MOOS_MANUAL_OVERRIDE=false",
        )
        time.sleep(0.2)
        self.poke(
            "RENDEZVOUS_REQUEST:=id=restore#x=20#y=0#speed=3"
            "#battery=80#priority=NORMAL#health=ok"
        )
        self.assert_query("PEARL_RENDEZVOUS_PHASE=REQUESTING")
        self.poke("RENDEZVOUS_RESPONSE:=id=restore#status=accepted")
        self.assert_query(
            "PEARL_RENDEZVOUS_PHASE=RENDEZVOUS",
            "PEARL_RENDEZVOUS_ACTIVE=true",
        )
        update = self.query_value(
            "PEARL_RENDEZVOUS_UPDATE", "PEARL_RENDEZVOUS_UPDATE!=__missing__"
        )
        self.assertIn("name=restore", update)
        self.assertIn(" # points=", update)

        # Periodic UAV phase/session publications provide a reconciliation
        # path when a one-shot mediated landing status is missed.
        self.poke(
            "UAV_RENDEZVOUS_SESSION:=restore",
            "UAV_RENDEZVOUS_PHASE:=COMPLETE",
        )
        self.assert_query(
            "PEARL_RENDEZVOUS_PHASE=COMPLETE",
            "PEARL_CONTROL_RESTORED=true",
            "DEPLOY=true",
            "RETURN=true",
            "STATION_KEEP=false",
            "MOOS_MANUAL_OVERRIDE=false",
        )

    def test_pearl_separation_is_continuous(self) -> None:
        self.poke("NAV_X=0", "NAV_Y=0")
        time.sleep(0.2)
        self.poke(
            "RENDEZVOUS_REQUEST:=id=test#x=20#y=0#speed=3#battery=10"
            "#priority=emergency#health=ok"
        )
        self.assert_query("PEARL_RENDEZVOUS_PHASE=REQUESTING")
        self.poke("RENDEZVOUS_RESPONSE:=id=test#status=accepted")
        self.assert_query("PEARL_RENDEZVOUS_PHASE=RENDEZVOUS")

        # Both vehicles are inside the five-metre rendezvous circle, but they
        # are more than three metres apart, so clearance remains blocked.
        self.poke("NAV_X=2.86", "NAV_Y=0", "NODE_REPORT:=NAME=uav,X=7,Y=0")
        self.assert_query(
            "PEARL_LANDING_GATE_READY=0",
            "PEARL_LANDING_GATE_REASON=peer_separation_too_large",
            "PEARL_RENDEZVOUS_PHASE=RENDEZVOUS",
        )

        # A brief close report starts the dwell, then moving apart must reset
        # it instead of leaving an old 'arrived' decision latched.
        self.poke("NODE_REPORT:=NAME=uav,X=2.9,Y=0")
        self.poke("NODE_REPORT:=NAME=uav,X=7,Y=0")
        self.assert_query(
            "PEARL_LANDING_GATE_REASON=peer_separation_too_large",
            "PEARL_RENDEZVOUS_PHASE=RENDEZVOUS",
        )

        self.poke("NODE_REPORT:=NAME=uav,X=2.9,Y=0")
        self.assert_query(
            "PEARL_RENDEZVOUS_PHASE=ACQUIRING_TARGET",
            "PEARL_LANDING_GATE_READY=1",
            wait=7,
        )
        self.poke("RENDEZVOUS_RESPONSE:=id=test#status=landing")
        self.assert_query("PEARL_RENDEZVOUS_PHASE=LANDING")


class PlatformRouteIntegrationTest(unittest.TestCase):
    def setUp(self) -> None:
        if not PMEDIATOR_BINARY.is_file():
            self.skipTest(
                "native pMediator unavailable; set PMEDIATOR_BINARY to run "
                "the mediated two-community test"
            )
        required = [
            MOOS_BIN_DIR / "MOOSDB",
            MOOS_BIN_DIR / "pAntler",
            MOOS_BIN_DIR / "pShare",
            MOOS_BIN_DIR / "uFldMessageHandler",
            MOOS_BIN_DIR / "uPokeDB",
            MOOS_BIN_DIR / "uQueryDB",
            APP_BINARY,
        ]
        missing = [str(path) for path in required if not path.is_file()]
        if missing:
            raise RuntimeError("Build dependencies are missing: " + ", ".join(missing))

        self.temp_dir = tempfile.TemporaryDirectory(prefix="rendezvous-route-")
        self.temp_path = Path(self.temp_dir.name)
        self.pearl_db_port = free_port()
        self.uav_db_port = free_port()
        self.pearl_pshare_port = free_udp_port()
        self.uav_pshare_port = free_udp_port()
        self.pearl_mission = self.temp_path / "pearl.moos"
        self.uav_mission = self.temp_path / "uav.moos"
        self.pearl_log_path = self.temp_path / "pearl.log"
        self.uav_log_path = self.temp_path / "uav.log"

        self.pearl_mission.write_text(
            f"""ServerHost = 127.0.0.1
ServerPort = {self.pearl_db_port}
Community  = pearl

ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = {MOOS_BIN_DIR / 'MOOSDB'} @ NewConsole = false
  Run = {MOOS_BIN_DIR / 'pShare'} @ NewConsole = false
  Run = {PMEDIATOR_BINARY} @ NewConsole = false
  Run = {MOOS_BIN_DIR / 'uFldMessageHandler'} @ NewConsole = false
  Run = {APP_BINARY} @ NewConsole = false
}}

ProcessConfig = pShare
{{
  AppTick   = 10
  CommsTick = 10
  input = route = localhost:{self.pearl_pshare_port}
  output = src_name=MEDIATED_MESSAGE_LOCAL, dest_name=MEDIATED_MESSAGE, route=127.0.0.1:{self.uav_pshare_port}
  output = src_name=ACK_MESSAGE_LOCAL, dest_name=ACK_MESSAGE, route=127.0.0.1:{self.uav_pshare_port}
  output = src_name=PEARL_BATTERY_SOC, route=127.0.0.1:{self.uav_pshare_port}
  output = src_name=PEARL_BATTERY_DATA_VALID, route=127.0.0.1:{self.uav_pshare_port}
  output = src_name=PEARL_WIND_SPEED, route=127.0.0.1:{self.uav_pshare_port}
  output = src_name=PEARL_WIND_DATA_VALID, route=127.0.0.1:{self.uav_pshare_port}
  output = src_name=PROC_WATCH_ALL_OK, dest_name=PEARL_PROC_WATCH_ALL_OK, route=127.0.0.1:{self.uav_pshare_port}
}}

ProcessConfig = pMediator
{{
  AppTick   = 10
  CommsTick = 10
  vname         = pearl
  mates         = uav
  resend_thresh = 1
  max_tries     = 10
}}

ProcessConfig = uFldMessageHandler
{{
  AppTick   = 10
  CommsTick = 10
  strict_addressing = true
}}

ProcessConfig = pRendezvous
{{
  AppTick   = 10
  CommsTick = 10
  role      = pearl
  ownship   = pearl
  peer_node = uav
  require_health = false
  require_battery = true
  require_flight_state = false
  require_landing_target = false
  require_platform_ready = true
  require_platform_health = true
  min_pearl_battery = 15
  max_landing_wind_speed = 4
  platform_data_max_age = 30
  nav_stale_thresh = 30
  request_timeout = 20
  transit_timeout = 30
  arrival_radius = 5
  arrival_dwell = 1
  max_peer_separation = 3
  state_post_interval = 0.1
}}
""",
            encoding="utf-8",
        )
        self.uav_mission.write_text(
            f"""ServerHost = 127.0.0.1
ServerPort = {self.uav_db_port}
Community  = uav

ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = {MOOS_BIN_DIR / 'MOOSDB'} @ NewConsole = false
  Run = {MOOS_BIN_DIR / 'pShare'} @ NewConsole = false
  Run = {PMEDIATOR_BINARY} @ NewConsole = false
  Run = {MOOS_BIN_DIR / 'uFldMessageHandler'} @ NewConsole = false
  Run = {APP_BINARY} @ NewConsole = false
}}

ProcessConfig = pShare
{{
  AppTick   = 10
  CommsTick = 10
  input = route = localhost:{self.uav_pshare_port}
  output = src_name=MEDIATED_MESSAGE_LOCAL, dest_name=MEDIATED_MESSAGE, route=127.0.0.1:{self.pearl_pshare_port}
  output = src_name=ACK_MESSAGE_LOCAL, dest_name=ACK_MESSAGE, route=127.0.0.1:{self.pearl_pshare_port}
}}

ProcessConfig = pMediator
{{
  AppTick   = 10
  CommsTick = 10
  vname         = uav
  mates         = pearl
  resend_thresh = 1
  max_tries     = 10
}}

ProcessConfig = uFldMessageHandler
{{
  AppTick   = 10
  CommsTick = 10
  strict_addressing = true
}}

ProcessConfig = pRendezvous
{{
  AppTick   = 10
  CommsTick = 10
  role      = uav
  ownship   = uav
  peer_node = pearl
  require_health = false
  require_battery = true
  battery_max_age = 30
  require_flight_state = false
  require_landing_target = false
  require_platform_ready = true
  require_platform_health = true
  min_pearl_battery = 15
  max_landing_wind_speed = 4
  platform_data_max_age = 10
  nav_stale_thresh = 10
  request_timeout = 20
  state_post_interval = 0.1
}}
""",
            encoding="utf-8",
        )

        self.pearl_log = self.pearl_log_path.open("w", encoding="utf-8")
        self.uav_log = self.uav_log_path.open("w", encoding="utf-8")
        self.processes = [
            subprocess.Popen(
                [str(MOOS_BIN_DIR / "pAntler"), str(self.pearl_mission)],
                cwd=self.temp_path,
                stdout=self.pearl_log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            ),
            subprocess.Popen(
                [str(MOOS_BIN_DIR / "pAntler"), str(self.uav_mission)],
                cwd=self.temp_path,
                stdout=self.uav_log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            ),
        ]
        self.wait_for_moosdb(self.pearl_db_port)
        self.wait_for_moosdb(self.uav_db_port)
        time.sleep(0.5)

    def tearDown(self) -> None:
        for process in self.processes:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
        for process in self.processes:
            if process.poll() is None:
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=3)
        self.pearl_log.close()
        self.uav_log.close()
        self.temp_dir.cleanup()

    def wait_for_moosdb(self, port: int) -> None:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                    return
            except OSError:
                time.sleep(0.1)
        self.fail(f"MOOSDB on port {port} did not become available")

    def poke(self, mission: Path, *pairs: str) -> None:
        result = subprocess.run(
            [str(MOOS_BIN_DIR / "uPokeDB"), str(mission), "--quiet", *pairs],
            cwd=self.temp_path,
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
        if result.returncode != 0:
            self.fail(f"uPokeDB failed: {pairs}\n{result.stdout}\n{result.stderr}")

    def assert_query(
        self, mission: Path, *conditions: str, wait: float = 5
    ) -> None:
        result = subprocess.run(
            [
                str(MOOS_BIN_DIR / "uQueryDB"),
                str(mission),
                *(f"--condition={condition}" for condition in conditions),
                f"--wait={wait}",
            ],
            cwd=self.temp_path,
            capture_output=True,
            text=True,
            timeout=wait + 3,
            check=False,
        )
        if result.returncode != 0:
            pearl_log = self.pearl_log_path.read_text(
                encoding="utf-8", errors="replace"
            )
            uav_log = self.uav_log_path.read_text(encoding="utf-8", errors="replace")
            self.fail(
                f"Routed conditions did not become true: {conditions}\n"
                f"{result.stdout}\n{result.stderr}\n"
                f"PEARL pAntler:\n{pearl_log}\nUAV pAntler:\n{uav_log}"
            )

    def test_platform_route_and_mediated_handshake(self) -> None:
        self.poke(
            self.pearl_mission,
            "NAV_X=10",
            "NAV_Y=0",
            "PEARL_BATTERY_SOC=80",
            "PEARL_BATTERY_DATA_VALID=true",
            "PEARL_WIND_SPEED=2",
            "PEARL_WIND_DATA_VALID=true",
            "PROC_WATCH_ALL_OK=true",
        )
        self.assert_query(
            self.uav_mission,
            "PEARL_BATTERY_SOC=80",
            "PEARL_BATTERY_DATA_VALID=true",
            "PEARL_WIND_SPEED=2",
            "PEARL_WIND_DATA_VALID=true",
            "PEARL_PROC_WATCH_ALL_OK=true",
        )

        self.poke(
            self.uav_mission,
            "NAV_X=0",
            "NAV_Y=0",
            "UAV_BATTERY_SOC=10",
            "UAV_BATTERY_DATA_VALID=true",
        )
        time.sleep(0.2)
        self.poke(self.uav_mission, "RENDEZVOUS_START=true")
        self.assert_query(
            self.uav_mission,
            "UAV_RENDEZVOUS_PHASE=REQUESTING",
            "ROUTE_BUFFER_COMMAND!=__missing__",
            "UAV_RECOVERY_PRIORITY=EMERGENCY",
        )
        self.poke(self.uav_mission, "ROUTE_BUFFER_VEHICLE_STATE:=DEPLOY_ACCEPTED")
        self.assert_query(self.uav_mission, "UAV_RENDEZVOUS_PHASE=RENDEZVOUS")
        self.assert_query(self.pearl_mission, "PEARL_RENDEZVOUS_PHASE=RENDEZVOUS")


if __name__ == "__main__":
    unittest.main(verbosity=2)
