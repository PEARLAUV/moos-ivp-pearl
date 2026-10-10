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
  vehicle_nav_prefix = UAV_NAV // Names native-pose output; not a direct-mode pose input.
  vehicle_pose_source = mavlink // Default; moos is explicit legacy compatibility.
  fc_component_id = 1
  native_pose_rate_hz = 50 // Requested FC ATTITUDE_QUATERNION/GLOBAL_POSITION_INT Hz.
  native_pose_timeout = 0.10 // Real receive age AND FC-clock sample-span limit (seconds).
  native_pose_max_skew = 0.02 // Max difference between native FC boot stamps, seconds.
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
relative to the native full UAV attitude and encoded as a normalized target-local
-> BODY_FRD quaternion, `inverse(q_body_to_NED) * q_level_target_to_NED`.
The target platform remains level; the aircraft need not be level. Heading is registered even when
visuals are disabled. Missing/stale heading or missing origin leaves orientation
unavailable (all-zero quaternion, this application's convention), without stopping
otherwise valid position observations. Angular sizes remain zero; the received
viewer uses its configured physical size. The point visual has no orientation.

### Direct native aircraft pose (default)

The existing sensor TCP connection receives CRC-checked MAVLink v1/v2 packets
directly from the configured FC system and `fc_component_id` (default 1).
On each connection it sends two **telemetry-only**
[`MAV_CMD_SET_MESSAGE_INTERVAL`](https://mavlink.io/en/messages/common.html#MAV_CMD_SET_MESSAGE_INTERVAL)
requests for [`ATTITUDE_QUATERNION`](https://mavlink.io/en/messages/common.html#ATTITUDE_QUATERNION)
and [`GLOBAL_POSITION_INT`](https://mavlink.io/en/messages/common.html#GLOBAL_POSITION_INT).
Requested rate is not proof of reception: actual complete native data is required.
Euler `ATTITUDE` is accepted only until the first valid quaternion; there is no
fallback to Euler after that, or to MOOS aircraft coordinates in direct mode.
No parameter, mode, mission, arming, estimator reset, or flight-control command
is sent. A shared MAVLink channel's stream rates may change; dedicate this
sensor endpoint where possible. The Briggs router is separate from the bridge's
controller link.

The quaternion is body-FRD to true NED. Position is native estimated lat/lon and
MSL altitude, not SITL ground truth. Actual FC boot timestamps must pair within
20 ms by default; **no interpolation/extrapolation or clock-offset guessing**.
A pair is immutable for its attitude timestamp. Both native receipts must be
younger than 100 ms by default; source-clock span is also capped at 100 FC ms,
independently of MOOS warp. Replays cannot renew receipts. Reconnect/FC rewind
clears previous generations. Excess skew, missing/invalid/stale data stop target
observations; fresh relayed MOOS aircraft inputs cannot unlock this mode.

The original paired samples are published as `<vehicle_nav_prefix>_MAVLINK_POSES`:
`attitude_boot_usec,roll_deg,pitch_deg,yaw_deg,lat_deg,lon_deg,MSL_m,original_attitude_receipt_MOOS,position_boot_usec;...`.
Only pairs actually used by this app are exported, with one-real-second/128-pair
retention. The window contains **received native FC aircraft telemetry**, never
simulated target position. Configure viewer/coordinator
`observation_pose_samples_var = UAV_NAV_MAVLINK_POSES` to match observation
timestamps even when their bridge attitude stream has different timestamps.
The viewer uses the paired aircraft position as well as attitude, removing
latest-position/exposure-vector mixing. Coordinator current yaw/readiness and
post-ACK measurement remain independently bridge-owned.

Direct reception reduces relay/sampling delay; it does not prove zero latency
or synchronize ArduPilot's internal `PLND_LAG`. No FC parameter is changed.
Receive-age/source-clock-span limits are not an absolute exposure-to-FC latency
measurement; a queued TCP stream can still add transit delay.
AppCasts show source, blockage, actual pose age, both stamps and skew/age limits.
TCP is trusted; CRC is validated but MAVLink signatures are not authenticated.

### Explicit legacy aircraft pose

Set `vehicle_pose_source=moos` only for a deliberately configured legacy source.
Vehicle inputs: `<vehicle_nav_prefix>_LAT/LONG/HEADING/ALTITUDE_MSL`, plus
`_ROLL/PITCH/ATTITUDE_AVAILABLE/ATTITUDE_AGE/ATTITUDE_TIME_USEC`.
Recommended `_ATTITUDE_SAMPLES` carries the bridge's atomic original native pose
window (boot_usec,roll,pitch,yaw,receipt_moos;...). The latest complete pose supplies
both rotation and timestamp; scalar-only legacy sources work until a window is
first seen. Invalid/empty windows stop observations, never restore scalar fallback.
Relay that string unchanged, including original receipt times. The bridge limits
the window to one real second/128 poses; normal `input_timeout` still applies.
Heading is true-north attitude yaw, never ground course or grid heading.
Roll/pitch are degrees, positive right-wing-down/nose-up. All Euler values come
from one native FC attitude snapshot; availability must be numeric 1, and native
age is checked in real seconds (including elapsed time after relay), against
`input_timeout`. Missing/invalid/stale attitude stops observations; **zero roll/
pitch is not substituted**. `pEchoVar` maps bridge `UAV_YAW` into this `_HEADING`,
and copies the other native fields. `_ATTITUDE_TIME_USEC` is the FC attitude's
boot-time microsecond timestamp, not MOOS time or a newly manufactured send time.

Fresh finite inputs with all configured `condition` entries true and within
the configured **3-D** detection range produce
target 0 observations in `MAV_FRAME_BODY_FRD`, with forward/right/down metres,
angles, distance, vision-fiducial type, and `position_valid=1`. The geographic
NED displacement is rotated by the **transpose of the full body-to-NED matrix**
`Rz(yaw) Ry(pitch) Rx(roll)`, not just inverse yaw. Target quaternion uses the
same attitude. Packet `time_usec` identifies that native attitude sample; the
viewer/coordinator can undo the exact exposure rotation while the drone turns.
ArduPilot ignores this timestamp for its backend receive-age handling.
Stale/invalid inputs or excessive range stop
observations; ArduPilot owns target-loss handling. The target has its **own MSL
altitude**, independent of the drone: `target_altitude_msl` supplies a constant,
or `target_altitude_var` selects a live MOOS input. Live height overrides the
constant and must be fresh and within -1000..10000 m MSL. **NED** down is
`vehicle_MSL - target_MSL`; body down is its full-attitude transformed component
and can differ when tilted. Range is rotation-invariant. The visualization slice
uses the geographic vertical separation, not the tilted body-down component.

### Angular-field signs

For a downward-facing camera, image +X is body right and image +Y is body
backward: `angle_x = atan2(right, down)` and
`angle_y = atan2(-forward, down)`, in radians. ArduPilot's angular-only backend
reconstructs `(-tan(angle_y), tan(angle_x), 1)` and normalizes it. For a target
2 m **ahead** and 8 m below, correct `angle_y` is **-0.244979 rad** (-14.04 deg).
The old positive sign would reconstruct a target **behind** the drone, reversing
that axis. This did not affect the investigated run: `position_valid=1` makes
ArduPilot use XYZ and ignore angular offsets. Angles now agree with XYZ for a
visible target below the camera; angle-only downward-camera LOS cannot encode
a target behind/up through the camera plane.

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
Direct-input tests also verify relayed aircraft coordinates cannot override native
pose, missing/wrong-system/excess-skew/stale telemetry cannot unlock broadcasts,
and telemetry requests are only command 511 for message IDs 31 and 33.
`python3 src/lib_mavlinkTransport/tests/test_direct_pose.py` checks mixed v1/v2
fragmentation, bad CRC, original receipt preservation, reboot generation,
strict window parsing, and the logged 8.13-degree/7.88-m pitch regression.

Native 50 Hz isolated yaw/deck flight was revalidated from
`/tmp/precision-yaw-sitl-9vskyyix/revalidated-result.json`: both coordinators
COMPLETE, 1.08 m MSL touchdown on a 1 m deck, 0.0292 m final horizontal error.
Its first test report failed after flight validation because the fixture had
not tracked peer coordinates; the captured flight was subsequently checked
offline with that fixture correction. Initial LAND separation was only 0.187 m,
so this is **not** an off-center Briggs overshoot reproduction.
A 2 m capture-tolerance exercise (`/tmp/precision-yaw-sitl-ow7ewx8s/`)
descended during Guided hold **before AUTOLAND** and correctly aborted
`landed_without_authorized_autoland`; no landing success is claimed for it.
A later nominal flight completed but failed the strict PARK-before-native-LAND
ordering assertion (`/tmp/precision-yaw-sitl-ovg_pfw5/`).
Those broader handoff/ordering cases remain unresolved and were not forced
through by weakening guards, freshness or validation.

For actual three-pass flight validation, see
[`target`](../../missions/pearl_uav_integration_tests/target/README.md).
