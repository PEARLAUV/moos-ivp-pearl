# uChecks

One `uChecks` instance evaluates named checks under a shared MOOS activation
condition. It has no mission-specific logic and issues no vehicle commands.
`result_var` owns the aggregate status; `result_prefix` plus a `check` name
owns each individual status. Each status also has a `_STATE` diagnostic
variable (the aggregate may override its `state_var`). Statuses are
`inactive|pending|success|fail|expired`. Any failed check makes the aggregate
`fail`; all successful checks make it `success`; otherwise it is `expired`
if any check has expired, `inactive` if all are inactive, or `pending`.

```moos
ProcessConfig = uChecks
{
  AppTick = 4
  CommsTick = 4
  condition = (UAV_FSM_TAKEOFF_OPERATIONS_STATE=ESTABLISHING_TAKEOFF_READINESS) or (UAV_FSM_TAKEOFF_OPERATIONS_STATE=REQUESTING_TAKEOFF_CLEARANCE)
  result_var = TAKEOFF_PRECHECKS
  result_prefix = TAKEOFF_PRECHECK_
  session_var = UAV_FSM_OPERATION_STATE_TAKEOFF_OPERATIONS_EPOCH
  snapshot_interval = 1

  check = name=FLIGHT_PLAN,timeout=60
  require = check=FLIGHT_PLAN,var=ROUTE_READY,type=string,equals=true,on_mismatch=wait

  check = name=BATTERY,timeout=5,max_age=3
  require = check=BATTERY,var=UAV_BATTERY_SOC,type=double,min=30,on_mismatch=fail
}
```

`check` names are unique and determine result variables (`FLIGHT_PLAN` gives
`TAKEOFF_PRECHECK_FLIGHT_PLAN`). Each named check requires a positive
timeout and at least one typed requirement. Optional `max_age` is positive;
when omitted, received inputs stay valid until updated. Optional
`success_hold` defaults to zero. Requirements accept exact, case-sensitive
string equality (`equals`), a pipe-delimited set of exact string values
(`one_of=1|2|4|5`), exact double equality, or inclusive double bounds; each
requires `on_mismatch=fail|wait`. No implicit string-to-double conversion.
Configuration lines can appear in any order; each `require` identifies its
check explicitly. Invalid configurations fail closed.

Optional `result_interval` is a finite, non-negative number of **MOOS seconds**,
defaulting to 0 (change-only results, preserving existing publication rates).
A positive interval refreshes the aggregate and named result variables together
with their session-bearing `_STATE` diagnostics while the instance is active.
Every renewal follows the normal evaluation of input freshness, types,
requirements, success holds and failure latches; it does not extend input
expiry or echo a cached success. Inactive instances never renew result leases.
A changed session resets the renewal deadline and the existing check state.
Snapshots have their own independent `snapshot_interval`.

For a consumer with a three-second permission lease, use `result_interval=1`.
Consumers must still verify the appropriate session and lifecycle conditions;
a fresh result timestamp alone is not proof of arrival or successful execution.

While the condition is true, checks re-evaluate inputs independently.
`wait` mismatches and missing/stale inputs stay pending until that check's
timeout, while `fail` mismatches latch immediately. A success hold requires
continuously matching inputs. When deactivated, each check preserves its
last result until its first required input expires; the expiry deadline is
frozen at deactivation. Checks without `max_age` retain their result until
reactivated. Expiry clears the check's failure latch. Activation-condition
values persist until changed; checked inputs alone can expire.

Each instance also owns a JSON snapshot output (`snapshot_var`, default
`<result_var>_SNAPSHOT`). `snapshot_interval` is a positive number of seconds,
defaulting to 1. A consistent snapshot is posted on the first evaluation,
on activation, session or diagnostic change, periodically while active, and
once on deactivation. Subsequent inactive changes such as result expiry also
post a new snapshot; otherwise there is no inactive heartbeat. The existing
result variables remain the authorization interface.

```json
{
  "schema_version": 1,
  "instance": "TAKEOFF_PRECHECKS",
  "epoch": "1234-1790525501.75",
  "sequence": 42,
  "published_at_moos": 1790525502.75,
  "active": true,
  "session": {"variable": "UAV_TAKEOFF_REQUEST_ID", "id": "req-17"},
  "activation": {"expression": "TAKEOFF_STATE=READY", "matched": true,
                 "inputs": {"TAKEOFF_STATE": {"type": "string", "value": "READY",
                                               "received": true, "age_s": 0.4}}},
  "aggregate": {"variable": "TAKEOFF_PRECHECKS", "status": "success"},
  "inputs": {"UAV_BATTERY_SOC": {"type": "double", "value": 41.2,
                                 "received": true, "age_s": 0.4}},
  "checks": {"BATTERY": {"status": "success", "reason": null,
                          "detail": "values=UAV_BATTERY_SOC:41.200",
                          "active_elapsed_s": 2.1, "timeout_s": 5,
                          "success_hold_s": 0, "hold_elapsed_s": 2.1,
                          "failure_latched": false,
                          "requirements": [{"variable": "UAV_BATTERY_SOC",
                                            "expected": {"min": 30},
                                            "on_mismatch": "fail",
                                            "max_age_s": 3, "fresh": true,
                                            "matched": true, "reason": null}]}}
}
```

Inputs are shared at the top level and include missing values (`value: null`,
`received: false`, `age_s: null`); activation inputs reflect what the condition
actually observed. Freshness and matching are reported per requirement since
checks can use different `max_age` values. An omitted `max_age` is `null` and
has no input-age lease; a missing or stale requirement has `matched: null`.
`reason`/`detail` on checks retain the current diagnostic text, while each
requirement's `reason` is `missing|stale|wrong_type|mismatch|null`. The process
startup `epoch` and increasing `sequence` distinguish restarts and updates;
consumers should apply a lease based on their **local receive time** and verify
`active` and the session ID. A retained aggregate `success` in a final
`active: false` snapshot does not authorize a new operation.

Optional `session_var` subscribes to a string MOOS request/session ID. Each
change clears check timers, held results and failure latches and re-evaluates
current inputs; `none` disables checks. Every `_STATE` diagnostic then includes
`session=<id>` alongside `status=<status>`. This prevents authorization from
stale results when a new request reuses a still-valid set of inputs. Any
processor consuming the result must verify both status and session ID.

Integration tests:
`python3 src/uChecks/tests/test_checks.py` and
`python3 src/uChecks/tests/test_sim_flags.py`, and
`python3 src/uChecks/tests/test_pearl_takeoff.py`.
