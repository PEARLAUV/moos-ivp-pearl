#!/usr/bin/env python3
"""Synthetic native-format timing tests; no FC or network access."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import dataflash_timing as timing

BASE = 1_800_000_000


def fmt(kind, name, codes, columns):
    length = 3 + sum(timing.FIELD_SIZES[code] for code in codes)
    return b"\xa3\x95\x80" + struct.pack("<BB4s16s64s", kind, length,
                                        name.encode(), codes.encode(), columns.encode())


def rtc(boot, utc):
    return b"\xa3\x95\x81" + struct.pack("<QQ", int(boot * 1e6), int(utc * 1e6))


class TimingTest(unittest.TestCase):
    def probe(self, data, end=BASE + 100):
        self.ranges = []
        def read(start, stop):
            self.ranges.append((start, stop))
            self.assertEqual(start % 90, 0)
            self.assertTrue(stop == len(data) or stop % 90 == 0)
            return data[start:stop]
        return timing.probe_interval(len(data), read, end)

    def test_rtc_interval_and_padding_not_fabricated(self):
        data = fmt(129, "RTC", "QQ", "TimeUS,TimeUTC") + rtc(1, BASE) + rtc(11, BASE + 10)
        result = self.probe(data)
        self.assertEqual(result["start_unix"], BASE)
        self.assertEqual(result["end_unix"], BASE + 10)

    def test_gps_anchor_without_rtc(self):
        week, remainder = divmod(BASE - timing.GPS_EPOCH_UNIX + 18, 604_800)
        data = fmt(130, "GPS", "QBIH", "TimeUS,Status,GMS,GWk")
        for boot, delta in ((1, 0), (11, 10)):
            data += b"\xa3\x95\x82" + struct.pack("<QBIH", boot * 1_000_000, 3,
                                                 (remainder + delta) * 1000, week)
        result = self.probe(data)
        self.assertEqual(result["start_unix"], BASE)
        self.assertEqual(result["end_unix"], BASE + 10)

    def test_large_file_uses_bounded_ranges_and_tail_anchor(self):
        data = fmt(129, "RTC", "QQ", "TimeUS,TimeUTC") + fmt(130, "TEST", "Q", "TimeUS")
        data += b"".join(b"\xa3\x95\x82" + struct.pack("<Q", i * 1_000_000)
                         for i in range(1, 40_001))
        data += rtc(40_001, BASE + 40_000)
        result = self.probe(data)
        self.assertEqual(result["start_unix"], BASE)
        self.assertEqual(result["end_unix"], BASE + 40_000)
        self.assertLessEqual(sum(b - a for a, b in self.ranges),
                             timing.MAX_PREFIX_BYTES + timing.PROBE_BYTES)
        self.assertLess(sum(b - a for a, b in self.ranges), len(data))

    def test_future_log_does_not_need_tail(self):
        data = fmt(129, "RTC", "QQ", "TimeUS,TimeUTC") + rtc(1, BASE + 1000) * 10_000
        result = self.probe(data)
        self.assertGreater(result["start_unix"], BASE + 100)
        self.assertNotIn("end_unix", result)
        self.assertEqual(len(self.ranges), 1)

    def test_missing_clock_fails_instead_of_using_catalog_time(self):
        data = fmt(130, "TEST", "Q", "TimeUS") + b"\xa3\x95\x82" + struct.pack("<Q", 1_000_000)
        with self.assertRaisesRegex(timing.TimingError, "No native UTC"):
            self.probe(data)

    def test_inconsistent_clock_rejected(self):
        data = fmt(129, "RTC", "QQ", "TimeUS,TimeUTC") + rtc(1, BASE) + rtc(11, BASE + 100)
        with self.assertRaisesRegex(timing.TimingError, "clock changed"):
            self.probe(data)

    def test_clock_reversal_rejected(self):
        data = fmt(129, "RTC", "QQ", "TimeUS,TimeUTC") + rtc(11, BASE + 10) + rtc(1, BASE)
        with self.assertRaisesRegex(timing.TimingError, "reversed"):
            self.probe(data)

    def test_empty_and_unframed_data_rejected(self):
        for data in (b"", b"not a DataFlash log"):
            with self.assertRaises(timing.TimingError):
                self.probe(data)


if __name__ == "__main__":
    unittest.main(verbosity=2)
