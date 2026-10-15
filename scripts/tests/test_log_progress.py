#!/usr/bin/env python3
"""Progress UI and live child-output tests; no remote hosts or flight controller."""
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import log_progress as progress


class Tty(io.StringIO):
    def isatty(self):
        return True


class ProgressTest(unittest.TestCase):
    def test_tty_bar_rate_eta_and_throttling(self):
        output = Tty()
        now = [0.0]
        with mock.patch.object(progress.shutil, "get_terminal_size", return_value=os.terminal_size((120, 24))):
            with progress.Progress("Transfer", 1000, stream=output, clock=lambda: now[0]) as bar:
                before = output.getvalue()
                now[0] = 0.1
                bar.update(100)
                self.assertEqual(output.getvalue(), before)
                now[0] = 2.0
                bar.update(500)
                self.assertIn("50.0%", output.getvalue())
                self.assertIn("250.0 B/s", output.getvalue())
                self.assertIn("ETA 2s", output.getvalue())
                now[0] = 3.0
                bar.update(1000)
        self.assertIn("[##########----------]", output.getvalue())
        self.assertIn("\r", output.getvalue())
        self.assertTrue(output.getvalue().endswith("\n"))
        self.assertIn("done", output.getvalue())

    def test_non_tty_is_line_based_and_reports_no_progress(self):
        output = io.StringIO()
        now = [0.0]
        with progress.Progress("Probe", 100, stream=output, clock=lambda: now[0]) as bar:
            now[0] = 6.0
            bar.update(0)
        self.assertNotIn("\r", output.getvalue())
        self.assertIn("waiting for data 6s", output.getvalue())
        self.assertIn("ETA --", output.getvalue())
        self.assertIn("incomplete", output.getvalue())
        self.assertNotIn("100.0%", output.getvalue())

    def test_exception_reports_failure_not_success(self):
        output = io.StringIO()
        with self.assertRaisesRegex(ValueError, "fixture"):
            with progress.Progress("Copy", 100, stream=output) as bar:
                bar.advance(25)
                raise ValueError("fixture")
        self.assertIn("failed", output.getvalue())
        self.assertNotIn("done", output.getvalue())
        self.assertNotIn("100.0%", output.getvalue())

    def test_empty_file_and_chunked_copy_preserve_bytes_and_metadata(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(progress.sys, "stderr", io.StringIO()):
            root = Path(directory)
            for index, data in enumerate((b"", b"sample" * 300_000)):
                source, target = root / f"source{index}", root / f"target{index}"
                source.write_bytes(data)
                source.chmod(0o640)
                os.utime(source, ns=(1_700_000_000_000_000_000, 1_700_000_000_000_000_000))
                progress.copy_file(source, target)
                self.assertEqual(target.read_bytes(), data)
                self.assertEqual(target.stat().st_mtime_ns, source.stat().st_mtime_ns)
                self.assertEqual(target.stat().st_mode, source.stat().st_mode)
                with progress.Progress("Hash", len(data)) as bar:
                    self.assertEqual(progress.hash_file(target, bar), hashlib.sha256(data).hexdigest())
                    self.assertEqual(bar.amount, len(data))
                with self.assertRaises(shutil.SameFileError):
                    progress.copy_file(source, source)
            fifo = root / "fifo"
            os.mkfifo(fifo)
            with self.assertRaises(shutil.SpecialFileError):
                progress.copy_file(fifo, root / "bad")

    def test_child_output_is_visible_before_child_completes(self):
        with tempfile.TemporaryDirectory() as directory:
            ack = Path(directory) / "ack"
            class AcknowledgingOutput(io.StringIO):
                def write(self, text):
                    result = super().write(text)
                    if "50%" in self.getvalue():
                        ack.touch()
                    return result
            output = AcknowledgingOutput()
            code = ("import pathlib,time,sys; p=pathlib.Path(sys.argv[1]); "
                    "print('50%',end='\\r',flush=True); deadline=time.monotonic()+3\n"
                    "while not p.exists() and time.monotonic()<deadline: time.sleep(.01)\n"
                    "print('100%',flush=True); sys.exit(0 if p.exists() else 2)")
            result, tail = progress.run_streaming([sys.executable, "-c", code, str(ack)],
                                                 "fixture", stream=output)
            self.assertEqual(result, 0)
            self.assertIn("50%", output.getvalue())
            self.assertNotIn("\r", output.getvalue())
            self.assertIn("100%", tail)

    def test_silent_child_has_wait_notice_and_bounded_error_tail(self):
        output = io.StringIO()
        result, tail = progress.run_streaming(
            [sys.executable, "-c", "import time,sys; time.sleep(.12); print('x'*20000,flush=True); print('failure',file=sys.stderr,flush=True); sys.exit(23)"],
            "rsync fixture", stream=output, heartbeat_interval=0.02)
        self.assertEqual(result, 23)
        self.assertIn("still running", output.getvalue())
        self.assertIn("failure", tail)
        self.assertLessEqual(len(tail.encode()), 8192)

    def test_interrupted_child_is_terminated_and_reaped(self):
        children = []
        original = subprocess.Popen
        def spawn(*args, **kwargs):
            child = original(*args, **kwargs)
            children.append(child)
            return child
        class InterruptedOutput(io.StringIO):
            def write(self, _text):
                raise KeyboardInterrupt
        with mock.patch.object(progress.subprocess, "Popen", side_effect=spawn):
            with self.assertRaises(KeyboardInterrupt):
                progress.run_streaming([sys.executable, "-c", "import time; print('ready',flush=True); time.sleep(60)"],
                                       "fixture", stream=InterruptedOutput())
        self.assertIsNotNone(children[0].poll())

    @unittest.skipUnless(shutil.which("rsync"), "rsync not installed")
    def test_actual_local_rsync_progress_flags_and_content(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, target = root / "source", root / "target"
            source.mkdir()
            (source / "data").write_bytes(b"payload" * 200_000)
            output = io.StringIO()
            result, _tail = progress.run_streaming(
                ["rsync", "-a", "--partial", "--protect-args", "--info=progress2", "--outbuf=L",
                 "--no-inc-recursive", "--human-readable", str(source) + "/", str(target) + "/"],
                "local fixture", stream=output)
            self.assertEqual(result, 0)
            self.assertIn("100%", output.getvalue())
            self.assertEqual((target / "data").read_bytes(), (source / "data").read_bytes())


if __name__ == "__main__":
    unittest.main(verbosity=2)
