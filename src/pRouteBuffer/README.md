# pRouteBuffer

Stages an editable shoreside route and delivers immutable vehicle snapshots
through the existing pMediator input `NODE_MESSAGE_LOCAL`. Vehicle-role
instances deliver snapshots to Helm. No new transport or guidance authority.

## Shared drawing (opt-in)

```moos
role = shoreside
destination_node = uav       // legacy DEPLOY/CLEAR/GOTO target, not task target
task_destination = pearl
task_destination = uav
task_request_var = ROUTE_BUFFER_TASK
stage_clear_request_var = ROUTE_BUFFER_CLEAR_STAGED
task_submit_flag = uav:RALLY_TASK_COMMIT=true
```

A fresh `ROUTE_BUFFER_TASK=pearl` or `uav` carries its destination with a
snapshot of the current nonempty drawing. A successful local MOOS post removes
only the drawing/preview and publishes `ROUTE_BUFFER_TASK_ID`,
`ROUTE_BUFFER_TASK_DESTINATION` and `TASK_SUBMITTED`. Empty, stale (>3 MOOS s),
pre-start or invalid-target requests do not submit or consume anything.
Coordinates must be finite. Subsequent edits cannot change the submitted copy.
Destination-specific flags run only after a valid submission. This does not
prove mediator acceptance, remote application acceptance or execution.

Staged clearing sends no vehicle CLEAR. Legacy DEPLOY/CLEAR/GOTO retain their
configured target; live append is optional and disabled in the prep mission.
The existing startup sync consumes the mediator's otherwise rejected ID zero;
it is not a readiness proof.

## Guarded vehicle execution (opt-in)

```moos
role = vehicle
helm_state_var = IVPHELM_STATE
task_execute_var = TASK_EXECUTE
task_started_state = PEARL_FSM_MAIN_ACTIVITY_STATE=EXECUTING_MISSION
task_guard = PEARL_FSM_OPERATION_STATE=NORMAL_OPERATION
task_guard = PEARL_FSM_MAIN_ACTIVITY_STATE=IDLE
task_guard = PEARL_FSM_SUPPORT_ACTIVITY_STATE=NO_ACTIVE_REQUEST
route_ready_name_var = ROUTE_READY_NAME
route_ready_name_prefix = pearl_main_task_
deploy_delay = 0
route_replay_delay = 0
```

Tagged deploys carry `task_id=ID # task_time=MOOS_TIME # points={...}`.
They reject missing/invalid identity or time, pre-start/future (>1 s)/old
(>30 s) requests, active-route replacement and unavailable/stale/disallowed
guards. Guard timestamps remain their producer times; leases are 3 MOOS s.
Duplicates cannot execute twice during the process lifetime.

After staging, both ROUTE_READY and the matching full Helm instance name must
be fresh and post-snapshot. Use spawn flags
`ROUTE_READY=true` and `ROUTE_READY_NAME=$[BHVNAME]`; the prefix accounts for
Helm's spawn-template name. Only then does the buffer request task execution.
Actual matching execution-state observation confirms TASK_STARTED; missing
readiness (>5 s), changed guards or missing start confirmation (>3 s) cancel
only the pending task. These are fail-closed bounds, not timer-based readiness.
A running task is not cancelled by support suspension. The FSM owns execution,
completion, return, and explicit `action=clear` cleanup.

Without task_execute_var, existing deployment policy remains in use. Tagged
UAV submissions still use the separate rally verification gate. With
helm_state_var, snapshots received while parked are retained until DRIVE;
CLEAR discards them. This does not manufacture readiness.

For vehicles that may park before a staged instance receives its expiry delta,
opt into `route_identity_var=UAV_ROUTE_ACTIVE_ID`. Each snapshot adds a Helm
`condition=UAV_ROUTE_ACTIVE_ID=<immutable instance id>` and publishes that
current identity; CLEAR publishes `none`. Only the matching instance may run,
even if parked Helm retained an older instance. Names under this policy must
contain only ASCII letters, digits, underscores or hyphens (no injected logic
syntax). The FSM must still issue explicit `action=clear` to release buffer
ownership; clearing a Helm flag alone does not release it. This policy is
disabled by default, does not acknowledge execution/expiry, and does not
change waypoint guidance or manufacture readiness.

## Optional task-frozen return point

Shore configuration:
```
return_point_var = PEARL_RETURN_POINT
task_return_destination = pearl
initial_return_point = x=10.6,y=4.8
```
Point edits are strings `x=X,y=Y` (finite local metres, each within ±100 km).
The cyan unlabeled `staged_return` marker and
`PEARL_RETURN_POINT_STAGED` show the staged selection. Consuming or clearing
the route drawing does not clear it. A task for the configured destination
requires a valid selection and carries one immutable
`return_point=x=X,y=Y` field inside the same mediated request. Other
destinations are unchanged; later edits do not modify sent/retried tasks.

Vehicle configuration `return_update_var = PEARL_RETURN_UPDATE` requires the
existing guarded task policy. Missing, invalid or repeated return metadata
rejects the task before it can execute. Only an accepted task emits its frozen
`points=X,Y` waypoint update; busy/duplicate tasks cannot redirect it.
Parked Helm receives the retained return and route snapshots only on DRIVE;
local clear/cancellation discards deferred updates. The return waypoint must
subscribe to that update variable. All options are disabled by default, so
existing deployments remain unchanged.

## Validation

`tests/test_shared_task.py`: destination isolation, immutable snapshots,
drawing consumption, empty/double-click/invalid-target rejection and local-only
clearing. Prep `tests/test_pearl_route_task.py`: real Helm/FSM staging,
waypoint completion/base return/repeated tasks, busy rejection,
support-history waypoint progress, stale/untagged rejection and duplicates.
All use owned isolated MOOS communities and synthetic navigation, not vehicles.
