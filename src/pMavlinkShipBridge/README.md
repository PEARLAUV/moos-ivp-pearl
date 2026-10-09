# pMavlinkShipBridge

SITL-only MOOS attachment/deck-pose producer. Sends MAVLink 2 `HEARTBEAT`
(surface boat, 1 Hz) and `GLOBAL_POSITION_INT` (10 Hz) from system 17/component
191 on its own TCP connection. It never consumes UAV navigation, broadcasts
landing targets, or sends flight-control commands.

```moos
ProcessConfig = pMavlinkShipBridge
{
  AppTick = 20
  CommsTick = 20
  mavlink_host = 127.0.0.1
  mavlink_port = 5763
  ship_nav_prefix = PEARL_NAV
  ship_system_id = 17
  source_component_id = 191
  publish_rate_hz = 10
  input_timeout = 1
  ship_pose_enable_var = SIM_UAV_ON_SHIP
  ship_altitude_msl = 0
  // Optional fresh live MSL height, replacing the constant:
  // ship_altitude_var = DECK_ALTITUDE_MSL
}
```

Inputs are `<ship_nav_prefix>_LAT/LONG/HEADING/SPEED` (degrees, degrees,
degrees, m/s). The prefix defaults to `NAV`. The enable variable defaults to
`SHIP_POSE_ENABLED` and accepts true/false or numeric 1/0; invalid values
disable transmission. Intent is latched, not proximity-filtered or timed out.
Fresh finite navigation is required. Optional live height must also be fresh
and within -1000..10000 m MSL.

Enabled fresh poses establish the level deck surface and move a grounded UAV
to the ship center without a distance filter. Airborne XY remains ArduPilot
physics. Disable poses before off-deck recovery; the patched simulator releases
coupling after one simulated second without a pose. Geographic velocity is
derived from GPS motion, with heading/speed used to initialize it. Never
overwrite UAV `NAV_*` to emulate attachment.

Requires the patched simulator with `SIM_SHIP_ENABLE=1`, `SIM_SHIP_EXT=1`
and matching `SIM_SHIP_SYSID`. Connect to the multi-client simulation router on
TCP 5763, shared with the target app. Its sole upstream uses SERIAL2; the direct
controller/ground-control links and GPS SERIAL3/4 are unchanged. Run MOOS warp
and SITL speedup at the same factor.

Outputs: `MAVLINK_SHIP_BRIDGE_CONNECTED`, `MAVLINK_SHIP_POSE_ACTIVE`
(numeric 0/1), plus AppCasts. Connection retries occur once per simulated second.

Visual observations belong exclusively to
[pMavlinkLandingTarget](../pMavlinkLandingTarget/README.md). Match its target
MSL height to this app's deck height (or configure the same live height variable).
A target-client failure does not interrupt this app's attachment stream.
A shared router/upstream failure affects both streams; stale poses release
coupling after the simulator's one-second deadline.

Build from the repository root:
```bash
PEARLOPTIONS=all cmake -S . -B build
cmake --build build --target pMavlinkShipBridge pMavlinkLandingTarget
```
