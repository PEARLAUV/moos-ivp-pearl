# pLandingTargetViewer

Small, passive MOOS AppCasting app displaying **received** MAVLink landing-target
telemetry already relayed by `pArduBridge`. No MAVLink socket, simulator-position
subscription, target broadcasting, flight command, or landing policy.

```moos
ProcessConfig = pLandingTargetViewer
{
  AppTick = 20
  CommsTick = 20
  target_prefix = UAV_LANDING_TARGET
  vehicle_prefix = UAV
  observation_timeout = 0.5
  input_timeout = 2
  target_size = 1
  use_received_size = true
  fallback_heading = 0
  label = received_landing_target
  color = magenta
}
```

Mission `LatOrigin` / `LongOrigin` are required. The app uses UTM offsets, matching
the integration mission and bridge. Output is a label-hidden `VIEW_POLYGON` plus
numeric `LANDING_TARGET_VIEW_ACTIVE` (0/1). Forward the polygon through pShare/
the node broker to shoreside. AppCasts report age, gating reason, reconstructed
position, dimensions, heading, and whether size/orientation came from received
data or configuration.

## Input and freshness

Required received fields under `target_prefix`:

- `_AVAILABLE`, `_AGE`, `_POSITION_VALID`, `_FRAME`, `_X`, `_Y`, `_Z`.
- Optional `_DISTANCE`, `_SIZE_X`, `_SIZE_Y` for size estimation.
- Optional `_Q_W`, `_Q_X`, `_Q_Y`, `_Q_Z` for orientation.

Native vehicle pose is `<vehicle_prefix>_LAT/LON/YAW`; yaw is true heading in
degrees, **not course over ground**. All these variables are already published
by the bridge. No `TARGET_NAV` or emulator-active flag is used.

Only position-valid **BODY_FRD (12)** packets are currently supported; other
frames and legacy angle-only packets are hidden rather than guessed. The
forward/right vector is rotated by native vehicle yaw, converted to geographic
position, then projected into the MOOS UTM grid. This uses the **level-attitude
model** of `pMavlinkLandingTarget`; roll/pitch compensation and synchronized pose
history are not implemented, so motion/attitude and transport latency can affect
reconstruction with real cameras or aggressive flight.

The square is shown only while `_AVAILABLE=1` and the actual bridge-reported
receive age is within `observation_timeout` (**real seconds**, default 0.5).
An old cached packet repeatedly relayed by the bridge is not treated as a new
observation. If the relay stops, elapsed time since the last `_AGE` update is
added, corrected for MOOS time warp, so a frozen age value still expires.
`input_timeout` is a separate freshness limit in **MOOS seconds** for pose/fields.
Missing, stale, invalid, unsupported, or unprojectable inputs post `active=false`
for the same polygon label, removing any previous footprint.

## Size and orientation

[MAVLink LANDING_TARGET](https://mavlink.io/en/messages/common.html#LANDING_TARGET)
provides `size_x` / `size_y` in **radians**, not physical metres. When both are
valid positive angles and distance is positive, the viewer estimates:

- Width: `2 * distance * tan(size_x / 2)`.
- Length: `2 * distance * tan(size_y / 2)`.

This is an apparent-size, near-normal-view estimate, not a full camera-pose/
perspective reconstruction. Unequal angular dimensions produce a rectangle
rather than inventing a square. Set `use_received_size=false` for a known fixed
physical square, or leave it true and use `target_size` as the square-side
fallback when angular sizes are absent/zero/invalid.

A finite nonzero received quaternion is normalized and interpreted as rotation
of target-local axes into the reported BODY_FRD frame. The corners are transformed
with that rotation and vehicle yaw, then projected into the map, preserving
quaternion tilt in the target footprint's horizontal projection. A degenerate
projection is hidden. Missing/zero quaternions use `fallback_heading`, in
**local-grid degrees**. No simulator heading is substituted for received data.

**Current emulator contract:** `pMavlinkLandingTarget` transmits zero angular sizes,
so the default footprint is a configurable 1 m square. It encodes fresh target
**local-grid heading** in a BODY_FRD quaternion, correcting for UTM grid north
and native vehicle yaw. Thus the static target's received footprint remains at
30 grid degrees on both N/S passes, rather than rotating with the aircraft.
Missing/stale target heading or unavailable mission origin produces an all-zero
quaternion (this application's unavailable-orientation convention); the viewer
then uses its configured heading fallback. The viewer still reads only received
telemetry, never silently borrowing the known simulation orientation.

## Build and test

```bash
PEARLOPTIONS=all cmake -S . -B build
cmake --build build --target pLandingTargetViewer
python3 src/pLandingTargetViewer/tests/test_viewer.py
```

The native tests use isolated MOOS instances to verify position reconstruction,
quaternion orientation, angular-size/configured-size handling, invalid-frame/
position rejection, and expiry of frozen receive-age telemetry under time warp.
The `target` integration mission runs this app alongside the broadcaster: its
lime source point stays visible, while the magenta received-data footprint is
shown on the first two passes and disappears on the out-of-range third pass.
