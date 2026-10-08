# pMavlinkShipBridge

SITL-only C++ MOOS producer of MAVLink 2 ship pose and simulated visual landing
targets. It does not arm, change modes, or override UAV navigation. No MAVSDK,
pymavlink, or Python MOOSDB client is required.

```moos
ProcessConfig = pMavlinkShipBridge
{
  AppTick = 20
  CommsTick = 20
  mavlink_host = 127.0.0.1
  mavlink_port = 5763
  ship_nav_prefix = NAV
  ship_system_id = 17
  source_component_id = 191
  target_system_id = 1
  target_number = 0
  publish_rate_hz = 10
  input_timeout = 1
  detection_range = 12
  ship_pose_enable_var = SHIP_POSE_ENABLED
  ship_altitude_msl = 1.5
  // Optional live deck-height input; omit to use the constant above:
  // ship_altitude_var = NAV_ALTITUDE_MSL
}
```

## Inputs

- Ship: `<ship_nav_prefix>_LAT`, `_LONG`, `_HEADING` (degrees), `_SPEED` (m/s).
  Prefix defaults to `NAV`. In the UAV community use `PEARL_NAV` so its own
  `NAV_*` cannot be mistaken for the ship. pShare supplies ship navigation;
  local `pEchoVar` supplies native UAV latitude/longitude/yaw/MSL height.
- Coupling intent: `SHIP_POSE_ENABLED` (or `ship_pose_enable_var`), boolean
  `true`/`false` or numeric 1/0. Unknown/invalid values fail closed. This is a
  latched MOOS fact, not a proximity test or an automatically timed-out heartbeat.
- Optional deck MSL height: the configured `ship_altitude_var`, in metres.
  When configured, this input must be finite, fresh, and in -1000..10000 m MSL;
  otherwise (when no live variable is configured) the constant
  `ship_altitude_msl` is used. The pose and visual target share this height.
- UAV: `UAV_NAV_LAT`, `UAV_NAV_LONG`, `UAV_NAV_HEADING` (attitude yaw, not course),
  `UAV_NAV_ALTITUDE_MSL` (metres).

Ship navigation produces `GLOBAL_POSITION_INT` as `ship_system_id` **only
while the coupling fact is true**. The mission/operator owns this fact: disable
it before contingency recovery or an off-deck landing, and enable it for a
primary deck approach/landing. Stopping the stream releases SITL coupling after
its one-simulated-second freshness window. Geographic
north/east velocity is derived from GPS changes with a 0.2-second smoothing
constant; heading/speed initialize velocity before a second GPS sample exists.
This avoids confusing MOOS map-grid north with geographic north.

Independently of that coupling fact, fresh navigation within the configured
**3-D** range produces
`LANDING_TARGET`, with valid body-forward/right/down position and the configured
target number. Its source is the UAV's `target_system_id`, component 191 by
default, so MAVSDK's UAV-system passthrough can observe it. A level camera/body
roll-pitch model is assumed. Geographic calculations use a local spherical-Earth
approximation suitable for this mission's short distances.

Unknown or stale inputs stop the corresponding transmissions. TCP disconnects
are retried once per second. The app drains incoming traffic but does not
consume MAVLink vehicle telemetry: UAV state arrives through local MOOS variables or pShare.

## Outputs

- `MAVLINK_SHIP_BRIDGE_CONNECTED`: numeric 0/1.
- `MAVLINK_SHIP_POSE_ACTIVE`: numeric 0/1; fresh, enabled pose stream.
- `LANDMARK_EMULATOR_ACTIVE`: numeric 0/1.
- `LANDMARK_EMULATOR_DISTANCE`: metres, updated with fresh geometry.
- AppCast includes input freshness and message counts.

## SITL requirements

Use the ArduPilot `external-ship.patch` build with `SIM_SHIP_ENABLE=1`,
`SIM_SHIP_EXT=1` and matching `SIM_SHIP_SYSID`. External mode deliberately has
**no distance check** (`SIM_SHIP_DSIZE` applies only to the internal ship model).
A grounded aircraft is forced to the supplied ship center, inherits velocity/yaw,
and rests at deck MSL height plus its frame clearance. Fresh poses also establish
the landing surface and ground-facing sensor height for an airborne approach,
without overriding airborne XY.
Height changes carry an already grounded aircraft up or down with the deck.
Disable the MOOS coupling fact before off-deck/contingency landing: otherwise
SITL will intentionally move that landed aircraft to the ship.

Run MOOS time warp and SITL speedup at the same positive factor (including >1). The deck remains level; roll/pitch are
not simulated. This app must not be enabled in REAL mode.

## Build and test

```bash
PEARLOPTIONS=all cmake -S . -B build
cmake --build build --target pMavlinkShipBridge
python3 src/pMavlinkShipBridge/tests/test_bridge.py
```

The test starts the real MOOS app against a TCP capture endpoint and verifies
MAVLink IDs/CRC/units, body geometry, condition gating independent of range,
constant/live deck altitude, stale-height cutoff, and TCP
reconnection. Python is used only for tests, not mission integration.
