"""Bounded DataFlash timing probes; no pymavlink dependency or vehicle writes.

LOG_ENTRY.time_utc is file mtime (or current RTC for an open log) in
ArduPilot AP_Logger_File::_get_log_time, NOT log start. Use native TimeUS
and UTC anchors instead. GPS conversion follows ArduPilot's current 18-second
GPS/UTC offset; disagreement with RTC or other anchors fails closed.
"""
from __future__ import annotations

import statistics
import struct
from dataclasses import dataclass
from typing import Callable

SYNC = b"\xa3\x95"
# Aligned to LOG_DATA's 90-byte blocks. At most 256 KiB head + 64 KiB tail.
PROBE_BYTES = 65_520
MAX_PREFIX_BYTES = 262_080
GPS_EPOCH_UNIX = 315_964_800
GPS_UTC_OFFSET_SECONDS = 18
FIELD_SIZES = dict(zip("bBhHiIqQfdnNZcCeELMa", (1, 1, 2, 2, 4, 4, 8, 8,
                                               4, 8, 4, 16, 64, 2, 2, 4, 4, 4, 1, 64)))


class TimingError(ValueError):
    """Native timing is missing, inconsistent, or outside the supported format."""


@dataclass(frozen=True)
class Format:
    length: int
    name: str
    codes: str
    columns: tuple[str, ...]


def records(data: bytes, formats: dict[int, Format]):
    """Walk framed records, tolerating a partial first/last probe record.

    Known packet lengths skip payloads rather than interpreting embedded sync
    bytes as records. When resynchronizing, also require the next frame boundary
    (unless the record reaches the end of the probe).
    """
    position = 0
    while position + 3 <= len(data):
        position = data.find(SYNC, position)
        if position < 0 or position + 3 > len(data):
            return
        kind = data[position + 2]
        length = 89 if kind == 128 else formats.get(kind, Format(0, "", "", ())).length
        if length < 3 or position + length > len(data):
            position += 1
            continue
        end = position + length
        if end + 3 <= len(data) and data[end:end + 2] != SYNC:
            position += 1
            continue
        payload = data[position + 3:end]
        if kind == 128:
            type_id, packet_length, name, codes, columns = struct.unpack("<BB4s16s64s", payload)
            try:
                name_s, code_s, column_s = (field.split(b"\0", 1)[0].decode("ascii")
                                           for field in (name, codes, columns))
            except UnicodeError as exc:
                raise TimingError("Non-ASCII DataFlash format") from exc
            if packet_length < 3 or not name_s or not code_s:
                raise TimingError("Invalid DataFlash format descriptor")
            formats[type_id] = Format(packet_length, name_s, code_s, tuple(column_s.split(",")))
        else:
            yield formats[kind], payload
        position = end


def integer_field(fmt: Format, payload: bytes, name: str) -> int | None:
    offset = 0
    for code, column in zip(fmt.codes, fmt.columns):
        size = FIELD_SIZES.get(code)
        if size is None:
            return None
        if column == name:
            if code not in "BHIQ" or offset + size > len(payload):
                return None
            return struct.unpack_from("<" + code, payload, offset)[0]
        offset += size
    return None


def timing_samples(data: bytes, formats: dict[int, Format]) -> tuple[list[int], list[float]]:
    stamps: list[int] = []
    offsets: list[float] = []
    for fmt, payload in records(data, formats):
        stamp = integer_field(fmt, payload, "TimeUS")
        if stamp is None or stamp == 0:
            continue
        stamps.append(stamp)
        utc = None
        if fmt.name == "RTC":
            utc_us = integer_field(fmt, payload, "TimeUTC")
            if utc_us is not None:
                utc = utc_us / 1_000_000
        elif fmt.name in ("GPS", "GPS2"):
            week = integer_field(fmt, payload, "GWk")
            millis = integer_field(fmt, payload, "GMS")
            status = integer_field(fmt, payload, "Status")
            if week and millis is not None and status is not None and status >= 3 and millis < 604_800_000:
                utc = GPS_EPOCH_UNIX + week * 604_800 + millis / 1000 - GPS_UTC_OFFSET_SECONDS
        if utc is not None and 946_684_800 <= utc < 4_102_444_800:
            offsets.append(utc - stamp / 1_000_000)
    return stamps, offsets


def probe_interval(size: int, read_range: Callable[[int, int], bytes],
                   mission_end: float) -> dict:
    """Read a bounded head/tail, returning native UTC bounds or explicit failure.

    A log starting after the mission needs no tail. Missing UTC never triggers
    a whole-log download. The caller owns safety guards and catalog stability.
    """
    if size <= 0:
        raise TimingError("Empty DataFlash log")
    prefix_end = min(size, PROBE_BYTES)
    prefix = read_range(0, prefix_end)
    formats: dict[int, Format] = {}
    stamps, offsets = timing_samples(prefix, formats)
    if not offsets and prefix_end < min(size, MAX_PREFIX_BYTES):
        extended_end = min(size, MAX_PREFIX_BYTES)
        prefix += read_range(prefix_end, extended_end)
        prefix_end = extended_end
        formats = {}
        stamps, offsets = timing_samples(prefix, formats)
    if not stamps:
        raise TimingError("No native timestamp in bounded DataFlash prefix")
    result = {"clock": "native TimeUS anchored by RTC/GPS UTC",
              "prefix_bytes": prefix_end, "tail_bytes": 0}
    # Exclude definitely later logs without reading their tails.
    if offsets and max(offsets) - min(offsets) <= 2:
        start = stamps[0] / 1_000_000 + statistics.median(offsets)
        if start > mission_end:
            return {**result, "start_unix": start,
                    "utc_offset_spread_seconds": max(offsets) - min(offsets)}
    if prefix_end == size:
        tail_stamps, tail_offsets = stamps, offsets
    else:
        tail_start = ((max(0, size - PROBE_BYTES) + 89) // 90) * 90
        tail = read_range(tail_start, size)
        result["tail_bytes"] = size - tail_start
        tail_stamps, tail_offsets = timing_samples(tail, formats)
    anchors = offsets + tail_offsets
    if not anchors:
        raise TimingError("No native UTC anchor in bounded DataFlash probes; mission membership unknown")
    if not tail_stamps:
        raise TimingError("No native timestamp in bounded DataFlash tail")
    if max(anchors) - min(anchors) > 2:
        raise TimingError("Native UTC clock changed across DataFlash log")
    offset = statistics.median(anchors)
    start = stamps[0] / 1_000_000 + offset
    end = tail_stamps[-1] / 1_000_000 + offset
    if end < start:
        raise TimingError("DataFlash clock reversed within log")
    return {**result, "start_unix": start, "end_unix": end,
            "utc_offset_spread_seconds": max(anchors) - min(anchors)}
