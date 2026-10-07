#!/usr/bin/env python3
"""Direct-link pOperator integration tests.

The tests use two MOOSDB/pShare communities and real UDP routing. They require
MOOS-IvP binaries and a built moos-ivp-pearl/bin/pOperator on PATH.
"""

import json
import os
import pathlib
import signal
import socket
import subprocess
import tempfile
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[4]
OPERATOR = ROOT / "moos-ivp-pearl/bin/pOperator"
DEFINITION = ROOT / "moos-ivp-pearl/missions/charles_river/2026_09_29_chevron_demo/operations/takeoff.toml"


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class DirectPair:
    def __init__(self, same_alias=True):
        self.work = pathlib.Path(tempfile.mkdtemp(prefix="operator-test-"))
        self.db_ports = [free_port(), free_port()]
        self.share_ports = [free_port(), free_port()]
        self.aliases = ["pOperator", "pOperator"] if same_alias else ["requester", "authority"]
        self.processes: list[subprocess.Popen] = []
        self.share_processes: list[subprocess.Popen | None] = [None, None]
        self.operator_processes: list[subprocess.Popen | None] = [None, None]
        self.missions = []
        for index, identity in enumerate(("uav", "pearl")):
            outbound = "TAKEOFF_OPERATION_REQUEST" if index == 0 else "TAKEOFF_OPERATION_COMMIT"
            mission = self.work / f"{identity}.moos"
            mission.write_text(
                f"""ServerHost=localhost
ServerPort={self.db_ports[index]}
Community={identity}
ProcessConfig=pShare
{{
 AppTick=20
 CommsTick=20
 input=route=localhost:{self.share_ports[index]}
 output=src_name={outbound},route=localhost:{self.share_ports[1-index]}
}}
ProcessConfig={self.aliases[index]}
{{
 AppTick=10
 CommsTick=10
 identity={identity}
 authority=pearl
 definition={DEFINITION}
 state_file={self.work}/{identity}.state
}}
"""
            )
            self.missions.append(mission)

    def start(self):
        for index in range(2):
            self._spawn("MOOSDB", self.missions[index], label=f"db-{index}")
            self.share_processes[index] = self._spawn(
                "pShare", self.missions[index], label=f"share-{index}"
            )
            self.start_operator(index)
        time.sleep(2)

    def _spawn(self, binary, mission, *args, label):
        log_fd = os.open(self.work / f"{label}.log", os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
        try:
            process = subprocess.Popen(
                [str(binary), str(mission), *map(str, args)],
                stdout=log_fd,
                stderr=subprocess.STDOUT,
            )
        finally:
            os.close(log_fd)
        self.processes.append(process)
        return process

    def start_operator(self, index):
        process = self._spawn(
            OPERATOR,
            self.missions[index],
            self.aliases[index],
            label=f"operator-{index}",
        )
        self.operator_processes[index] = process
        return process

    def restart_operator(self, index):
        process = self.operator_processes[index]
        if process is None:
            raise RuntimeError("operator is not running")
        process.terminate()
        process.wait(timeout=5)
        time.sleep(0.5)
        return self.start_operator(index)

    def poke(self, index, assignment):
        subprocess.run(
            ["uPokeDB", str(self.missions[index]), assignment],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=8,
        )

    def query(self, index, condition, wait=6):
        result = subprocess.run(
            ["uQueryDB", str(self.missions[index]), f"--condition={condition}", f"--wait={wait}"],
            cwd=self.work,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=wait + 4,
        )
        return result.returncode == 0

    def value(self, index, variable):
        check_file = self.work / ".checkvars"
        check_file.unlink(missing_ok=True)
        subprocess.run(
            [
                "uQueryDB", str(self.missions[index]),
                "--condition=DB_UPTIME>=0", f"--check_var={variable}", "--esv", "--wait=3",
            ],
            cwd=self.work,
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=7,
        )
        for line in check_file.read_text().splitlines():
            if line.strip().startswith(variable + "="):
                return line.strip().split("=", 1)[1]
        raise AssertionError(f"No value for {variable}")

    def correlated(self, index, variable, target, reason="test"):
        operation_id = self.value(index, "TAKEOFF_OPERATION_ID")
        revision = int(float(self.value(index, "TAKEOFF_REVISION")))
        self.poke(
            index,
            f"{variable}:=id={operation_id}#revision={revision}#to={target}#reason={reason}",
        )

    def suspend_share(self, index):
        process = self.share_processes[index]
        if process is None:
            raise RuntimeError("pShare is not running")
        process.send_signal(signal.SIGSTOP)

    def resume_share(self, index):
        process = self.share_processes[index]
        if process is None:
            raise RuntimeError("pShare is not running")
        process.send_signal(signal.SIGCONT)

    def close(self):
        for process in reversed(self.processes):
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
        for process in reversed(self.processes):
            try:
                process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


class OperatorProtocolTest(unittest.TestCase):
    def setUp(self):
        self.pair = DirectPair(same_alias=True)
        self.pair.start()
        self.start_sequence = 0

    def tearDown(self):
        self.pair.close()

    def ready(self):
        self.pair.poke(
            1,
            f"TAKEOFF_AUTHORITY_READY:=ready=true#evidence_id=test-{time.time_ns()}"
            f"#observed_at={time.time()}",
        )

    def start_request(self):
        self.start_sequence += 1
        self.pair.poke(
            0,
            f"TAKEOFF_REQUEST:=command_id=test-{self.start_sequence}#start=true",
        )

    def admit(self):
        self.ready()
        self.start_request()
        self.assertTrue(self.pair.query(0, "TAKEOFF_PHASE=ACCEPTED"))
        self.assertTrue(self.pair.query(1, "TAKEOFF_PHASE=ACCEPTED"))

    def test_same_process_name_and_false_trigger(self):
        self.pair.poke(0, "TAKEOFF_REQUEST=false")
        time.sleep(0.5)
        self.assertTrue(self.pair.query(0, "TAKEOFF_PHASE=IDLE"))
        self.admit()

    def test_correlated_progress_and_safe_cancellation_settlement(self):
        self.admit()
        self.pair.correlated(0, "TAKEOFF_TRANSITION", "DEPARTING")
        self.assertTrue(self.pair.query(0, "TAKEOFF_PHASE=DEPARTING"))
        operation_id = self.pair.value(0, "TAKEOFF_OPERATION_ID")
        revision = int(float(self.pair.value(0, "TAKEOFF_REVISION")))
        self.pair.poke(
            0,
            f"TAKEOFF_CANCEL:=id={operation_id}#revision={revision}#cancel=true",
        )
        self.assertTrue(
            self.pair.query(1, "TAKEOFF_PHASE=ABORTING"),
            f"cancel rejected at id={operation_id}, revision={revision}, "
            f"requester_phase={self.pair.value(0, 'TAKEOFF_PHASE')}",
        )
        self.pair.correlated(1, "TAKEOFF_AUTHORITY_TRANSITION", "CANCELLED", "physically_safe")
        self.assertTrue(self.pair.query(0, "TAKEOFF_PHASE=CANCELLED"))

    def test_cancellation_is_retained_behind_lost_progress_commit(self):
        self.admit()
        operation_id = self.pair.value(0, "TAKEOFF_OPERATION_ID")
        revision = int(float(self.pair.value(0, "TAKEOFF_REVISION")))
        self.pair.suspend_share(1)
        self.pair.poke(
            0,
            f"TAKEOFF_TRANSITION:=id={operation_id}#revision={revision}#to=DEPARTING",
        )
        self.pair.poke(
            0,
            f"TAKEOFF_CANCEL:=id={operation_id}#revision={revision}#cancel=true",
        )
        time.sleep(1)
        self.pair.resume_share(1)
        self.assertTrue(self.pair.query(1, "TAKEOFF_PHASE=ABORTING", wait=10))

    def test_expired_wire_request_is_not_admitted(self):
        protocol_hash = self.pair.value(0, "TAKEOFF_PROTOCOL_HASH")
        discovery = json.loads(self.pair.value(0, "TAKEOFF_OPERATION_COMMIT"))
        request = {
            "protocol": "takeoff", "version": 5, "hash": protocol_hash,
            "operation_id": "expired-test", "requester": "uav",
            "requester_epoch": "expired-epoch", "authority": "pearl",
            "authority_epoch": discovery["authority_epoch"], "action": "REQUEST", "actor": "",
            "from": "", "to": "", "phase": "", "previous_phase": "",
            "reason": "", "payload": "", "event_result": "",
            "actor_sequence": 0, "accepted_event_sequence": 0,
            "base_revision": 0, "revision": 0, "created_at": 1, "deadline": 2,
            "sent_at": time.time(), "terminal_at": 0,
        }
        encoded = json.dumps(request, separators=(",", ":"))
        self.pair.poke(1, f"TAKEOFF_OPERATION_REQUEST:={encoded}")
        self.assertTrue(self.pair.query(1, "TAKEOFF_LAST_REQUEST_DECISION=EXPIRED"))
        self.assertTrue(self.pair.query(1, "TAKEOFF_PHASE=IDLE"))

    def test_terminal_request_replay_cannot_resurrect_old_operation(self):
        self.admit()
        old_request = self.pair.value(0, "TAKEOFF_OPERATION_REQUEST")
        old_id = self.pair.value(0, "TAKEOFF_OPERATION_ID")
        old_revision = int(float(self.pair.value(0, "TAKEOFF_REVISION")))
        self.pair.poke(
            0,
            f"TAKEOFF_CANCEL:=id={old_id}#revision={old_revision}#cancel=true",
        )
        self.assertTrue(self.pair.query(1, "TAKEOFF_PHASE=ABORTING"))
        self.pair.correlated(1, "TAKEOFF_AUTHORITY_TRANSITION", "CANCELLED")
        self.assertTrue(self.pair.query(0, "TAKEOFF_PHASE=CANCELLED"))

        self.ready()
        self.start_request()
        self.assertTrue(self.pair.query(1, "TAKEOFF_PHASE=ACCEPTED"))
        new_id = self.pair.value(1, "TAKEOFF_OPERATION_ID")
        self.assertNotEqual(old_id, new_id)

        self.pair.poke(1, f"TAKEOFF_OPERATION_REQUEST:={old_request}")
        time.sleep(1)
        self.assertEqual(new_id, self.pair.value(1, "TAKEOFF_OPERATION_ID"))
        self.assertTrue(self.pair.query(1, "TAKEOFF_PHASE=ACCEPTED"))

    def test_retained_start_command_does_not_restart_terminal_operation(self):
        self.admit()
        old_id = self.pair.value(0, "TAKEOFF_OPERATION_ID")
        revision = int(float(self.pair.value(0, "TAKEOFF_REVISION")))
        self.pair.poke(
            0,
            f"TAKEOFF_CANCEL:=id={old_id}#revision={revision}#cancel=true",
        )
        self.assertTrue(self.pair.query(1, "TAKEOFF_PHASE=ABORTING"))
        self.pair.correlated(1, "TAKEOFF_AUTHORITY_TRANSITION", "CANCELLED")
        self.assertTrue(self.pair.query(0, "TAKEOFF_PHASE=CANCELLED"))
        self.pair.restart_operator(0)
        time.sleep(3)
        self.assertEqual(old_id, self.pair.value(0, "TAKEOFF_OPERATION_ID"))
        self.assertTrue(self.pair.query(0, "TAKEOFF_PHASE=CANCELLED"))

    def test_stale_readiness_evidence_is_not_admitted(self):
        self.pair.poke(
            1,
            "TAKEOFF_AUTHORITY_READY:=ready=true#evidence_id=stale"
            f"#observed_at={time.time() - 60}",
        )
        self.start_request()
        self.assertTrue(self.pair.query(1, "TAKEOFF_PHASE=DEFERRED"))

    def test_regressive_higher_revision_snapshot_is_rejected(self):
        self.admit()
        self.pair.correlated(0, "TAKEOFF_TRANSITION", "DEPARTING")
        self.assertTrue(self.pair.query(0, "TAKEOFF_PHASE=DEPARTING"))
        self.pair.suspend_share(1)
        snapshot = json.loads(self.pair.value(0, "TAKEOFF_OPERATION_SNAPSHOT"))
        snapshot["action"] = "STATE_SNAPSHOT"
        snapshot["revision"] += 5
        snapshot["phase"] = "DEFERRED"
        encoded = json.dumps(snapshot, separators=(",", ":"))
        self.pair.poke(0, f"TAKEOFF_OPERATION_COMMIT:={encoded}")
        time.sleep(1)
        self.assertTrue(self.pair.query(0, "TAKEOFF_PHASE=DEPARTING"))
        self.pair.resume_share(1)

    def test_malformed_json_is_ignored(self):
        discovery = self.pair.value(0, "TAKEOFF_OPERATION_COMMIT")
        malformed = discovery.replace(',"version"', '"version"', 1)
        self.pair.poke(1, f"TAKEOFF_OPERATION_REQUEST:={malformed}")
        time.sleep(1)
        self.assertTrue(self.pair.query(1, "TAKEOFF_PHASE=IDLE"))

    def test_storage_failure_blocks_proposal_transmission(self):
        self.admit()
        (self.pair.work / "uav.state.tmp").mkdir()
        self.pair.correlated(0, "TAKEOFF_TRANSITION", "DEPARTING")
        time.sleep(2)
        self.assertTrue(self.pair.query(0, "TAKEOFF_STORAGE_FAULT=1"))
        self.assertTrue(self.pair.query(0, "TAKEOFF_LOCAL_STATE=STORAGE_FAULT"))
        self.assertTrue(self.pair.query(1, "TAKEOFF_PHASE=ACCEPTED"))

    def test_requester_restart_before_first_response_recovers(self):
        # Discover the authority, then isolate its outbound path before starting.
        self.pair.suspend_share(1)
        self.start_request()
        self.assertTrue(self.pair.query(0, "TAKEOFF_PHASE=REQUESTED"))
        self.pair.restart_operator(0)
        time.sleep(1)
        self.assertTrue(self.pair.query(0, "TAKEOFF_PHASE=REQUESTED"))
        self.pair.resume_share(1)
        self.ready()
        self.assertTrue(self.pair.query(1, "TAKEOFF_PHASE=ACCEPTED", wait=10))

    def test_readiness_revocation_cannot_be_reordered(self):
        now = time.time()
        self.pair.poke(
            1,
            f"TAKEOFF_AUTHORITY_READY:=ready=false#evidence_id=new#observed_at={now}",
        )
        self.pair.poke(
            1,
            f"TAKEOFF_AUTHORITY_READY:=ready=true#evidence_id=old#observed_at={now - 1}",
        )
        self.start_request()
        self.assertTrue(self.pair.query(1, "TAKEOFF_PHASE=DEFERRED"))

    def test_replayed_equal_revision_does_not_refresh_lease(self):
        self.admit()
        stale = self.pair.value(0, "TAKEOFF_OPERATION_SNAPSHOT")
        self.pair.suspend_share(1)
        self.assertTrue(self.pair.query(0, "TAKEOFF_LOCAL_STATE=AUTHORITY_TIMEOUT", wait=8))
        self.pair.poke(0, f"TAKEOFF_OPERATION_COMMIT:={stale}")
        time.sleep(1)
        self.assertTrue(self.pair.query(0, "TAKEOFF_LOCAL_STATE=AUTHORITY_TIMEOUT"))
        self.pair.resume_share(1)

    def test_terminal_snapshots_continue_with_discovery(self):
        self.admit()
        operation_id = self.pair.value(0, "TAKEOFF_OPERATION_ID")
        revision = int(float(self.pair.value(0, "TAKEOFF_REVISION")))
        self.pair.poke(
            0,
            f"TAKEOFF_CANCEL:=id={operation_id}#revision={revision}#cancel=true",
        )
        self.assertTrue(self.pair.query(1, "TAKEOFF_PHASE=ABORTING"))
        self.pair.correlated(1, "TAKEOFF_AUTHORITY_TRANSITION", "CANCELLED")
        self.assertTrue(self.pair.query(0, "TAKEOFF_PHASE=CANCELLED"))
        time.sleep(2)
        message = json.loads(self.pair.value(1, "TAKEOFF_OPERATION_COMMIT"))
        self.assertEqual("STATE_SNAPSHOT", message["action"])
        self.assertEqual("CANCELLED", message["phase"])

    def test_unissued_acknowledgement_is_rejected(self):
        self.admit()
        snapshot = json.loads(self.pair.value(0, "TAKEOFF_OPERATION_SNAPSHOT"))
        snapshot["action"] = "STATE_SNAPSHOT"
        snapshot["revision"] += 1
        snapshot["previous_phase"] = "ACCEPTED"
        snapshot["phase"] = "DEPARTING"
        snapshot["actor"] = "requester"
        snapshot["accepted_event_sequence"] = 999
        snapshot["sent_at"] = time.time()
        encoded = json.dumps(snapshot, separators=(",", ":"))
        self.pair.poke(0, f"TAKEOFF_OPERATION_COMMIT:={encoded}")
        time.sleep(1)
        self.assertTrue(self.pair.query(0, "TAKEOFF_PHASE=ACCEPTED"))

    def test_authority_restart_without_state_fails_closed(self):
        self.admit()
        authority = self.pair.operator_processes[1]
        if authority is None:
            self.fail("authority is not running")
        authority.terminate()
        authority.wait(timeout=5)
        (self.pair.work / "pearl.state").unlink()
        self.pair.start_operator(1)
        self.assertTrue(
            self.pair.query(0, "TAKEOFF_LOCAL_STATE=AUTHORITY_RESTARTED", wait=8)
        )
        self.pair.restart_operator(0)
        self.assertTrue(
            self.pair.query(0, "TAKEOFF_LOCAL_STATE=AUTHORITY_RESTARTED", wait=8)
        )
        self.assertTrue(self.pair.query(0, "TAKEOFF_RECONCILIATION_REQUIRED=1"))
        self.assertTrue(self.pair.query(1, "TAKEOFF_PHASE=IDLE"))

    def test_authority_restart_recovers_same_generation_and_revision(self):
        self.admit()
        operation_id = self.pair.value(0, "TAKEOFF_OPERATION_ID")
        revision = self.pair.value(0, "TAKEOFF_REVISION")
        self.pair.restart_operator(1)
        time.sleep(2)
        self.assertTrue(self.pair.query(1, "TAKEOFF_PHASE=ACCEPTED"))
        self.assertEqual(operation_id, self.pair.value(1, "TAKEOFF_OPERATION_ID"))
        self.assertEqual(revision, self.pair.value(1, "TAKEOFF_REVISION"))
        self.assertTrue(self.pair.query(0, "TAKEOFF_RESPONSE_FRESH=1"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
