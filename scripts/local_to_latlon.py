#!/usr/bin/env python3
"""Convert MOOS local x/y to latitude/longitude.

MOOS-IvP is built with USE_UTM, so local coordinates are zone UTM offsets
from LatOrigin/LongOrigin (CMOOSGeodesy::LLtoUTM, WGS-84). A flat
metres-per-degree conversion is off by the UTM grid convergence (~1.4 deg
near Boston, i.e. ~1 m per 40 m of northing).

Usage: local_to_latlon.py LAT_ORIGIN LON_ORIGIN X Y
"""

import sys
from math import cos, radians, sin, sqrt, tan

USAGE = "Usage: local_to_latlon.py LAT_ORIGIN LON_ORIGIN X Y"
EQUATORIAL_RADIUS = 6378137.0
ECC_SQUARED = 0.00669438


def utm(lat, lon, zone=None):
    """USGS Bulletin 1532 forward UTM, matching CMOOSGeodesy::LLtoUTM."""
    k0 = 0.9996
    ep2 = ECC_SQUARED / (1 - ECC_SQUARED)
    e2 = ECC_SQUARED
    if zone is None:
        zone = int((lon + 180) / 6) + 1
    lon_origin = (zone - 1) * 6 - 180 + 3
    la = radians(lat)
    n = EQUATORIAL_RADIUS / sqrt(1 - e2 * sin(la) ** 2)
    t = tan(la) ** 2
    c = ep2 * cos(la) ** 2
    a = cos(la) * radians(lon - lon_origin)
    m = EQUATORIAL_RADIUS * (
        (1 - e2 / 4 - 3 * e2**2 / 64 - 5 * e2**3 / 256) * la
        - (3 * e2 / 8 + 3 * e2**2 / 32 + 45 * e2**3 / 1024) * sin(2 * la)
        + (15 * e2**2 / 256 + 45 * e2**3 / 1024) * sin(4 * la)
        - (35 * e2**3 / 3072) * sin(6 * la)
    )
    east = k0 * n * (
        a + (1 - t + c) * a**3 / 6
        + (5 - 18 * t + t * t + 72 * c - 58 * ep2) * a**5 / 120
    ) + 500000.0
    north = k0 * (m + n * tan(la) * (
        a * a / 2 + (5 - t + 9 * c + 4 * c * c) * a**4 / 24
        + (61 - 58 * t + t * t + 600 * c - 330 * ep2) * a**6 / 720
    ))
    return east, north, zone


def to_latlon(lat0, lon0, x, y):
    """Newton inversion of the local UTM offset; sub-millimetre in a few steps."""
    east0, north0, zone = utm(lat0, lon0)

    def local(lat, lon):
        east, north, _ = utm(lat, lon, zone)
        return east - east0, north - north0

    lat, lon, step = lat0, lon0, 1e-6
    for _ in range(20):
        fx, fy = local(lat, lon)
        dx, dy = x - fx, y - fy
        if abs(dx) < 1e-5 and abs(dy) < 1e-5:
            break
        ax, ay = local(lat + step, lon)
        bx, by = local(lat, lon + step)
        j11, j12 = (ax - fx) / step, (bx - fx) / step
        j21, j22 = (ay - fy) / step, (by - fy) / step
        det = j11 * j22 - j12 * j21
        lat += (j22 * dx - j12 * dy) / det
        lon += (-j21 * dx + j11 * dy) / det
    return lat, lon


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(USAGE)
    lat, lon = to_latlon(*(float(arg) for arg in sys.argv[1:]))
    print(f"{lat:.8f} {lon:.8f}")
