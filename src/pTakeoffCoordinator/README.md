# pTakeoffCoordinator

UAV-side takeoff coordinator. Configure one or more `request_condition` expressions and
one or more `checks_result_var` names from `uChecks`, plus `ownship`, `authority`,
`result_var`, `receipt_var`, `armed_var`, `landed_var`, `arming_flag=VAR=value`, and
`takeoff_flag=VAR=value`. Set `request_interval` (5 seconds in the demo),
`completion_retry_time` (60 seconds in the demo), and a positive clearance
`timeout`. Optionally set `checks_session_var` to an externally owned string
attempt ID (the demo uses the FSM's takeoff-entry epoch). The coordinator
captures that ID when it sends a request, cancels if it changes before arming,
and requires every check's scalar and `_STATE` diagnostic to say `success` for
that ID. Without this option, it compares against the clearance request ID.
The separate `receipt_var` is published locally on each new request and cleared on
cancellation, rejection, or timeout. AppCast lists the
conditions, evaluations, check results, phase, and request transmission count.
It also shows a table of state-change events for the current or most recent
takeoff (elapsed seconds, phase, status, reason). Retransmissions do not create
extra rows; starting another request replaces the previous takeoff's table.
Authority replies use `AUTHORITY_` reasons in the table and published reason
(for example, `AUTHORITY_CONDITIONS_CHANGED`, `AUTHORITY_DENIED`, or
`AUTHORITY_BUSY`).

When all request conditions become true (a false-to-true edge), the coordinator
sends an ID-tagged `TAKEOFF_REQUEST`
(`id=<unique-id>#status=requested`) through `NODE_MESSAGE_LOCAL`; it
retransmits the *same ID* every interval until the matching PEARL grant, a
rejection, cancellation, or timeout. One request can be pending at a time.
If request conditions cease before arming, the coordinator cancels the pending
request with reason `OWN_CONDITIONS_CHANGED` and ignores late grants. Once
arming starts it no longer applies those conditions. Authority timeout and a
safety disarm before departure also cancel.
A grant alone does not authorize arming: all configured UAV
checks must also pass. Once both are true, it posts the configured arming flag
once, reports `APPROVED`, and stops consulting checks or clearance conditions
for that flight. It never posts the takeoff flag until it observes armed
telemetry. It sends ID-tagged `armed` repeatedly to PEARL, then posts the
takeoff flag once. On `TAKING_OFF` telemetry it sends `taking_off`, repeated
until `IN_AIR`; then it sends `takeoff_complete`, repeating it every request
interval for the bounded completion retry window. Progress uses the same
`TAKEOFF_REQUEST` wire variable, and PEARL publishes it separately from its
local clearance status. It never synthesizes armed or airborne telemetry.

`result_var` remains `APPROVED` throughout the flight progression; its optional
`state_var` (default `<result_var>_STATE`) includes the request ID, result and
current phase reason. The optional `reason_var` exposes that reason separately.
Other outcomes are `UNKNOWN`, `PENDING`, `REJECTED`, `CANCELLED`, and `TIMEOUT`.
A fresh request-condition edge is needed for a later request; a late or mismatched
clearance cannot resurrect a canceled flight.

Native exchange tests: `python3 src/pTakeoffCoordinator/tests/test_exchange.py`.
