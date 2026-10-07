#!/usr/bin/env python3
"""Native MOOS tests for request identity, aggregation, cancellation and denial."""

from __future__ import annotations

import os
import re
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MOOS_BIN = Path(os.environ.get("MOOS_BIN_DIR", ROOT.parent / "moos-ivp" / "bin"))
COORDINATOR = ROOT / "bin/pTakeoffCoordinator"
PROCESSOR = ROOT / "bin/pTakeoffProcessor"


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class ExchangeTest(unittest.TestCase):
    def setUp(self):
        for binary in (COORDINATOR, PROCESSOR, *(MOOS_BIN / n for n in
                       ("MOOSDB", "pAntler", "uPokeDB", "uQueryDB"))):
            if not binary.is_file():
                raise RuntimeError(f"Missing {binary}")
        self.temp = tempfile.TemporaryDirectory(prefix="takeoff-exchange-")
        self.root = Path(self.temp.name)
        self.mission = self.root / "exchange.moos"
        timeout = 2 if self._testMethodName in ("test_coordinator_timeout", "test_processor_timeout") else 60
        epoch_session = (" checks_session_var=TEST_ATTEMPT_EPOCH\n"
                         if self._testMethodName.startswith("test_epoch_session_") else "")
        self.mission.write_text(f"""
ServerHost=127.0.0.1
ServerPort={free_port()}
Community=takeoff_exchange
ProcessConfig=ANTLER
{{
 MSBetweenLaunches=100
 Run = {MOOS_BIN / 'MOOSDB'} @ NewConsole=false
 Run = {COORDINATOR} @ NewConsole=false
 Run = {PROCESSOR} @ NewConsole=false
}}
ProcessConfig=pTakeoffCoordinator
{{
 AppTick=20
 CommsTick=20
 ownship=uav
 authority=pearl
 result_var=TEST_RESULT
 state_var=TEST_STATE
 reason_var=TEST_REASON
 receipt_var=TEST_REQUEST_ID
 timeout={timeout}
 request_interval=0.5
 completion_retry_time=8
 armed_var=UAV_IS_ARMED
 landed_var=UAV_LANDED_STATE
 arming_flag=ARM_UAV=true
 takeoff_flag=ARDU_COMMAND=DO_TAKEOFF
 checks_result_var=TEST_CHECK_A
 checks_result_var=TEST_CHECK_B
{epoch_session}request_condition=TAKEOFF_PHASE=REQUESTING
request_condition=UAV_LANDED_STATE=ON_GROUND
}}
ProcessConfig=pTakeoffProcessor
{{
 AppTick=20
 CommsTick=20
 ownship=pearl
 requester=uav
 receipt_var=TEST_RECEIPT
 status_var=TEST_PROCESSOR_STATUS
 progress_var=TEST_PROGRESS
 progress_grace=8
 view_label=pearl:takeoff-support
 view_radius=3
 view_x_var=NAV_X
 view_y_var=NAV_Y
 view_post_interval=0.2
 checks_result_var=TEST_CHECK_A
 checks_result_var=TEST_CHECK_B
 release_condition=SUPPORT_OPERATION=NORMAL
 release_condition=RELEASE_OK=1
 timeout={timeout}
}}
""", encoding="utf-8")
        self.log = (self.root / "antler.log").open("w", encoding="utf-8")
        self.antler = subprocess.Popen([str(MOOS_BIN / "pAntler"), str(self.mission)],
                                       cwd=self.root, stdout=self.log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
        time.sleep(1.5)
        self.assertIsNone(self.antler.poll())

    def tearDown(self):
        if self.antler.poll() is None:
            os.killpg(self.antler.pid, signal.SIGTERM)
            try:
                self.antler.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(self.antler.pid, signal.SIGKILL)
                self.antler.wait(timeout=3)
        self.log.close()
        self.temp.cleanup()

    def run_tool(self, command, timeout, *, cwd=None):
        # MOOS command-line clients occasionally hang during disconnect even
        # after posting; all test pokes are idempotent, so retry once.
        for attempt in range(2):
            try:
                return subprocess.run(command, cwd=cwd, capture_output=True,
                                      text=True, timeout=timeout)
            except subprocess.TimeoutExpired:
                if attempt:
                    raise
                time.sleep(0.2)
        raise AssertionError("unreachable")

    def poke(self, *args):
        proc = self.run_tool([str(MOOS_BIN / "uPokeDB"), str(self.mission),
                              "--quiet", *args], timeout=12)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)

    def query(self, *conditions, wait=5):
        proc = self.run_tool([str(MOOS_BIN / "uQueryDB"), str(self.mission),
                              *(f"--condition={c}" for c in conditions),
                              f"--wait={wait}"], timeout=wait + 8, cwd=self.root)
        if proc.returncode:
            self.log.flush()
            self.fail(proc.stdout + proc.stderr +
                      (self.root / "antler.log").read_text(encoding="utf-8"))

    def value(self, var, condition):
        proc = self.run_tool([str(MOOS_BIN / "uQueryDB"), str(self.mission),
                              f"--condition={condition}", f"--check_var={var}",
                              "--vo", "--wait=5"], timeout=13, cwd=self.root)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        return (self.root / ".checkvars").read_text(encoding="utf-8").strip().partition("=")[2]

    def begin(self):
        self.poke("TAKEOFF_PHASE:=REQUESTING", "UAV_LANDED_STATE:=ON_GROUND",
                  "UAV_IS_ARMED=0")
        result = self.value("TEST_STATE", "TEST_RESULT=PENDING")
        match = re.search(r"id=([^,]+),status=PENDING", result)
        self.assertIsNotNone(match, result)
        assert match is not None
        self.query(f"TEST_REQUEST_ID=={match.group(1)}")
        return match.group(1)

    def test_appinfo_shows_each_condition_and_unseen_inputs(self):
        self.poke("TAKEOFF_PHASE:=REQUESTING",
                  "APPCAST_REQ:=node=all,app=pTakeoffCoordinator,duration=10,key=test,thresh=any")
        time.sleep(2.5)  # Let the requested AppCast be posted.
        report = self.value("APPCAST", "TEST_RESULT=UNKNOWN")
        self.assertIn("TAKEOFF_PHASE=REQUESTING => true", report)
        self.assertIn("UAV_LANDED_STATE=ON_GROUND => false (unseen: UAV_LANDED_STATE)", report)
        self.assertIn("Met: false", report)
        self.assertIn("Takeoff events (none):", report)
        self.assertIn("No takeoff events yet", report)

    def test_request_busy_stale_checks_and_grant_after_postcheck(self):
        request_id = self.begin()
        self.poke(f"TAKEOFF_REQUEST:=id={request_id}#status=requested")
        self.query(f"TEST_RECEIPT=={request_id}", "TEST_PROCESSOR_STATUS==pending")
        self.poke("TAKEOFF_REQUEST:=id=other#status=requested")
        self.query(f"TEST_RECEIPT=={request_id}", "TEST_PROCESSOR_STATUS==pending")
        self.poke("TEST_CHECK_A:=success", "TEST_CHECK_B:=success",
                  "TEST_CHECK_A_STATE:=status=success,session=old",
                  "TEST_CHECK_B_STATE:=status=success,session=old")
        self.query(f"TEST_RECEIPT=={request_id}")  # Old successes cannot grant.
        self.poke(f"TEST_CHECK_A_STATE:=status=success,session={request_id}")
        self.query(f"TEST_RECEIPT=={request_id}")  # All checks must pass.
        self.poke("TEST_CHECK_A:=expired",
                  f"TEST_CHECK_A_STATE:=status=expired,session={request_id}")
        self.query(f"TEST_RECEIPT=={request_id}")  # Precheck success is retained per ID.
        self.poke(f"TEST_CHECK_B_STATE:=status=success,session={request_id}")
        self.query("TEST_PROCESSOR_STATUS==granted")
        # Retransmissions may already replace the grant in NODE_MESSAGE_LOCAL.
        self.poke(f"TAKEOFF_REQUEST:=id={request_id}#status=requested")
        self.query(f"TEST_RECEIPT=={request_id}")  # Duplicate active ID is idempotent.
        self.assertNotIn("status=busy", self.value("NODE_MESSAGE_LOCAL",
                                                   f"TEST_RECEIPT=={request_id}"))
        self.poke(f"TAKEOFF_CLEARANCE:=id={request_id}#status=granted")
        self.query("TEST_RESULT==PENDING")  # An expired UAV check still blocks arming.
        self.poke("TEST_CHECK_A:=success",
                  f"TEST_CHECK_A_STATE:=status=success,session={request_id}")
        self.query("TEST_RESULT==APPROVED", "TEST_REASON==ARMING", "ARM_UAV==true")
        self.poke("SUPPORT_OPERATION:=TAKEOFF_SUPPORT")
        self.poke("SUPPORT_OPERATION:=NORMAL")
        self.query(f"TEST_RECEIPT=={request_id}")  # Second release condition missing.
        self.poke("RELEASE_OK=1")
        self.query("TEST_RECEIPT==none", "TEST_PROCESSOR_STATUS==inactive")

    def test_epoch_session_arms_only_for_current_attempt(self):
        self.poke("TEST_ATTEMPT_EPOCH:=attempt-1")
        request_id = self.begin()
        self.assertNotEqual(request_id, "attempt-1")  # PEARL uses a separate ID.
        self.poke(f"TAKEOFF_CLEARANCE:=id={request_id}#status=granted")
        self.poke("TEST_CHECK_A:=success", "TEST_CHECK_B:=success",
                  f"TEST_CHECK_A_STATE:=status=success,session={request_id}",
                  f"TEST_CHECK_B_STATE:=status=success,session={request_id}")
        self.query("TEST_RESULT==PENDING")  # Clearance ID is not the checks session.
        self.poke("TEST_CHECK_A_STATE:=status=success,session=attempt-1",
                  "TEST_CHECK_B_STATE:=status=success,session=attempt-1")
        self.query("TEST_RESULT==APPROVED", "ARM_UAV==true")

    def test_epoch_session_change_cancels_pending_request(self):
        self.poke("TEST_ATTEMPT_EPOCH:=attempt-1")
        self.begin()
        self.poke("TEST_ATTEMPT_EPOCH:=attempt-2")
        self.query("TEST_RESULT==CANCELLED", "TEST_REQUEST_ID==none")

    def test_grant_and_checks_gate_arming_then_confirm_flight(self):
        request_id = self.begin()
        self.poke(f"TAKEOFF_CLEARANCE:=id={request_id}#status=granted")
        self.query("TEST_RESULT==PENDING")
        # Clearance alone is not permission to arm; an up-to-date uChecks
        # aggregate and its diagnostic must both be green.
        self.poke("TEST_CHECK_A:=success", "TEST_CHECK_A_STATE:=status=success,session=old")
        self.query("TEST_RESULT==PENDING")  # Stale session cannot authorize.
        self.poke("TEST_CHECK_A_STATE:=status=success,session=" + request_id,
                  "TEST_CHECK_B:=success", "TEST_CHECK_B_STATE:=status=success,session=old")
        self.query("TEST_RESULT==PENDING")  # Both UAV checks are required.
        self.poke(f"TEST_CHECK_B_STATE:=status=success,session={request_id}")
        self.query("TEST_RESULT==APPROVED", "ARM_UAV==true")
        self.poke("TEST_CHECK_A:=fail", "TEST_CHECK_A_STATE:=status=fail")
        self.query("TEST_RESULT==APPROVED")  # Checks no longer gate flight.
        self.poke("UAV_IS_ARMED=1")
        self.query("ARDU_COMMAND==DO_TAKEOFF", "TEST_REASON==WAITING_TAKEOFF")
        self.assertIn("status=armed", self.value("NODE_MESSAGE_LOCAL",
                                                 "ARDU_COMMAND=DO_TAKEOFF"))
        self.poke("UAV_LANDED_STATE:=TAKING_OFF")
        self.query("TEST_REASON==TAKING_OFF")
        self.assertIn("status=taking_off", self.value("NODE_MESSAGE_LOCAL",
                                                      "TEST_REASON=TAKING_OFF"))
        self.poke("UAV_LANDED_STATE:=IN_AIR")
        self.query("TEST_REASON==TAKEOFF_COMPLETE")
        self.assertIn("status=takeoff_complete", self.value("NODE_MESSAGE_LOCAL",
                                                           "TEST_REASON=TAKEOFF_COMPLETE"))
        time.sleep(1.2)  # Completion must be repeated, not a one-shot post.
        self.poke("APPCAST_REQ:=node=all,app=pTakeoffCoordinator,duration=10,key=retries")
        time.sleep(1.2)
        report = self.value("APPCAST", "TEST_REASON=TAKEOFF_COMPLETE")
        attempts = re.search(r"Completion transmissions: (\d+)", report)
        self.assertIsNotNone(attempts, report)
        assert attempts is not None
        self.assertGreaterEqual(int(attempts.group(1)), 2, report)
        self.poke("APPCAST_REQ:=node=all,app=pTakeoffCoordinator,duration=10,key=events")
        time.sleep(1.2)
        report = self.value("APPCAST", "TEST_REASON=TAKEOFF_COMPLETE")
        self.assertIn(f"Takeoff events ({request_id}):", report)
        table = report.partition(f"Takeoff events ({request_id}):")[2].partition(
            "!@!@#!@#events_total"
        )[0]
        self.assertIn("Elapsed (s)", table)
        rows = [row.split() for row in table.split("!@")
                if re.match(r"^\d+(?:\.\d+)?\s+", row.strip())]
        self.assertEqual([row[-1] for row in rows], [
            "AWAITING_AUTHORITY", "AUTHORITY_GRANTED_CHECKING", "ARMING",
            "WAITING_TAKEOFF", "TAKING_OFF", "TAKEOFF_COMPLETE",
        ], report)

    def test_request_condition_change_cancels_before_arming(self):
        request_id = self.begin()
        self.poke("TAKEOFF_PHASE:=IDLE")
        self.query("TEST_RESULT==CANCELLED", "TEST_REASON==OWN_CONDITIONS_CHANGED")
        self.poke("TEST_CHECK_A:=success", "TEST_CHECK_A_STATE:=status=success",
                  "TEST_CHECK_B:=success", "TEST_CHECK_B_STATE:=status=success")
        self.poke(f"TAKEOFF_CLEARANCE:=id={request_id}#status=granted")
        self.query("TEST_RESULT==CANCELLED")

    def test_disarm_before_departure_cancels_without_retrying_takeoff(self):
        request_id = self.begin()
        self.poke("TEST_CHECK_A:=success",
                  f"TEST_CHECK_A_STATE:=status=success,session={request_id}",
                  "TEST_CHECK_B:=success",
                  f"TEST_CHECK_B_STATE:=status=success,session={request_id}")
        self.poke(f"TAKEOFF_CLEARANCE:=id={request_id}#status=granted")
        self.query("ARM_UAV==true", "TEST_RESULT==APPROVED")
        self.poke("UAV_IS_ARMED=1")
        self.query("ARDU_COMMAND==DO_TAKEOFF")
        self.poke("UAV_IS_ARMED=0")
        self.query("TEST_RESULT==CANCELLED", "TEST_REASON==DISARMED_BEFORE_TAKEOFF")

    def test_authority_condition_change_is_identified_in_event_reason(self):
        request_id = self.begin()
        self.poke(f"TAKEOFF_CLEARANCE:=id={request_id}#status=rejected"
                  "#reason=CONDITIONS_CHANGED")
        self.query("TEST_RESULT==REJECTED",
                   "TEST_REASON==AUTHORITY_CONDITIONS_CHANGED")
        self.poke("APPCAST_REQ:=node=all,app=pTakeoffCoordinator,duration=10,key=origin")
        time.sleep(1.2)
        report = self.value("APPCAST", "TEST_RESULT=REJECTED")
        table = report.partition(f"Takeoff events ({request_id}):")[2].partition(
            "!@!@#!@#events_total"
        )[0]
        self.assertRegex(table, r"IDLE\s+REJECTED\s+AUTHORITY_CONDITIONS_CHANGED")
        self.assertNotIn("OWN_CONDITIONS_CHANGED", table)

    def test_processor_draws_dashed_circle_until_granted(self):
        request_id = "viewdemo"
        self.poke("NAV_X=4", "NAV_Y=6",
                  f"TAKEOFF_REQUEST:=id={request_id}#status=requested")
        self.query("TEST_PROCESSOR_STATUS==pending")
        time.sleep(0.4)
        pending = self.value("VIEW_SEGLIST", "TEST_PROCESSOR_STATUS=pending")
        self.assertIn("pearl:takeoff-support-dash-11", pending)
        self.assertIn("pts={", pending)
        self.poke("TEST_CHECK_A:=success", "TEST_CHECK_B:=success",
                  f"TEST_CHECK_A_STATE:=status=success,session={request_id}",
                  f"TEST_CHECK_B_STATE:=status=success,session={request_id}")
        self.query("TEST_PROCESSOR_STATUS==granted")
        time.sleep(0.4)
        solid = self.value("VIEW_CIRCLE", "TEST_PROCESSOR_STATUS=granted")
        self.assertIn("label=pearl:takeoff-support", solid)
        self.assertNotIn("active=false", solid)
        self.assertIn("radius=3", solid)
        self.assertIn("active=false", self.value("VIEW_SEGLIST",
                                                  "TEST_PROCESSOR_STATUS=granted"))
        self.poke(f"TAKEOFF_REQUEST:=id={request_id}#status=cancelled")
        self.query("TEST_PROCESSOR_STATUS==canceled")
        time.sleep(0.4)
        self.assertIn("active=false", self.value("VIEW_CIRCLE",
                                               "TEST_PROCESSOR_STATUS=canceled"))

    def test_processor_tracks_progress_and_marks_request_complete(self):
        request_id = self.begin()
        self.poke(f"TAKEOFF_REQUEST:=id={request_id}#status=requested")
        self.query(f"TEST_RECEIPT=={request_id}")
        self.poke("TEST_CHECK_A:=success", "TEST_CHECK_B:=success",
                  f"TEST_CHECK_A_STATE:=status=success,session={request_id}",
                  f"TEST_CHECK_B_STATE:=status=success,session={request_id}")
        self.query("TEST_PROCESSOR_STATUS==granted")
        for status in ("armed", "taking_off", "takeoff_complete"):
            self.poke(f"TAKEOFF_REQUEST:=id={request_id}#status={status}")
            time.sleep(0.4)
            local_status = "complete" if status == "takeoff_complete" else "granted"
            self.assertEqual(self.value("TEST_PROGRESS",
                                        f"TEST_PROCESSOR_STATUS={local_status}"),
                             f"id={request_id},status={status}")
        self.poke("TAKEOFF_REQUEST:=id=other#status=armed",
                  f"TAKEOFF_REQUEST:=id={request_id}#status=armed")
        self.assertEqual(self.value("TEST_PROGRESS", "TEST_PROCESSOR_STATUS=complete"),
                         f"id={request_id},status=takeoff_complete")

    def test_complete_after_release_reaches_pearl_but_stale_id_does_not(self):
        request_id = "late_completion"
        self.poke(f"TAKEOFF_REQUEST:=id={request_id}#status=requested")
        self.query(f"TEST_RECEIPT=={request_id}")
        self.poke("TEST_CHECK_A:=success", "TEST_CHECK_B:=success",
                  f"TEST_CHECK_A_STATE:=status=success,session={request_id}",
                  f"TEST_CHECK_B_STATE:=status=success,session={request_id}")
        self.query("TEST_PROCESSOR_STATUS==granted")
        self.poke("SUPPORT_OPERATION:=NORMAL", "RELEASE_OK=1")
        self.query("TEST_RECEIPT==none", "TEST_PROCESSOR_STATUS==inactive")
        self.poke(f"TAKEOFF_REQUEST:=id={request_id}#status=takeoff_complete")
        time.sleep(0.4)
        self.assertEqual(self.value("TEST_PROGRESS", "TEST_PROCESSOR_STATUS=complete"),
                         f"id={request_id},status=takeoff_complete")
        # A subsequent request supersedes the late-progress allowance.
        self.poke("TAKEOFF_REQUEST:=id=next_session#status=requested")
        self.query("TEST_RECEIPT==next_session")
        self.poke(f"TAKEOFF_REQUEST:=id={request_id}#status=takeoff_complete")
        time.sleep(0.4)
        self.assertEqual(self.value("TEST_PROGRESS", "TEST_RECEIPT=next_session"),
                         "none")

    def test_processor_appinfo_keeps_last_takeoff_and_resets_for_next(self):
        first_id = "processor_session_a"
        self.poke(f"TAKEOFF_REQUEST:=id={first_id}#status=requested")
        self.query(f"TEST_RECEIPT=={first_id}", "TEST_PROCESSOR_STATUS==pending")
        self.poke("TEST_CHECK_A:=success", "TEST_CHECK_B:=success",
                  f"TEST_CHECK_A_STATE:=status=success,session={first_id}",
                  f"TEST_CHECK_B_STATE:=status=success,session={first_id}")
        self.query("TEST_PROCESSOR_STATUS==granted")
        # Repeated requests and progress reports must not produce extra rows.
        self.poke(f"TAKEOFF_REQUEST:=id={first_id}#status=requested")
        for progress in ("armed", "taking_off", "takeoff_complete"):
            self.poke(f"TAKEOFF_REQUEST:=id={first_id}#status={progress}")
            time.sleep(0.4)
            local_status = "complete" if progress == "takeoff_complete" else "granted"
            self.assertEqual(self.value("TEST_PROGRESS",
                                        f"TEST_PROCESSOR_STATUS={local_status}"),
                             f"id={first_id},status={progress}")
        self.poke(f"TAKEOFF_REQUEST:=id={first_id}#status=takeoff_complete")
        self.poke("SUPPORT_OPERATION:=NORMAL", "RELEASE_OK=1")
        self.query("TEST_RECEIPT==none", "TEST_PROCESSOR_STATUS==complete")
        self.poke("APPCAST_REQ:=node=all,app=pTakeoffProcessor,duration=10,key=processor")
        time.sleep(1.2)
        report = self.value("APPCAST", "TEST_PROCESSOR_STATUS=complete")
        table = report.partition(f"Takeoff events ({first_id}):")[2].partition(
            "!@!@#!@#events_total"
        )[0]
        self.assertIn("Elapsed (s)", table)
        self.assertIn("Progress", table)
        rows = [row.split() for row in table.split("!@")
                if re.match(r"^\d+(?:\.\d+)?\s+", row.strip())]
        self.assertEqual([row[-1] for row in rows], [
            "REQUEST_RECEIVED", "CHECKS_SUCCEEDED",
            "REQUESTER_PROGRESS", "REQUESTER_PROGRESS", "REQUESTER_PROGRESS",
            "RELEASE_CONDITIONS_MET",
        ], report)
        self.assertEqual([row[1] for row in rows], [
            "PENDING", "GRANTED", "GRANTED", "GRANTED", "GRANTED", "COMPLETE",
        ], report)

        second_id = "processor_session_b"
        self.poke(f"TAKEOFF_REQUEST:=id={second_id}#status=requested")
        self.query(f"TEST_RECEIPT=={second_id}", "TEST_PROCESSOR_STATUS==pending")
        time.sleep(1.2)
        report = self.value("APPCAST", "TEST_PROCESSOR_STATUS=pending")
        table = report.partition(f"Takeoff events ({second_id}):")[2].partition(
            "!@!@#!@#events_total"
        )[0]
        self.assertNotIn(first_id, table)
        self.assertEqual(table.count("REQUEST_RECEIVED"), 1, report)
        self.assertNotIn("CHECKS_SUCCEEDED", table)
        self.poke(f"TAKEOFF_REQUEST:=id={second_id}#status=cancelled"
                  "#reason=OWN_CONDITIONS_CHANGED")
        self.query("TEST_RECEIPT==none", "TEST_PROCESSOR_STATUS==canceled")
        time.sleep(1.2)
        report = self.value("APPCAST", "TEST_PROCESSOR_STATUS=canceled")
        self.assertIn("REQUESTER_OWN_CONDITIONS_CHANGED", report)

    def test_request_repeats_same_id_until_grant(self):
        request_id = self.begin()
        self.poke("APPCAST_REQ:=node=all,app=pTakeoffCoordinator,duration=10,key=retries")
        time.sleep(1.8)  # The test interval is 0.5 seconds.
        report = self.value("APPCAST", "TEST_RESULT=PENDING")
        self.assertIn(f"ID: {request_id}", report)
        count = re.search(r"Request transmissions: (\d+)", report)
        self.assertIsNotNone(count, report)
        assert count is not None
        self.assertGreaterEqual(int(count.group(1)), 2, report)

    def test_postcheck_failure_denies(self):
        request_id = self.begin()
        self.poke(f"TAKEOFF_REQUEST:=id={request_id}#status=requested")
        self.query(f"TEST_RECEIPT=={request_id}")
        self.poke("TEST_CHECK_A:=success",
                  f"TEST_CHECK_A_STATE:=status=success,session={request_id}")
        self.query(f"TEST_RECEIPT=={request_id}")
        self.poke("TEST_CHECK_B:=fail",
                  f"TEST_CHECK_B_STATE:=status=fail,session={request_id}")
        self.query("TEST_RECEIPT==none", "TEST_PROCESSOR_STATUS==denied")

    def test_precheck_failure_after_success_denies_before_postcheck(self):
        request_id = self.begin()
        self.poke(f"TAKEOFF_REQUEST:=id={request_id}#status=requested")
        self.query(f"TEST_RECEIPT=={request_id}")
        self.poke("TEST_CHECK_A:=success",
                  f"TEST_CHECK_A_STATE:=status=success,session={request_id}")
        self.query(f"TEST_RECEIPT=={request_id}")  # Bay check still pending.
        self.poke("TEST_CHECK_A:=fail",
                  f"TEST_CHECK_A_STATE:=status=fail,session={request_id}")
        self.query("TEST_RECEIPT==none")

    def test_failed_check_denies_and_releases(self):
        request_id = self.begin()
        self.poke(f"TAKEOFF_REQUEST:=id={request_id}#status=requested")
        self.query(f"TEST_RECEIPT=={request_id}")
        self.poke("TEST_CHECK_A:=fail",
                  f"TEST_CHECK_A_STATE:=status=fail,session={request_id}")
        self.query("TEST_RECEIPT==none")
        self.poke(f"TAKEOFF_CLEARANCE:=id={request_id}#status=denied")
        self.query("TEST_RESULT==REJECTED", "TEST_REASON==AUTHORITY_DENIED")

    def test_busy_reply_uses_authority_reason(self):
        request_id = self.begin()
        self.poke(f"TAKEOFF_CLEARANCE:=id={request_id}#status=busy")
        self.query("TEST_RESULT==REJECTED", "TEST_REASON==AUTHORITY_BUSY")

    def test_coordinator_timeout(self):
        self.poke("TAKEOFF_PHASE:=REQUESTING", "UAV_LANDED_STATE:=ON_GROUND")
        # No PEARL reply; the coordinator must settle without a grant.
        time.sleep(2.5)
        result = self.value("TEST_STATE", "TEST_RESULT=TIMEOUT")
        self.assertIn("status=TIMEOUT", result)
        match = re.search(r"id=([^,]+)", result)
        self.assertIsNotNone(match, result)
        assert match is not None
        request_id = match.group(1)
        self.poke(f"TAKEOFF_CLEARANCE:=id={request_id}#status=granted")
        result = self.value("TEST_STATE", "TEST_RESULT=TIMEOUT")
        self.assertIn("status=TIMEOUT", result)

    def test_processor_timeout(self):
        self.poke("TAKEOFF_REQUEST:=id=session_a#status=requested")
        self.query("TEST_RECEIPT==session_a")
        time.sleep(2.5)
        self.query("TEST_RECEIPT==none", "TEST_PROCESSOR_STATUS==denied")




if __name__ == "__main__":
    unittest.main()
