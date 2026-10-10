#!/usr/bin/env python3
"""Real-MOOS destination routing with owned ports/processes; no flight apps."""

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
NODES = ("uav", "pearl", "shoreside")


def allocate(kind, used):
    while True:
        with socket.socket(socket.AF_INET, kind) as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        if port not in used:
            used.add(port)
            return port


def read_log(root, node):
    files = list((root / node).glob("**/*.alog"))
    if not files:
        raise AssertionError((root / node / "apps.log").read_text())
    rows, start = set(), None
    for line in files[0].read_text().splitlines():
        if "LOGSTART" in line:
            start = float(line.split()[-1])
        fields = line.split(None, 3)
        if len(fields) == 4 and not line.startswith("%"):
            try:
                timestamp = float(fields[0])
            except ValueError:
                continue  # Multiline diagnostic values, not standalone alog rows.
            rows.add((timestamp, fields[1], fields[2], fields[3].rstrip()))
    assert start is not None
    return sorted(rows), start


class MediatedRoutingTest(unittest.TestCase):
    def exercise(self, brokered=False):
        root = Path(tempfile.mkdtemp(prefix="mediated-routes-"))
        used = set()
        ports = {
            (node, kind): allocate(
                socket.SOCK_STREAM if kind == "db" else socket.SOCK_DGRAM, used
            )
            for node in NODES
            for kind in ("db", "udp")
        }
        processes, handles = [], []

        def poke(node, *assignments):
            subprocess.run(
                ["uPokeDB", str(root / node / "test.moos"), "--quiet", *assignments],
                check=True,
                capture_output=True,
                timeout=10,
            )

        try:
            for node in NODES:
                directory = root / node
                directory.mkdir()
                peers = [name for name in NODES if name != node]
                routes = []
                # Static routes match Briggs; optional broker coverage uses $V
                # only for shore's own outgoing traffic, never peer transit.
                if node != "shoreside" or not brokered:
                    for dest in peers:
                        for prefix, incoming in (
                            ("MEDIATED_TX", "MEDIATED_MESSAGE"),
                            ("ACK_TX", "ACK_MESSAGE"),
                        ):
                            if node == "pearl" and dest == "uav" and prefix == "ACK_TX":
                                continue  # Deliberately lose early ACKs in this owned test.
                            routes.append(
                                f"output = src_name={prefix}_{dest.upper()},dest_name={incoming},route=127.0.0.1:{ports[dest, 'udp']}"
                            )
                if brokered and node != "shoreside":
                    routes.append(
                        f"output = src_name=NODE_BROKER_PING,route=127.0.0.1:{ports['shoreside', 'udp']}"
                    )
                extra_app = (
                    "Run = uFldShoreBroker @ NewConsole = false"
                    if brokered and node == "shoreside"
                    else ""
                )
                mission = directory / "test.moos"
                mission.write_text(f"""ServerHost = 127.0.0.1
ServerPort = {ports[node, "db"]}
Community = {node}
LatOrigin = 42.3569186052
LongOrigin = -71.0990291116
ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = MOOSDB @ NewConsole = false
  Run = pShare @ NewConsole = false
  Run = {ROOT / "bin/pMediatorRouter"} @ NewConsole = false ~ pMediatorRouter
  Run = pMediator @ NewConsole = false
  Run = uFldMessageHandler @ NewConsole = false
  Run = pLogger @ NewConsole = false
  {extra_app}
}}
ProcessConfig = pShare
{{
  AppTick = 40
  CommsTick = 40
  input = route=127.0.0.1:{ports[node, "udp"]}
  {chr(10).join(routes)}
}}
ProcessConfig = pMediatorRouter
{{
  AppTick = 40
  CommsTick = 40
  vname = {node}
  {chr(10).join("peer = " + peer for peer in peers)}
  allow_relay = false
}}
ProcessConfig = pMediator
{{
  AppTick = 20
  CommsTick = 40
  vname = {node}
  mates = {"pearl,shoreside" if node == "uav" else "uav"}
  resend_thresh = 1
  max_tries = 10
}}
ProcessConfig = uFldMessageHandler
{{
  AppTick = 40
  CommsTick = 40
  strict_addressing = true
}}
ProcessConfig = uFldShoreBroker
{{
  AppTick = 40
  CommsTick = 40
  auto_bridge_appcast = false
  auto_bridge_realmcast = false
  bridge = src=MEDIATED_TX_$V,alias=MEDIATED_MESSAGE
  bridge = src=ACK_TX_$V,alias=ACK_MESSAGE
}}
ProcessConfig = pLogger
{{
  AppTick = 20
  CommsTick = 40
  File = LOG
  PATH = ./
  AsyncLog = true
  FileTimeStamp = false
  WildCardLogging = true
}}
""")
                log = (directory / "apps.log").open("w")
                handles.append(log)
                processes.append(
                    subprocess.Popen(
                        ["pAntler", str(mission)],
                        cwd=directory,
                        stdout=log,
                        stderr=subprocess.STDOUT,
                        start_new_session=True,
                    )
                )
            time.sleep(2)
            if brokered:
                for node in ("uav", "pearl"):
                    poke(
                        node,
                        f"NODE_BROKER_PING:=community={node},hostip=127.0.0.1,port_db={ports[node, 'db']},pshare_iroutes=127.0.0.1:{ports[node, 'udp']},timewarp=1,time=0,key=0",
                    )
            time.sleep(0.5)
            # Prime global sequence zero once per sender before real test data.
            for src in NODES:
                dest = next(name for name in NODES if name != src)
                poke(
                    src,
                    f"NODE_MESSAGE_LOCAL:=src_node={src},dest_node={dest},var_name=PROBE_SYNC,string_val=true",
                )
            time.sleep(0.3)
            expected = {}
            for src in NODES:
                for dest in NODES:
                    if src == dest:
                        continue
                    variable = f"PROBE_{src.upper()}_{dest.upper()}"
                    value = f"payload_{src}_{dest},points={{1,2:3,4}}#session=probe"
                    expected[variable] = (src, dest, value)
                    poke(
                        src,
                        f'NODE_MESSAGE_LOCAL:=src_node={src},dest_node={dest},var_name={variable},string_val="{value}"',
                    )
                    if src == "uav" and dest == "pearl":
                        time.sleep(
                            1.3
                        )  # Test loss interval, never flight-readiness evidence.
                        poke(
                            "pearl",
                            f"PSHARE_CMD:=cmd=output,src_name=ACK_TX_UAV,dest_name=ACK_MESSAGE,route=127.0.0.1:{ports['uav', 'udp']}",
                        )
                    # Cancellation is a distinct event, never latest/coalesced.
                    poke(
                        src,
                        f"NODE_MESSAGE_LOCAL:=src_node={src},dest_node={dest},var_name={variable}_CANCEL,string_val=true",
                    )
            time.sleep(1)
            # Retried duplicate envelope must cause an ACK but not re-execution.
            uav_rows, _ = read_log(root, "uav")
            duplicate = next(
                r[3]
                for r in uav_rows
                if r[1] == "MEDIATED_MESSAGE_LOCAL"
                and "var_name=PROBE_UAV_PEARL," in r[3]
            )
            poke("pearl", f"MEDIATED_MESSAGE:={duplicate}")
            # Invalid envelopes are rejected at the sender, before transport.
            poke(
                "uav",
                "MEDIATED_MESSAGE_LOCAL:=ack_id=uav_bad,src_node=uav,dest_node=unknown,var_name=PROBE_BAD,string_val=true",
            )
            poke(
                "pearl",
                "MEDIATED_MESSAGE_LOCAL:=ack_id=pearl_dup,src_node=pearl,dest_node=shoreside,dest_node=uav,var_name=PROBE_BAD,string_val=true",
            )
            poke(
                "pearl",
                "MEDIATED_MESSAGE_LOCAL:=ack_id=uav_bad_src,src_node=uav,dest_node=shoreside,var_name=PROBE_BAD,string_val=true",
            )
            for node in NODES:
                poke(
                    node,
                    "APPCAST_REQ:=node=all,app=pMediator,duration=1,key=routing_test",
                )
            time.sleep(1)
        finally:
            for process in processes:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
            for process in processes:
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=3)
            for handle in handles:
                handle.close()
        logs = {node: read_log(root, node) for node in NODES}
        for node, (rows, _) in logs.items():
            apps = [
                r[3]
                for r in rows
                if r[1] == "APPCAST" and r[3].startswith("proc=pMediator!")
            ]
            self.assertTrue(apps, str(root))
            self.assertIn("run_warning_total=0", apps[-1], str(root) + "\n" + apps[-1])
            for _, key, _, value in rows:
                if key in ("MEDIATED_MESSAGE", "ACK_MESSAGE"):
                    field = "dest_node" if key == "MEDIATED_MESSAGE" else "dest"
                    match = re.search(rf"(?:^|,)\s*{field}=([^,]+)", value)
                    self.assertIsNotNone(match, value)
                    assert match is not None
                    self.assertEqual(match[1], node, str(root))
                self.assertNotIn(
                    key,
                    ("MEDIATED_MESSAGE_IN", "ACK_MESSAGE_IN", "PROBE_BAD"),
                    str(root),
                )
                if key.startswith(("MEDIATED_TX_", "ACK_TX_")):
                    field = "dest_node" if key.startswith("MEDIATED_TX_") else "dest"
                    match = re.search(rf"(?:^|,)\s*{field}=([^,]+)", value)
                    assert match is not None
                    self.assertEqual(key.rsplit("_", 1)[1], match[1].upper(), str(root))
                    source = re.search(r"(?:^|,)\s*(?:src_node|src)=([^,]+)", value)
                    assert source is not None
                    self.assertEqual(source[1], node, str(root))
        for variable, (src, dest, value) in expected.items():
            for node, (rows, _) in logs.items():
                self.assertEqual(
                    [r[3] for r in rows if r[1] == variable],
                    [value] if node == dest else [],
                    (variable, node, root),
                )
                self.assertEqual(
                    [r[3] for r in rows if r[1] == variable + "_CANCEL"],
                    ["true"] if node == dest else [],
                    (variable, node, root),
                )
            original = next(
                r
                for r in logs[src][0]
                if r[1] == "MEDIATED_MESSAGE_LOCAL" and f"var_name={variable}," in r[3]
            )
            received = next(
                r
                for r in logs[dest][0]
                if r[1] == "MEDIATED_MESSAGE" and f"var_name={variable}," in r[3]
            )
            self.assertEqual(original[3], received[3])
            self.assertAlmostEqual(
                original[0] + logs[src][1], received[0] + logs[dest][1], delta=0.001
            )
            ack_id = re.search(r"ack_id=([^,]+)", original[3])
            assert ack_id is not None
            self.assertTrue(
                any(
                    r[1] == "ACK_MESSAGE" and f"id={ack_id[1]}" in r[3]
                    for r in logs[src][0]
                ),
                (variable, root),
            )
        retries = [
            r
            for r in logs["uav"][0]
            if r[1] == "MEDIATED_MESSAGE_LOCAL" and "var_name=PROBE_UAV_PEARL," in r[3]
        ]
        self.assertGreaterEqual(len(retries), 2, str(root))
        shore_commands = [r[3] for r in logs["shoreside"][0] if r[1] == "PSHARE_CMD"]
        if brokered:
            for node in ("uav", "pearl"):
                for prefix, alias in (
                    ("MEDIATED_TX", "MEDIATED_MESSAGE"),
                    ("ACK_TX", "ACK_MESSAGE"),
                ):
                    self.assertTrue(
                        any(
                            f"src_name={prefix}_{node.upper()},dest_name={alias},route=127.0.0.1:{ports[node, 'udp']}"
                            in command
                            for command in shore_commands
                        ),
                        str(root),
                    )
        else:
            self.assertFalse(shore_commands, str(root))
        print(f"Verified sender-only direct links (brokered={brokered}): {root}")

    def test_sender_only_static_direct_links(self):
        self.exercise()

    def test_sender_only_brokered_direct_shore_outputs(self):
        self.exercise(brokered=True)


if __name__ == "__main__":
    unittest.main()
