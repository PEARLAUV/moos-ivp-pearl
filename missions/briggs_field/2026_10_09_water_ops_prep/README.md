# aug_pearl_uav

`aug_pearl_uav` is a two-vehicle Briggs/Charles prep mission for a UAV and
the PEARL surface vehicle. It combines the qualified UAV operator workflow from
`uav_solo/briggs_test` with PEARL's hardware and Helm stack from
`may_test_run`.

The UAV may still fly operator-selected routes. The additional `RENDEZVOUS`
workflow is deliberately small: after the UAV is airborne, PEARL chooses one
meeting point, the UAV validates it, both vehicles travel there, PEARL grants
landing clearance, and the UAV acquires PEARL's landing target before it
commits precision landing.

## Per-host Briggs / Charles launch targets

Run **one target on its corresponding host**, from this mission directory:

| Site | Shoreside host | PEARL host | Drone host |
|---|---|---|---|
| Briggs | `make shoreside-briggs` — REAL, `100.88.214.50` | `make pearl-briggs` — GRASS, `100.71.137.47` | `make drone-briggs` — REAL, `100.112.119.114` |
| Charles | `make shoreside-charles` — REAL, `100.88.214.50` | `make pearl-charles` — REAL, `100.113.99.58` | `make drone-charles` — REAL, `100.112.119.114` |

Both drone targets pass **`--model=x500`**. Every target supplies its own
advertised IP and the appropriate shore/peer addresses. These are local
launch commands, **not SSH orchestration**. Bare `make` prints help, not a live
launch. No aggregate target starts physical nodes together.

- **Briggs:** preserve datum `42.3569186052,-71.0990291116`, current
  `briggs_test.tif` and framing (pan `50,200`, zoom `2`), and PEARL
  configured start `51.033305,-56.215822,0`.
- **Charles:** use `2026_09_29_chevron_demo`'s datum
  `42.358436,-71.087448`, `MIT_SP.tif`, framing (pan `0,-400`, zoom `1`),
  and configured start `-4,-22,0`. This selects geography, not the old
  chevron mission/FSM/task; the prep mission's lifecycle remains. No default
  loiter is configured at either site; idle station-keeping and recovery holds
  remain distinct safety/coordination behaviors.

The combined and split launchers support `--site=briggs|charles` (default Briggs). All nodes
must use the same site/datum. **REAL/GRASS positions and aircraft Home remain
native sensor/FC-derived**; configured starts do not teleport or initialize
native navigation. Place hardware appropriately and verify the measured
position before tasking. GRASS has no Helm/propulsion return executor.

User-selected initial **map-editable** points, in each site's local metres:

| Site | PEARL return | UAV rally |
|---|---|---|
| Briggs | `43.787856,-39.259280` = **42.356575,-71.098486** | `15.042299,-51.993939` = **42.356454,-71.098831** |
| Charles | `9.138278,2.997768` = **42.358465,-71.087338** | `7.783478,11.586484` = **42.358542,-71.087357** |

These are provisional points, **not surveyed safe landing sites, FC Home,
clearance, readiness or flight authorization**. Check terrain/obstacles/deck
geometry before committing a task. The existing task freeze and native rally
ACK/readback still own readiness; launch does not automatically upload a rally
or issue a flight command. Shoreside return/rally editing remains available.

Preview or generate only, without starting applications or writing parameters:

```bash
make -n drone-briggs
make shoreside-charles LAUNCH_ARGS=--just_make
```

Make variables `BRIGGS_START/RETURN/RALLY`, `CHARLES_START/RETURN/RALLY`,
`MODEL` and the IP variables are overridable. Start and marker
overrides are independent: override return/rally too if relocating a start.
Use `LAUNCH_ARGS` for common flags and `SHORESIDE_ARGS`, `PEARL_ARGS`,
`DRONE_ARGS` for node-specific options. Parameter upload remains **explicit**
(`DRONE_ARGS=--upload-params` on REAL); ordinary targets never request it.
Do not regenerate a different site's targets over a running mission.

The REAL drone launcher connects `pArduBridge` to the Pixhawk's stable USB
interface `serial/by-id/usb-ArduPilot_Pixhawk6X_200023000E51333531343838-if00`
at 115200 baud. Interface `if00` is reserved for `pArduBridge`; the independent
`mavp2p`/QGC link uses `if02`. This avoids volatile `/dev/ttyACM*` numbering,
which changes when USB devices reconnect. The serial endpoint passed to
`pArduBridge` is intentionally relative to `/dev/`. For replacement hardware,
override it explicitly, for example:

```bash
make drone-briggs \
  DRONE_ARGS='--ap_url=serial/by-id/usb-ArduPilot_NEW_SERIAL-if00:115200'
```

### Local simulation and explicit housekeeping

```bash
make sim-briggs
make sim-charles
# Optional speedup (default 1) and GUI flags:
make sim-charles SIM_WARP=5 SIM_ARGS=--nogui
# Generate all three targets only; no Docker, MOOS apps or parameter uploads:
make sim-charles LAUNCH_ARGS=--just_make
```

Both sim targets call `launch.sh`: UAV SITL, PEARL SIM, and local shoreside.
All peer links use localhost, never the physical Tailnet addresses. The sites
reuse their physical targets' return/rally points, configured PEARL start,
datum, imagery and viewer pan/zoom. No loiter center/behavior is added. Idle
station-keeping, recovery holds and the approved native RTL pause are unchanged.

A new owned SITL receives an explicit Home at the selected datum
(`lat,lon,0,0`, aircraft local `0,0`), not a borrowed PEARL navigation sample.
`SITL_HOME` can override the simulator's startup Home; `SITL_PORT`,
`SITL_DIAGNOSTIC_PORT`, and `SITL_TARGET_PORT` select its host endpoints.
These are forwarded by CLI to the wrapper, not assumed to take effect merely
because environment variables exist. Site selection never writes REAL Home
or resets an existing FC/estimator. `SIM_ARGS` supplies combined-launch flags.

Housekeeping is explicit and never runs before a launch target:

- `make clean` runs `./clean.sh`: deletes this mission's logs/generated targets.
- `make kill` stops this mission's MarineViewer VNC wrapper and QGC VNC
  containers, runs `ktm` (**host-wide**, potentially affecting other MOOS
  communities), then tears down registered owned ArduPilot SITL projects.

Ctrl+C or normal operator-console exit from a combined sim launch stops its
owned community process groups, VNC/QGC sessions and exact SITL project. It
never runs host-wide `ktm` on that path. `--xlaunched` explicitly hands background
lifecycle ownership back to automation; later `make kill` still finds its SITL.
SITL ownership markers in `.sitl-owned/` survive log cleanup and are removed only
on successful teardown. Failed teardown is reported and retains a retry marker.
Unregistered/manual/other-mission SITL projects are not adopted or removed.
GUI cleanup scopes MarineViewer wrappers by user, mission directory and (for
Ctrl+C) run group; QGC cleanup also recognizes default mission export mounts
for whole-mission orphan recovery. It does not stop unrelated VNC desktops.

Preview with `make -n clean` / `make -n kill`. Use `clean` only after shutdown;
it deletes logs/targets, not applications or containers.

### Post-mission local artifact collection

After stopping the local mission, run:

```bash
make logs
```

`make logs` identifies the local physical node from the configured Tailnet IP,
selects its newest MOOS log directory, and creates a timestamped directory under
`field_logs/`. Each result contains `MANIFEST.json` and `SHA256SUMS`; collection
uses a staging directory and marks retained partial results `complete: false`.
Use `LOG_NODE=shoreside|pearl-real|pearl-grass|drone` only when automatic node
identification is unavailable, and `LOG_DIRECTORY=LOG_...` to override newest-log
selection. `LOG_OUTPUT_ROOT`, `LOG_VIDEO_ROOT`, `LOG_AP_PROTOCOL`,
`LOG_AP_ENDPOINT`, and `LOG_ARGS` are also overridable.

Artifact content is node-specific:

- Shoreside and GRASS PEARL: the latest local MOOS log.
- REAL PEARL: the latest MOOS log. The currently deployed Jetson D455 WebRTC
  stack explicitly does **not** record, so collection also writes
  `jetson-video/UNAVAILABLE.txt`, marks the result incomplete, and exits nonzero.
  A future task-scoped Jetson recorder must publish/provide an authoritative path
  before this requirement can be completed.
- Drone: the latest MOOS log, both task-scoped `fpv` and `nadir` camera trees,
  the newest Pixhawk DataFlash log, and a complete post-flight parameter export.
  Video must already report `VIDEO_RECORD_STATE=STOPPED`, and both stream
  directories must be nonempty.

The DataFlash and parameter files do not already exist on the drone filesystem:
they remain on the Pixhawk until a MAVLink client requests them. The collector
therefore implements `LOG_REQUEST_LIST`/`LOG_REQUEST_DATA` and
`PARAM_REQUEST_LIST`/`PARAM_REQUEST_READ`; simply copying MOOS/video directories
would omit those artifacts. Drone collection requires the mission to be stopped,
refuses a running `pArduBridge`, exclusively opens Pixhawk `if00`, and admits only
a freshly observed disarmed, `ON_GROUND` flight controller. It sends no arm,
mode, flight, parameter-set, reboot, or log-delete command. At 115200 baud a
multi-megabyte DataFlash download can take several minutes. The independent
`if02` QGC/mavp2p interface remains untouched.

Verify a result with:

```bash
cd field_logs/<artifact-directory>
sha256sum -c SHA256SUMS
```

After `make logs` has run on both vehicle hosts, fetch their newest bundles from
**shoreside** with the target for the physical site:

```bash
make collect-logs-briggs
make collect-logs-charles
```

These targets do not execute collection remotely. They use SSH only to select
the newest site-appropriate bundle, transfer it with `rsync`, and independently
verify every entry in each remote `SHA256SUMS` plus the manifest node identity.
Both bundles are placed under one timestamped
`field_logs/fetched/<timestamp>_remote-artifacts/` directory with a
`FETCH_MANIFEST.json`. A transferred REAL-PEARL bundle may retain
`source_complete: false` because of the documented Jetson gap; this produces a
warning but does not mean the transfer failed.

Defaults assume `pi@100.112.119.114` for the Landmark/drone computer,
`pearl@100.71.137.47` for Briggs GRASS PEARL, and
`pearl@100.113.99.58` for Charles REAL PEARL. The remote artifact root is
`workspace/mit/pearl/moos-ivp-pearl/missions/briggs_field/2026_10_09_water_ops_prep/field_logs`
relative to each SSH user's home. Override `COLLECT_DRONE_HOST`,
`COLLECT_PEARL_GRASS_HOST`, `COLLECT_PEARL_REAL_HOST`, `COLLECT_REMOTE_ROOT`,
`COLLECT_OUTPUT_ROOT`, or `COLLECT_ARGS` for a different deployment. Existing
SSH host-key and authentication policy is preserved; the targets do not disable
host-key checking or copy credentials.

For a stopped local three-community simulation, use:

```bash
make collect-logs-sim
```

This requires closed logs and selects the newest UAV MOOS log plus PEARL and
shoreside logs whose native `LOGSTART` values are within ten seconds. It also
preserves the newest owned `LOG_sitl/moos-sitl-*.log` launcher log and a nonempty
`LOG_params/` directory when present. The checksummed artifact is written under
`field_logs/sim/`. Physical camera recordings and Pixhawk DataFlash retrieval do
not apply to this local post-shutdown SIM target; the container's `/state` is
temporary and is removed during owned SITL teardown.

Run `make logs` on each physical node before any remote `make clean`; cleanup
deletes the source MOOS logs. Run the site-specific collection target before
deleting remote `field_logs`.

## Task-scoped UAV video recording

Every REAL UAV launch runs `pVideoRecorder`. When recording support is enabled,
assigning a UAV task starts the `fpv` and `nadir` recordings. Recording continues
through recovery and stops after confirmed nominal landing (or a grounded
pre-takeoff abort). It deliberately continues after an off-nominal landing until
an operator sends `VIDEO_RECORD_COMMAND=STOP`. Multiple tasks use separate
`<LOGGER_DIRECTORY>/<UAV_ROUTE_ACTIVE_ID>/` folders.

SITL leaves physical-UAV recording control disabled unless explicitly enabled
with `--video-recording`, for example
`make sim-briggs SIM_ARGS=--video-recording`. Video never traverses MOOS. The
recording host, task ID, and absolute path are published in the UAV MOOSDB and
bridged to shoreside. Manual `VIDEO_RECORD_COMMAND` remains available only for
recovery/override. See [VIDEO_RECORDING.md](VIDEO_RECORDING.md) for lifecycle,
paths, API access, status variables, one-minute fMP4 segments, and retrieval
cautions.

## Shared route tasking

Draw waypoints with the **route** left-click context. **TASK-PEARL** and
**TASK-UAV** submit an immutable copy to the selected vehicle and remove only
the shoreside drawing. **route-clear** clears that drawing without cancelling
either vehicle; **uav-cancel** remains the separate UAV cancellation action.
Empty/invalid submissions start nothing, and a second click without a new
drawing cannot resubmit the consumed route.

PEARL accepts tasks only with fresh NORMAL_OPERATION / IDLE / NO_ACTIVE_REQUEST
state. Its existing FSM enters EXECUTING_MISSION after matching Helm route
readiness, follows the route at the existing PEARL speed, and returns to the
existing base `(10.6,4.8)` on actual waypoint completion. Support interruption
suspends the route and preserves waypoint progress for history resumption.
Busy/stale tasks are rejected, not queued for surprise execution later.

UAV tasking retains its separate **uav-rally** selection and verified rally /
takeoff / recovery workflow below. Editing the next drawing cannot mutate a
submitted or executing route. Submission and mediator receipt do **not** prove
vehicle acceptance or completion: inspect ROUTE_BUFFER_TASK_ID/DESTINATION,
PEARL_ROUTE_BUFFER_STATE, PEARL_ROUTE_READY and the vehicle FSM states.

The UAV FSM issues local `ROUTE_BUFFER_COMMAND=action=clear` on mission exit
and IDLE entry, releasing the previous immutable task's buffer ownership as
well as clearing Helm execution. A Helm flag alone does not release that
ownership. UAV snapshots also carry an identity condition tied to
`UAV_ROUTE_ACTIVE_ID`; a retained old Helm instance cannot run after a parked
clearance denial and replacement. Busy task rejection and duplicate-ID
suppression remain in force. `tests/test_uav_repeated_task.py` verifies normal
completion/landing/second task and denied-clearance/replacement without
restarting applications, using real FSM/Helm/buffer with synthetic evidence.

These paths were exercised in owned isolated real-MOOS/Helm/FSM tests using
synthetic navigation, not a new physical two-vehicle flight. Apply through the
next safe normal launch; no running missions were restarted.

## Return/rally defaults and map selection

Seed both points using local mission coordinates (metres), not latitude/longitude:

```bash
./launch.sh --pearl_return=10.6,4.8 --uav_rally=10,15
```

These are the unchanged defaults, **not locked destinations**. Left-click
**pearl-return** or **uav-rally** in the map context menu to select the point
for future tasks; PEARL's staged return marker is cyan and unlabeled.
**TASK-PEARL** freezes the selected return point with the route/task ID.
Later map edits cannot redirect an executing task or its return. The next
accepted task may use a different return point. Clearing/consuming the shared
route drawing does not clear either point selection or another vehicle's task.
An invalid/missing PEARL return selection rejects submission without consuming
the drawing; malformed/missing task return metadata cannot start PEARL.

For split launches, give both switches to `launch_shoreside.sh`, and give
`--pearl_return=X,Y` to `launch_pearl.sh` /
`--uav_rally=X,Y` to `launch_uav.sh` for consistent initial behavior fallbacks.
The vehicle accepts the task-frozen selection, not live map-setting updates.
`--secondary_landing=X,Y` remains an alias for `--uav_rally=X,Y`.
Coordinates must be finite, within ±100 km on each axis; launch generation
rejects malformed values. Rally altitude, upload/readback, checks and ARM
protection are unchanged. Apply through the next safe normal launch.

## Rally selection and task commit

Left-click **uav-rally** in the map context menu to stage the fallback point.
Its orange marker has no label. Change the transit altitude on shoreside:

```bash
uPokeDB targ_shoreside.moos RALLY_ALTITUDE=20
```

The default is **15 m above flight-controller Home**; use whole metres.
**TASK-UAV** freezes the current point and altitude while deploying the staged
route. The UAV must be IDLE, freshly grounded and disarmed. Task execution is
admitted immediately into TAKEOFF_OPERATIONS / ESTABLISHING_TAKEOFF_READINESS,
not held in IDLE until rally verification. Progress beyond readiness requires
`pArduBridge` to upload/read back the single FC rally, the rally-only selection
policy and above-Home RTL altitude settings. Missing/failed verification blocks
FLIGHT_PLAN readiness and ARM, not the task-received state transition.

`ENTER_GUIDED` also supports a subsequent task after successful recovery:
ArduPilot may retain LAND (or RTL) after touchdown/disarm. A fresh explicit
request may leave only the same recovery mode observed at admission, with
connected native ON_GROUND and disarmed samples no older than two real
seconds. That request-scoped exception is permanently revoked on mode change
or loss of proof, and cleared on success, cancellation or timeout. A new
LAND/RTL mode after admission, pending LAND/RTL intent, manual parking or
deadman still preempts. Guided readiness still requires native mode telemetry;
no ARM, takeoff, rally verification or FSM checks are bypassed.
Bridge AppCast exposes `Guided Readiness Pending` and
`Ground Recovery Exception`. Production-path doubles cover freshness,
airborne/unknown evidence and preemption;
`src/pArduBridge/tests/test_guided_ground_sitl.py --run-sitl` in the UAV-base
repo verifies two owned disarmed LAND-to-Guided requests using native
heartbeats, without ARM/TAKEOFF or a live mission change.

MOOS off-nominal waypoint guidance uses that same frozen point and altitude.
Generic FC RTL/failsafe recovery uses the FC's stored rally, with
`RALLY_INCL_HOME=0` and `RALLY_LIMIT_KM=0`: a closer Home is not considered
while the selected rally remains valid. Current firmware receives
`RTL_ALT_M`; legacy Copter uses `RTL_ALT` in centimetres, with
`RTL_ALT_TYPE=0`. These persisted settings do not override ArduPilot safety
limits, higher-current-altitude/minimum-climb behavior, or fallback if rally
storage later becomes invalid. Validate the landing site and terrain yourself.

Later map/altitude edits apply only to the **next task**. The staged marker is
not proof of the committed point: inspect `RALLY_ACTIVE`,
`RALLY_TASK_STATE/REASON`, `UAV_RALLY_STATE/ID/READY`.
`--secondary_landing=X,Y` initializes the staged rally; the context selection
supersedes it at tasking. No FSM states, edges, labels or layout were changed.

The existing **FLIGHT_PLAN** check requires `ROUTE_READY=true`, current
`RALLY_TASK_STATE=READY` and numeric `UAV_RALLY_READY=1`; there is no separate
rally check. Verification can finish while the FSM is establishing readiness,
and its result never issues a second TASK_EXECUTE trigger.
With `helm_state_var=IVPHELM_STATE`, vehicle `pRouteBuffer` retains a
snapshot received while Helm is parked, then delivers it on actual DRIVE.
Parked Helm otherwise clears update deltas before spawning the operator route,
leaving the received route with no `ROUTE_READY` evidence. CLEAR discards
the deferred snapshot. This handoff does not synthesize readiness or depend
on how long the FC rally upload takes.

See [pRallyPoint](../../../src/pRallyPoint/README.md) for the protocol, parameter
references and safety limitations. Isolated MOOS transaction tests and two
actual SITL FC upload/replacement/readback transactions passed. Airborne RTL
and a complete off-nominal landing have **not** been flight-validated here.
Existing live missions were not moved or restarted.

## Direct native pose for the SITL target emulator

Next launch uses `vehicle_pose_source=mavlink` in the existing
`pMavlinkLandingTarget`, receiving FC quaternion/position directly on its sensor
TCP connection, not through bridge/EchoVar aircraft-pose relays. It requests
50 Hz telemetry only, requires receive age <=100 ms and position/attitude FC
timestamp skew <=20 ms, and never falls back to MOOS aircraft coordinates.
Native sample windows feed the viewer's observation-time position/attitude and
coordinator's exposure rotation; FC readiness and post-ACK yaw stay bridge-owned.
PEARL target position still comes through MOOS. No `PLND_LAG` change or new app/
FSM topology is involved. See [the source contract](../../../src/pMavlinkLandingTarget/README.md).
This reduces relay latency, not proof of zero exposure-to-FC delay.

## UAV heading and course over ground

`pArduBridge` always publishes body orientation and travel direction separately;
there is no heading-source configuration switch:

| Publication | Source and reference | Units |
| --- | --- | --- |
| `UAV_YAW` | Received FC attitude yaw, true north (unchanged) | degrees clockwise, 0–360 |
| `UAV_HEADING` | Received FC attitude yaw converted to mission-grid bearing | degrees clockwise, 0–360 |
| `UAV_COG` | Received FC raw-GPS course over ground converted to mission grid | degrees clockwise, 0–360 |
| `UAV_SPEED` | Received FC raw-GPS ground speed (unchanged) | m/s |

The existing `UAV_HEADING -> NAV_HEADING` mapping now supplies actual body
orientation to Helm and the existing node reporter/viewer. `UAV_COG -> NAV_COG`
makes travel direction available separately; consumers needing COG must not
treat `NAV_HEADING` as course anymore. This changes the Helm's measured heading
input, not just the display; no behavior or flight-command algorithms were
changed. PEARL and landing-target true-north yaw inputs remain unchanged.

Bridge yaw and COG are received from the FC, not inferred from successive MOOS
positions. COG can still flip at negligible speed: it is not body orientation.
NodeReporter already uses `report_cog=true`; its optional node-report COG is
separately position-derived, not a subscription to `NAV_COG`. No node reporter,
contact-library, or viewer modifications are required.

This is an unconditional bridge publication semantics change (including other
missions using pArduBridge): old course consumers should use `*_COG` instead
of `*_HEADING`. Native attitude/GPS validity and telemetry-loss limitations
are not changed by this separation. Changes take effect on the next launch;
running missions are not restarted.

Isolated publication and mission-wiring regressions, from the repository root:
`python3 ../moos-ivp-uav-base/src/pArduBridge/tests/test_heading_publications.py`.

## Precision target frame and optional yaw alignment

The emulator now uses native FC **roll, pitch and true yaw** to express target
XYZ and orientation in actual BODY_FRD. Missing/stale attitude stops emulation;
no level-pose substitution is made. The received viewer applies the full inverse
mapping, including the vertical component's horizontal projection under tilt.
Native attitude boot timestamps identify the exact pose used for the observation.

Yaw alignment is enabled by default for both REAL and SITL launches. Use
`--no-yaw-alignment` to disable it for one generated mission, or
`--yaw-alignment` to select it explicitly. The launcher expands both the
coordinator option and the independent FSM check consistently: enabled requires
`LANDING_OWN_YAW_STATE=ALIGNED`, while disabled requires the explicit
`DISABLED` state. REAL and SITL use the same method when enabled: the received
BODY_FRD target quaternion directly supplies signed relative yaw error. Stable observations cause one relative `CONDITION_YAW`
request during the established Guided hold; FC ACK is followed by fresh,
advancing target observations held within 5 degrees before AUTOLAND. No
exposure-time NED pose, clock match, simulator heading, peer heading or COG can
authorize alignment. Timeout is 60 MOOS seconds and command rate is 30 degrees
per FC second.

`uLandingYawChecks` independently requires `LANDING_OWN_YAW_STATE=ALIGNED` on
the FSM's APPROACHING-to-LANDING edge. `pLandingCoordinator` remains the owner
of the active command/ACK/measurement transaction and cannot publish LANDING
until the yaw gate has passed and LAND has been accepted. Missing or unstable
orientation, stale/nonadvancing observations, excessive tilt, rejection,
cancellation or lost permission cannot manufacture readiness or trigger a timed
retry. ArduPilot owns LAND/descent/retries/touchdown/disarm; yaw is not
continuously tracked during LAND.

`PLND_YAW_ALIGN` is sensor mounting calibration, not this feature. ArduPilot
ignores LANDING_TARGET quaternion for its own yaw control. See
[pLandingCoordinator](../../../src/pLandingCoordinator/README.md) for the
freshness, stability, command and confirmation contracts, and
[pMavlinkLandingTarget](../../../src/pMavlinkLandingTarget/README.md) for the
corrected angular signs. No live mission is regenerated, restarted or commanded
by these edits.

## Guided ownership diagnostics

Bridge `HELM_PARKED` means **pArduBridge Helm authority is parked**, not
that `pHelmIvP` reports PARK. An actual DRIVE Helm may still lack bridge
flight authority during preflight/FC-owned takeoff.

While the bridge is parked, fresh native ON_GROUND plus disarmed evidence
makes Guided informational (`UAV_GUIDED_OWNERSHIP_STATE=GROUNDED_DISARMED`).
An actual accepted FC TAKEOFF with a live activation/completion deadline is
`FC_TAKEOFF_ACKNOWLEDGED`; its existing command confirmation and timeout
reporting remain authoritative. A task/FSM intent alone grants no ownership.
Completion, abort, or timeout revokes that diagnostic lease.

Unowned Guided operation still warns after the existing **2 MOOS-second**
grace; armed airborne Guided is not excused by a historical takeoff ACK.
Disconnected, missing, UNKNOWN, or older-than-2-real-second native
landed-state telemetry is `TELEMETRY_UNCERTAIN`, also warning after that grace.
Inspect `UAV_GUIDED_OWNERSHIP_STATE/WARNING` and the bridge AppCast.
This monitor does not arm, command flight/modes, grant Helm authority, change
FSM topology, or synthesize readiness. Passive telemetry mode does not run it.

Isolated policy/timer/ACK-lease regressions:
`python3 ../../../../moos-ivp-uav-base/src/pArduBridge/tests/test_guided_ownership.py`
(from the repository root, use the sibling path
`../moos-ivp-uav-base/src/pArduBridge/tests/test_guided_ownership.py`).

## PEARL supervisory FSM source

[`pearl_fsm.toml`](pearl_fsm.toml) is the source for the executable
[`pearl_fsm/plug_pearl_fsm_runtime.moos`](pearl_fsm/plug_pearl_fsm_runtime.moos)
configuration, generated pShare-route and shoreside-scope include fragments in
`pearl_fsm/`, the composed [`pearl_fsm/overview.d2`](pearl_fsm/overview.d2)
overview and detail boards, the linked SVG set under `pearl_fsm/svg/`, and the
[`pearl_fsm/bindings.json`](pearl_fsm/bindings.json) UI binding map. Regenerate
and check them with:

```bash
../../../scripts/generate_fsm.py pearl_fsm.toml
../../../scripts/generate_fsm.py pearl_fsm.toml --check
```

`launch_pearl.sh` and `launch_uav.sh` automatically regenerate their respective
FSM artifacts before expanding mission targets, including
in `--just_make` mode. Generation failures stop that vehicle's launch. Both
launchers therefore require a TALA-capable `d2` executable (or `D2_BIN`).

The sole backend is the standalone `pFSMRuntime` MOOS utility. It publishes
`FSM_TRANSITION` as a JSON batch whenever transitions occur; each occurrence
includes its transition ID, source and destination node IDs, and occurrence ID.
Top-level `transition_time` records the MOOS time of the match; a top-level
`context` records all candidate guard inputs from the winning
source node when its guard matched, before exit effects. For `check_result(...)`
inputs it embeds the full local `uChecks` snapshot as a JSON object under
`<RESULT_VAR>_SNAPSHOT` instead of repeating the scalar result (or `null` if
unavailable). The scalar still controls the guard; a snapshot is diagnostic and
may lag it. PEARL
and UAV route these to shoreside under distinct prefixed names. The utility
also publishes
`<PREFIX>_FSM_RUNTIME_STATE=READY`, a startup epoch, and a periodic heartbeat;
consumers can use the heartbeat as a lease and reject persistent MOOS values
after the runtime process fails. The `pFSMRuntime` blocks in `meta_uav.moos`
and `meta_pearl.moos` also expose `min_state_time`; it defaults to zero and may
be increased temporarily to make fast transitions visible during debugging.

The generator uses Python 3.11's standard `tomllib`; older Python installations
may provide the compatible `tomli` package instead. The TOML separates the
state-machine model from its output adapters:

- Top-level `[facts]`, `[machine]`, and `[states]` tables declare each fact,
  root-machine property, state, label, transition, guard, and hierarchy
  relationship exactly once.
  Fact keys are IDs. A scalar value is initialized by the selected runtime; the
  table form `{ initial = ..., initialize = false }` declares a subscribed fact
  owned by another app or MOOS community and prevents this FSM from initializing
  it. A machine may declare `condition = "..."` and `history = true|false`;
  a false condition publishes `none` throughout its hierarchy, and reactivation
  either restores deep history or resets all descendants. States may contain
  `entry_effects` and `exit_effects` tables whose keys
  are declared fact IDs and whose scalar values are typed assignments. Entry
  effects execute after the state publication and exit effects before leaving
  the state. States and transitions are keyed tables; child-machine IDs are inferred
  from the hierarchy.
  Each transition is nested beneath its source state, so only its `to` endpoint
  is declared. A target may belong to the source machine or an ancestor
  machine. Ancestor-targeting transitions retain their nested source in D2 and
  are lifted to the owning executable FSM with an inferred source-state guard.
  Transitions have nonempty labels except that a parent named-exit route may
  omit one and use its inferred sink label. Node placement and optional dimensions
  are co-located in `layout`; visual styling is inferred. The model contains no
  MOOS variables or `graphics` tables.
- A state without children is simple, a state with nested `states` is
  composite, and a state with named `regions` is orthogonal. Declaring both
  `states` and `regions` is invalid. The generator infers executable FSMs for
  the root, each composite state, and every region.
- Neutral guards use fact identifiers and inferred-machine state references,
  for example `takeoff_support.state == support_completed`. They may also use
  `check_result(takeoff_prechecks, success)` inside `when`, combined with
  `&&`, `||`, and parentheses. Its first argument is an externally owned string
  fact ID; the generator compiles it to a comparison of that scalar result and
  derives its diagnostic snapshot variable as `<RESULT_VAR>_SNAPSHOT`. A state
  can opt into `entry_epoch = true` to publish a string per-entry token derived
  from the runtime epoch and state entry count. The guard
  binding AST retains both fact and snapshot references without a separate
  check-set table. It does **not** imply that the checks are active or
  session-matched: PEARL still verifies
  request IDs before granting clearance. Child activation is inferred from
  the hierarchy, so guards do not repeat root-state checks.
- `[targets.runtime]` provides the naming prefix. Fact IDs are converted
  directly to upper-snake-case MOOS variables. Each machine automatically
  receives `<PREFIX>_FSM_<MACHINE>_STATE`, `_STATUS`, and `_TRANSITION` output
  variables.
  Each state entry increments an inferred `<STATE_VAR>_<STATE>_COUNT` variable;
  a count variable is not published until its state is entered. The root
  publishes the shared `<PREFIX>_FSM_STATE` aggregate. State names and
  hierarchical `substate` entries are derived from `[states]`.
- `[outputs].pshare` and `[outputs].scopes` name generated MOOS include
  fragments. Shoreside receives only runtime health (`READY`, heartbeat, epoch)
  and the JSON `FSM_TRANSITION` event; individual FSM state, status, transition,
  aggregate-state, and entry-count variables are neither bridged nor scoped
  there. Cross-vehicle state routes required by coordination guards remain.
- `[targets.d2]` contains `layout.direction`. The generator fixes the layout
  engine to TALA and emits no diagram title. Per-node layout may use `near`, or
  a paired `top` and `left`; `near` cannot be combined with absolute
  coordinates. Optional positive `width` and `height` values also live in the
  node's `layout` table. The target has no independent node, edge, class,
  class-definition, or comment declarations.
- `[machine]` configures the root while all root states live under `[states]`.
  Composite states and regions infer child machines, initial markers, and
  transitions from the hierarchy. A composite state with `diagram = true`
  remains a linked node in its parent board and is expanded into an independent
  D2 layer in the same composed source.
  Terminal detail states may transition to the default `to = "sink"`, with an
  optional `when`. Each direct parent exit requires its own guard (when
  provided) **and** an active nested default-sink transition whose source
  state and optional `when` hold. Multiple default-sink transitions are ORed; their result is ANDed with
  each parent exit guard. If `when` is omitted, the transition is eligible as
  soon as its source state is active (subject to any other parent guard and
  configured dwell time); ordinary and named-sink transitions support the same
  omission. Entry and exit effects still run on the actual transition. The
  parent transition retains the runtime ID, target, and optional history. For
  individually routed exits, nested transitions
  target `sink.<id>` and exactly one
  parent transition pairs with each inferred sink through `exit = "<id>"`. An
  optional keyed `[...sinks.<id>]` table may provide that sink's `label` and
  `layout`, but is not required. Without an explicit label, the sink ID is shown
  with underscores replaced by spaces. The generator compiles each guarded
  sink transition into an executable ancestor transition whose guard includes
  the nested source-state constraint; the paired parent transition supplies the
  destination and optional history target. A parent exit-route label is optional:
  an authored label is used verbatim, while an omitted label becomes the sink
  label in brackets, such as `[Completed]`; if the sink has no authored label,
  its inferred ID-based label is bracketed instead. Named sink labels are also
  shown on the child sink.
  Multiple child transitions into the same sink remain distinct detail edges and
  runtime transition IDs, but share one parent-board exit edge carrying all of
  their runtime selectors. Named sink edges retain runtime MOOS bindings in both
  overview and detail boards. Inferred initial and sink nodes
  link to the parent board. Legacy `sink` uses optional `sink_layout`; a named
  sink may use its optional keyed table when a custom label or layout is needed.
- A transition targets history with
  `history = { machine = "...", state = "history" }` or `state =
  "deep_history"`. A machine's `history_layout` and `deep_history_layout`
  provide optional placement and dimensions; their node IDs and labels are
  inferred. Likewise, `initial_layout` positions the inferred initial node. A history
  node is drawn only when a rendered transition targets it, so shallow and deep
  history may both appear when both have incoming transitions.
- D2 classes are inferred from structure: `state`, `composite`, `region`,
  `initial`, `history`, `sink`, and `transition`. State nodes additionally receive
  `moos_state`, `moos_var_<STATE_VAR>`, and `moos_value_<STATE>` classes.
  Runtime edges receive `moos_transition`, `moos_var_<TRANSITION_VAR>`, and
  `moos_id_<TRANSITION_ID>` classes. Initial edges use the inferred transition
  ID `INITIAL`; the selected execution backend posts that edge for the root FSM
  and every child FSM along the active composite hierarchy. Inactive detail FSMs
  publish `none`; they publish their initial state and transition on activation.
  A UI can therefore select the matching SVG element using
  the reported MOOS variable and value/ID without depending on D2's internal
  SVG identifiers. Shapes are also inferred: states, composites, and regions
  are rounded rectangles, with dashed region outlines separating orthogonal
  regions; initial nodes are filled circles, history nodes are unfilled circles,
  and sinks use an unfilled outer circle with a smaller filled inner circle.
  Initial circles and sink outer circles have the same inferred size. State borders, history
  borders, transition arrows, and initial nodes share the same diagram color.
  Explicit model classes, shape declarations, and class definitions are
  rejected. D2 shape keys are model IDs, while connection identity is exposed
  through the generated transition classes. The JSON map provides direct
  `states[moos_variable][value]` and
  `transitions[moos_variable][report_id]` lookups. Each transition binding
  includes its machine, label, source and target states, D2 identity, selector,
  source guard, compiled effective MOOS guard, and typed source/effective guard
  ASTs. Ancestor-targeting transitions also expose their inferred
  `source_constraint`. Boolean AST nodes contain `and`/`or` operands;
  predicate nodes contain typed fact, check-result, or machine-state references
  plus literal or state-valued right operands.

All generated D2 sources select the TALA layout engine. SVG rendering uses 10
pixels of outer canvas padding. Generation requires a TALA-capable `d2`
executable in `PATH` (or specified through `D2_BIN`) and fails if rendering is
unavailable. D2 renders the composition as a linked SVG set:
`index.svg` for the overview plus one SVG per detail layer. `--check` rerenders
and detects stale, missing, or extra SVG files.

The generator validates references, transition endpoints, state reachability,
render hierarchy, and generated node identity before writing any output.
Generated files are deterministic, and `--check` fails when an output differs
from its TOML source.

## Recovery diagnostics and display

- Every `uChecks` collection must declare both `result_var` and
  `result_prefix`. Missing the prefix disables the collection; fresh airborne
  telemetry alone then cannot advance the takeoff guard.
- Point calculation accepts a current-session rendezvous phase of either
  `RENDEZVOUS` or `COMPLETE`: a fast capture can finish between FSM ticks.
- Fresh, disarmed ground contact before accepted coordinator `AUTOLAND`
  aborts recovery with `landed_without_authorized_autoland`; it never reports
  successful coordinated recovery. The existing secondary/off-nominal path
  may finish on verified ground without inventing rally-point arrival.
  `SECONDARY_LAND` is an FSM-owned entry/exit intent. Its conditional dispatcher
  requires real `SECONDARY_ARRIVED` evidence and armed/in-air telemetry; an
  already-grounded abort issues no additional flight command.
- UAV stationkeeping rings, the bridge's `Loiter_point_uav` gateway marker,
  control-vector arrows, and shoreside comms pulses are hidden. The bridge
  marker is disabled with `show_loiter_visuals=false` in
  `plug_uav_ardupilot.moos`; this also posts its inactive removal. Loiter/hold
  guidance and communications remain enabled, and PEARL visuals are unchanged.
  These settings apply on the next normal launch, not by restarting a live UAV.
- The simulator marker retains last-known geometry when peer navigation is
  stale; it is not proof of a fresh received target. The magenta received
  footprint independently expires. Investigate sharing/heartbeat gaps before
  blaming apparent target lag on vehicle estimation or widening freshness
  limits. The 20:37:53 run had a 106–143-second UAV-side sharing-summary and
  heartbeat gap, delayed recovery agreement, and a deadman RTL. An isolated
  test reproduced a matching pShare starvation mechanism: comms-driven mail
  continually reset the MOOS-core iteration deadline. The rebuilt pShare uses
  fixed 40 Hz mail/iteration scheduling; no live mission was restarted and a
  fresh full mission run still needs verification.

## UAV companion FSM and authority boundary

[`uav_fsm.toml`](uav_fsm.toml) defines the sequential UAV lifecycle
`IDLE → TAKEOFF_OPERATIONS → MISSION_EXECUTION → LANDING_OPERATIONS`. A nominal
landing exits through the named `nominal` sink and returns to `IDLE`. The nominal
landing detail proceeds through `ESTABLISHING_LANDING_READINESS →`
`REQUESTING_RECOVERY → CALCULATING_RENDEZVOUS_POINT →`
`MOVING_TO_RENDEZVOUS_POINT → WAITING_FOR_LANDING_CLEARANCE → APPROACHING →`
`LANDING`. Target loss during landing enters `RETRYING_LANDING_TARGET`; exhausted
retries, request failure, point-calculation failure, clearance failure, or approach
failure divert through the rally point and `SECONDARY_LANDING` before exiting to
`OFFNOMINAL`. Readiness failure instead transits to the secondary loiter
location and exits through `LOITERING`. Neither root outcome has a takeoff exit,
so both inhibit a new takeoff pending recovery intervention.

An external `MOOS_MANUAL_OVERRIDE=true` still parks Helm. The bridge skips its
legacy automatic RTL side effect only when the FC is connected and native
landed-state and armed samples, both at most 2 real seconds old, confirm
`ON_GROUND` and disarmed. This prevents a nominal return to `IDLE` after
touchdown from producing a spurious `RTL REJECTED: NOT_ARMED`. Unavailable,
stale or indeterminate evidence retains the existing RTL behavior; explicit
RTL/deadman requests and independently pending RTL are not cleared. Neither
FSM state, low altitude nor simulator attachment is used as ground proof.

The FSM generates
[`uav_fsm/plug_uav_fsm_runtime.moos`](uav_fsm/plug_uav_fsm_runtime.moos), the
pShare/scope fragments and JSON UI bindings under `uav_fsm/`,
[`uav_fsm/overview.d2`](uav_fsm/overview.d2), and `uav_fsm/svg/index.svg`:

```bash
../../../scripts/generate_fsm.py uav_fsm.toml
../../../scripts/generate_fsm.py uav_fsm.toml --check
```

The cross-community contract has one writer per coordination fact:

- PEARL's `pTakeoffProcessor` owns its local `TAKEOFF_REQUEST_STATE` clearance
  status. It posts `granted` only after sending the ID-tagged clearance; this
  PEARL-owned variable is routed to shoreside for diagnosis, not to the UAV.
  The processor publishes `complete` only for session-matched takeoff
  completion; the FSM uses this single fact to leave takeoff support. PEARL
  also owns `TAKEOFF_CLEARANCE_STATE`, `RECOVERY_REQUEST_STATE`, and
  `LANDING_CLEARANCE_STATE`; `initialize = false` prevents the UAV FSM from
  posting fallback values for externally owned facts.
- The UAV community's `pFSMRuntime` owns `UAV_FSM_OPERATION_STATE` and the
  active nested takeoff and landing state variables. Helm behavior modes consume
  these authoritative publications rather than writing them. PEARL subscribes
  only to the operation and landing variables and does not initialize them.
- UAV telemetry (`UAV_LANDED_STATE`, `UAV_IS_ARMED`) and rendezvous facts are
  owned by their UAV source applications. `pRendezvous` owns
  `UAV_RENDEZVOUS_PHASE` and `UAV_RENDEZVOUS_ARRIVED`; the landing-target retry
  controller owns `UAV_LANDING_TARGET_STATE`. `pRendezvous` owns
  `UAV_SECONDARY_RECOVERY_STATE` and drives primary-landing failures to the
  secondary landing point (default `(10,15)`), overridable with
  `--secondary_landing=X,Y` on `launch.sh` or `launch_uav.sh`. The FSM consumes
  `UAV_LANDING_TARGET_STATE=TRACKED|LOST|RETRIES_EXHAUSTED` and
  `UAV_SECONDARY_RECOVERY_STATE=ARRIVED|LANDING|LANDED|LOITERING`. The companion
  FSM observes these facts but never rewrites them. The landing-target retry
  contract still requires a production publisher.
- PEARL's active recovery postchecks renew their evaluated result and
  session diagnostics every 1 MOOS second (`result_interval=1`), within the
  landing coordinator's 3-second permission lease. Unchanged success therefore
  stays fresh while waiting for UAV rendezvous completion. Inactive or stale
  checks cannot renew success; input-age limits and FSM topology are unchanged.
- PEARL-only execution facts, including `RECOVERY_POSTCHECKS_STATE`, are never
  routed from or published by the UAV FSM. The UAV may report detailed landing
  progress and its authoritative root outcome, but only PEARL can report that
  its own recovery postchecks passed and grant landing clearance.

Direct pShare routes carry other PEARL-owned coordination variables to the
UAV and the UAV-owned operation/detail variables to PEARL. The UAV also
periodically routes its rendezvous phase and session to PEARL as a reconciliation
channel for delayed or lost one-shot landing status messages. UAV FSM runtime
health and JSON transition batches are routed to shoreside; per-machine FSM
state, status, transition, and count variables remain local except for the
two operation/landing state routes PEARL requires for coordination. The D2
binding maps remain generated, but their state/edge highlighting needs a
consumer of the JSON feed rather than individual MOOS scopes.

## Implemented rendezvous state machine

Both vehicles run the same role-free `RendezvousPlanner` implementation inside
`pRendezvous`. Each starts with the geometric midpoint of its own fresh
`NAV_X/Y` and the peer's fresh `NODE_REPORT`. Once both measured speeds are
available, it recalculates once per simulated second:

```text
point = own_position + own_measured_speed /
        (own_measured_speed + peer_measured_speed) * (peer_position - own_position)
```

Each vehicle estimates its own speed from cumulative XY path length divided
by elapsed time, starting at approach entry or the trailing 20-second boundary,
whichever is newer. The window boundary is interpolated between observations;
turns count as travelled distance, not net displacement. The session-tagged
estimate is published locally and sent to the peer once per second.
Missing/stale estimates or two stationary vehicles fall back to the geometric
midpoint. There are no nominal vehicle speeds, turn-radius assumptions, proposed
points, or point acceptance messages in this calculation.

Each vehicle publishes its own `VIEW_POINT` with a unique
`rendezvous_<ownship>` label and its own `<OWNSHIP>_RENDEZVOUS_POINT`.
Its point freezes for the rest of that approach once it is within
`point_freeze_radius` of the previously published point; moving away again
does not unfreeze it. A new rendezvous resets the latch and speed history.

### Startup history gate

Both prep-mission planners now use `planning_history_window=3` seconds and
`planning_max_positive_accel=0.5` m/s². Speed-based replanning, local freezing
(including the capture shortcut), and local arrival require a usable recent
window on **both** vehicles, entirely after this approach's initialization.
Until it qualifies, the initial midpoint stays provisional; later loss of
qualification retains the last candidate rather than moving it from bad data.

Positive acceleration is the largest increase between consecutive one-second
XY path-speed averages in that window; deceleration is allowed. Latest samples
must be at most 1.5 MOOS seconds old, with no sample gaps above 1.5 seconds.
Repeated timestamps, missing peer history, elapsed time alone, and stale
previous success cannot qualify it. This gates decisions, not native telemetry
values. The existing 20-second speed estimator and earliest canonical
peer-freeze arbitration remain unchanged; a frozen canonical point never
unfreezes, but local arrival still waits for qualified history.

AppCast exposes usability, each side's blocking reason, and maximum positive
acceleration. Local diagnostics: `RENDEZVOUS_OWN_HISTORY_USABLE`,
`RENDEZVOUS_OWN/PEER_HISTORY_REASON`, and
`RENDEZVOUS_OWN/PEER_MAX_POSITIVE_ACCEL`. Other missions retain legacy behavior
unless they opt in (`planning_history_window=0` is the application default).
A positive window must be a whole number of seconds from 3 through 20.

Regression: `python3 src/pRendezvous/tests/test_history_gate.py` from the repo
root uses owned MOOS processes and synthetic navigation only, never a flight
controller or running mission. Apply the rebuilt app/templates through the next
safe normal launch; no airborne bridge or community restarts.

The three distances are independent:

| Setting | UAV default | PEARL default | Meaning |
| --- | --- | --- | --- |
| `point_freeze_radius` in `meta_*.moos` | 4 m | 4 m | Stop recalculating this vehicle's point. |
| `capture_radius` in the dedicated `meta_*.bhv` waypoint | 0.5 m | 1 m | Stop navigating to this vehicle's point. |
| `rendezvous_distance` in `meta_*.moos` | 3 m | 3 m | Mark rendezvous reached from fresh vehicle-to-vehicle XY separation. |

Both vehicles use dedicated, persistent `BHV_Waypoint` instances updated
directly through `UAV_RENDEZVOUS_UPDATE` or `PEARL_RENDEZVOUS_UPDATE`;
rendezvous never stages or waits for acceptance from `pRouteBuffer`.
Capturing a waypoint does not publish rendezvous arrival. PEARL station-keeps
after capture, while UAV control handoff is gated on actual waypoint readiness,
airborne/armed telemetry, and the recovery phase. Operator routes and secondary
recovery remain independent and continue using the route buffer.

Platform permission is still required: the UAV requests recovery, PEARL runs
its session-scoped checks and replies `ready` without specifying a point.
This is clearance coordination, not rendezvous-point negotiation.

| State | Action and transition |
| --- | --- |
| `IDLE` | Wait for an airborne recovery request. |
| `REQUESTING` | Request recovery support and wait for PEARL's checks/permission. |
| `RENDEZVOUS` | Independently update and follow local points. Arrival uses separation, not waypoint capture. PEARL also requires its fresh platform and separation gates continuously for 2 s before landing clearance. |
| `ACQUIRING_TARGET` | PEARL station-keeps; the UAV's dedicated waypoint follows fresh platform reports at mission altitude while waiting for the visual-target gates. |
| `LANDING` | The target lock has remained valid for 2 s; ArduPilot owns descent and target-loss handling. |
| `COMPLETE` | Publish completion and restore PEARL's saved control state. |
| `ABORT` | Disable dedicated guidance and use the existing secondary-recovery policy. |

The UAV still requires fresh navigation and appropriate flight/health telemetry.
Stale positions, invalid platform data, mismatched sessions and timeouts remain
fail-closed. Low valid UAV battery raises recovery priority rather than rejecting
the aircraft; takeoff has its separate reserve gate.

## Explicit sharing behavior

pShare output directives and uFldNodeBroker bridge directives can explicitly
select a per-route behavior and rate. There is **no destination-wide 1 Hz
policy** and no payload-specific interpretation of targets, graphics, clears,
or AppCasts. The 21 `UAV_LANDING_TARGET_*` shore bridges explicitly use
`frequency=2,behavior=latest` (up to 42 publications/sec across the fields).
Additional approved shore policies are: altitude (`NAV_ALTITUDE` and
`UAV_ALTITUDE_MSL`) latest at 5 Hz; all exported `VIEW_*` graphics coalesce at
1 Hz; simulator/received-view flags, landing status, Helm state and check
snapshots coalesce at 1 Hz; simulator distance latest at 2 Hz; UAV node reports
latest at 4 Hz (matching the nominal unwarped producer rate); detailed ALFA
signal/bitrate/retry counters latest at 1 Hz. ALFA
link/validity/age fields remain unrestricted; hardware rates are unmeasured.
See [telemetry_candidates.md](telemetry_candidates.md) for the exact groups.
Unlisted exports retain their previous policy. Native local target
telemetry, local landing checks/received-footprint reconstruction, peer traffic,
and the target broadcaster's `publish_rate_hz = 10` are unchanged. Each field
has its own rate clock; the shore exports are not an atomic target snapshot.

```moos
// pShare, static or PSHARE_CMD dynamic output:
output = src_name=STATUS,route=localhost:9200,frequency=1,behavior=coalesce

// uFldNodeBroker passes these options to pShare:
bridge = src=STATUS,alias=STATUS,frequency=1,behavior=coalesce
```

- `forward` (default without a frequency): sends every message.
- `drop`: deliberately ignores any message arriving inside the rate period.
- `coalesce`: ignores only identical repeats inside the period. Every changed
  payload passes immediately, including a one-shot clearing command after a
  draw. Equality includes opaque data/type/source metadata, excluding only
  transport timestamp and message ID. Thus fresh observations with unchanged
  values can be suppressed **only when the owner opts in**.
- `latest`: explicitly retains only the newest pending message inside the
  period. It is sent at the previous-send deadline even if no more mail arrives.
  Updating the pending value does not slide the deadline. **It can discard
  intermediate changed values and visual clears**, so use only when the owner
  declares those latest-state semantics safe; never apply it blindly to a
  shared VIEW_* key containing multiple objects.

Frequencies use MOOS time by default, including the configured time warp;
`rate_clock=wall` selects monotonic real time for an individual route.
Forwarding and pending sends preserve original source timestamps. `frequency`
and shorthand `@Hz` keep their existing lossy/drop semantics when no behavior
is specified. Defaults are not changed by the broker. Timing is enforced in
pShare, not the broker or the target producer. Ordinary UDP delivery is still
unacknowledged.

The former implicit shore coalescer has been removed. The pre-change inventory
and removal proposals remain in [telemetry_inventory.md](telemetry_inventory.md)
and [telemetry_inventory.csv](telemetry_inventory.csv); no proposed topic
removals were applied. Configuration/binary changes take effect at the next
safe normal relaunch, not by restarting an airborne mission. Isolated
regressions are in `moos-ivp/MOOS/MOOSEssentials/Essentials/pShare/tests`.

## Normal UAV route guidance

The `operator_route_` waypoint template uses `lead=5` metres and
`lead_to_start=true` to follow a look-ahead point on the current leg, including
the initial leg. `lead_damper` remains unset. Lead is capped at the current
waypoint: it does not anticipate a turn onto the following leg or slow the UAV.

`end_spd = slow_dist=8, stop_dist=0` linearly reduces desired speed over the
final 8 metres of remaining route distance. At 3 m/s cruise, 6 m remaining
requests 2.25 m/s. Capture/slip remain the current 4/5 m fly-by settings.

Only the normal route's final capture posts `BRAKE_AND_HOLD`. Existing
`pArduBridge` takes ownership from Helm and streams zero horizontal Guided
velocity while preserving the native relative altitude at the request. It
latches a native position hold only after three distinct post-request FC
`GLOBAL_POSITION_INT` samples show horizontal NED speed <=0.15 m/s. Coherent
position/velocity receipt age must be <=0.5 real seconds; connected, fresh
armed/IN_AIR and existing Guided evidence are required. Duplicate FC timestamps
never renew freshness or count as stopped evidence. No five-second sleep or
new MOOS application is involved.

`UAV_BRAKE_HOLD_PHASE`, `_REASON`, `_SPEED`, `_MOTION_AGE`, `_SAMPLES` and `_ID`
(and the bridge AppCast) expose progress and blocking facts, on change with
1 Hz diagnostic refresh. `HOLD_TARGET_SENT`
means a position packet was queued after measured stopping, not a MAVLink ACK
or proof of FC execution. A 10-real-second timeout, stale/invalid telemetry or
send failure reports failure and suppresses old Helm replay; it never installs
a stale hold. Explicit LAND/RTL/deadman/park and newer guidance cancel the old
transaction. Some forward braking travel is expected: the captured hold is
where the aircraft stopped, not where braking was requested. Precise endpoint
arrival still depends on final-approach guidance.

Rendezvous, secondary guidance, intermediate points and PEARL retain their
existing behavior/`HOLD_POSITION` commands. Apply through a safe normal relaunch;
do not restart an airborne bridge.

Opt-in physical regression (fresh owned landing-mission copy, independent ports,
no recovery request or pokes to running missions):

```bash
python3 tests/test_route_brake_sitl.py
python3 tests/test_route_brake_sitl.py --legacy
```

At warp 1, the isolated 3 m/s / 4 m capture / 8 m slowdown baseline reversed
2.155 m after entering hold at 2.68 m/s. Brake-then-hold entered at 2.72 m/s,
queued its hold after native speed fell to 0.06 m/s, and reversed only 0.044 m.
Relative-altitude deviation was 0.011 m and yaw deviation 0.102 degrees during
that stop. Artifacts: `/tmp/route-brake-native-oxrxztve` and
`/tmp/route-brake-legacy-l0vl4jpo`. A repeat with the final binary
(`/tmp/route-brake-native-6g92ntj7`) also reversed 0.044 m, with native speed
0.03 m/s when the hold was queued. This isolates stopping physics; it does not
certify every full-FSM recovery/preemption combination or exact endpoint arrival.

This uses the upstream post-24.8 `BHV_Waypoint` feature described in the
[development notes](https://oceanai.mit.edu/moos-ivp/pmwiki/pmwiki.php?n=Memo.DevNotesByMod)
under `250810 VERY-MENU`. These settings are an initial trial, not validated
flight-performance guarantees. Apply through regenerated configs at the next
safe normal launch; no live airborne guidance is changed.

## Reliable versus periodic transport

`pMediator` carries the actions that must survive an occasional dropped UDP
datagram:

- UAV-directed operator ARM, DISARM, TAKEOFF, route DEPLOY, and route CLEAR;
- UAV takeoff request, PEARL takeoff clearance, UAV rendezvous request, PEARL
  recovery-ready response, measured motion estimates, UAV completion, and PEARL landing clearance;
- acknowledgements and retries at one-second mission-time intervals, up to ten
  retries.

Mouse clicks remain shoreside-local until DEPLOY sends one complete route
snapshot. The staged path is solid orange and marked `route (uncommitted)`;
a successful submission turns it yellow and removes that text. Editing again
restores the orange preview until resubmission. On the UAV, `pRouteBuffer`
stages each snapshot on
`ROUTE_UPDATE` and raises `ROUTE_EXECUTE` in both SITL and REAL. The shared
`uRouteSequence` waits for `ROUTE_READY`, armed, and `IN_AIR` feedback before
setting `ROUTE_DEPLOY` for the Helm. Operator snapshots may be staged in IDLE,
TAKEOFF_OPERATIONS, or MISSION_EXECUTION, but execute only in MISSION_EXECUTION.
Only `pRendezvous` may submit `purpose=recovery` snapshots, which stage and
execute in LANDING_OPERATIONS. A rejected operator snapshot cannot overwrite an
active recovery route. `pArduBridge` performs the Helm handoff and commands
`FLY_WAYPOINT` in both UAV modes. PEARL-shoreside
task, manual-control, abort, and telemetry traffic uses the bidirectional field
broker rather than `pMediator`. Periodic state such
as node reports, health, rendezvous status, and navigation uses ordinary
broker/pShare routing. RealmCast bridging is disabled.

Mediated messages and ACKs are dispatched **only on the sending side** by
`pMediatorRouter` onto `MEDIATED_TX_<DEST>` and `ACK_TX_<DEST>`. pShare sends
each channel directly to its named recipient's native `MEDIATED_MESSAGE` or
`ACK_MESSAGE` inbox. The receiving pMediator consumes it directly; no incoming
dispatcher pass, `*_IN` transport channels, or transit forwarding is used.
Relay is disabled on UAV, PEARL and shoreside. Source-wide protocol bridges,
mediated/ACK qbridges, brokered protocol bridges and ACK-name echoes are absent;
pShare never parses payloads.

All six directed message/ACK paths are static and direct, including
shoreside-to-PEARL. They do not wait for broker enrollment. The vehicle launcher
`DIRECT_PEER` flag now controls only the existing optional peer telemetry and
authority facts; it never switches mediated traffic through shore. Peer hosts
default to localhost for local simulation. Distributed deployments must set
`--pearl_ip` on UAV, `--uav_ip` on PEARL, and both `--uav_ip`/`--pearl_ip`
on shoreside, with their matching `--*_pshare` ports. The whole-mission launcher
passes both vehicle ports to shoreside. There is no central protocol relay,
route discovery, or automatic alternate-path selection.

Static paths are not remote-readiness evidence. A peer may still be starting
or disconnected when a request consumes pMediator's retry budget. Producer-side
startup readiness gating remains separate work; no arbitrary startup sleep or
retry-budget increase was added.

The dispatcher preserves raw payloads, IDs, ACK policy and timestamps. Retries,
duplicate suppression and receipt tracking remain in pMediator. Unknown or
ambiguous destinations fail closed; protocol channels have no rate policy.
Node-report and other telemetry routing, safety/FSM topology and approved
sharing rates are unchanged. See
[`pMediatorRouter`](../../../src/pMediatorRouter/README.md) for interfaces,
limitations and isolated all-direction/lost-ACK/cancellation regressions.
Changes apply on the next safe normal launch, never a live airborne restart.

PEARL and the UAV intentionally use different platform groups, so
`uFldNodeComms` group filtering is disabled for this mission; otherwise PEARL
would not receive the UAV reports required to grant clearance.

## Operator interface

UAV controls:

- `UAV TASK`: send `TASK_EXECUTE=true` to the UAV and submit the selected route.
  After readiness checks and PEARL clearance, the UAV coordinator arms and
  commands takeoff; route traversal waits for airborne telemetry. `PEARL TASK`
  remains a separate button.
- `UAV DISARM`: remote disarm remains for safety. No shoreside arm/takeoff
  buttons exist; the on-board coordinator commands them only after checks and
  PEARL clearance.
- `uav-clear`: right-click context action that clears the staged UAV route and
  cancels an active task. The standalone `UAV DEPLOY` and `UAV CLEAR` buttons
  are removed; `UAV TASK` still deploys the selected route.

PEARL's `pTakeoffProcessor` draws a red, 6 m-diameter circle centered on
`NAV_X`/`NAV_Y` while a request is active. Its outline is dashed while checks
are pending, solid after clearance is granted, and removed when the request is
released, denied, or canceled. It refreshes once per second and is visual only;
it does not change StationKeep's 0.25/0.5/0.75 m control radii.

A condition edge in `pTakeoffCoordinator` starts an ID-tagged clearance request
from the UAV's requesting-clearance state while grounded. It retries that same
ID every five seconds until a reply, cancellation, or timeout. PEARL's
`pTakeoffProcessor` publishes the received ID, verifies session-matched
`uChecks` results, denies concurrent requests, and times out unanswered ones.
It grants only after both PEARL check collections succeed for the same request:
GPS/battery/wind prechecks and a post-interruption droneport-bay check requiring
`PEARL_DRONEPORT_BAY_COVER_STATE=open`. The receipt remains busy until PEARL
returns to normal operation. The coordinator cancels before arming if its
conditions cease to hold and ignores late grants. PEARL's GPS, battery,
and wind checks remain active through pre-interruption, establishing conditions,
and post-interruption checks. A failure before clearance denies the request;
pre-interruption failures return support activity to `NO_ACTIVE_REQUEST`;
a withdrawn or failed request during takeoff support enters
`CANCELLING_BEFORE_CLEARANCE`. The processor does not retract a clearance
already granted if a later check fails.
The UAV enters `TAKEOFF_OPERATIONS` on its own `TASK_EXECUTE=true`.
`uPretakeoffChecks` publishes individual UAV readiness checks and aggregate
`TAKEOFF_PRECHECKS` locally for the FSM's readiness transition. Each entry into
`TAKEOFF_OPERATIONS` publishes
`UAV_FSM_OPERATION_STATE_TAKEOFF_OPERATIONS_EPOCH`, which is the readiness
collection's session ID. A new ID clears its held results and failure latches;
the scalar FSM guards do not compare sessions, so an old result can still be
observed briefly before `uChecks` receives the new ID. The same collection
stays active through clearance; the coordinator verifies its scalar status and
session against the entry epoch before arming. Its distinct
`UAV_TAKEOFF_REQUEST_ID` still identifies the mediated PEARL clearance request.
Before first
activation statuses are `inactive`; age-limited checks retain their last
result after deactivation until a required input expires. Flight-plan input
has no age limit: the Helm's `ROUTE_READY` spawnflag remains valid until changed.
On each entry to TAKEOFF_OPERATIONS, the FSM posts `MOOS_MANUAL_OVERRIDE=false`
to wake the Helm's planning loop after landing parks it. This is an entry effect
in `uav_fsm.toml`, not a separate timer or a continuously asserted condition.
It lets successive routes spawn and publish `ROUTE_READY` without activating
`pArduBridge`: `HELM_STATUS` and flight-control handoff remain gated by
`uRouteSequence` until takeoff completes. A subsequent manual override is not
cleared again unless a new TAKEOFF_OPERATIONS entry occurs.
GPS requires a fresh 3D fix within 60 seconds and a continuous 5-second hold;
flight-plan receipt has 60 seconds; low or invalid battery fails immediately
(missing data gets 5 seconds). The armability check requires fresh flight-controller health telemetry,
`UAV_IS_ARMABLE=1`, disarmed state, `UAV_LANDED_STATE=ON_GROUND`, and landed
telemetry no older than 3 seconds. Each named check has typed requirements,
per-variable immediate-fail or timeout policies, and an optional continuous
success hold. Readiness succeeds only when all six named checks succeed; one
failure follows the existing aborted takeoff path. The coordinator publishes
`TAKEOFF_REQUEST_RESULT` (`PENDING`, `APPROVED`, `REJECTED`, `CANCELLED`, or
`TIMEOUT`), ID-tagged `TAKEOFF_REQUEST_STATE`, and
`TAKEOFF_REQUEST_REASON`. It waits for PEARL's grant *and* the UAV `uChecks`
result before sending `ARM_UAV=true`; after `UAV_IS_ARMED=1` telemetry it
sends `ARDU_COMMAND=DO_TAKEOFF`. Once arming begins it no longer gates on
prechecks. It repeatedly sends ID-tagged `armed`, then `taking_off` and finally
`takeoff_complete` progress on the existing request channel. Completion is
retried for 60 seconds; PEARL accepts late progress for the just-released grant
for 120 seconds. PEARL publishes progress separately as `UAV_TAKEOFF_PROGRESS`
and pShare bridges it to shoreside. The processor advances its local
`TAKEOFF_REQUEST_STATE` from `granted` to `complete` on confirmed departure;
the PEARL FSM accepts either for the earlier clearance transition and requires
`complete` before leaving takeoff support. `uOperatorPrecLand` still posts
`ARDU_COMMAND=AUTOLAND` after landing clearance. ArduPilot supplies armed,
altitude, landed, and mode feedback in both SITL and REAL.
The PEARL processor does
not hard-code battery or wind thresholds: the PEARL
`uChecks` configuration owns them. Each collection publishes a JSON snapshot
of its session, activation, observed inputs, configured requirements, check
statuses, and timers at 1 Hz while active; the four demo snapshots are routed
to shoreside. PEARL's local default snapshot names are aliased to
`PEARL_TAKEOFF_PRECHECKS_SNAPSHOT` and `PEARL_TAKEOFF_POSTCHECKS_SNAPSHOT`
on shoreside. PEARL's pre-recovery snapshot is aliased to
`PEARL_RECOVERY_PRECHECKS_SNAPSHOT`. The UAV keeps
`TAKEOFF_PRECHECKS_SNAPSHOT` locally and aliases it to
`UAV_TAKEOFF_PRECHECKS_SNAPSHOT` on the shoreside bridge. These are diagnostic,
not alternate authorization facts.

PEARL also runs session-scoped `uChecksRecovery` while its support FSM is in
`RECOVERY_PRE_INTERRUPTION_CHECKS`. It requires valid GPS, battery, and wind
inputs plus `PEARL_DRONEPORT_BAY_COVER_STATE=open`; the recovery FSM cannot
advance to `CALCULATING_RENDEZVOUS` until the aggregate succeeds for the current
`PEARL_RENDEZVOUS_SESSION`. This is a passive telemetry check: entering recovery
never posts an opening command and does not activate the takeoff cover mock.
Takeoff-support Helm controls are not
snapshotted by this exchange; their binding to authoritative PEARL states and
post-interruption support lifecycle remain separate work. In SITL, the PEARL
simulator supplies a GPS fix, 80-percent battery, and 2 m/s wind for PEARL
prechecks. A timed mock reports the droneport bay cover `opening` on entering
takeoff support and `open` ten seconds later. With no physical cover installed,
this mock runs in both SITL and REAL, but only while establishing takeoff
conditions; recovery
merely observes the resulting cover state.

Choose the `route` mouse context and click an ordered route. `UAV TASK` sends
the stored snapshot, waits for independent prechecks and coordinator-driven
arming/takeoff feedback, then uses short deliberate delays before route release
and rendezvous request so their FSM states
remain visible in the UI. Every stage still waits for authoritative feedback,
and route execution remains inhibited until `UAV_LANDED_STATE=IN_AIR`. When the
route behavior completes, the UAV posts `RENDEZVOUS_START` locally after the
presentation delay. During
coordinated landing-target acquisition, `pRendezvous` automatically maintains
a one-point route to PEARL's fresh position. This is not a separate operator
button.

PEARL controls:

- `PEARL TASK`: sends `TASK_EXECUTE=true` directly to PEARL through the
  addressed field broker after tasking has been prepared by its owning component.
- `PEARL ALLSTOP`: sets `PEARL_ALLSTOP=true` and clears manual override,
  suspending the PEARL FSM with deep history while the existing autonomous
  StationKeep behavior holds position. `PEARL TASK`, `PEARL RETURN`, and
  `PEARL STATION` clear the hold and resume the FSM.
- `CLOSE PORT`: sends the addressed telemetry spoof
  `PEARL_DRONEPORT_BAY_COVER_STATE=closed` to the PEARL community. This supports
  the current mock setup; it is not an actuator command, and there is
  intentionally no matching manual open button.

Rendezvous starts automatically from the feedback-gated UAV task sequence; no
standalone `RENDEZVOUS` or `RNDV ABORT` buttons are exposed.

The intended operator sequence is:

1. Select the UAV route with the `route` mouse context.
2. Press `UAV TASK` once.
3. Monitor `UAV_TASK_ACTIVE`, `UAV_FSM_OPERATION_STATE`,
   `UAV_RENDEZVOUS_STATE`, and `PEARL_RENDEZVOUS_STATE`.
4. Let the coordinated workflow request precision landing, choose `uav-clear`
from the right-click menu, or press `UAV DISARM` as appropriate.

The task sequencer remains feedback-gated: presentation delays do not replace
its checks. The takeoff coordinator requires PEARL clearance and successful
UAV prechecks before arming, then waits for armed feedback before takeoff;
route execution waits for
`UAV_LANDED_STATE=IN_AIR`, and rendezvous waits for a route that was observed
active to finish. A new autonomous task may start only from `IDLE`. After a
secondary landing reaches terminal `OFFNOMINAL`, every later `UAV_TASK_REQUEST`
is cleared and reported as `UAV_TASK_REJECTION=OFFNOMINAL_LOCKOUT`; the UAV
cannot be autonomously re-tasked without restarting its community.

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

In SITL, REAL, and GRASS modes the same pre-commit gate also requires PEARL
process health, fresh valid PEARL battery and wind telemetry, at least 15%
PEARL battery, and wind no greater than 4 m/s. SITL and GRASS use the shared
synthetic platform-input script for battery and wind; REAL uses live PEARL
telemetry. GRASS still obtains GPS and UAV-relative facts from physical inputs.
These conditions are evaluated again during target acquisition; passing the takeoff gate is not treated as
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

## SITL deck attachment

SITL and REAL keep ArduPilot authoritative for UAV navigation. In SITL,
PEARL's `uSimMarineV22` remains authoritative for PEARL's Helm-driven trajectory.
The UAV community runs two independent C++ MOOS apps. `pMavlinkShipBridge`
takes `PEARL_NAV_*` from PEARL's navigation-only pShare routes and sends that
trajectory as MAVLink 2 `GLOBAL_POSITION_INT` (system 17) to TCP
`127.0.0.1:5763`. `pMavlinkLandingTarget` independently connects to that same
multi-client simulation port, using PEARL position and native UAV telemetry via
`pEchoVar`. The SITL Compose stack routes both clients through **mavp2p v1.3.3**,
the version observed on landmark-1.
Neither app sends flight-control commands or overwrites UAV navigation.

The ArduPilot Docker build applies `patches/external-ship.patch` to its pinned,
clean upstream submodule. With `SIM_SHIP_ENABLE=1` and `SIM_SHIP_EXT=1`,
`SIM_Ship` uses incoming system-17 position, velocity, and heading rather than
its circular trajectory. While grounded, ArduPilot forces the UAV to the ship
center at deck height plus frame clearance and applies ship velocity/yaw.
There is **no external-mode distance filter**; `SIM_SHIP_DSIZE` applies only to
the internal ship model. Airborne XY and takeoff detachment remain ArduPilot
physics, not MOOS `NAV_*` overrides. Fresh poses establish the deck landing
surface (including ground-facing sensor readings) and carry an already grounded UAV with upward/downward height changes.
External pose loss for more than one simulated second disables this coupling.

The bridge sends ship poses only while the MOOS fact `SIM_UAV_ON_SHIP=true`.
FSM entry/exit effects in `uav_fsm.toml` own this policy: entering Idle on deck
sets it true, leaving Taking off sets it false, and entering the primary
approach sets it true again (remaining latched through landing and target retries).
Secondary landing/loiter diversion and recovery entries set it false, so a
completed Rally landing remains detached. There are no ship-coupling timers;
only the SITL bridge consumes this intent. Change the Idle entry effect for an
off-deck start. Between state entries, an operator can also set the fact with
`uPokeDB targ_uav.moos SIM_UAV_ON_SHIP=false`. Stop poses before off-deck
landing, allowing the one-second SITL expiry window. If the fact is incorrectly
left true, **any** grounded UAV will intentionally be moved to the ship.

PEARL defaults to **42.356424, -71.098393**, corresponding to local UTM
`51.033305,-56.215822,0` (heading unchanged at 0°). The map datum and
loiter center remain unchanged. Override the start with
`./launch.sh --pearl_start=X,Y,H` or `./launch_pearl.sh --start_pos=X,Y,H`. The grounded UAV is attached to
that ship position; PEARL is not relocated. Configure the deck's absolute MSL
altitude with `ship_altitude_msl` in `meta_uav.moos` (default 0 m MSL, not height
above SITL home). For live height/heave input, set `ship_altitude_var` in that
block and refresh it within `input_timeout`. Match the independent landmark
app's `target_altitude_msl` to that deck height, or configure the same live
variable as `target_altitude_var`. Deck roll/pitch are not modeled.

Time warp above 1 is supported. In SITL mode, `launch_uav.sh`
automatically passes the mission warp to the simulator as **SITL speedup**:

```bash
./launch.sh --mode=SITL 5
```

Use the same factor for split community launches, for example
`./launch_uav.sh --mode=SITL 5`. Freshness and publish periods are in simulated
seconds; CPU/network capacity must sustain the accelerated update rates.

`pMavlinkLandingTarget` independently emulates the landmark camera: it sends
`LANDING_TARGET` at 10 Hz only while fresh UAV/PEARL navigation is within the
configured **3-D** detection range (12 m default). It uses the UAV's attitude
yaw—not ground course—and MSL altitude to encode body-forward/right/down
position. The camera model assumes level roll/pitch. Inputs older than one
second or separation beyond the range stop target transmission. ArduPilot's
precision-landing target-loss policy then owns recovery.

Landmark range (`detection_range=12`), deck height, and takeoff altitude (8 m)
remain in their respective MOOS/BHV app configs, not launcher arguments. Edit
`pMavlinkShipBridge` in `meta_uav.moos` for attachment, and
`pMavlinkLandingTarget` for visual range/geometry. Each has independent
connection, rate, source IDs, and freshness settings. Three host TCP ports are
exposed: **5760** directly for pArduBridge, **5762** directly for ground control,
and **5763** through the multi-client simulation router. The router uses the
existing private SERIAL2 link; GPS SERIAL3/4 are unchanged. `SITL_TARGET_PORT`
overrides the shared simulation port and is propagated into both producer
configs. The launcher waits for its owned simulator and router before ANTLER;
cleanup removes both. REAL launch does not start either simulation component.
The native takeoff altitude is in `plug_uav_ardupilot.moos`; route altitude is
in `meta_uav.bhv`. Secondary recovery coordinates are supplied to
`meta_uav.moos` with `--secondary_landing=X,Y` (default `10,15`). Bridge state is posted
as `MAVLINK_SHIP_BRIDGE_CONNECTED`, `MAVLINK_SHIP_POSE_ACTIVE`,
`MAVLINK_LANDING_TARGET_CONNECTED`, `LANDMARK_EMULATOR_ACTIVE`, and
`LANDMARK_EMULATOR_DISTANCE`. REAL mode does not run this emulator.

Regression checks: `python3 tests/test_ship_coupling.py` verifies the actual
mission-owned coupling policy across primary/secondary recovery cycles;
the SITL repository's `make test-deck` validates physical deck following, stale release,
and two isolated takeoff/landing cycles (not precision-landing guidance).

## Layout and launch modes

- Map: `MIT_SP.tif`
- Datum: `42.358436, -71.087448`
- Shoreside: MOOSDB `9000`, pShare `9200`
- UAV: MOOSDB `9001`, pShare `9201`
- PEARL: MOOSDB `9002`, pShare `9202`

The UAV supports two modes:

- `SITL`: UAV ArduCopter SITL and simulated PEARL
- `REAL`: UAV and PEARL hardware

The FSM minimum dwell defaults to zero. For slowed state visualization, pass it
through the top-level launcher to both vehicle runtimes:

```bash
./launch.sh --mode=SITL --min_state_time=20
```

Generate targets without launching:

```bash
./launch.sh --mode=SITL --just_make --nogui 5
```

For SITL, launch the mission normally; no separate simulator terminal is needed:

```bash
./launch.sh --mode=SITL
```

In SITL mode, `launch_uav.sh` starts the simulator through `launch_sitl.sh`
with the mission time warp, waits for its owned container and controller TCP
endpoint, then launches the UAV community with `pAntler`. `meta_uav.moos`
launches `pArduBridge` normally in both modes; no bridge wrapper or simulator
entry is present in ANTLER. REAL never starts Docker. `--just_make` only
generates configuration; it does not start the simulator.

Startup readiness has a 60-second wall-clock deadline (`SITL_STARTUP_TIMEOUT`
may override it). An autopilot heartbeat must still arrive within three seconds
of the bridge connecting. Startup failures are reported in `LOG_sitl/*.log`;
fix the error and relaunch. A startup failure or timeout prevents the UAV
community from launching. A background cleanup watcher survives `--auto` and
removes the private Compose project on normal mission shutdown or UAV ANTLER
exit. Simulator logs are collected before removal; bridge output is handled
normally by ANTLER. SIGKILL cannot run this cleanup.

Do not also launch a manual simulator on the same ports: the launcher creates
its own isolated Compose project, and port conflicts fail rather than adopting
or stopping another simulator. `launch_sitl.sh` remains available for standalone
simulator diagnostics, but is no longer a prerequisite for mission launch.

`launch_sitl.sh` delegates to the shared `moos-ivp-pearl/scripts/launch_ardupilot_sitl.sh`
helper, which passes optional ordered drone → mode → mission files to
`ardupilot-sitl` via repeated `--param-file`. Defaults are:

| Layer | File | Purpose |
|---|---|---|
| Drone | `../../../config/ardupilot/drones/x500.parm` (default model `x500`) | Physical downward sensor: minimum 0.15 m, maximum 12 m, clearance 0.17 m; installed hardware driver preserved |
| Mode | `sitl.parm` or `real.parm` | SITL backend 100 overrides minimum/clearance to **0/0**; REAL inherits physical values |
| Mission | `mission.parm` | Shared precision policy, including `PLND_ALT_MIN=0`, `PLND_ALT_MAX=4`, `PLND_STRICT=2` |

Mode files are mission-local/selectable so water-operations ship simulation
settings do not silently become universal settings for unrelated missions.
Drone profiles are reusable and selectable; no host-side MAVLink package is
required (the uploader uses Python's standard library). Missing default role
files are omitted. Explicit paths must exist; use `none` to omit a role:

```bash
./launch.sh --mode=SITL --model=x500
./launch_uav.sh --mode=REAL --model=x500 --just_make
./launch.sh --mode=SITL --model=x500 --drone-params=/path/to/drone.parm
./launch_uav.sh --mode=SITL --mode-params=none --mission-params=/path/to/mission.parm
```

`--model=NAME` on both `launch.sh` and `launch_uav.sh` selects
`config/ardupilot/drones/NAME.parm`; default **x500**. Names are case-sensitive,
start with a letter/digit and otherwise contain only letters, digits, `_`, or
`-` (no path or `.parm` extension). An explicitly selected missing model is an
error before simulator/MOOS startup, unless an explicit `--drone-params` or
`DRONE_PARAM_FILE` override supplies/omits that layer. Such overrides take
precedence regardless of CLI argument order. Model selection chooses **only
the parameter profile**, not SITL airframe physics, vehicle name, FC identity,
readiness or permission to upload. Layer order remains drone → mode → mission.

These three profile options also work on `launch_uav.sh`. For standalone
`launch_sitl.sh`, use `DRONE_MODEL=x500` to select a model, or `DRONE_PARAM_FILE`,
`MODE_PARAM_FILE`, and `MISSION_PARAM_FILE` for explicit role files; empty/`none`
file values omit a role. `DRONE_MODEL` also supplies the mission-launcher default;
`--model` overrides it, but explicit drone-file overrides still win.
Shared helpers accept arbitrary repeated `--param-file` options in caller order;
extra standalone SITL files follow the default three layers. Preview effective
values and winning file/line provenance without opening an FC connection:

```bash
python3 ../../../scripts/ardupilot_params.py \
  --param-file ../../../config/ardupilot/drones/x500.parm \
  --param-file real.parm --param-file mission.parm
```

#### Explicit REAL upload

Ordinary launch and `--just_make` do not write live parameters. Explicitly
request automatic verified upload **before MOOS/bridge startup**:

```bash
./launch_uav.sh --mode=REAL --upload-params \
  --ap_protocol=serial --ap_url=/dev/ttyACM0:115200
# Or select an existing trusted TCP MAVLink endpoint:
./launch_uav.sh --mode=REAL --upload-params \
  --ap_protocol=tcp --ap_url=127.0.0.1:5760
```

Select native FC IDs with `--param-system-id` and `--param-component-id` (both
default 1). An endpoint/profile name is not authenticated aircraft identity.
Close other serial readers first: exclusive open does not evict a process that
already held the device. UDP endpoints are **local bind** host:port, like the
bridge, not remote destinations; a native FC heartbeat selects the peer. Do
not share an active bridge UDP listener. CRC is checked; signatures are not
authenticated, so use trusted links.

The uploader requires fresh native ArduPilot heartbeat (disarmed) and native
`EXTENDED_SYS_STATE=ON_GROUND` throughout. It requests that message with command
512, reads/types/checks the entire effective stack before mutation, then uses
ArduPilot C-cast PARAM_SET, consumes its echo, issues separate readback requests,
and checks the whole stack again. No ARM, flight-mode, mission, reboot or
estimator-reset commands are sent. Unknown parameters, wrong source, invalid
native type, missing/stale ground/disarm, mismatched readback or an observed
arming/airborne transition stop launch. REAL rejects SIM_* entries, rangefinder
backend 100 and zero sensor minimum/clearance. A selected rangefinder profile
requires an already installed nonzero, non-SITL hardware backend.

Known boot-instantiated differences (`PLND_ENABLED/TYPE/ORIENT`, rangefinder
TYPE/ORIENT and SERIALn_PROTOCOL) are **refused before any writes**. Provision
those manually, perform any required reboot, then rerun. This avoids mistaking
stored-but-not-active driver settings for success on a second invocation. New
parameters still require review of their own reboot semantics. Readback proves
configuration only—not active sensor health, precision acquisition, GPS
readiness, landing clearance or arming permission. Existing native/FSM checks
remain authoritative.

Audit files in `LOG_params/` preserve effective values/provenance, initial values,
attempted and verified writes, final readback, and errors. Upload is not atomic:
failure can leave earlier values applied. There is **no automatic rollback**;
review the snapshot and restore deliberately while grounded/disarmed. Without
`--upload-params`, REAL warns that this launch did not verify configuration.
Retry-exhaustion → RTL remains a separate pending policy; these files do not
implement it.

Ground-only validation covered native ordered startup and the uploader with an
isolated analog-backend SITL fixture. It did **not** contact hardware or validate
flight/target loss. Reproduce explicitly:

```bash
RUN_PARAM_STACK_SITL=1 python3 ../../../scripts/tests/test_param_stack_sitl.py
```
 Rebuild the Docker image after
installing the external-ship patch, and build
`pMavlinkShipBridge`, `pMavlinkLandingTarget`, and `pArduBridge` before launching. Use fresh SITL state: persisted parameter values
can override defaults. `PLND_TYPE=1` selects the MAVLink target backend and
`SIM_PLD_ENABLE=0` disables the competing stationary target simulator.

SITL uses PEARL's simulated battery, wind, and process-health telemetry to
exercise the same pre-commit platform gate enabled in REAL.

`launch.sh` is the local SITL-only orchestrator. Physical REAL and GRASS
operation uses the sublaunchers independently:

The deployed Alfa topology is routed through Sherlock rather than attaching
both radios directly to the two mission computers. See
[`RADIO_BACKHAUL.md`](RADIO_BACKHAUL.md) for provisioning, routes, activation,
and verification, and [`OAK_VIDEO.md`](OAK_VIDEO.md) for the independent
Tailnet camera path.

```bash
# Shoreside
./launch_shoreside.sh --auto --mode=REAL \
  --ip=100.127.231.65 \
  --uav_ip=100.70.189.91 --uav_pshare=9201 \
  --pearl_ip=100.69.111.61 --pearl_pshare=9202

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

For the manually moved grass test, keep the UAV and shoreside in REAL and start
only PEARL in GRASS:

```bash
./launch_pearl.sh --auto --mode=GRASS \
  --ip=100.69.111.61 \
  --shore=100.127.231.65 --shore_pshare=9200 \
  --uav_ip=172.22.90.2 --uav_pshare=9201
```

GRASS runs the unchanged FSM, takeoff/recovery checks, rendezvous planner, and
landing coordinator. It uses the physical `/dev/ttyUSB0` receiver through
`iDualGPS` with `DUAL_GPS=false`. The legacy GPS application is deliberately
not aliased because its entry point does not consume pAntler's plain alias
argument. GRASS does not run `pHelmIvP`,
`pRouteBuffer`, `uSimMarineV22`, PID, or thrust mixing. The missing route buffer
means the main-activity transition cannot receive a legitimate `ROUTE_READY`,
while support-state coordination remains available. PEARL's `NODE_REPORT` is
sent directly to the UAV so both rendezvous planners continue to converge near
the stationary platform. The SITL-only `PEARL_NAV_*` target-emulation feed is
not sent.

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
addresses through the routed backhaul. Mediated message and ACK outputs are
always direct; omitting peer hosts defaults to localhost for local simulation,
not a shore relay. Set the peer hosts explicitly for distributed REAL operation.
Supplying vehicle peer hosts also enables the existing optional direct peer
telemetry outputs. Shoreside needs both vehicle hosts and ports for its static
protocol paths. These addresses and ports are launch-time inputs, not hardcoded
Wi-Fi configuration.

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

The simulated PEARL used in SITL mode runs `pHelmIvP`, `pMarinePIDV22`,
`pThrustMixer`, and `uSimMarineV22` in differential-thrust mode. The mixer
converts the PID's conventional thrust/rudder output into left/right motor
commands using the same 100% thrust and 50% rudder limits as `iPEARL`. Opposed
motor commands therefore rotate PEARL in place instead of forcing it through a
rudder-model turn.

## Cleanup

```bash
./clean.sh
```
