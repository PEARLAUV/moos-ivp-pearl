# pMavlinkLandingTarget

Independent MOOS visual-target broadcaster. Sends MAVLink 2 `LANDING_TARGET`
on its own TCP connection; no attachment policy, ship poses, UAV navigation
overrides, or flight commands.

```moos
ProcessConfig = pMavlinkLandingTarget
{
  AppTick = 20
  CommsTick = 20
  mavlink_host = 127.0.0.1
  mavlink_port = 5763
  target_nav_prefix = TARGET_NAV
  vehicle_nav_prefix = UAV_NAV
  target_system_id = 1
  source_component_id = 191
  target_number = 0
  publish_rate_hz = 10
  input_timeout = 1
  detection_range = 12
  target_altitude_msl = 1
  show_visuals = true
  // Optional fresh live MSL height, replacing the constant:
  // target_altitude_var = TARGET_ALTITUDE_MSL
  // Optional gates (all must be true; no conditions means true):
  // condition = CAMERA_ENABLED=true
  // condition = QUALITY>=2
}
```

Target inputs: `<target_nav_prefix>_LAT/LONG`, plus constant or live MSL height.
The default prefix is `TARGET_NAV`; heading/speed are not required for position
observations. Optional fresh `<target_nav_prefix>_HEADING` is **local-grid heading**:
with mission `LatOrigin`/`LongOrigin`, it is converted to true heading, then made
relative to native UAV yaw and encoded as a normalized BODY_FRD yaw quaternion
`(cos(relative_yaw/2),0,0,sin(relative_yaw/2))`. Heading is registered even when
visuals are disabled. Missing/stale heading or missing origin leaves orientation
unavailable (all-zero quaternion, this application's convention), without stopping
otherwise valid position observations. Angular sizes remain zero; the received
viewer uses its configured physical size. The point visual has no orientation.

Vehicle inputs: `<vehicle_nav_prefix>_LAT/LONG/HEADING/ALTITUDE_MSL`.
Heading must be attitude yaw, not ground course. The mission's `pEchoVar`
maps native ArduPilot latitude, longitude, yaw and MSL height into these variables.

Fresh finite inputs with all configured `condition` entries true and within
the configured **3-D** detection range produce
target 0 observations in `MAV_FRAME_BODY_FRD`, with forward/right/down metres,
angles, distance, vision-fiducial type, and `position_valid=1`. A level
roll/pitch camera model is assumed. Stale/invalid inputs or excessive range stop
observations; ArduPilot owns target-loss handling. The target has its **own MSL
altitude**, independent of the drone: `target_altitude_msl` supplies a constant,
or `target_altitude_var` selects a live MOOS input. Live height overrides the
constant and must be fresh and within -1000..10000 m MSL. BODY_FRD down is
`vehicle_MSL - target_MSL`; both range and visualization projection use that
vertical difference.

Conditions use standard MOOS-IvP logic expressions. Multiple entries are ANDed;
their numeric/string variables are automatically registered. Unknown condition
inputs evaluate false; invalid expressions warn and block both observations and
visuals rather than silently defaulting true. No entries means true. Condition
values retain their last received values; `input_timeout` applies to geometry,
not the condition variables.

With `show_visuals=true`, local target `<target_nav_prefix>_X/Y` inputs place a
lime `VIEW_POINT` (`landing_target_point`), a lime detection-radius circle, and
a yellow altitude-projected horizontal cross-section circle. Target heading is
reported in the AppCast and encoded in transmitted observations, but the point
has no drawn orientation.
Visuals are gated **only by conditions**, once finite x/y have been received;
they retain the last-known position even with stale geometry, a disconnected
link, or a UAV outside the detection sphere. The projected circle uses the
last-known heights and collapses to a 0.001 m renderer minimum when the vertical
separation exceeds the range or height inputs are not yet known. False conditions
post `active=false` for all three shapes. `show_visuals` still defaults false.

The target is the configured physical geographic position, **not** a rendezvous
waypoint. Broadcasting never depends on `SIM_UAV_ON_SHIP`. Match target
height to the attachment app's deck MSL height, or use the same live variable.

Source system ID defaults to the UAV's ID (1), component 191; this lets native
UAV-system passthrough observe the packets. A separate onboard-controller
`HEARTBEAT` advertises that vision component at 1 Hz. It is not a ship or
autopilot heartbeat. Observation rate defaults to 10 Hz.

Outputs: `MAVLINK_LANDING_TARGET_CONNECTED`, `LANDMARK_EMULATOR_ACTIVE`
(numeric 0/1), `LANDMARK_EMULATOR_CONDITIONS_MET` (numeric 0/1),
`LANDMARK_EMULATOR_DISTANCE` (metres), and AppCasts.
TCP reconnects are independent of [attachment](../pMavlinkShipBridge/README.md),
once per simulated second. The mission runs this emulator only in SITL.
Both apps share a policy-free MAVLink codec/nonblocking TCP library and connect
to the same multi-client mavp2p simulation listener on TCP 5763. Their client
connections are independent; a shared router/upstream failure stops both streams.
The controller (5760) and ground-control (5762) links bypass this router.

Build from the repository root:
```bash
PEARLOPTIONS=all cmake -S . -B build
cmake --build build --target pMavlinkLandingTarget
python3 src/pMavlinkLandingTarget/tests/test_target.py
```

The native test uses isolated MOOS ports and a TCP packet capture; it checks
MAVLink CRC/frame/vector fields, target quaternion with changing UAV yaw and
UTM grid-north correction, range, string/numeric/multiple/invalid conditions,
and independent visual persistence with stale geometry or a disconnected link.
For actual three-pass flight validation, see
[`target`](../../missions/pearl_uav_integration_tests/target/README.md).
