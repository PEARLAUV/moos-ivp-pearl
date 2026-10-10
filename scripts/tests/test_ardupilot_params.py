#!/usr/bin/env python3
"""Protocol/guard/precedence tests; no real FC or flight commands."""

import importlib.util
import json
import os
import socket
import struct
import subprocess
import tempfile
import time
import unittest
from pathlib import Path
from typing import Any, cast

SCRIPT = Path(__file__).resolve().parents[1] / "ardupilot_params.py"
spec = importlib.util.spec_from_file_location("params", SCRIPT)
assert spec is not None and spec.loader is not None
params = cast(Any, importlib.util.module_from_spec(spec))
spec.loader.exec_module(params)


def entry(value):
    return {"value": float(value), "file": "test.parm", "line": 1}


class Peer:
    protocol = "tcp"

    def __init__(self, values=None, *, armed=False, ground=True, system=1):
        self.values = values or {"P": (1.0, 6), "Q": (1.0, 9)}
        self.armed, self.ground, self.system = armed, ground, system
        self.queue = bytearray()
        self.reader = params.Reader()
        self.sent = []
        self.writes = []
        self.bad_storage = False
        self.arm_on_write = False
        self.arm_on_read = False
        self.echo = True

    def frame(self, mid, payload):
        return params.encode(mid, payload, system=self.system, component=1)

    def state(self):
        body = struct.pack("<IBBBBB", 0, 2, 3, 128 if self.armed else 0, 4, 3)
        packet = self.frame(0, body)
        if self.ground:
            packet += self.frame(245, bytes((0, 1)))
        return packet

    def parameter(self, name, value, typ):
        return self.frame(22, struct.pack("<fHH16sB", value, len(self.values), 0,
                                          name.encode(), typ))

    def recv(self, _timeout):
        queued = bytes(self.queue)
        self.queue.clear()
        return self.state() + queued

    def send(self, packet):
        for mid, _, _, body in self.reader.feed(packet):
            self.sent.append((mid, body))
            if mid == 20:
                _, system, component, name = struct.unpack("<hBB16s", body)
                assert (system, component) == (1, 1)
                name = name.split(bytes(1))[0].decode()
                if self.arm_on_read:
                    self.armed = True
                if name in self.values:
                    self.queue += self.parameter(name, *self.values[name])
            elif mid == 23:
                value, system, component, name, typ = struct.unpack("<fBB16sB", body)
                assert (system, component) == (1, 1)
                name = name.split(bytes(1))[0].decode()
                self.writes.append(name)
                self.values[name] = (value + 1 if self.bad_storage else value, typ)
                if self.arm_on_write:
                    self.armed = True
                if self.echo:
                    self.queue += self.parameter(name, value, typ)
            elif mid == 76:
                values = struct.unpack("<7fHBBB", body)
                assert values[7] == 512 and values[0] == 245
            else:
                raise AssertionError(f"Unexpected command {mid}")


class ParameterTest(unittest.TestCase):
    def client(self, peer):
        return params.Client(peer, timeout=0.03)

    def test_precedence_and_provenance_with_spaces(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for name, body in (("drone file.parm", "P 1\nQ 3\n"),
                               ("mode.parm", "P 2\n"),
                               ("mission.parm", "P 4 # final\nP 5\n")):
                path = Path(directory) / name
                path.write_text(body)
                paths.append(path)
            stack = params.load_stack(paths)
            self.assertEqual(stack["P"]["value"], 5)
            self.assertEqual(stack["P"]["file"], str(paths[-1]))
            self.assertEqual(stack["P"]["line"], 2)
            self.assertEqual(stack["Q"]["value"], 3)
            self.assertEqual(params.load_stack([]), {})

    def test_missing_explicit_and_malformed_files_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.parm"
            with self.assertRaises(FileNotFoundError):
                params.load_stack([path])
            for text in ("P nan", "P inf", "P 1e100", "bad 1", "P 1 2", "X" * 17 + " 1"):
                path.write_text(text)
                with self.subTest(text=text), self.assertRaises(ValueError):
                    params.load_stack([path])

    def test_plan_does_not_connect_or_write(self):
        result = subprocess.run([str(SCRIPT)], capture_output=True, text=True, check=True)
        self.assertEqual(json.loads(result.stdout), {})

    def test_crc_and_partial_frames(self):
        reader = params.Reader()
        frame = params.encode(245, bytes((0, 1)), system=1, component=1)
        self.assertEqual(list(reader.feed(frame[:5])), [])
        self.assertEqual(list(reader.feed(frame[5:])), [(245, 1, 1, bytes((0, 1)))])
        bad = frame[:-1] + bytes((frame[-1] ^ 1,))
        self.assertEqual(list(reader.feed(bad)), [])

    def test_native_grounded_upload_readback_and_command_allowlist(self):
        peer = Peer()
        report = {}
        params.upload(self.client(peer), {"P": entry(2), "Q": entry(0.15)}, report)
        self.assertEqual(report["status"], "VERIFIED_CONFIGURATION")
        self.assertEqual(report["written"], ["P", "Q"])
        self.assertGreaterEqual(sum(mid == 20 for mid, _ in peer.sent), 6)
        self.assertTrue(all(mid in (20, 23, 76) for mid, _ in peer.sent))

    def test_armed_airborne_or_wrong_source_cannot_write(self):
        for peer in (Peer(armed=True), Peer(ground=False), Peer(system=2)):
            with self.subTest(peer=peer), self.assertRaises(params.UploadError):
                params.upload(self.client(peer), {"P": entry(2)}, {})
            self.assertEqual(peer.writes, [])

    def test_stale_evidence_is_not_renewed_by_parameters(self):
        client = self.client(Peer())
        client.admit()
        client.heartbeat = (False, time.monotonic() - 3)
        with self.assertRaises(params.UploadError):
            client.guard()
        client.heartbeat = (False, time.monotonic())
        client.ground = (True, time.monotonic() - 3)
        with self.assertRaises(params.UploadError):
            client.guard()

    def test_all_parameters_exist_and_types_fit_before_first_write(self):
        for stack in ({"P": entry(2), "UNKNOWN": entry(3)},
                      {"P": entry(2.5)}, {"P": entry(2 ** 24 + 1)}):
            peer = Peer()
            with self.subTest(stack=stack), self.assertRaises(params.UploadError):
                params.upload(self.client(peer), stack, {})
            self.assertEqual(peer.writes, [])

    def test_real_rejects_simulation_and_zero_overrides_before_io(self):
        for name, value in (("SIM_SONAR_ROT", 25), ("RNGFND1_TYPE", 100),
                            ("RNGFND1_MIN", 0), ("RNGFND1_GNDCLR", 0)):
            peer = Peer()
            with self.subTest(name=name), self.assertRaises(params.UploadError):
                params.upload(self.client(peer), {name: entry(value)}, {})
            self.assertEqual(peer.sent, [])

    def test_real_requires_actual_hardware_sensor_backend(self):
        for backend in (0, 100):
            peer = Peer({"RNGFND1_MIN": (0.15, 9), "RNGFND1_TYPE": (backend, 2)})
            with self.subTest(backend=backend), self.assertRaises(params.UploadError):
                params.upload(self.client(peer), {"RNGFND1_MIN": entry(0.2)}, {})
            self.assertEqual(peer.writes, [])

    def test_echo_is_not_storage_readback_and_partial_failure_is_reported(self):
        peer = Peer()
        peer.bad_storage = True
        report = {}
        with self.assertRaises(params.UploadError):
            params.upload(self.client(peer), {"P": entry(2), "Q": entry(3)}, report)
        self.assertEqual(peer.writes, ["P"])
        self.assertEqual(report["attempted"], ["P"])
        self.assertEqual(report["written"], [])

    def test_arming_during_preflight_or_mutation_stops_remaining_writes(self):
        for key in ("arm_on_read", "arm_on_write"):
            peer = Peer()
            setattr(peer, key, True)
            report = {}
            with self.subTest(key=key), self.assertRaises(params.UploadError):
                params.upload(self.client(peer), {"P": entry(2), "Q": entry(3)}, report)
            self.assertLessEqual(len(peer.writes), 1)
            self.assertNotIn("Q", peer.writes)

    def test_reboot_required_is_not_active_backend_success(self):
        peer = Peer({"PLND_ENABLED": (0.0, 2)})
        report = {}
        with self.assertRaises(params.RebootRequired):
            params.upload(self.client(peer), {"PLND_ENABLED": entry(1)}, report)
        self.assertEqual(report["reboot_required"], ["PLND_ENABLED"])
        self.assertEqual(peer.writes, [])

    def test_audit_is_saved_before_send_and_io_failure_prevents_writes(self):
        peer = Peer()
        snapshots = []
        report = {}
        def persist():
            snapshots.append(json.loads(json.dumps(report)))
        params.upload(self.client(peer), {"P": entry(2)}, report, persist)
        self.assertTrue(any(row.get("attempted") == ["P"] and not row["written"]
                            for row in snapshots))
        peer = Peer()
        def fail():
            raise OSError("disk full")
        with self.assertRaises(OSError):
            params.upload(self.client(peer), {"P": entry(2)}, {}, fail)
        self.assertEqual(peer.writes, [])

    def test_tcp_transport_round_trip_and_closed_connection(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            link = params.Link("tcp", "127.0.0.1:" + str(listener.getsockname()[1]))
            self.addCleanup(link.close)
            connection, _ = listener.accept()
            with connection:
                connection.sendall(b"native fixture")
                self.assertEqual(link.recv(1), b"native fixture")
                link.send(b"read-only fixture")
                self.assertEqual(connection.recv(1024), b"read-only fixture")
            with self.assertRaises(params.UploadError):
                link.recv(1)

    def test_udp_transport_locks_peer_only_after_native_heartbeat(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
            peer.bind(("127.0.0.1", 0))
            link = params.Link("udp", "127.0.0.1:0")
            self.addCleanup(link.close)
            client = self.client(link)
            self.assertIsNone(link.peer)
            heartbeat = params.encode(0, struct.pack("<IBBBBB", 0, 2, 3, 0, 4, 3),
                                      system=1, component=1)
            peer.sendto(heartbeat + params.encode(245, bytes((0, 1)), system=1, component=1),
                        link.socket.getsockname())
            client.admit()
            self.assertEqual(link.peer, peer.getsockname())
            data, _ = peer.recvfrom(1024)
            self.assertTrue(list(params.Reader().feed(data)))

    def test_serial_transport_round_trip_on_owned_pty(self):
        master, slave = os.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        link = params.Link("serial", os.ttyname(slave) + ":115200")
        self.addCleanup(link.close)
        os.write(master, b"native fixture")
        self.assertEqual(link.recv(1), b"native fixture")
        link.send(b"read-only fixture")
        self.assertEqual(os.read(master, 1024), b"read-only fixture")

    def test_model_selection_override_and_invalid_names(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            profiles = root / "config/ardupilot/drones"
            profiles.mkdir(parents=True)
            mission = root / "mission"
            mission.mkdir()
            for path in (profiles / "x500.parm", profiles / "quad-test_2.parm",
                         mission / "sitl.parm", mission / "mission.parm"):
                path.write_text("# Empty selection-only fixture; never uploaded.\n")
            override = root / "explicit drone.parm"
            override.write_text("# Empty explicit-override fixture; never uploaded.\n")
            env = {key: value for key, value in os.environ.items()
                   if key not in ("DRONE_MODEL", "DRONE_PARAM_FILE",
                                  "MODE_PARAM_FILE", "MISSION_PARAM_FILE")}
            command = ["bash", "-c",
                       ('source "$1"; ardupilot_parameter_stack "$2" "$3" SITL || exit; '
                        'printf "%s\\n" "${ARDUPILOT_PARAM_ARGS[@]}"'),
                       "_", str(SCRIPT.parent / "ardupilot_param_stack.sh"),
                       str(root), str(mission)]

            def select(**overrides):
                return subprocess.run(command, env=dict(env, **overrides),
                                      capture_output=True, text=True, check=False)

            for overrides, wanted in (({}, profiles / "x500.parm"),
                                      ({"DRONE_MODEL": "quad-test_2"},
                                       profiles / "quad-test_2.parm"),
                                      ({"DRONE_MODEL": "missing",
                                        "DRONE_PARAM_FILE": str(override)}, override)):
                result = select(**overrides)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.splitlines(),
                                 ["--param-file", str(wanted), "--param-file",
                                  str(mission / "sitl.parm"), "--param-file",
                                  str(mission / "mission.parm")])
            result = select(DRONE_MODEL="missing")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Drone model profile not found", result.stderr)
            for omitted in ("none", ""):
                result = select(DRONE_MODEL="missing", DRONE_PARAM_FILE=omitted)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.splitlines(),
                                 ["--param-file", str(mission / "sitl.parm"),
                                  "--param-file", str(mission / "mission.parm")])
            for invalid in ("", "../x500", "/tmp/x500", "x500.parm", "a b", "a;echo"):
                result = select(DRONE_MODEL=invalid, DRONE_PARAM_FILE="none")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Invalid drone model", result.stderr)

    def test_optional_role_files_and_explicit_missing_path(self):
        root = SCRIPT.parents[1]
        command = ['bash', '-c',
                   ('source "$1"; ardupilot_parameter_stack "$2" "$3" REAL; '
                    'printf "%s\\n" "${ARDUPILOT_PARAM_ARGS[@]}"'),
                   '_', str(SCRIPT.parent / 'ardupilot_param_stack.sh')]
        # Supply empty directories: all default role files may be absent.
        with tempfile.TemporaryDirectory() as directory:
            command += [directory, directory]
            env = {key: value for key, value in os.environ.items()
                   if key not in ('DRONE_MODEL', 'DRONE_PARAM_FILE',
                                  'MODE_PARAM_FILE', 'MISSION_PARAM_FILE')}
            result = subprocess.run(command, env=env, capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), '')
            result = subprocess.run(['bash', '-c',
                                     'source "$1"; ardupilot_parameter_stack "$2" "$3" REAL',
                                     '_', str(SCRIPT.parent / 'ardupilot_param_stack.sh'),
                                     str(root), directory],
                                    env=dict(env, DRONE_PARAM_FILE=directory + '/missing'),
                                    capture_output=True, text=True, check=False)
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
