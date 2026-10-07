# pFSMRuntime

`pFSMRuntime` is the standalone MOOS execution backend for the repository's
implementation-neutral FSM models.

The runtime:

- evaluates generated machine activation conditions and transition
  `LogicCondition` guards;
- maintains simple, composite, orthogonal, and history state;
- gates child machines from parent state;
- synchronizes hierarchy changes in one iteration: exited descendants publish
  `none` from innermost to outermost, then entered states publish from the
  outermost machine toward its leaves;
- executes typed variable-assignment effects on state entry and exit;
- takes at most one guarded transition per application iteration;
- holds the hierarchy on ambiguous transitions;
- publishes periodic state, transition, status, aggregate-state, and sparse
  entry-count variables, plus one JSON `FSM_TRANSITION` event for each cycle
  with one or more logical transitions;
- publishes generated `*_FSM_RUNTIME_STATE`, heartbeat, and epoch facts so
  consumers can reject stale state after publisher failure;
- reports configured state effects, each active state's possible outgoing
  transitions, full guard, evaluation, and an atomic-condition table with input
  type, current value, comparison operator, expected value, and match result in
  its AppCast; and
- initializes only facts whose model metadata has `initialize = true`.

Use `scripts/generate_fsm.py MODEL.toml` to generate the MOOS runtime plug.

`FSM_TRANSITION` has the form
`{"transitions":[{"occurrence_id":1,"machine":"operation","id":"INITIAL","from":"start","to":"IDLE"}],"transition_time":1234.5,"context":{}}`.
Occurrences are globally numbered within this runtime instance, including
initial transitions. The top-level `transition_time` is numeric MOOS time in
seconds: the winning guard's match time for guarded transitions, or the first
initial transition's publication time for an initial-only batch. A parent
transition and any newly entered child initial transitions appear in the same
array in publication order. The top-level
`context` is a typed map of the observed values of every guard input in *all*
candidate transitions from the winning transition's source state, sampled
when its guard matched, before any exit or entry effects. For generated
`check_result(...)` guards it contains the entire `uChecks` snapshot as a JSON
object under its local `<RESULT_VAR>_SNAPSHOT` key **instead of** the scalar
result variable; for example, `"TAKEOFF_PRECHECKS_STATE_SNAPSHOT":{...}`.
Missing snapshots and unseen ordinary inputs are JSON `null`. A snapshot is
only diagnostic and may lag the scalar guard result; it does not imply active,
fresh, or session-matched checks. Initial-only batches have an empty context.
Existing per-machine
transition variables remain available for diagnostics. PEARL and UAV route the local JSON
event to shoreside as `PEARL_FSM_TRANSITION` and `UAV_FSM_TRANSITION`.

Machines may be gated by a fact expression and configured either to preserve
full deep history or reset their complete hierarchy when the condition becomes
true again:

```toml
[machine]
initial = "idle"
condition = "moos_manual_override==false"
history = true # false resets this machine and all descendants
```

When the condition is false or unavailable, the machine and every descendant
publish `none` and take no transitions. With `history = true`, this is a
suspension: exit effects do not run, and reactivation republishes the complete
retained hierarchy outermost-first without entry effects or new entry counts.
With `history = false`, exit effects run
innermost-first during deactivation and the hierarchy restarts from its initial
states when reactivated.

Effects are declared on model states as assignments to declared facts:

```toml
[states.idle.entry_effects]
command_state = "ready"
attempts = 0

[states.idle.exit_effects]
command_state = "departing"
```

An entry effect runs after its state variable has published the entered state.
An exit effect runs before that state variable leaves the exited state. During
hierarchy changes, descendant exit effects run innermost-first and entry effects
run outermost-first with their corresponding state publications. Effects also
run when child machines deactivate or reactivate through their parent gate.

Set `entry_epoch = true` on a state to publish
`<STATE_VAR>_<STATE>_EPOCH` as a string `<runtime-start-epoch>:<state-entry-count>`
on each actual entry, alongside its numeric `_COUNT`. The token changes on
re-entry and runtime restart but not on a deep-history resume. For example,
`TAKEOFF_OPERATIONS` publishes
`UAV_FSM_OPERATION_STATE_TAKEOFF_OPERATIONS_EPOCH`; it can be used as an
explicit `uChecks` session input without making `check_result(...)` imply a
session match.

Set `min_state_time = <seconds>` directly in the application's
`ProcessConfig = pFSMRuntime` block to require that every machine spend at
least that much active time in a state before an outgoing guard can fire. The
default is zero. A parent transition whose guard references an active descendant's
state cannot exit its composite state until both the parent and that descendant
have met the minimum dwell. In particular, entering the last state of a child
diagram does not immediately allow its parent exit just because the parent is
older. Time is paused while a history-preserving child machine is inactive;
reset-on-inactive children restart the timer from zero.
