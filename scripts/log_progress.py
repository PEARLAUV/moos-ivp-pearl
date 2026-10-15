"""Stderr-only progress for artifact collection; no vehicle operations."""
from __future__ import annotations

import codecs
import hashlib
import os
import selectors
import shutil
import stat
import subprocess
import sys
import time
from pathlib import Path
from typing import Callable, TextIO


def status(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


def byte_size(value: float) -> str:
    for unit in ("B", "KiB", "MiB", "GiB", "TiB"):
        if value < 1024 or unit == "TiB":
            return f"{value:.1f} {unit}"
        value /= 1024
    raise AssertionError("unreachable")


class Progress:
    """Measured bytes, throttled redraws, and explicit no-progress time.

    Updates run on the caller's thread: FC safety polling is never replaced by
    a progress worker. A completed byte count is not a checksum-verification claim.
    """
    def __init__(self, label: str, total: int, *, stream: TextIO | None = None,
                 clock: Callable[[], float] | None = None):
        self.label = label
        self.total = total
        self.stream = stream if stream is not None else sys.stderr
        self.clock = clock if clock is not None else time.monotonic
        self.tty = self.stream.isatty()
        self.interval = 1.0 if self.tty else 5.0
        self.started = self.last_change = self.clock()
        self.last_display = float("-inf")
        self.amount = 0
        self.width = 0

    def __enter__(self):
        self.update(0, force=True)
        return self

    def __exit__(self, exc_type, _exc, _traceback):
        state = "failed" if exc_type else ("done" if self.amount >= self.total else "incomplete")
        self.update(self.amount, force=True, detail=state)
        if self.tty:
            self.stream.write("\n")
            self.stream.flush()

    def advance(self, count: int) -> None:
        self.update(self.amount + count)

    def update(self, amount: int, *, force: bool = False, detail: str = "") -> None:
        now = self.clock()
        if amount > self.amount:
            self.last_change = now
        self.amount = amount
        if not force and now - self.last_display < self.interval:
            return
        elapsed = max(0.0, now - self.started)
        idle = max(0.0, now - self.last_change)
        rate = amount / elapsed if elapsed > 0 else 0.0
        fraction = min(1.0, amount / self.total) if self.total else float(detail == "done")
        filled = int(20 * fraction)
        bar = "#" * filled + "-" * (20 - filled)
        eta = f"{max(0, self.total - amount) / rate:.0f}s" if rate > 0 and idle < 5 else "--"
        state = detail or (f"waiting for data {idle:.0f}s" if idle >= 5 else "")
        metrics = (f"[{bar}] {fraction * 100:5.1f}% "
                   f"{byte_size(amount)}/{byte_size(self.total)} "
                   f"{byte_size(rate)}/s ETA {eta}")
        if not self.tty:
            metrics += f" elapsed {elapsed:.0f}s"
        if state:
            metrics += " | " + state
        label = self.label
        if self.tty:
            columns = shutil.get_terminal_size((120, 24)).columns - 1
            if len(metrics) + 16 > columns:
                metrics = metrics.replace(f"[{bar}] ", "")
            available = max(0, columns - len(metrics) - 1)
            if len(label) > available:
                label = (label[:max(0, available - 3)] + "...") if available >= 3 else ""
        line = f"{label} {metrics}".lstrip()
        self.stream.write(("\r" + line.ljust(self.width)) if self.tty else line + "\n")
        self.stream.flush()
        self.width = len(line)
        self.last_display = now


def hash_file(path: Path, progress: Progress | None = None) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
            if progress is not None:
                progress.advance(len(chunk))
    return digest.hexdigest()


def copy_file(source, destination):
    """copytree callback retaining copy2 metadata, with per-file byte progress."""
    source, destination = Path(source), Path(destination)
    if not stat.S_ISREG(source.stat().st_mode):
        raise shutil.SpecialFileError(f"Not a regular source file: {source}")
    if destination.exists():
        if source.samefile(destination):
            raise shutil.SameFileError(f"Source and destination are the same file: {source}")
        if not stat.S_ISREG(destination.stat().st_mode):
            raise shutil.SpecialFileError(f"Not a regular destination file: {destination}")
    with Progress(f"Copy {source.name}", source.stat().st_size) as progress:
        with source.open("rb") as reader, destination.open("wb") as writer:
            for chunk in iter(lambda: reader.read(1024 * 1024), b""):
                writer.write(chunk)
                progress.advance(len(chunk))
        shutil.copystat(source, destination)
    return str(destination)


def run_streaming(command: list[str], label: str, *, stream: TextIO | None = None,
                  heartbeat_interval: float = 10.0) -> tuple[int, str]:
    """Stream subprocess CR/newline output immediately; retain only an 8 KiB tail.

    Keep stdin/session behavior unchanged for SSH authentication. Silent file
    listing/network waits get elapsed-time notices, not fabricated percentages.
    Interrupted children are terminated and reaped before callers retain staging.
    """
    output = stream if stream is not None else sys.stderr
    started = last_output = last_notice = time.monotonic()
    tail = b""
    decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as child:
        try:
            assert child.stdout is not None
            with selectors.DefaultSelector() as selector:
                selector.register(child.stdout, selectors.EVENT_READ)
                while selector.get_map() or child.poll() is None:
                    if selector.get_map():
                        events = selector.select(timeout=min(1.0, heartbeat_interval))
                    else:
                        events = []
                        try:
                            child.wait(timeout=min(1.0, heartbeat_interval))
                        except subprocess.TimeoutExpired:
                            pass
                    for key, _mask in events:
                        chunk = os.read(key.fd, 16384)
                        if not chunk:
                            selector.unregister(key.fileobj)
                            continue
                        tail = (tail + chunk)[-8192:]
                        text = decoder.decode(chunk)
                        output.write(text if output.isatty() else text.replace("\r", "\n"))
                        output.flush()
                        last_output = time.monotonic()
                    now = time.monotonic()
                    if now - last_output >= heartbeat_interval and now - last_notice >= heartbeat_interval:
                        output.write(f"\n{label}: still running, no subprocess output for "
                                     f"{now - last_output:.0f}s (elapsed {now - started:.0f}s; "
                                     "may be listing files, awaiting authentication, or waiting on I/O)\n")
                        output.flush()
                        last_notice = now
            remaining = decoder.decode(b"", final=True)
            if remaining:
                output.write(remaining)
            if tail and not tail.endswith(b"\n"):
                output.write("\n")
            output.flush()
            return child.wait(), tail.decode("utf-8", errors="replace")
        finally:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
