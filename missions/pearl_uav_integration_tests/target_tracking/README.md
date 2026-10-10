# Real-UAV target-tracking bench mission

Single-community, shoreside mission for observing a real UAV's MAVLink
`LANDING_TARGET` stream. It is intended for manually moving and tilting a
disarmed aircraft around a real target while inspecting the received target
projection and native attitude telemetry.

## Safety and control boundary

The MOOS control path is receive-only:

- `pArduBridge` runs with `passive=true`.
- There is no Helm, behavior file, synthetic target broadcaster, SITL, pShare,
  node broker, arming script, or flight-command producer.
- The passive bridge does not register `ARM_UAV` or `ARDU_COMMAND`, perform
  mission setup, change modes, arm, or send setpoints.

The GUI also launches QGroundControl. QGroundControl is connected directly to
the vehicle and is capable of arming or sending commands if an operator uses its
controls; passive pArduBridge cannot prevent that. Remove propellers for initial
bench work, keep the vehicle physically restrained when appropriate, do not use
QGC flight controls, and verify `UAV_IS_ARMED=false`.

## MAVLink connection

By default, both pArduBridge and QGroundControl connect directly to the drone's
mavp2p UDP server:

```text
udp://100.112.119.114:14550
```

MAVSDK and QGroundControl send heartbeats to establish their independent UDP
routes; no shoreside relay is needed. Override `--udp_bind` and `--udp_port` for
a different endpoint. Setting `--udp_bind=0.0.0.0` retains listener mode for
synthetic/local packet sources. The real stream must contain a vehicle heartbeat,
attitude, and position-valid `LANDING_TARGET` messages in `BODY_FRD` frame 12.

Run:

```bash
./launch.sh
./launch.sh --udp_bind=100.112.119.114 --udp_port=14550
./launch.sh -ng -x
./launch.sh -j
./launch.sh --help
```

The mission uses one local MOOSDB on port 9000. Override it with `--mport`.
Use `--lat_origin` and `--lon_origin` to select the viewer's UTM origin.
With the GUI enabled, pMarineViewer is available on noVNC port 6080 and
QGroundControl on noVNC port 6081. pMarineViewer uses a blank local frame with
10 m hash lines, keeps the viewport centered on the fixed vehicle anchor, and
shows a yellow quadcopter at that anchor (default `(0,0)`).

The mission exposes the MOOSDB through the read-only `pWebSocketServer` endpoint:

```text
ws://localhost:9090/listen
```

This is the default `demo-frontend` MOOS URL. `AllowSubmissions=false` prevents
WebSocket clients from writing MOOS variables while allowing the frontend to
subscribe to the `UAV_LANDING_TARGET_*` telemetry used by the video overlay.

## Displayed telemetry

The viewer scopes:

- Passive connection state
- Armed state
- Roll, pitch, yaw, attitude availability, and attitude age
- Rangefinder availability, age, distance, limits, and sensor orientation when
  the vehicle emits MAVLink distance-sensor telemetry
- Landing-target availability, age, frame, validity, BODY_FRD position,
  distance, and angular size
- `LANDING_TARGET_VIEW_ACTIVE`

`pLandingTargetViewer` displays only target packets received through the real
MAVLink connection. This mission does not run `pMavlinkLandingTarget` and cannot
create synthetic observations.

## GPS-free relative projection

The target viewer runs with `projection_mode=relative`. It fixes the vehicle at
local `(0,0)`, rotates each received BODY_FRD target vector using native roll,
pitch and yaw, and plots the resulting north/east offset. It does not require
vehicle latitude/longitude or a valid GPS fix.

This view is deliberately drone-relative. It is useful for checking target-vector
and attitude transformations while manually moving the aircraft, but it cannot
show a world-stable target during vehicle translation without an external
position source. The bridge publishes attitude independently of global position, so GPS is not
required for this projection mode.

## Validation

The configuration safety test does not connect to a drone. The passive UDP test
uses synthetic heartbeat, attitude, distance-sensor and LANDING_TARGET packets;
it never sends a command:

```bash
python3 tests/test_launch.py
python3 tests/test_passive_udp.py
```
