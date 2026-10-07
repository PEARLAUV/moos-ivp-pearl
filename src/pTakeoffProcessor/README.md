# pTakeoffProcessor

PEARL-side transport coordinator for ID-tagged takeoff requests. Configure
`receipt_var`, `status_var`, optional `progress_var`, and one or more
`checks_result_var` entries,
`ownship`, `requester`, and a positive `timeout`. On
`TAKEOFF_REQUEST=id=<id>#status=requested`, the
processor publishes the ID on `receipt_var` and waits for every configured
`uChecks` aggregate to succeed for that ID. Each success is retained for the
request, so a completed precheck may expire while later checks run. A
session-matched `fail`, or `expired` before that check succeeded, denies;
missing or pending results wait until timeout. A later explicit `fail` before
granting still denies. The reply is
`TAKEOFF_CLEARANCE=id=<id>#status=granted|denied` via `NODE_MESSAGE_LOCAL`.
After successfully posting a grant, the processor publishes `granted` locally
on `status_var`; the PEARL FSM waits for this acknowledgement before entering
`AWAITING_DRONE_DEPARTURE`. The local status starts `inactive`, becomes
`pending` on acceptance, reports `complete` on confirmed departure, and reports
`denied` on failed checks or timeout and `canceled` on cancellation. Release
returns it to `inactive` only if departure was not yet confirmed; a late
completion may still advance the just-released request to `complete`.
The AppCast still distinguishes a timeout from a check denial. The local status
is not sent to the UAV; the demo routes it to shoreside for diagnosis.
The AppCast includes a `Takeoff events` table for the current or most recent
accepted request, with elapsed seconds, local status, requester progress, and
reason. It records request acceptance, grant, progress, and terminal outcomes
(`CHECKS_FAILED`, `CHECKS_TIMEOUT`, `RELEASE_CONDITIONS_MET`, or a
`REQUESTER_` cancellation reason). Retransmissions and duplicate progress do
not create rows; a new accepted request replaces the previous history. The
published `status_var` advances to `complete` when departure is confirmed.
Concurrent IDs receive `busy`; repeated requests for the active ID are
idempotent (neither denied nor restarted). A matching `status=cancelled` clears
the receipt. After granting, the active ID can also send `armed`,
`taking_off`, and `takeoff_complete` on `TAKEOFF_REQUEST`. These updates are
monotonic, idempotent and (when `progress_var` is configured) published as
`id=<id>,status=<progress>` on a separate PEARL-local variable. Completion
also advances `status_var` from `granted` to `complete`; the PEARL postcheck
FSM guard accepts either value to avoid missing a fast completion. The optional
`progress_grace` (120
seconds in the demo) also accepts advances from the just-released granted ID:
direct FSM release can precede a mediated completion report. A new request
supersedes that ID; stale IDs, regressions, and reports after the grace window
are ignored.

**Session safety:** Each `uChecks` instance supplying a result must set
`session_var` to `receipt_var`. The processor reads both the result and its
`<result>_STATE` diagnostic, which must contain the same status and
`session=<active request ID>`. It cannot authorize from retained results of a
previous request or from missing diagnostics. The checks' own configuration
owns battery, wind, GPS, and timing policy; the processor contains none.

Optional `release_condition = VAR=value` entries are ANDed and must be
observed matching *after* granting; the receipt remains busy until then.
String values match exactly; numeric values match numeric MOOS posts. Without
release conditions, the receipt clears at settlement. PEARL supplies two
session-scoped check results (`TAKEOFF_PRECHECKS_STATE` and
`TAKEOFF_POSTCHECKS_STATE`) and releases on
`PEARL_FSM_OPERATION_STATE=NORMAL_OPERATION`. The processor has no grant
conditions or FSM-specific state logic; unsupported `grant_condition` settings
are rejected. Cancellation or denial clears immediately.

Optional `view_label`, `view_radius`, `view_x_var`, and `view_y_var` configure
an operator-only takeoff circle centered on current navigation. Until grant it
publishes twelve short red `VIEW_SEGLIST` arcs (pMarineViewer has no dashed
`VIEW_CIRCLE` edge); after grant it retracts them and publishes one solid
`VIEW_CIRCLE`. On settlement it retracts both. `view_post_interval` controls
refresh cadence (one second by default). The label remains a stable viewer
identity, but its text is hidden.

The legacy `pTakeoffGate` is not launched. Native tests:
`python3 src/pTakeoffCoordinator/tests/test_exchange.py` and
`python3 src/uChecks/tests/test_pearl_takeoff.py`.
