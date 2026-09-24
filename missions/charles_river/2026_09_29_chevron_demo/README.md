# aug_pearl_uav

`aug_pearl_uav` is a two-vehicle Charles River mission for a generic UAV and
the PEARL surface vehicle. It combines the qualified UAV operator workflow from
`uav_solo/briggs_test` with PEARL's hardware and Helm stack from
`may_test_run`.

The UAV may still fly operator-selected routes. The additional `RENDEZVOUS`
workflow is deliberately small: after the UAV is airborne, PEARL chooses one
meeting point, the UAV validates it, both vehicles travel there, PEARL grants
landing clearance, and the UAV acquires PEARL's landing target before it
commits precision landing.

## Implemented rendezvous state machine

PEARL is treated as having negligible turn radius. The coordinator therefore
chooses a point on the straight line between the current vehicle positions.
Its fractional distance from PEARL is:

```text
pearl_speed / (pearl_speed + uav_speed)
```

This equalizes nominal arrival times using the configured speeds. It is a
demonstration policy, not a current-aware or obstacle-aware planner.

| State | Action and transition |
| --- | --- |
| `IDLE` | Wait for an airborne operator request. |
| `REQUESTING` | UAV sends position, speed, health, and available battery data. PEARL proposes one session-tagged point. UAV accepts only after its complete one-point route is staged locally. |
| `RENDEZVOUS` | PEARL activates its rendezvous waypoint and the UAV follows its existing Helm route. Both must be inside the 5 m capture zone, no more than 3 m apart, and continuously satisfy the clearance gate for 2 s. |
| `ACQUIRING_TARGET` | PEARL is in StationKeep. The UAV follows PEARL's fresh reported position horizontally at the existing mission altitude and waits for the landing-target gates below. It aborts after 30 s without a valid lock. |
| `LANDING` | The target lock has remained valid for 2 s and the UAV has issued the internal precision-landing commit. ArduPilot now owns descent and target-loss handling. |
| `COMPLETE` | UAV reports landed; both coordinators publish completion. |
| `ABORT` | Cancel the UAV route, put the UAV in its configured hold, and put PEARL in StationKeep. |

The UAV must have fresh navigation and, outside basic SIM, must be armed,
airborne, and healthy before it may start. PEARL rejects stale navigation,
unhealthy requests, malformed proposals, invalid or stale battery data, and
timeouts. A low valid UAV battery raises recovery priority rather than
rejecting the aircraft; takeoff has the separate reserve gate described below.

## Reliable versus periodic transport

`pMediator` carries the actions that must survive an occasional dropped UDP
datagram:

- operator ARM, DISARM, TAKEOFF, route DEPLOY, route CLEAR,
  RENDEZVOUS, and RNDV ABORT;
- UAV request, PEARL proposal, UAV acceptance/completion, and PEARL landing
  clearance;
- acknowledgements and retries at one-second mission-time intervals, up to ten
  retries.

Mouse clicks remain shoreside-local until DEPLOY sends one complete route
snapshot. Periodic state such as node reports, health, rendezvous status, and
navigation uses ordinary broker/pShare routing. RealmCast bridging is disabled.

UAV and PEARL pMediator envelopes and node reports also use direct unicast
pShare routes on their shared vehicle network. Shoreside uses the UAV's
Tailnet address directly. There is no PEARL UDP relay or automatic alternate
runtime path.

PEARL and the UAV intentionally use different platform groups, so
`uFldNodeComms` group filtering is disabled for this mission; otherwise PEARL
would not receive the UAV reports required to grant clearance.

## Operator interface

UAV controls:

- `UAV ARM`, `UAV DISARM`, `UAV TAKEOFF`
- `UAV DEPLOY`, `UAV CLEAR`

Each `UAV TAKEOFF` request is consumed on the UAV and approved only when all
three fresh, valid inputs pass:

- UAV battery SOC is at least 30 percent;
- PEARL battery SOC is at least 15 percent; and
- apparent wind over PEARL is no more than 4 m/s.

The gate publishes `UAV_TAKEOFF_GATE_READY`, `UAV_TAKEOFF_GATE_REASON`, and a
per-request `UAV_TAKEOFF_RESULT`. A rejected request cannot remain pending and
cause a later takeoff. SIM supplies 80-percent batteries and 2 m/s wind so the
normal operator sequence remains immediately usable; focused tests may poke
the same variables to exercise each rejection.

Choose the `route` mouse context and click an ordered route. `UAV DEPLOY` sends
the stored snapshot. During coordinated landing-target acquisition,
`pRendezvous` automatically maintains a one-point route to PEARL's fresh
position. This is not a separate operator button.

PEARL controls:

- `PEARL DEPLOY`, `PEARL RETURN`, `PEARL STATION`, `PEARL ALLSTOP`

Coordinated controls:

- `RENDEZVOUS`: request a meeting after the UAV is armed and airborne.
- `RNDV ABORT`: independently sends acknowledged aborts to both vehicles.

The normal sequence is:

1. ARM and TAKEOFF the UAV.
2. Press `RENDEZVOUS`.
3. Monitor `UAV_RENDEZVOUS_STATE` and `PEARL_RENDEZVOUS_STATE`.
4. Let the coordinated workflow request precision landing, or press
   `RNDV ABORT` to hold both vehicles.

## OAK camera viewer

The UAV Pi's boot-managed OAK service is independent of the MOOS launchers.
From an authorized Tailnet client, open `http://uav-pi-1:8082/` for the tiny
four-view JPEG monitor. No helper or special browser context is required. The
independently decodable MJPEG profile favors reliable late and reconnect
viewing over motion smoothness. The health, snapshot, and Prometheus API is
directly available at `http://uav-pi-1:9102/`. Rigging's
`./.bin/oak-camera-viewer` is an optional localhost relay for TCP `8082` and
`9102`; it is not part of the normal viewing path. See
[`OAK_VIDEO.md`](OAK_VIDEO.md) for the operator workflow, stream profile,
scene-dependent bitrate metrics, and observability boundary.

The local helper uses direct Tailnet TCP and requires no SSH tunnel, PEARL
proxy, Tailscale Serve, Funnel, or fixed deployed relay. The viewer does not
currently publish the MOOS or MAVLink landing-target inputs used by the landing
gate. Grafana is limited to camera health and performance metrics; live video
remains in the browser monitor, and the dashboard viewer link uses the direct
Tailnet URL.

## Landing assurance

Landing is intentionally fail-closed. PEARL grants clearance only while both
vehicles have fresh navigation, both are inside the rendezvous capture zone,
and their current horizontal separation is no more than 3 m for 2 continuous
seconds. Arrival is recomputed on every iteration; moving apart resets the
dwell instead of leaving an old decision latched.

In SITL and REAL modes the same pre-commit gate also requires PEARL process
health, fresh valid PEARL battery and wind telemetry, at least 15% PEARL
battery, and wind no greater than 4 m/s. SITL supplies those inputs from the
PEARL simulator; REAL uses live PEARL telemetry. These conditions are evaluated
again during target acquisition; passing the takeoff gate is not treated as
proof that the conditions are still safe at landing time. Deck motion and
automatic landing-area clearance are not currently measured and are therefore
not claimed as software predicates.

The UAV battery inputs are `UAV_BATTERY_SOC` and
`UAV_BATTERY_DATA_VALID`. They must be valid and fresh, but a low value does
not reject recovery. `pRendezvous` instead publishes
`UAV_RECOVERY_PRIORITY=NORMAL` at or above 25%, `URGENT` from 15% to 25%, and
`EMERGENCY` below 15%, so low energy increases operational priority without
waiving the landing-safety gates.

Clearance carries PEARL's current local `x,y` position derived from its GPS.
The UAV also receives PEARL `NODE_REPORT` updates directly over the Alfa path.
While acquiring the target, it updates a one-point horizontal route when
PEARL moves at least 1 m, no faster than once every 2 s. The normal REAL UAV
behavior holds the configured mission altitude during this approach; this
route does not command descent.

The internal `UAV_PREC_LAND_COMMIT` is published only after all of these have
remained true for 2 continuous seconds:

- the UAV-to-PEARL reported separation is no more than 3 m;
- `UAV_LANDING_TARGET_AVAILABLE=1` and target age is no more than 0.5 s;
- MAVLink landing `target_num` is the configured PEARL target, currently `0`;
- a position-valid target is within 1.5 m horizontally, or an angle-only
  target is within 0.20 rad of the camera center.

Loss of any input before commit resets the target-lock dwell. Failure to
acquire in 30 s clears the route, commands the normal hold, aborts the session,
and tells PEARL to remain in StationKeep. The old external
`UAV_PREC_LAND_REQUEST` path is rejected, so an operator message cannot bypass
the coordinated gates.

The operational authority handoff is explicit. The current MOOS state names
are shown in parentheses where they differ from the authority-model label:

1. `APPROACH` / `ACQUIRING_TARGET`: `pRendezvous` may wait, update the
   horizontal route, or abort; no descent has been commanded.
2. `AUTHORIZED_TO_COMMIT`: every platform, separation, and visual-target gate
   has remained true for the lock dwell.
3. `COMMITTED_TO_ARDUPILOT` (`LANDING`): `UAV_PREC_LAND_COMMIT=true` has
   selected LAND and ArduPilot owns the descent and target-loss response.
4. `COMPLETE` / `FAILED`: landed-state and ArduPilot/safety-pilot outcomes
   close the attempt. `pRendezvous` autonomously publishes `COMPLETE` from the
   landed state; a failed post-commit attempt remains an ArduPilot/safety-pilot
   outcome that must be recorded in the experiment log.

Operator abort is intentionally ignored after `COMMITTED_TO_ARDUPILOT`;
changing flight modes during terminal descent is not treated as a safe
supervisor action. From that boundary onward, ArduPilot failsafes and the
safety pilot own recovery.

Before every REAL flight, verify the Pixhawk precision-landing parameters,
especially `PLND_ENABLED=1`, the correct
backend, moving-target option, and `PLND_STRICT=2` so target loss retries and
then hovers instead of silently continuing a blind descent. The successful
hardware flight recorded in [`flight_logs/README.md`](flight_logs/README.md)
used `PLND_TYPE=1`, `PLND_OPTIONS=1`, `PLND_STRICT=2`, and a 4 s timeout. Also
verify that the vision source labels PEARL as `target_num=0`. See the
[ArduPilot precision-landing documentation](https://ardupilot.org/copter/docs/precision-landing-and-loiter.html)
and [MAVLink `LANDING_TARGET`](https://mavlink.io/en/messages/common.html#LANDING_TARGET).

## SIM landing attachment

SIM starts with the UAV visually attached to PEARL. Once fresh PEARL pose data
arrives, `uSimAttachment` disables the UAV's `uSimMarineV22` navigation output
and publishes the PEARL position, heading, and speed as the UAV navigation
solution. The two icons therefore move together without running two competing
UAV navigation publishers.

An approved SIM takeoff detaches the UAV automatically. The app captures one
final platform position and heading, resets `uSimMarineV22` to that exact pose,
and then returns navigation ownership to the simulator. The UAV therefore
resumes independent flight from the takeoff point without jumping. SIM
precision landing reattaches it, and the cycle may repeat.

The app interface is vehicle-name agnostic: it consumes generic attachment
pose and request variables. This mission maps PEARL navigation into that
interface and aliases the generic status outputs to `UAV_SIM_ATTACHED` and
`UAV_SIM_ATTACHMENT_STATE` for operator display.

If the attachment pose becomes stale, the UAV freezes at the last received
pose and reports `ATTACHED_POSE_STALE`. While detached, the app reasserts local
simulator ownership so restarting the attachment helper cannot leave the UAV
simulator disabled.

This visual attachment is SIM-only. SITL and REAL continue to use flight-
controller navigation, do not launch `uSimAttachment`, and do not forward the
PEARL attachment-pose stream.

## Layout and launch modes

- Map: `MIT_SP.tif`
- Datum: `42.358436, -71.087448`
- Shoreside: MOOSDB `9000`, pShare `9200`
- UAV: MOOSDB `9001`, pShare `9201`
- PEARL: MOOSDB `9002`, pShare `9202`

The top-level launcher is for same-host testing:

```bash
./launch.sh --mode=SIM
```

Modes are:

- `SIM`: UAV SIM and PEARL SIM
- `SITL`: UAV ArduCopter SITL and PEARL SIM
- `REAL`: UAV REAL and PEARL REAL

Generate targets without launching:

```bash
./launch.sh --mode=SIM --just_make --nogui 5
```

For SITL, start ArduCopter first and then start the mission. On macOS or in a
headless environment, `--direct` avoids `sim_vehicle.py` opening another
terminal and uses the already-built ArduCopter binary:

```bash
./launch_sitl.sh --direct --no_rebuild
./launch.sh --mode=SITL
```

`launch_sitl.sh` starts `sitl_landing_target.py`, which publishes a centered
MAVLink 2 `LANDING_TARGET` on ArduCopter's SERIAL2 connection as the
reproducible stand-in for the real vision source. Use `--no_landing_target` to
verify that acquisition fails closed. The ArduPilot precision-landing target
in `sitl.parm` matches the default SITL UAV home and default PEARL start. If
those positions change, update `SIM_PLD_LAT` and `SIM_PLD_LON` before treating
precision-landing results as meaningful.

SITL uses PEARL's simulated battery, wind, and process-health telemetry to
exercise the same pre-commit platform gate enabled in REAL. Basic SIM keeps
that gate disabled so its lightweight attachment workflow remains immediately
usable.

Field operation uses the sublaunchers independently:

The deployed Alfa topology is routed through Sherlock rather than attaching
both radios directly to the two mission computers. See
[`RADIO_BACKHAUL.md`](RADIO_BACKHAUL.md) for provisioning, routes, activation,
and verification, and [`OAK_VIDEO.md`](OAK_VIDEO.md) for the independent
Tailnet camera path.

```bash
# Shoreside
./launch_shoreside.sh --auto --mode=REAL \
  --ip=100.127.231.65 \
  --uav_ip=100.70.189.91 --uav_pshare=9201

# UAV vehicle computer
./launch_uav.sh --auto --mode=REAL \
  --ip=100.70.189.91 \
  --shore=100.127.231.65 --shore_pshare=9200 \
  --pearl_ip=192.168.88.253 --pearl_pshare=9202

# PEARL vehicle computer
./launch_pearl.sh --auto --mode=REAL \
  --ip=100.69.111.61 \
  --shore=100.127.231.65 --shore_pshare=9200 \
  --uav_ip=172.22.90.2 --uav_pshare=9201
```

PEARL REAL mode also runs `iSherlockTelemetry`. By default it reads Sherlock's
Telegraf endpoint over the direct PEARL Ethernet network at
`192.168.88.252:9273`. Override that independently when needed:

```bash
./launch_pearl.sh --auto --mode=REAL \
  --sherlock_metrics_host=100.64.194.59 --sherlock_metrics_port=9273
```

The interface publishes PEARL battery state, apparent wind, and Alfa backhaul
health. Wind speed means apparent wind over the moving PEARL deck, which is the
airflow relevant to UAV landing. The mission does not currently publish a
second apparent-wind alias or true wind because neither adds a new input to the
present landing decision.

Consumers must also check `PEARL_BATTERY_DATA_VALID` or
`PEARL_WIND_DATA_VALID`. `PEARL_BATTERY_DATA_AGE` is the CMP sample age;
`PEARL_AIRMAR_DATA_AGE` is the age of the latest valid sentence from the Airmar
feed. The current Airmar exporter does not provide a wind-sentence-specific
age, so wind validity combines the latest MWV validity status with overall
Airmar feed freshness.

Sherlock polls the configured UAV station every two seconds. `ALFA_LINK_UP`
reports association independently from `ALFA_DATA_VALID`, which reports that
the collector itself is healthy and fresh. When linked, the interface also
publishes `ALFA_SIGNAL_DBM`, `ALFA_SIGNAL_AVG_DBM`,
`ALFA_TX_BITRATE_MBPS`, `ALFA_RX_BITRATE_MBPS`,
`ALFA_TX_RETRIES_TOTAL`, `ALFA_TX_FAILED_TOTAL`, and `ALFA_INACTIVE_MS`.
TX is Sherlock-to-UAV and RX is UAV-to-Sherlock; signal is the UAV signal as
received by Sherlock. Retry and failure values are cumulative since the
current association. `ALFA_DATA_AGE` is the age of the latest collector
observation as measured on Sherlock, and `ALFA_STATION_COUNT` helps detect an
unexpected client. Since MOOSDB retains the last numeric values after a link
drops, consumers must require `ALFA_SIGNAL_DATA_VALID=1` before interpreting
signal or bitrate. Sherlock's collector and Telegraf endpoint both update on a
two-second interval.

Selected battery, wind, and Alfa values are bridged to shoreside as ordinary
latest-state telemetry, not mediated commands. PEARL's wildcard `pLogger`
records every published Alfa variable in the mission `.alog`.

Battery, wind, and Alfa telemetry remain in one interface app because Sherlock
exposes them through the same HTTP endpoint. Splitting the app would download
the same roughly 180 KiB metrics document repeatedly and would not isolate
endpoint failure. The app instead validates and retains all three source
snapshots independently: one missing source does not suppress the others.

In SITL and REAL, `pArduBridge` publishes the flight controller's primary
battery estimate as `UAV_BATTERY_SOC` and `UAV_BATTERY_VOLTAGE`, together with
`UAV_BATTERY_DATA_VALID` and `UAV_BATTERY_DATA_AGE`. The SOC estimate is used
only when it is finite and no more than three seconds old.

All applications connect to their same-host MOOSDB through
`ServerHost=localhost`. Each sublauncher's `--ip` is its advertised pShare
address; `--shore` is independently the shoreside address reachable from that
vehicle. `--pearl_ip` and `--uav_ip` are the other vehicle's directly reachable
addresses through the routed backhaul. Supplying them enables the direct
pShare outputs;
omitting them leaves mediated traffic on the ordinary vehicle-to-shoreside
broker path. The direct addresses and ports are launch-time inputs, not
hardcoded Wi-Fi configuration.

The preferred traffic paths are:

| Traffic | Addressing | Physical path |
| --- | --- | --- |
| UAV to/from PEARL mission traffic | `172.22.90.2` and `192.168.88.253` | Alfa to Sherlock to PEARL Ethernet; no Tailscale or internet required |
| Shoreside mission traffic | Tailnet `100.x` addresses | Tailscale over the available underlay |
| SSH and administration | Tailnet `100.x` addresses | Tailscale over the available underlay |
| OAK four-view monitor | Direct `uav-pi-1:8082` or optional helper-local TCP `8082` | Tailscale to the UAV; no PEARL relay |
| OAK snapshots and API | Direct `uav-pi-1:9102` or optional helper-local TCP `9102` | Tailscale to the UAV |
| OAK metrics collection | OAK API to Telegraf to local Prometheus | UAV-local by default; external `remote_write` is disabled |

A Tailscale address is a logical endpoint, not a guarantee that packets visit
the public internet. Tailscale normally uses a direct UDP path when possible;
that path may remain on a mutually reachable local network or use Sherlock's
upstream internet connection. If direct traversal fails, Tailscale may use a
DERP relay. At deployments where Sherlock's upstream is Starlink, remote
shoreside and management traffic may therefore cross Starlink. Explicit
PEARL/UAV peer traffic does not.

The retired PEARL UDP relays and their port `9300` path are removed by
Rigging's `wifi_backhaul` role. Allow the ordinary mission UDP ports
`9200`-`9202` and disable wireless client isolation. The OAK viewer uses
Tailnet TCP `8082`, and its API uses TCP `9102`; neither is a mission UDP port.

## Validation performed

- Generated SIM, SITL, and REAL targets and standard static/port checks.
- Normal SIM rendezvous from the default starts through landing and completion.
- Extended SIM rendezvous from 283 m initial separation, proving UAV report
  freshness beyond the timeout interval.
- Mid-transit operator abort, including UAV route clear/hold and PEARL
  StationKeep.
- Start while disarmed rejected without movement.
- SIM with 50% `uFldNodeComms` message loss; the workflow completed after
  observed pMediator retransmissions.
- Historical bidirectional pMediator tests covered both direct delivery and
  the now-retired relay topology before the setup was simplified.
- On 2026-08-24, an isolated three-community ArduCopter SITL run used the
  direct launcher and its built-in landing-target publisher with the platform
  gate enabled. Fresh PEARL process health, 80% battery, 2 m/s wind, and a
  valid landing target authorized commit after acquisition; the mission then
  progressed through Land, touchdown, `COMPLETE`, `ON_GROUND`, and disarm.
  The closed UAV `.alog` records the gate becoming ready and
  `UAV_PREC_LAND_RESULT=status=committed#reason=landing_gate_ready` before
  touchdown. REAL targets were also generated and checked with custom vehicle,
  peer, and shoreside addresses.
- Historical three-computer dock SIM across shoreside, PEARL, and the UAV Pi
  used the original PEARL relay and Alfa link. A shore route command reached the
  UAV, both simulated vehicles moved, rendezvous session
  `uav_1786659149728036` completed at `(3.32,-5.63)`, and the simulated UAV
  finished disarmed, on ground, at zero altitude. Isolated ports were used and
  no hardware interface application was launched.
- Three-computer headless SIM using the preferred Tailscale address advertisements
  and direct Alfa peer addresses. Shoreside saw both nodes, process watch was
  healthy, and a shoreside route was accepted by and moved the simulated UAV.
  No arm request or hardware interface application was launched.
- On 2026-08-19, the deployed Sherlock collector reported a healthy AP with
  no UAV associated. An isolated PEARL MOOS test on port 19402 published
  `ALFA_LINK_UP=0`, `ALFA_DATA_VALID=1`, `ALFA_STATION_COUNT=0`, and fresh
  `ALFA_DATA_AGE`; wildcard pLogger logging recorded all four in an `.alog`.
  No helm, actuator, or vehicle hardware process was launched.
- The automated Alfa telemetry integration test covers connected,
  disconnected, collector-failed, and stale samples using a temporary MOOSDB:
  `python3 src/iSherlockTelemetry/tests/test_alfa_telemetry.py`.
- The automated rendezvous integration test proves continuous PEARL/UAV
  separation, non-latching arrival, rejection of the old manual landing path,
  wrong-target rejection, target-loss dwell reset, successful sustained lock,
  and fail-closed acquisition timeout:
  `python3 src/pRendezvous/tests/test_landing_gate.py`.
- On 2026-08-20, all three communities ran concurrently in REAL mode over the
  close-range physical Alfa link. Bidirectional packet tests had zero loss;
  the PEARL log recorded -52 to -40 dBm, zero TX retries/failures, and actual
  battery/wind delivery to the UAV. Telemetry loss rejected takeoff as stale,
  a shoreside takeoff message was acknowledged then rejected on the actual
  low PEARL battery, and a shoreside rendezvous request safely aborted on
  missing Pixhawk navigation. After PEARL charged to its real 15% threshold,
  the gate reached READY with only the disconnected UAV battery synthesized.
  No flight-controller command was posted. Subsequent independent Sherlock
  and UAV Pi reboots restored Alfa association, routes, Tailscale, upstream
  internet, and fresh signal telemetry without manual intervention; Sherlock
  requires roughly two minutes for its network-online-dependent NAT and
  Telegraf services to start. See
  [`FIELD_TEST_2026-08-20.md`](FIELD_TEST_2026-08-20.md).

## PEARL stack

REAL mode retains the `may_test_run` setup:

- `iDualGPS`
- `iPEARL`
- `pPearlPID`
- `iBlueRoboticsPing`
- `iSherlockTelemetry`
- `pEchoVar` sensor-to-NAV translations

SIM mode uses `pHelmIvP`, `pMarinePIDV22`, `pThrustMixer`, and
`uSimMarineV22`. The simulator runs in differential-thrust mode. The mixer
converts the PID's conventional thrust/rudder output into left/right motor
commands using the same 100% thrust and 50% rudder limits as `iPEARL`. Opposed
motor commands therefore rotate PEARL in place instead of forcing it through a
rudder-model turn.

## Cleanup

```bash
./clean.sh
```
