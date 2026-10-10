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
  projection_mode = geographic // or relative for GPS-free bench work
  relative_anchor_x = 0
  relative_anchor_y = 0
  observation_timeout = 0.5
  input_timeout = 2
  target_size = 1
  use_received_size = true
  fallback_heading = 0
  label = received_landing_target
  color = magenta
}
```

The default `geographic` projection requires mission `LatOrigin` / `LongOrigin`
and fresh vehicle latitude/longitude. The app uses UTM offsets, matching the
integration mission and bridge. `projection_mode=relative` instead places the
vehicle at `relative_anchor_x/y` and draws the target at its attitude-rotated
north/east offset from that anchor; it requires no mission origin or vehicle
latitude/longitude. Relative mode is explicitly drone-relative and cannot show
a world-stable target position during vehicle translation without an external
position source.

Output is a label-hidden `VIEW_POLYGON` plus numeric
`LANDING_TARGET_VIEW_ACTIVE` (0/1). Relative mode also publishes a stationary
`NODE_REPORT` named `uav` at the configured anchor, with heading from native
vehicle yaw, so pMarineViewer can draw and center on the fixed vehicle. Forward
these visuals through pShare/the node broker when running distributed. AppCasts report projection mode, age, gating reason,
reconstructed position, dimensions, heading, and whether size/orientation came
from received data or configuration.

## Input and freshness

Required received fields under `target_prefix`:

- `_AVAILABLE`, `_AGE`, `_POSITION_VALID`, `_FRAME`, `_X`, `_Y`, `_Z`.
- Optional `_DISTANCE`, `_SIZE_X`, `_SIZE_Y` for size estimation.
- Optional `_Q_W`, `_Q_X`, `_Q_Y`, `_Q_Z` for orientation.

For the direct-input emulator, configure
`observation_pose_samples_var = UAV_NAV_MAVLINK_POSES`. This native FC pose
window is forwarded unchanged from the emulator's MAVLink receiver; it contains
aircraft telemetry only, not target geometry. Both attitude and aircraft position
are taken from the exact window entry matching received `_TIME_USEC`.
Unavailable/invalid/old/unmatched windows hide the footprint; **no latest-pose
fallback** is allowed with this option. Original native receipt must be within
`observation_timeout` real seconds. Paired position/attitude boot stamps have the
emulator's explicit bounded skew; they are not an interpolated simultaneous pose.
Reposts do not renew that original receipt.

Without this option, native vehicle attitude is
`<vehicle_prefix>_ROLL/PITCH/YAW`, with `_ATTITUDE_AVAILABLE=1` and
`_ATTITUDE_AGE` in real seconds. Geographic mode additionally requires
`<vehicle_prefix>_LAT/LON`; relative mode does not. Euler angles are native
degrees (positive right bank/nose-up/clockwise true yaw), never COG. Missing
attitude is not treated as level. All fields are published by the bridge; no
`TARGET_NAV`, simulator position or emulator-active flag is used.

Only position-valid **BODY_FRD (12)** packets are currently supported; other
frames and legacy angle-only packets are hidden rather than guessed. The
full forward/right/down vector is rotated by the native **full** body-to-NED
matrix, converted to geographic position, then projected into the MOOS UTM grid.
The vertical body component participates in the horizontal map reconstruction
when tilted. Actual native age plus elapsed relay age must remain within
`input_timeout` real seconds; pose/field message age is separately bounded in
MOOS seconds.

A bounded 128-sample native attitude history uses `_ATTITUDE_TIME_USEC` and
`_ATTITUDE_SAMPLE_TIME`. When received target `_TIME_USEC` matches a recent native
sample, its exposure-time rotation is used instead of newer attitude.
`_ATTITUDE_SAMPLES` imports the bridge's atomic original-pose window in full,
avoiding skipped snapshots at lower subscriber rates. Reposted windows retain
original receipt times, and malformed/empty data clear the history. Matching a
known stale sample still hides the footprint; the map is never flight permission. The emulator
supplies this contract. Source-clock rewind clears old history. An old matched
sample hides the footprint. Unmatched/missing camera clocks use the latest fresh
native attitude with an explicit AppCast approximation label; no clock offset or
interpolation is guessed. Latitude/longitude remain latest received navigation,
not synchronized position history, so motion/transport latency can still affect
map position. The yaw coordinator fails closed on unmatched observation clocks.

The target marker is shown only while `_AVAILABLE=1` and the actual bridge-reported
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
perspective reconstruction. Unequal angular dimensions retain a rectangular
base rather than inventing a square. Set `use_received_size=false` for a known fixed
physical square, or leave it true and use `target_size` as the square-side
fallback when angular sizes are absent/zero/invalid.

The display is a five-vertex **house outline**: three sides keep the original
base dimensions, while the projected forward side is replaced by two roof
edges. The apex extends outward by half that side's displayed width, giving both
roof edges 45-degree angles to the removed side **on the map**, including for a
rectangular or tilted base. The original three projected sides are unchanged.
The roof marks the target's forward side, using the same received orientation
(or configured fallback). It is a direction glyph, not a larger physical landing
surface: target center, reported dimensions, and original-base degeneracy
validation are unchanged.

A finite nonzero received quaternion is normalized and interpreted as rotation
of target-local axes into the reported BODY_FRD frame. The corners are transformed
with that rotation and full vehicle attitude, then projected into the map, preserving
quaternion tilt in the target footprint's horizontal projection. A degenerate
projection is hidden. Missing/zero quaternions use `fallback_heading`, in
**local-grid degrees**. No simulator heading is substituted for received data.

**Current emulator contract:** `pMavlinkLandingTarget` transmits zero angular sizes,
so the default marker has a configurable 1 m square base plus the forward roof. It encodes fresh target
**local-grid heading** in a BODY_FRD quaternion, correcting for UTM grid north
and the full native vehicle attitude. The packet identifies its attitude sample.
Thus the static target's received footprint remains at
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
quaternion orientation, preserved base dimensions and 45-degree roof geometry
(including rectangular bases and fallback headings), invalid-frame/
position rejection, and expiry of frozen receive-age telemetry under time warp.
The `target` integration mission runs this app alongside the broadcaster: its
lime source point stays visible, while the magenta received-data footprint is
shown on the first two passes and disappears on the out-of-range third pass.
