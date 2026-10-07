# pOperator

`pOperator` replicates a declarative operation FSM between one requester and one
authority over direct MOOS/pShare transport. The authority is the only state
sequencer. Both peers load and validate the same TOML graph.

This application coordinates an operation; it does not decide whether a
physical vehicle is safe. Mission adapters must derive readiness and progress
from session-correlated evidence.

## Configuration

```moos
ProcessConfig = pTakeoffOperator
{
  AppTick   = 10
  CommsTick = 10
  identity   = uav
  authority  = pearl
  definition = operations/takeoff.toml
  state_file = state/uav-takeoff.state
}
```

The authority loads the same definition with `identity=pearl` and its own state
file. A default `.pOperator-<identity>-<operation>.state` is used if omitted.
State updates use a checksummed versioned checkpoint, write, fsync, atomic
rename, and checked directory fsync. A nonblocking lock prevents two processes
from owning one state file. The state directory must already exist; corrupt,
unreadable, incomplete, incompatible, or unresolved temporary checkpoints fail
startup. Runtime persistence failure latches `STORAGE_FAULT` and stops all
protocol input, retransmission, and authority commits. The mission must route the request wire variable requester-to-authority and the commit wire
variable authority-to-requester. Process aliases need not be globally unique.

## Protocol model

- The authority periodically advertises its current generation. The requester
  must receive that discovery before creating a generation-bound request.
- The requester creates an operation ID from an idempotent local command ID and
  retransmits one immutable request.
- The authority commits the initial phase and all subsequent canonical
  revisions.
- Requester transitions are correlated proposals, never local commits.
- Immediate commits contain one consecutive edge. Periodic `STATE_SNAPSHOT`
  messages permit catch-up after missed commits without replaying physical
  entry actions. Authority send timestamps prevent replayed equal-revision
  packets from refreshing the requester lease.
- Actor sequence numbers provide cumulative requester acknowledgement.
- Authority generations fence restarts. A requester does not silently adopt a
  new generation for an in-flight operation. A mismatch durably latches
  reconciliation-required and stops operation traffic; clearing it requires an
  explicit recovery procedure rather than a later heartbeat.
- Active state, proposals, bounded queued observations, consumed start-command
  IDs, cancellation intent, terminal decisions, and replay tombstones survive
  restart. Queue or command-ID cache exhaustion latches fail-closed.
- Terminal decisions are replayed for duplicate request IDs. If replay storage
  saturates, admission fails closed.
- Invalid, expired, future-dated, incompatible, stale-generation, and
  undeclared messages do not refresh the peer lease.

`AUTHORITY_TIMEOUT` and `AUTHORITY_RESTARTED` are requester-local conditions,
not fabricated shared phases.

## Declarative graph

```toml
[states]
pending = ["REQUESTED", "DEFERRED", "ACCEPTED", "ABORTING"]
success = ["COMPLETED"]
failure = ["REJECTED", "FAILED", "CANCELLED", "EXPIRED"]

[transitions.authority]
REQUESTED = ["DEFERRED", "ACCEPTED", "REJECTED"]
ABORTING = ["CANCELLED", "FAILED"]

[transitions.requester]
ACCEPTED = ["ABORTING"]

[transitions.protocol]
REQUESTED = ["EXPIRED"]
DEFERRED = ["EXPIRED"]
```

Declared edges are allowed; all others are forbidden. Startup rejects unknown sections and fields, unknown actors, overlapping state
classes, duplicate edges, edges from terminal states, undeclared states,
non-finite or invalid timing/cache values, and graphs
missing admission-policy edges. Multiline TOML state arrays are supported.
The wire compatibility hash is derived from a canonical typed representation,
including state-class labels and protocol-schema version, not comments or table
ordering. Wire JSON uses a strict typed parser and rejects unknown or malformed
fields.

Requester observation skips may be declared where physical telemetry can move
farther than the replicated phase during a partition. They are catch-up paths,
not permission to issue skipped physical actions.

Cancellation should normally transition to nonterminal `ABORTING`. Only the
authority may commit terminal `CANCELLED` after mission-specific evidence says
the shared resource is safe to release.

## MOOS interface

Requester starts an operation with an idempotent command:

```text
TAKEOFF_REQUEST=command_id=flight-42#start=true#payload=optional
```

Command IDs use letters, digits, `_`, `-`, or `.` and are durably remembered.
A retained or replayed command cannot create another operation. Bare Boolean
commands are accepted only when `require_correlated_commands=false`.

Safety-relevant local transition commands are correlated:

```text
TAKEOFF_TRANSITION=id=<operation-id>#revision=<revision>#to=DEPARTING
TAKEOFF_CANCEL=id=<operation-id>#revision=<revision>#cancel=true
```

`<OP>_PROGRESS` remains an alias for `<OP>_TRANSITION`. If a proposal is in flight or authority synchronization is unavailable, later
observations are retained in order up to `event_queue_size`. Overflow latches
`RECOVERY_REQUIRED`. Cancellation is persisted independently of any proposal and is retried against
later committed phases when permitted. It does not erase queued physical
observations; if cancellation is not legal in the current phase, an applicable
front observation may advance the physical state first.

Authority commands are also correlated:

```text
TAKEOFF_AUTHORITY_TRANSITION=id=<operation-id>#revision=<revision>#to=COMPLETED#reason=DEPARTURE_CONFIRMED
```

Admission capability is supplied as timestamped evidence:

```text
TAKEOFF_AUTHORITY_READY=ready=true#evidence_id=deck-42#observed_at=1790574064.2
```

It expires after `readiness_max_age`; stale or retained evidence is rejected. `<OP>_AUTHORITY_REASON` supplies diagnostics.

Primary outputs are:

```text
<OP>_PHASE
<OP>_PHASE_CLASS
<OP>_REVISION
<OP>_OPERATION_ID
<OP>_OPERATION_SNAPSHOT
<OP>_LOCAL_STATE
<OP>_RESPONSE_FRESH
<OP>_TRANSITION_RESULT
<OP>_TRANSITION_REASON
<OP>_STORAGE_FAULT
<OP>_EVENT_QUEUE_SATURATED
<OP>_START_CACHE_SATURATED
<OP>_RECONCILIATION_REQUIRED
```

The JSON snapshot is the atomic consumer interface. Separate scalar outputs are
for guards and diagnostics and must be correlated with operation ID/revision
when used for consequential behavior.

## Trust and resource boundaries

The protocol provides consistency and replay protection, not authentication.
Envelope identities remain self-declared; deployment must restrict direct
publishers and pShare routes.

Separate `pOperator` instances do not arbitrate a shared physical resource.
Takeoff and recovery adapters must use one authority-local reservation policy
before asserting readiness. A protocol terminal state must not release a deck,
landing area, or vehicle unless the operation graph requires the appropriate
physical settlement evidence.
