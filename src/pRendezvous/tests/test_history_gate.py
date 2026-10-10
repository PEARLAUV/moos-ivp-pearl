"""Owned real-MOOS history-gate tests; synthetic navigation, no FC or flight stack."""

import os
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


class HistoryGateTest(unittest.TestCase):
    def run_probe(self, *, continuous=True, peer_continuous=True, peer_freeze=False,
                  accelerating=False, window=3):
        with tempfile.TemporaryDirectory(prefix="rendezvous-history-") as directory:
            directory = Path(directory)
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            now = time.time()
            peer_event = ('event = var=NODE_REPORT, val="NAME=pearl,X=6,Y=0", time=0\n'
                          if peer_continuous else "")
            startup_peer = ('event = var=NODE_REPORT, val="NAME=pearl,X=6,Y=0", time=0\n'
                            if not peer_continuous else "")
            freeze_event = (
                'event = var=RENDEZVOUS_MOTION, val="session=peer#node=pearl#phase=RENDEZVOUS'
                f'#time={now}#position_time={now}#x=6#y=0#freeze_time={now-1}'
                '#freeze_node=pearl#point_x=4#point_y=0", time=2.5\n'
                if peer_freeze else ""
            )
            own_event = "event = var=NAV_X, val=0, time=0" if not accelerating else ""
            motion_events = "\n".join(
                f"event = var=NAV_X, val={0.75 * (tick / 4) ** 2}, time={tick / 4}"
                for tick in range(33)
            ) if accelerating else ""
            mission = directory / "test.moos"
            mission.write_text(f"""ServerHost = localhost
ServerPort = {port}
Community = uav
ProcessConfig = ANTLER
{{
  MSBetweenLaunches = 100
  Run = MOOSDB @ NewConsole = false
  Run = {ROOT / 'bin/pRendezvous'} @ NewConsole = false
  Run = pLogger @ NewConsole = false
  Run = uTimerScript @ NewConsole = false ~ uNav
  Run = uTimerScript @ NewConsole = false ~ uStart
  Run = uTimerScript @ NewConsole = false ~ uMotion
}}
ProcessConfig = pRendezvous
{{
  AppTick = 20
  CommsTick = 20
  ownship = uav
  peer_node = pearl
  nav_stale_thresh = 30
  state_post_interval = 0.1
  point_freeze_radius = 6
  arrival_radius = 4
  planning_history_window = {window}
  planning_max_positive_accel = 0.5
}}
ProcessConfig = pLogger
{{
  File = LOG_probe
  AsyncLog = true
  WildCardLogging = true
}}
ProcessConfig = uNav
{{
  AppTick = 20
  CommsTick = 20
  paused = false
  reset_max = {'nolimit' if continuous else '0'}
  reset_time = 0.25
  {own_event}
  event = var=NAV_Y, val=0, time=0
  {peer_event}
}}
ProcessConfig = uStart
{{
  AppTick = 20
  CommsTick = 20
  paused = false
  reset_max = 0
  {startup_peer}
  event = var=RENDEZVOUS_START, val=true, time=2
  event = var=RENDEZVOUS_OWN_WAYPOINT_REACHED, val=true, time=2.25
  {freeze_event}
}}
ProcessConfig = uMotion
{{
  AppTick = 20
  CommsTick = 20
  paused = false
  reset_max = 0
  {motion_events}
}}
""")
            with (directory / "antler.log").open("w") as output:
                process = subprocess.Popen(
                    ["pAntler", str(mission)], cwd=directory, stdout=output,
                    stderr=subprocess.STDOUT, start_new_session=True,
                )
                try:
                    time.sleep(9)
                finally:
                    if process.poll() is None:
                        os.killpg(process.pid, signal.SIGINT)
                        try:
                            process.wait(timeout=6)
                        except subprocess.TimeoutExpired:
                            os.killpg(process.pid, signal.SIGKILL)
                            process.wait(timeout=5)
            # Stop/flush the owned logger before inspecting its buffered alog.
            result = []
            for path in directory.glob("LOG_probe*/*.alog"):
                for line in path.read_text().splitlines():
                    fields = line.split(None, 3)
                    if len(fields) == 4 and fields[2].startswith("pRendezvous"):
                        try:
                            stamp = float(fields[0])
                        except ValueError:  # multiline APPCAST/status continuation
                            continue
                        result.append((stamp, fields[1], fields[3].strip()))
            self.assertTrue(result, (directory / "antler.log").read_text())
            return result

    @staticmethod
    def values(rows, variable):
        return [(stamp, value) for stamp, key, value in rows if key == variable]

    def test_radius_and_capture_cannot_complete_during_bootstrap(self):
        rows = self.run_probe()
        states = self.values(rows, "RENDEZVOUS_OWN_PHASE")
        start = next(stamp for stamp, value in states if value == "RENDEZVOUS")
        complete = next(stamp for stamp, value in states if value == "COMPLETE")
        self.assertGreaterEqual(complete - start, 2.95)
        first_frozen = next(stamp for stamp, value in
                            self.values(rows, "RENDEZVOUS_OWN_POINT_FROZEN") if float(value) == 1)
        self.assertGreaterEqual(first_frozen - start, 2.95)
        self.assertIn("HOLD_POSITION", [value for _, value in self.values(rows, "ARDU_COMMAND")])

    def test_elapsed_time_without_fresh_samples_never_freezes_or_arrives(self):
        rows = self.run_probe(continuous=False)
        self.assert_no_arrival(rows)

    def test_fresh_own_history_does_not_replace_missing_peer_history(self):
        rows = self.run_probe(peer_continuous=False)
        self.assert_no_arrival(rows)

    def test_peer_canonical_freeze_survives_but_arrival_still_needs_history(self):
        rows = self.run_probe(peer_freeze=True)
        states = self.values(rows, "RENDEZVOUS_OWN_PHASE")
        start = next(stamp for stamp, value in states if value == "RENDEZVOUS")
        complete = next(stamp for stamp, value in states if value == "COMPLETE")
        self.assertGreaterEqual(complete - start, 2.95)
        point = self.values(rows, "RENDEZVOUS_OWN_POINT")[-1][1]
        self.assertEqual(tuple(map(float, point.strip("{}").split(","))), (4, 0))

    def test_accelerating_history_blocks_capture_after_warmup(self):
        rows = self.run_probe(accelerating=True)
        self.assert_no_arrival(rows)
        self.assertIn("positive_acceleration", [value for _, value in
                                               self.values(rows, "RENDEZVOUS_OWN_HISTORY_REASON")])
        self.assertTrue(any(float(value) > 0.5 for _, value in
                            self.values(rows, "RENDEZVOUS_OWN_MAX_POSITIVE_ACCEL")))

    def test_invalid_configuration_fails_closed(self):
        rows = self.run_probe(window=2)
        states = [value for _, value in self.values(rows, "RENDEZVOUS_OWN_PHASE")]
        self.assertIn("ABORT", states)
        self.assertNotIn("RENDEZVOUS", states)
        self.assertFalse(self.values(rows, "ARDU_COMMAND"))

    def assert_no_arrival(self, rows):
        self.assertIn("RENDEZVOUS", [value for _, value in self.values(rows, "RENDEZVOUS_OWN_PHASE")])
        self.assertNotIn("COMPLETE", [value for _, value in self.values(rows, "RENDEZVOUS_OWN_PHASE")])
        self.assertFalse(any(float(value) for _, value in
                             self.values(rows, "RENDEZVOUS_OWN_POINT_FROZEN")))
        self.assertFalse(self.values(rows, "ARDU_COMMAND"))


if __name__ == "__main__":
    unittest.main()
