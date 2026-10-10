# pRallyPoint: staged selection, verified task commit

This application does not command flight modes or claim waypoint arrival.
The FSM retains flight lifecycle ownership; Helm owns secondary waypoint
arrival; `pArduBridge` owns the actual flight-controller transaction.

## Shoreside

Configure `role=shoreside`, `destination_node=uav`, optionally
`initial_point=x=10,y=15` and `initial_altitude=15`.

- `RALLY_POINT`: string `x=…,y=…`, local MOOS coordinates in metres.
- `RALLY_ALTITUDE`: numeric whole metres **above flight-controller Home**;
  default **15**, accepted range 1–1000. Fractional/nonfinite/out-of-range
  values invalidate staging, rather than silently rounding.
- `RALLY_TASK_COMMIT=true`: snapshot coordinates and altitude into a unique
  ID-tagged `RALLY_TASK`, addressed using `NODE_MESSAGE_LOCAL`.
- `RALLY_STAGED_POINT/STATE/REASON/ID`: diagnostic output; orange unlabeled
  marker represents the editable **next-task** point, not proof of FC readiness.

A later edit never mutates an already transmitted snapshot. Node-message
loss does not fabricate acceptance or start flight; the operator must check
vehicle transaction state and may submit a new task if necessary.

## Vehicle

Configure `role=vehicle`. Accepts a new `RALLY_TASK` only with fresh
`UAV_FSM_OPERATION_STATE=IDLE`, numeric `UAV_IS_ARMED=0`, available native
`ON_GROUND` state and reported native state age ≤2 s. Input leases are 3 s.

Admission publishes `RALLY_UPLOAD` and `TASK_EXECUTE=true` immediately,
entering the existing TAKEOFF_OPERATIONS / ESTABLISHING_TAKEOFF_READINESS
states while verification is pending. Admission is not permission to fly.
Only a matching `UAV_RALLY_RESULT` with `status=READY`, still freshly
grounded/disarmed and either IDLE or establishing takeoff readiness,
commits `RALLY_ACTIVE`, `RALLY_WAYPOINT_UPDATE`, `RALLY_ALTITUDE_UPDATE`
and `RALLY_TASK_STATE=READY`. Verification never retriggers TASK_EXECUTE.
The prep FLIGHT_PLAN check requires both current `RALLY_TASK_STATE=READY`
and actual `UAV_RALLY_READY=1`, so retained readiness for an earlier FC
transaction alone cannot pass. Failure or the 60 s verification timeout
blocks readiness; the FSM owns the existing abort path. Duplicate IDs do not
retrigger admission. Vehicle outputs are `RALLY_TASK_STATE/REASON/ID`.

## Flight-controller contract

The bridge converts the frozen local point using configured MOOS geodesy.
It replaces the **rally** mission list with exactly one
`MAV_CMD_NAV_RALLY_POINT`, using `MAV_FRAME_GLOBAL_RELATIVE_ALT`, through
MAVSDK/MAVLink. It does not replace the flight-plan mission or write
`RALLY_TOTAL` manually. MAVSDK requires the one item's `current=1` flag even
for rally lists; ArduPilot ignores that flag for rally storage.

Readiness requires all of:

1. FC rally upload acknowledgement;
2. typed `MAV_MISSION_TYPE_RALLY` download/readback with exactly one matching
   point (coordinates to MAVLink integer precision and altitude to 0.1 m);
3. FC read-back `RALLY_INCL_HOME=0`, `RALLY_LIMIT_KM=0` (write if needed);
4. written/read-back Copter RTL altitude matching the snapshot (`RTL_ALT_M`
   in metres on current firmware; legacy `RTL_ALT` in centimetres), and
   `RTL_ALT_TYPE=0` for above-Home altitude;
5. fresh native ground/disarm evidence after the transaction.

`require_verified_rally=true` in `pArduBridge` additionally rejects ARM while
no verified rally exists. It defaults false for unrelated missions.
The installed MAVSDK lacks typed rally download, so `UAV_Rally.cpp` implements
only that bounded readback through MAVLink passthrough, not a flight-plan
fallback.

Integer policy parameters use fresh FC GETs, not cached parameter values.
An already-correct value needs no redundant SET. ArduPilot's default-value
save path can echo a grouped integer as REAL32 (AP_Param.cpp, save:
`info->type` versus the leaf parameter type); MAVSDK then reports Timeout
despite the correct numeric echo. After a required integer SET, a new typed
GET must verify the desired value while native ground/disarm evidence remains
fresh. Incorrect/missing readback, wrong type or loss of ground/disarm still
fails closed; neither a float echo nor Timeout grants readiness. This applies
to RALLY_INCL_HOME, RTL_ALT_TYPE and legacy RTL_ALT. The native rally upload
ACK and full rally/selection/altitude readback remain required.

For owned diagnostic runs, `MAVSDK_PARAMETER_DEBUGGING=1` enables MAVSDK
parameter traces on bridge stderr without restoring SDK's default discovery
logging path. `tests/test_rally_sitl.py --run-sitl --param-file PATH` accepts
repeatable ordered profile overlays; the prep's drone/SITL/mission profiles
reproduce the former failure and pass after this fix. No ArduPilot or MAVSDK
patch, safety-check relaxation, parameter retry inflation or flight command
is involved.

**Persistence and safety:** the rally list and selection/RTL parameters are
saved by the FC. Re-tasking replaces earlier rally points. Disabling the
rally-distance limit deliberately removes protection against a leftover
rally from another operating area. Excluding Home prevents closer-Home
selection while this valid single rally is present; it cannot prevent
ArduPilot's internal fallback if stored rally data later becomes unavailable
or invalid. These settings do not override geofences, EKF/GPS failures,
failsafe action selection, obstacles, pilot intervention, minimum RTL climb,
altitude/cone/fence limits, precision-land behavior, or landing safety.
A rally point is not terrain clearance or a validated landing site.

Official references:

- [Rally points](https://ardupilot.org/copter/docs/common-rally-points.html)
- [Copter RTL](https://ardupilot.org/copter/docs/rtl-mode.html)
- [RALLY_INCL_HOME](https://ardupilot.org/copter/docs/parameters.html#rally-incl-home-rally-include-home):
  “Controls if Home is included as a Rally point … for RTL”.
- [RALLY_LIMIT_KM](https://ardupilot.org/copter/docs/parameters.html#rally-limit-km-rally-limit):
  “If this is set to 0 then the closest rally point is always used.”
- [MAVLink mission protocol](https://mavlink.io/en/services/mission.html)
- [ArduPilot rally implementation](https://github.com/ArduPilot/ardupilot/blob/master/libraries/AP_Rally/AP_Rally.cpp)
- [Copter RTL path construction](https://github.com/ArduPilot/ardupilot/blob/master/ArduCopter/mode_rtl.cpp)

This uses upstream rally behavior, not the custom external-ship patch.

## Tests

`python3 tests/test_task.py` runs isolated real MOOS transaction tests without
an FC. `python3 tests/test_rally_sitl.py --run-sitl` verifies two actual
FC uploads/replacements and readbacks on unique ports in an owned Compose
project. It retains artifacts, sends **no flight command**, and is not proof
of airborne RTL navigation or a completed off-nominal landing.
