# pLandingCoordinator

Independent landing agreement/readiness owner. Aircraft receives addressed
`RECOVER`; platform accepts the session via `pMediator` and separately grants
clearance after both rendezvous planners complete. ArduPilot owns descent,
precision guidance, target-loss/retries, touchdown and disarm. Coordinator
permissions may be supplied by the existing supervisory FSM.

## Optional pre-LAND yaw alignment

Disabled by default. Enable **only on the aircraft**:

```moos
align_yaw = true
// Maximum confirmed target-relative error (0 < degrees <= 45; default 5).
yaw_tolerance = 5
// FC relative CONDITION_YAW speed (0 < degrees/s <= 90; default 30).
yaw_rate = 30
// Stable observation dwell and allowed circular jitter before dispatch.
yaw_stability_time = 0.5
yaw_stability_tolerance = 3
// Continuous fresh in-tolerance confirmation before AUTOLAND.
yaw_hold_time = 0.5
// Combined current roll/pitch bound for BODY_FRD horizontal projection.
yaw_max_tilt = 15
// Includes observation wait, ACK, physical rotation and confirmation.
yaw_timeout = 60
```

REAL and SITL use the same target-relative method. The coordinator does not use
a simulator heading, peer heading, COG, exposure-time NED attitude, clock offset,
interpolation or `UAV_NAV_MAVLINK_POSES` to authorize yaw alignment.

With the option enabled, fresh clearance, both rendezvous completions, existing
health/armed/native-IN_AIR/received-target checks and `command_condition` must
permit execution. The coordinator remains in **RENDEZVOUS** during alignment and:

1. Requires `BODY_FRD` (`frame=12`), `position_valid=1`, a target age no greater
   than 0.5 real seconds, a finite nonzero `UAV_LANDING_TARGET_Q_W/X/Y/Z`
   quaternion, a valid stable target number, and an advancing positive integer
   target timestamp.
2. Normalizes the received quaternion, rotates its forward axis into BODY_FRD,
   and computes signed target-relative yaw with `atan2(body_y, body_x)`.
3. Requires fresh native attitude availability and a combined roll/pitch no
   greater than `yaw_max_tilt`. Attitude is used only as a tilt safety bound;
   it is not paired to the camera exposure and does not determine the yaw goal.
4. Requires at least three advancing observations stable for
   `yaw_stability_time`. If already in tolerance, it skips the FC yaw command
   but still performs the confirmation hold.
5. Otherwise emits exactly one
   `YAW_ALIGN_REQUEST=id=<session>_yaw#angle=<signed relative degrees>#rate=<degrees/s>`.
6. Waits for a matching `UAV_YAW_ALIGN_RESULT` and `status=ACCEPTED`. This proves
   FC command acceptance only. The bridge sends `MAV_CMD_CONDITION_YAW` with
   `param1=abs(angle)`, signed direction, and `param4=1` (relative).
7. Requires new post-ACK target observations. The live target-relative error
   must remain within `yaw_tolerance` for `yaw_hold_time` and at least three
   advancing observations before `AUTOLAND` is issued.

A changing target-relative quaternion while the aircraft turns is expected; it
is the alignment feedback, not a target-heading-change fault. Target loss,
malformed or nonadvancing observations, excessive tilt, or missing telemetry
cannot prove alignment. They block until recovery or `yaw_timeout`; timeout,
rejection, cancellation, lost peer clearance, lost command permission or lost
Guided ownership abort before LAND. There is no retry or elapsed-time success.

The bridge accepts requests only in its already-established
`HELM_INACTIVE_LOITERING` Guided hold and independently requires connected,
armed, fresh native IN_AIR Copter evidence. It never enters Guided, steals Helm
authority, arms, moves the hold point or changes altitude. After acceptance, the
existing position-only Guided XYZ target and FC-owned FIXED-yaw command remain
latched; refreshing the position target would reset ArduPilot's fixed yaw.

### Supervisory transition gate

`pLandingCoordinator` owns the active yaw transaction and does not publish
`LANDING_OWN_PHASE=LANDING` until it has confirmed alignment, issued `AUTOLAND`,
and received the LAND command acceptance. A mission may additionally run a
`uChecks` instance while the FSM is `APPROACHING`, requiring
`LANDING_OWN_YAW_STATE=ALIGNED`. The Briggs mission uses this defense-in-depth
check (`LANDING_YAW_CHECKS`) on the `APPROACHING -> LANDING` edge. The check does
not command yaw and cannot substitute for coordinator validation.

### Diagnostics

- `LANDING_OWN_YAW_STATE`: `DISABLED`, `WAITING`,
  `WAITING_FOR_ORIENTATION`, `STABILIZING_TARGET_ORIENTATION`, `COMMANDING`,
  `ACCEPTED`, `WAITING_FOR_FC_ACK`, `WAITING_FOR_POST_ACK_TARGET`, `ALIGNING`,
  `CONFIRMING_ALIGNMENT`, `ALIGNED`, or `ABORTED`.
- `LANDING_OWN_YAW_INPUT_REASON`: `ready` or a fail-closed input reason.
- `LANDING_OWN_YAW_GOAL`: the signed relative angle sent to the FC, if needed.
- `LANDING_OWN_YAW_ERROR`: latest measured target-relative circular error.

ArduPilot's [precision-landing message interface](https://ardupilot.org/dev/docs/mavlink-precision-landing.html)
marks quaternion `q` as **not used** by the precision-landing controller. This
application consumes it before LAND. Copter LAND then holds its normal yaw;
this is not continuous pad-yaw tracking during descent. `PLND_YAW_ALIGN` remains
sensor-to-body mounting calibration, not pad alignment.

## Tests

```bash
cmake --build build --target pLandingCoordinator
python3 src/pLandingCoordinator/tests/test_landing_checks.py
python3 src/pLandingCoordinator/tests/test_fsm_guards.py
python3 src/pLandingCoordinator/tests/test_yaw_alignment.py
# Opt-in: patched local SITL image; isolated ports/directories and owned cleanup.
python3 src/pLandingCoordinator/tests/test_yaw_sitl.py --warp=2
```

The isolated MOOS fixture uses mock age publications only as test data. The SITL
flight test uses the same BODY_FRD quaternion path as REAL and validates one
relative command, FC ACK, post-ACK target-relative convergence, authorized
precision LAND, elevated touchdown and disarm.
