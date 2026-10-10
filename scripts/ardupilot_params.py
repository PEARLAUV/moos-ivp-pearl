#!/usr/bin/env python3
"""Ordered parameter overlays and explicitly invoked, guarded ArduPilot upload.

Standard library only. PARAM_SET uses ArduPilot's C-cast encoding, not bytewise
encoding. Trusted MAVLink transport; CRC is checked, signatures not authenticated.
No flight, arm, mode, mission, reboot or estimator-reset commands are emitted.
"""

import argparse
import fcntl
import json
import math
import os
import re
import select
import socket
import struct
import sys
import termios
import time
from pathlib import Path

EXTRAS = {0: 50, 20: 214, 22: 220, 23: 168, 76: 152, 77: 143, 245: 130}
INTEGER_LIMITS = {
    1: (0, 255), 2: (-128, 127), 3: (0, 65535), 4: (-32768, 32767),
    5: (0, 4294967295), 6: (-2147483648, 2147483647),
}
NAME = re.compile(r"[A-Z][A-Z0-9_]{0,15}\Z")


class UploadError(RuntimeError):
    """A fail-closed configuration or native-evidence failure."""


class RebootRequired(UploadError):
    """Known boot-instantiated changes require manual provisioning/reboot."""


def load_stack(paths):
    """Last occurrence wins; retain source provenance, including within a file."""
    entries = {}
    for source in paths:
        path = Path(source).resolve(strict=True)
        if not path.is_file():
            raise ValueError(f"Not a parameter file: {path}")
        for number, line in enumerate(path.read_text().splitlines(), 1):
            fields = line.split("#", 1)[0].split()
            if not fields:
                continue
            if len(fields) != 2 or not NAME.fullmatch(fields[0]):
                raise ValueError(f"{path}:{number}: expected PARAM_NAME numeric_value")
            try:
                value = float(fields[1])
                wire = struct.unpack("<f", struct.pack("<f", value))[0]
            except (ValueError, OverflowError) as exc:
                raise ValueError(f"{path}:{number}: invalid finite float32 value") from exc
            if not math.isfinite(value) or not math.isfinite(wire):
                raise ValueError(f"{path}:{number}: nonfinite value")
            entries[fields[0]] = {"value": value, "file": str(path), "line": number}
    return entries


def validate_real_stack(entries):
    for name, entry in entries.items():
        value = entry["value"]
        if name.startswith("SIM_"):
            raise UploadError(f"REAL refuses simulation parameter {name}")
        if re.fullmatch(r"RNGFND\d+_TYPE", name) and value == 100:
            raise UploadError(f"REAL refuses SITL backend {name}=100")
        if re.fullmatch(r"RNGFND\d+_(MIN|GNDCLR)", name) and value <= 0:
            raise UploadError(f"REAL refuses idealized zero/negative sensor limit {name}")


def crc(data):
    result = 65535
    for byte in data:
        tmp = byte ^ (result & 255)
        tmp = (tmp ^ (tmp << 4)) & 255
        result = (result >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)
    return result


def encode(mid, payload, sequence=0, system=252, component=197):
    header = bytes((len(payload), 0, 0, sequence % 256, system, component))
    header += mid.to_bytes(3, "little")
    return b"\xfd" + header + payload + struct.pack("<H", crc(header + payload + bytes((EXTRAS[mid],))))


class Reader:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        while self.buffer:
            if self.buffer[0] not in (253, 254):
                del self.buffer[0]
                continue
            v2 = self.buffer[0] == 253
            header = 10 if v2 else 6
            if len(self.buffer) < header:
                return
            length = self.buffer[1]
            size = header + length + 2 + (13 if v2 and self.buffer[2] & 1 else 0)
            if len(self.buffer) < size:
                return
            packet = bytes(self.buffer[:size])
            del self.buffer[:size]
            if v2:
                system, component = packet[5:7]
                mid = int.from_bytes(packet[7:10], "little")
            else:
                system, component, mid = packet[3:6]
            if mid not in EXTRAS:
                continue
            expected = crc(packet[1:header + length] + bytes((EXTRAS[mid],)))
            if expected != struct.unpack_from("<H", packet, header + length)[0]:
                continue
            yield mid, system, component, packet[header:header + length]


class Link:
    """TCP client, UDP listener (like the bridge), or exclusively opened serial."""
    def __init__(self, protocol, endpoint):
        self.protocol = protocol
        self.socket = None
        self.fd = None
        self.peer = None
        self.last_sender = None
        if protocol in ("tcp", "udp"):
            host, port = endpoint.rsplit(":", 1)
            if protocol == "tcp":
                self.socket = socket.create_connection((host, int(port)), timeout=3)
            else:
                self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                # No SO_REUSE*: do not steal an active bridge/GCS listener.
                self.socket.bind((host, int(port)))
            self.socket.setblocking(False)
        else:
            path, baud = endpoint.rsplit(":", 1)
            if not path.startswith("/"):
                path = "/dev/" + path
            baud_constant = getattr(termios, "B" + baud, None)
            if baud_constant is None:
                raise ValueError(f"Unsupported serial baud {baud}")
            self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            try:
                fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                fcntl.ioctl(self.fd, termios.TIOCEXCL)
                attrs = termios.tcgetattr(self.fd)
                attrs[0] = attrs[1] = attrs[3] = 0
                attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
                attrs[4] = attrs[5] = baud_constant
                attrs[6][termios.VMIN] = attrs[6][termios.VTIME] = 0
                termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
            except BaseException:
                self.close()
                raise

    def recv(self, timeout):
        handle = self.socket if self.socket is not None else self.fd
        if handle is None:
            raise UploadError("MAVLink transport closed")
        if not select.select([handle], [], [], timeout)[0]:
            return b""
        if self.protocol == "udp":
            if self.socket is None:
                raise UploadError("UDP socket closed")
            data, sender = self.socket.recvfrom(8192)
            if self.peer is not None and sender != self.peer:
                return b""
            self.last_sender = sender
            return data
        if self.socket is not None:
            data = self.socket.recv(8192)
        else:
            if self.fd is None:
                raise UploadError("Serial port closed")
            data = os.read(self.fd, 8192)
        if not data:
            raise UploadError("MAVLink transport closed")
        return data

    def send(self, data):
        deadline = time.monotonic() + 2
        while data:
            if self.protocol == "udp":
                if self.peer is None or self.socket is None:
                    raise UploadError("No confirmed native FC UDP peer")
                self.socket.sendto(data, self.peer)
                return
            handle = self.socket if self.socket is not None else self.fd
            if handle is None:
                raise UploadError("MAVLink transport closed")
            if not select.select([], [handle], [], max(0, deadline - time.monotonic()))[1]:
                raise UploadError("MAVLink write timeout")
            if self.socket is not None:
                written = self.socket.send(data)
            else:
                if self.fd is None:
                    raise UploadError("Serial port closed")
                written = os.write(self.fd, data)
            if written <= 0:
                raise UploadError("MAVLink write failed")
            data = data[written:]

    def close(self):
        if self.socket is not None:
            self.socket.close()
        if self.fd is not None:
            with_context = self.fd
            self.fd = None
            try:
                fcntl.ioctl(with_context, termios.TIOCNXCL)
            finally:
                os.close(with_context)


class Client:
    def __init__(self, link, system=1, component=1, timeout=3, freshness=2):
        self.link, self.system, self.component = link, system, component
        self.timeout, self.freshness = timeout, freshness
        self.reader, self.sequence = Reader(), 0
        self.heartbeat = self.ground = None
        self.params = {}
        self.admitted = False
        self.unsafe_seen = False
        self.last_ground_request = 0

    def send(self, mid, payload):
        self.link.send(encode(mid, payload, self.sequence))
        self.sequence += 1

    def pump(self, timeout=0.05):
        data = self.link.recv(timeout)
        for mid, system, component, payload in self.reader.feed(data):
            if (system, component) != (self.system, self.component):
                continue
            now = time.monotonic()
            if mid == 0 and len(payload) >= 9 and payload[5] == 3:
                self.heartbeat = (bool(payload[6] & 128), now)
                if self.link.protocol == "udp" and self.link.peer is None:
                    self.link.peer = self.link.last_sender
                if self.admitted and self.heartbeat[0]:
                    self.unsafe_seen = True
            elif mid == 245 and len(payload) >= 2:
                self.ground = (payload[1] == 1, now)
                if self.admitted and not self.ground[0]:
                    self.unsafe_seen = True
            elif mid == 22 and len(payload) >= 24:
                value, _, _, name, typ = struct.unpack("<fHH16sB", payload.ljust(25, bytes(1))[:25])
                key = name.split(bytes(1), 1)[0].decode("ascii", errors="replace")
                self.params[key] = (value, typ, now)
        # Request a fresh native landed fact, not a fabricated ground assumption.
        now = time.monotonic()
        if (self.heartbeat is not None and now - self.last_ground_request >= 0.4
                and (self.link.protocol != "udp" or self.link.peer is not None)):
            self.send(76, struct.pack("<7fHBBB", 245, 0, 0, 0, 0, 0, 0,
                                      512, self.system, self.component, 0))
            self.last_ground_request = now

    def guard(self):
        now = time.monotonic()
        if self.unsafe_seen:
            raise UploadError("Native armed/airborne transition during upload")
        if self.heartbeat is None or now - self.heartbeat[1] > self.freshness:
            raise UploadError("Missing/stale native ArduPilot heartbeat")
        if self.heartbeat[0]:
            raise UploadError("Flight controller is armed")
        if self.ground is None or now - self.ground[1] > self.freshness or not self.ground[0]:
            raise UploadError("Missing/stale native ON_GROUND evidence")

    def admit(self):
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            self.pump()
            if self.heartbeat is not None and self.heartbeat[0]:
                raise UploadError("Flight controller is armed")
            try:
                self.guard()
            except UploadError:
                continue
            self.admitted = True
            return
        self.guard()
        raise UploadError("Native ground/disarm admission timed out")

    def drain(self):
        # Bound drain work even if another client floods the channel.
        for _ in range(16):
            self.pump(0)
        self.guard()

    def read(self, name):
        self.drain()
        sent = time.monotonic()
        self.send(20, struct.pack("<hBB16s", -1, self.system, self.component, name.encode()))
        deadline = sent + self.timeout
        while time.monotonic() < deadline:
            self.pump()
            self.guard()
            result = self.params.get(name)
            if result is not None and result[2] >= sent:
                if not math.isfinite(result[0]):
                    raise UploadError(f"Nonfinite native parameter {name}")
                return result[:2]
        raise UploadError(f"No native PARAM_REQUEST_READ response for {name}")

    def write(self, name, value, typ):
        self.drain()
        self.params.pop(name, None)
        sent = time.monotonic()
        self.send(23, struct.pack("<fBB16sB", value, self.system, self.component,
                                  name.encode(), typ))
        # Consume the SET echo before issuing a separate readback request.
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            self.pump()
            self.guard()
            result = self.params.get(name)
            if (result is not None and result[2] >= sent
                    and result[1] == typ and same(result[0], value)):
                self.params.pop(name)
                return
        raise UploadError(f"No matching native SET echo for {name}")


def same(actual, wanted):
    wire = struct.unpack("<f", struct.pack("<f", wanted))[0]
    return actual == wire


def validate_type(name, value, typ):
    if typ in INTEGER_LIMITS:
        low, high = INTEGER_LIMITS[typ]
        if not value.is_integer() or not low <= value <= high or not same(value, value):
            raise UploadError(f"{name}: value cannot be represented by native integer type {typ}")
    elif typ != 9:
        raise UploadError(f"{name}: unsupported native parameter type {typ}")


def reboot_key(name):
    # Configuration readback is not proof that a boot-instantiated backend changed.
    return (name in {"PLND_ENABLED", "PLND_TYPE", "PLND_ORIENT"}
            or bool(re.fullmatch(r"RNGFND\d+_(TYPE|ORIENT)", name))
            or bool(re.fullmatch(r"SERIAL\d+_PROTOCOL", name)))


def upload(client, entries, report, persist=None):
    validate_real_stack(entries)
    if not entries:
        raise UploadError("No parameters selected for upload")
    client.admit()
    report["native_admission"] = {
        "heartbeat": client.heartbeat, "on_ground": client.ground,
        "receipt_clock": "host monotonic seconds", "freshness_limit_s": client.freshness,
    }
    report["before"] = {}
    # Read and type-check the WHOLE stack before the first mutation.
    for name, entry in entries.items():
        value, typ = client.read(name)
        validate_type(name, entry["value"], typ)
        report["before"][name] = {"value": value, "type": typ}
    for sensor in {name.split("_")[0] for name in entries if name.startswith("RNGFND")}:
        backend, _ = client.read(sensor + "_TYPE")
        if backend in (0, 100):
            raise UploadError(f"REAL requires installed hardware backend: {sensor}_TYPE={backend}")
    report["attempted"] = []
    report["written"] = []
    report["reboot_required"] = [
        name for name, entry in entries.items()
        if reboot_key(name) and not same(report["before"][name]["value"], entry["value"])
    ]
    if report["reboot_required"]:
        # Do not SET boot-sensitive values and then accidentally accept them on
        # a later invocation without knowing whether the FC actually rebooted.
        raise RebootRequired("Manually provision/reboot before upload: "
                             + ", ".join(report["reboot_required"]))
    report["status"] = "IN_PROGRESS"
    if persist is not None:
        persist()
    for name, entry in entries.items():
        client.guard()
        before = report["before"][name]
        if not same(before["value"], entry["value"]):
            # Persist before SEND: interrupted/refused writes may still mutate FC.
            report["attempted"].append(name)
            if persist is not None:
                persist()
            client.write(name, entry["value"], before["type"])
            value, typ = client.read(name)
            if typ != before["type"] or not same(value, entry["value"]):
                raise UploadError(f"Readback mismatch after writing {name}: {value}")
            report["written"].append(name)
            if persist is not None:
                persist()
    # Final independent readback catches concurrent changes after an earlier ACK.
    report["readback"] = {}
    for name, entry in entries.items():
        value, typ = client.read(name)
        if typ != report["before"][name]["type"] or not same(value, entry["value"]):
            raise UploadError(f"Final readback mismatch for {name}: {value}")
        report["readback"][name] = value
    client.guard()
    report["native_final"] = {"heartbeat": client.heartbeat, "on_ground": client.ground}
    report["status"] = "VERIFIED_CONFIGURATION"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--param-file", action="append", default=[], help="Ordered overlay; repeat, later wins")
    parser.add_argument("--apply", action="store_true", help="Explicitly enable guarded REAL PARAM_SET")
    parser.add_argument("--protocol", choices=("tcp", "udp", "serial"), default="tcp")
    parser.add_argument("--endpoint", help="TCP host:port, UDP local bind host:port, serial device:baud")
    parser.add_argument("--system-id", type=int, default=1)
    parser.add_argument("--component-id", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("--report", help="JSON audit/partial-failure report (required with --apply)")
    args = parser.parse_args()
    report = {"status": "FAILED", "written": [], "note": "Readback verifies configuration, not active flight readiness."}
    link = None
    path = None
    audit_created = False
    try:
        report["effective"] = load_stack(args.param_file)
        if not args.apply:
            print(json.dumps(report["effective"], indent=2))
            return 0
        if not args.endpoint or not args.report:
            raise ValueError("--apply requires --endpoint and --report")
        if not (1 <= args.system_id <= 255 and 1 <= args.component_id <= 255):
            raise ValueError("Explicit FC system/component IDs must be 1..255")
        if not math.isfinite(args.timeout) or not 0 < args.timeout <= 30:
            raise ValueError("--timeout must be finite and in (0,30] seconds")
        validate_real_stack(report["effective"])
        report["target"] = {"protocol": args.protocol, "endpoint": args.endpoint,
                            "system_id": args.system_id, "component_id": args.component_id}
        # Create the audit path before connecting; do not mutate if it is unwritable.
        path = Path(args.report)
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("x") as destination:
            audit_created = True
            json.dump(report, destination, indent=2)
        link = Link(args.protocol, args.endpoint)
        upload(Client(link, args.system_id, args.component_id, args.timeout),
               report["effective"], report,
               persist=lambda: path.write_text(json.dumps(report, indent=2) + "\n"))
        print(report["status"] + ": " + str(path))
        return 0
    except (OSError, ValueError, UploadError, KeyboardInterrupt) as exc:
        report["status"] = "REBOOT_REQUIRED" if isinstance(exc, RebootRequired) else "FAILED"
        report["error"] = str(exc)
        print(f"Parameter upload refused/failed: {exc}", file=sys.stderr)
        return 3 if isinstance(exc, RebootRequired) else 1
    finally:
        if link is not None:
            link.close()
        # Only overwrite an audit file created by this invocation.
        if args.apply and path is not None and audit_created:
            path.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
