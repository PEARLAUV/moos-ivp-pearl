# pMediatorRouter

A small, protocol-aware destination dispatcher for pMediator's message and
ACK envelopes. pShare remains an opaque transport; it never examines payloads
to determine a receiver. This app is separate from the pinned pMediator binary.

## Interface

Inputs:
- `MEDIATED_MESSAGE_LOCAL`, `ACK_MESSAGE_LOCAL`: outgoing pMediator envelopes.
- `MEDIATED_MESSAGE_IN`, `ACK_MESSAGE_IN`: transport/router ingress.

Outputs:
- `MEDIATED_TX_<UPPERCASE_DEST>`, `ACK_TX_<UPPERCASE_DEST>`: exactly one
  destination-specific outgoing channel for each valid envelope.
- `MEDIATED_MESSAGE`, `ACK_MESSAGE`: local pMediator inboxes, only when an
  ingress envelope names this node as its final destination.
- APPCAST: configuration, rejected inputs, local posts and per-destination posts.

Example on UAV:

```moos
ProcessConfig = pMediatorRouter
{
  AppTick = 40
  CommsTick = 40
  vname = uav
  peer = pearl
  peer = shoreside
  allow_relay = false
}
```

## Outgoing-only direct configuration (Briggs)

Run the dispatcher on each node for its outgoing messages **and ACKs**. Deliver
transport directly to the receiver's native mediator inbox; do not configure
any transport outputs to `MEDIATED_MESSAGE_IN` or `ACK_MESSAGE_IN`. Those
subscriptions remain available in the app but are unused in this configuration.
Set `allow_relay=false` everywhere.

Matching pShare routes are explicit, unlimited, and content-agnostic:

```moos
output = src_name=MEDIATED_TX_PEARL,dest_name=MEDIATED_MESSAGE,route=pearl-host:9202
output = src_name=ACK_TX_PEARL,dest_name=ACK_MESSAGE,route=pearl-host:9202
output = src_name=MEDIATED_TX_SHORESIDE,dest_name=MEDIATED_MESSAGE,route=shore-host:9200
output = src_name=ACK_TX_SHORESIDE,dest_name=ACK_MESSAGE,route=shore-host:9200
```

Briggs also uses static shore-to-vehicle routes for both peers, so all six
message/ACK paths exist without broker enrollment. If another mission needs
broker-discovered direct shore outputs, it can instead use:

```moos
bridge = src=MEDIATED_TX_$V,alias=MEDIATED_MESSAGE
bridge = src=ACK_TX_$V,alias=ACK_MESSAGE
```

Do not combine static and brokered routes to different hosts for the same peer;
that would duplicate transport. Neither approach requires a receiving
dispatcher pass.

Remove old source-wide exports of MEDIATED_MESSAGE_LOCAL/ACK_MESSAGE_LOCAL and
old mediated/ACK qbridges. Do not export the final canonical mediator inbox
keys: besides cross-delivery, registering them as pShare output sources can
suppress incoming transport messages.

## Ownership and limitations

- Sender names must match vname on local channels. Incoming names must be
  configured peers. Endpoint names are exact-case; uppercase topic suffixes
  must be unique. Invalid, unknown, duplicate-address, self-addressed,
  multi-destination and unexpanded-group envelopes fail closed.
- Raw string values, IDs, acknowledgement policy and source timestamps are
  copied unchanged. The router does not unpack application payloads, execute
  commands, acknowledge, retry, de-duplicate, or rate-limit.
- Relay is disabled by default. `allow_relay=true` explicitly dispatches a
  peer's transit envelope to its final destination-specific output, never to
  the relay's own mediator. This capability is not used by Briggs; relay is
  disabled on all three nodes.
- No route discovery, automatic failover, hop tracking or loop prevention for
  arbitrary mesh topologies. Briggs uses only direct protocol links; optional
  peer telemetry settings do not enable protocol transit.
- No authentication or cryptographic spoofing protection is provided.
- Post counts are local MOOS posts, not proof of remote delivery. Missing
  transport routes do not create readiness evidence; pMediator's normal
  retries/ACK tracking and failure policy remain authoritative.
- Native pMediator sequence-zero priming remains unchanged. Peer restarts and
  broader recovery scenarios are not newly certified by this dispatcher.

## Verification

```sh
cmake --build build --target pMediatorRouter -j4
python3 src/pMediatorRouter/tests/test_routing.py
```

The real-MOOS test uses owned localhost ports/process groups and no flight
apps. It covers outgoing-only dispatch with direct native mediator reception,
all six directions with static routes, and separately the actual shore broker's
$V expansion to native inboxes. It exercises deliberate early ACK loss and
retransmission, de-duplication, cancellation, byte-identical payload strings
with commas/braces, timestamps, correct ACK destinations and invalid outgoing
envelopes. No `*_IN` messages or foreign-source relay outputs may appear.
Each intended payload is dispatched exactly once and all tested pMediator
instances have zero run warnings. Logs remain under
`/tmp/mediated-routes-*` for diagnosis.
