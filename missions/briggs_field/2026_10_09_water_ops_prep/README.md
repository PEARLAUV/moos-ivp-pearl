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

## PEARL supervisory FSM source

[`pearl_fsm.toml`](pearl_fsm.toml) is the source for the executable
[`plug_pearl_fsm_runtime.moos`](plug_pearl_fsm_runtime.moos) configuration,
generated pShare-route and shoreside-scope include fragments, the composed
[`pearl_fsm/overview.d2`](pearl_fsm/overview.d2) overview and detail boards,
the linked SVG set under `pearl_fsm/svg/`, and the
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

## UAV companion FSM and authority boundary

[`uav_fsm.toml`](uav_fsm.toml) defines the sequential UAV lifecycle
`IDLE → TAKEOFF_OPERATIONS → MISSION_EXECUTION → LANDING_OPERATIONS`. A nominal
landing exits through the named `nominal` sink and returns to `IDLE`. The nominal
landing detail proceeds through `ESTABLISHING_LANDING_READINESS →`
`REQUESTING_RECOVERY → NEGOTIATING_RENDEZVOUS_POINT →`
`MOVING_TO_RENDEZVOUS_POINT → WAITING_FOR_LANDING_CLEARANCE → APPROACHING →`
`LANDING`. Target loss during landing enters `RETRYING_LANDING_TARGET`; exhausted
retries, request failure, negotiation failure, clearance failure, or approach
failure divert through the rally point and `SECONDARY_LANDING` before exiting to
`OFFNOMINAL`. Readiness failure instead transits to the secondary loiter
location and exits through `LOITERING`. Neither root outcome has a takeoff exit,
so both inhibit a new takeoff pending recovery intervention. It generates the
[`plug_uav_fsm_runtime.moos`](plug_uav_fsm_runtime.moos), pShare/scope fragments, JSON UI
bindings, [`uav_fsm/overview.d2`](uav_fsm/overview.d2), and
`uav_fsm/svg/index.svg`:

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

PEARL is treated as having negligible turn radius. The coordinator therefore
chooses a point on the straight line between the current vehicle positions.
Its fractional distance from PEARL is:

```text
pearl_speed / (pearl_speed + uav_speed)
```

This equalizes nominal arrival times using the configured speeds. It is a
demonstration policy, not a current-aware or obstacle-aware planner. Recovery
routes use a 1.5 m/s speed with 0.5 m capture and slip radii: faster than the
original 1 m/s stabilization setting, but with enough braking and settling margin to
preserve the two-second landing-gate dwell. Both vehicles must remain within
1.5 m of the rendezvous point, and within 3 m of each other. PEARL receives a
fresh, session-named waypoint behavior for each recovery so a close-range
request cannot be dropped as a non-spawning update. Operator mission routes
remain independently configured.

| State | Action and transition |
| --- | --- |
| `IDLE` | Wait for an airborne operator request. |
| `REQUESTING` | UAV sends position, speed, health, and available battery data. PEARL proposes one session-tagged point. UAV accepts only after its complete one-point route is staged locally. |
| `RENDEZVOUS` | PEARL activates a fresh spawned rendezvous waypoint and the UAV follows its existing Helm route. Both must be inside the 5 m capture zone, no more than 3 m apart, and continuously satisfy the clearance gate for 2 s. If route capture leaves them on opposite sides of the nominal point, the UAV closes directly on PEARL until the separation gate is satisfied. |
| `ACQUIRING_TARGET` | PEARL is in StationKeep. The UAV follows PEARL's fresh reported position horizontally at the existing mission altitude and waits for the landing-target gates below. It aborts after 30 s without a valid lock. |
| `LANDING` | The target lock has remained valid for 2 s and the UAV has issued the internal precision-landing commit. ArduPilot now owns descent and target-loss handling. |
| `COMPLETE` | UAV reports landed; both coordinators publish completion. PEARL restores the deploy, return, station-keep, and manual-override values saved before recovery, so the Helm behavior resumes consistently with FSM history. |
| `ABORT` | Cancel the UAV route, put the UAV in its configured hold, and put PEARL in StationKeep. |

The UAV must have fresh navigation and must be armed, airborne, and healthy
before it may start. PEARL rejects stale navigation,
unhealthy requests, malformed proposals, invalid or stale battery data, and
timeouts. A low valid UAV battery raises recovery priority rather than
rejecting the aircraft; takeoff has the separate reserve gate described below.

## Reliable versus periodic transport

`pMediator` carries the actions that must survive an occasional dropped UDP
datagram:

- UAV-directed operator ARM, DISARM, TAKEOFF, route DEPLOY, and route CLEAR;
- UAV takeoff request, PEARL takeoff clearance, UAV rendezvous request, PEARL
  proposal, UAV acceptance/completion, and PEARL landing clearance;
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

UAV and PEARL pMediator envelopes and node reports also use direct unicast
pShare routes on their shared vehicle network. Shoreside uses the UAV's
Tailnet address directly. There is no PEARL UDP relay or automatic alternate
runtime path.

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
advance to `AGREEING_RENDEZVOUS` until the aggregate succeeds for the current
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

## SITL deck attachment

SITL and REAL keep ArduPilot authoritative for UAV navigation. In SITL,
PEARL's `uSimMarineV22` remains authoritative for PEARL's Helm-driven trajectory.
The UAV community runs `pMavlinkShipBridge`, taking `PEARL_NAV_*` from
PEARL's navigation-only pShare routes and native UAV telemetry via `pEchoVar`.
The C++ MOOS app sends that trajectory as MAVLink 2
`GLOBAL_POSITION_INT` (system 17) to TCP `127.0.0.1:5763`.

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
The mission seeds it true for its on-deck start, sets it false during secondary
landing/loiter diversion states, and re-enables it on primary approach/landing
(including target retries). It stays latched after those states, so a completed
Rally landing remains detached. `uSimShipStart`, `uSimShipDetach`, and
`uSimShipAttach` in `meta_uav.moos` own this policy; change the startup seed for an off-deck start.
Outside those active recovery states, an operator can also set the fact with
`uPokeDB targ_uav.moos SIM_UAV_ON_SHIP=false`. Stop poses before off-deck
landing, allowing the one-second SITL expiry window. If the fact is incorrectly
left true, **any** grounded UAV will intentionally be moved to the ship.

PEARL defaults to its original `-4,-22,0` start. Override it with
`./launch.sh --pearl_start=X,Y,H` or `./launch_pearl.sh --start_pos=X,Y,H`. The grounded UAV is attached to
that ship position; PEARL is not relocated. Configure the deck's absolute MSL
altitude with `ship_altitude_msl` in `meta_uav.moos` (default 0 m MSL, not height
above SITL home). For live height/heave input, set `ship_altitude_var` in that
block and refresh it within `input_timeout`. Landmark geometry uses the same
deck height. Deck roll/pitch are not modeled.

Time warp above 1 is supported. In SITL mode, `launch_uav.sh`
automatically passes the mission warp to the simulator as **SITL speedup**:

```bash
./launch.sh --mode=SITL 5
```

Use the same factor for split community launches, for example
`./launch_uav.sh --mode=SITL 5`. Freshness and publish periods are in simulated
seconds; CPU/network capacity must sustain the accelerated update rates.

`pMavlinkShipBridge` also emulates the landmark camera: it sends
`LANDING_TARGET` at 10 Hz only while fresh UAV/PEARL navigation is within the
configured **3-D** detection range (12 m default). It uses the UAV's attitude
yaw—not ground course—and MSL altitude to encode body-forward/right/down
position. The camera model assumes level roll/pitch. Inputs older than one
second or separation beyond the range stop target transmission. ArduPilot's
precision-landing target-loss policy then owns recovery.

Landmark range (`detection_range=12`), deck height, and takeoff altitude (8 m)
remain in their respective MOOS/BHV app configs, not launcher arguments. Edit the `pMavlinkShipBridge` block in
`meta_uav.moos` for range, height, connection, rate, IDs, and freshness settings.
The native takeoff altitude is in `plug_uav_ardupilot.moos`; route altitude is
in `meta_uav.bhv`. Secondary recovery coordinates are supplied to
`meta_uav.moos` with `--secondary_landing=X,Y` (default `10,15`). Bridge state is posted
as `MAVLINK_SHIP_BRIDGE_CONNECTED`, `MAVLINK_SHIP_POSE_ACTIVE`,
`LANDMARK_EMULATOR_ACTIVE`, and
`LANDMARK_EMULATOR_DISTANCE`. REAL mode does not run this emulator.

Regression checks: `python3 tests/test_ship_coupling.py` verifies the actual
mission-owned coupling policy across primary/secondary recovery cycles;
`python3 ../../../src/pMavlinkShipBridge/tests/test_bridge.py` verifies wire
format, range/condition independence, and constant/live deck height. The SITL
repository's `make test-deck` validates physical deck following, stale release,
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

`launch_sitl.sh` delegates to
`~/workspace/mit/pearl/ardupilot-sitl/bin/ardupilot-sitl`, passing `sitl.parm`
as an additional startup defaults file through `SITL_PARAM_FILE`. No Python
helper or host-side MAVLink package is required. Rebuild the Docker image after
installing the external-ship patch, and build `pMavlinkShipBridge` and
`pArduBridge` before launching. Use fresh SITL state: persisted parameter values
can override defaults. `PLND_TYPE=1` selects the MAVLink target backend and
`SIM_PLD_ENABLE=0` disables the competing stationary target simulator.

SITL uses PEARL's simulated battery, wind, and process-health telemetry to
exercise the same pre-commit platform gate enabled in REAL.

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
