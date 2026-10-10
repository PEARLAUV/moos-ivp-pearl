# pLandingCoordinator

Independent landing agreement/readiness owner. Aircraft receives addressed
`RECOVER`; platform accepts the session via `pMediator` and separately grants
clearance after both rendezvous planners complete. ArduPilot owns descent,
precision guidance, target-loss/retries, touchdown and disarm. Coordinator
permissions may be supplied by the existing supervisory FSM; no new FSM states
or edges are needed.

## Optional pre-LAND yaw alignment

Disabled by default. Enable **only on the aircraft**:

```moos
align_yaw = true
// Maximum circular measured error, degrees (0 < value <= 45; default 5).
yaw_tolerance = 5
// FC CONDITION_YAW speed, degrees per FC second (0 < value <= 90; default 30).
yaw_rate = 30
// MOOS seconds, including orientation wait, ACK and physical rotation; default 60.
// Increase appropriately with mission warp. Expiry aborts; it never lands/retries.
yaw_timeout = 60
```

With the option enabled, fresh clearance, both rendezvous completions, existing
health/armed/native-IN_AIR/received-target checks and `command_condition` must
permit execution. The coordinator remains in **RENDEZVOUS** during alignment;
`LANDING_OWN_YAW_STATE` reports the internal substep. It then:

1. Normalizes the **actually received** `UAV_LANDING_TARGET_Q_W/X/Y/Z` quaternion.
   It rotates the target's forward axis through the native observation-time
   body-to-NED attitude to obtain true-north target heading. No peer heading,
   COG, simulator orientation or zero-quaternion fallback substitutes for data.
2. Freezes that heading and emits one
   `YAW_ALIGN_REQUEST=id=<session>_yaw#heading=<true degrees>#rate=<degrees/s>`.
3. Waits for matching `UAV_YAW_ALIGN_RESULT` with matching heading and
   `status=ACCEPTED`. This is an actual FC command ACK, **not alignment**.
4. Requires a fresh native attitude sample whose original native receipt time
   (`UAV_ATTITUDE_SAMPLE_TIME`, MOOS seconds) is at/after ACK. Actual `UAV_YAW`
   must be within tolerance using the circular signed error. Reposting an old
   sample does not renew that receipt time. Current received target heading must
   still agree with the frozen heading within tolerance.
5. Rechecks readiness, permission and fresh clearance, then issues the existing
   generic `AUTOLAND` once. No yaw requests continue during LAND.

Native pose requires numeric `UAV_ROLL/PITCH/YAW`, `UAV_ATTITUDE_AVAILABLE=1`,
and effective native `UAV_ATTITUDE_AGE <= 0.5` **real seconds**. Target receive
age also remains bounded at 0.5 real seconds. Exact observation-time matching
uses `UAV_ATTITUDE_TIME_USEC` (FC attitude boot microseconds) and received
`UAV_LANDING_TARGET_TIME_USEC`. A bounded 128-sample native attitude history
is cleared on source-clock rewind. Matched pose must be recent (0.5 real s).
The bridge also publishes atomic `UAV_ATTITUDE_SAMPLES`: an original-receipt
window of at most 128 native poses, spanning at most one real second. Format is
`boot_usec,roll_deg,pitch_deg,yaw_deg,receipt_moos;...`, oldest first. Every window
replaces the previous history; malformed/empty windows clear it. Consumers import
**all** original poses, so a 4 Hz subscriber cannot lose an intermediate 5 Hz
sample by retaining only the final scalar pose in a mail batch. The explicit legacy MOOS-mode emulator uses
the same atomic snapshot, not mixed scalar Euler fields. Current measured yaw and
its post-ACK receipt time are selected together from that window. The one-second
transport window **does not extend the 0.5-second admission limit**. Reposts do not
renew original receipt times. Scalar-only legacy inputs remain supported until a
window is first received; there is no scalar fallback after invalid window data.
`pMavlinkLandingTarget` tags its geometry with the native attitude sample used,
so rotating the drone does not masquerade as a changing target heading.

For the direct-input emulator, configure
`observation_pose_samples_var = UAV_NAV_MAVLINK_POSES`. The exact exposure
rotation then comes from its original, checksum-validated FC pose window.
Missing/invalid/unmatched windows block alignment, without bridge-history
fallback; original attitude receipt still must be within **0.5 real seconds**.
This option changes **only exposure rotation**. Current measured yaw, its
post-ACK receipt, Guided ownership, health, armed/landed telemetry and all
existing permissions remain bridge-owned. No simulator target coordinates or
peer heading grant flight authority.

**Real cameras:** this option requires an observation timestamp matching a
native attitude sample on the same clock. Unsynchronized timestamps, missing
quaternions, unavailable/stale attitude or a vertical target-forward axis block
alignment; no clock offset/interpolation is guessed. Adapt the camera/pose
interface explicitly before enabling it on hardware. The passive viewer may
show an approximate latest-attitude reconstruction; that is not permission to
land or proof of yaw readiness.

Rejection, timeout, changed target heading, cancellation or lost peer clearance/
command permission abort before LAND. Missing temporary orientation/readiness
blocks dispatch until the deadline; no elapsed time proves arrival/alignment.
Cancellation does not undo an already accepted FC yaw command: the existing
Guided position hold remains the safe owner. The bridge accepts requests only
in its already-established `HELM_INACTIVE_LOITERING` Guided hold, and separately
requires connected, armed, fresh native IN_AIR Copter evidence. It never enters
Guided, steals Helm authority, arms, moves the hold point or changes altitude to
satisfy a request. ACK waits are asynchronous. After acceptance, the FC's existing position-only
Guided XYZ target and `CONDITION_YAW` FIXED-yaw controller are left latched; the
bridge stops refreshing that position target until a new hold is requested.
ArduPilot's `ModeGuided::set_yaw_state_rad()` resets ignored-yaw position messages
to default yaw, while angle-bearing updates replace FIXED yaw with ANGLE_RATE.
Either would overwrite the accepted command. Position-only holds persist; the
Guided RATE/ANGLE_RATE timeout does not expire FIXED yaw or the XYZ position.
This is not velocity/acceleration guidance and does not change deadman policy. Stale/restart-retained/duplicate/busy requests are rejected.

### Diagnostics

- `LANDING_OWN_YAW_STATE`: DISABLED, WAITING, WAITING_FOR_ORIENTATION,
  COMMANDING, ACCEPTED, WAITING_FOR_FC_ACK, WAITING_FOR_POST_ACK_ATTITUDE,
  ALIGNING, ALIGNED, ABORTED. Recovered orientation restores the actual substep
  instead of leaving WAITING_FOR_ORIENTATION displayed during physical rotation.
- `LANDING_OWN_YAW_INPUT_REASON`: ready or the missing/stale/invalid input reason.
- `LANDING_OWN_YAW_GOAL`: frozen true-north degrees, when requested.
- `LANDING_OWN_YAW_ERROR`: last measured signed circular degrees, when available.
- AppCast: option, substep, input blockage, tolerance/rate/timeout, request/ACK,
  goal/error plus existing checks/permissions/session/peer freshness.

ArduPilot's [precision-landing message interface](https://ardupilot.org/dev/docs/mavlink-precision-landing.html)
marks quaternion `q` as **not used**. Copter LAND holds its normal yaw; this option
aligns **before** handing control to LAND, not continuous target-yaw tracking
throughout descent. `PLND_YAW_ALIGN` is fixed sensor-to-body mounting calibration,
not pad alignment. A platform turning after AUTOLAND can therefore become
misaligned again; continuous tracking requires an FC-owned implementation.

## Tests

```bash
cmake --build build --target pLandingCoordinator
python3 src/pLandingCoordinator/tests/test_landing_checks.py
python3 src/pLandingCoordinator/tests/test_fsm_guards.py
python3 src/pLandingCoordinator/tests/test_yaw_alignment.py
python3 src/lib_mavlinkTransport/tests/test_body_frame.py
# Opt-in: patched local SITL image; isolated ports, directories and owned cleanup.
python3 src/pLandingCoordinator/tests/test_yaw_sitl.py --warp=2
```

The isolated MOOS fixture's mock age stream is test data only. No operational
app substitutes a timer for native telemetry or command acceptance. The SITL test
uses separately simulated pad axes to demand a >90-degree turn, actual FC ACK,
post-ACK native yaw, authorized precision LAND, elevated touchdown and disarm.
It is not a full Briggs FSM or post-disarm link-loss test. Historical isolated pass:
156.33-degree requested turn, 0.111-degree pre-LAND yaw error, 1.08 m MSL touchdown
on a 1 m MSL deck, 0.0088 m final horizontal error. Artifacts:
`/tmp/precision-yaw-sitl-su_1o2e3/`. A separate diagnostic STATUSTEXT capture was
intermittent in an earlier run; the final test captured native acquisition/init.
