# PEARL/UAV rendezvous integration test

A minimal two-vehicle mission exercising independent `pRendezvous` instances.
There is no rendezvous role, request/response handshake, completion authority,
landing clearance, or landing behavior in this app. Landing coordination is a
separate concern owned by `pLandingCoordinator`.

- **UAV**: ArduCopter SITL through `pArduBridge`. SITL startup leaves it armed
  and airborne at 8 m, then it flies south along its lane.
- **PEARL**: `uSimMarineV22` starts at the south end and heads north.
- **DEPLOY** (pMarineViewer button) publishes `RENDEZVOUS_START_ALL` at
  shoreside. `uFldShoreBroker` independently delivers local
  `RENDEZVOUS_START` notifications to both vehicles.
- Each vehicle publishes its own position and measured speed history, uses
  the peer history to calculate its own waypoint, and follows that waypoint.
  Each vehicle independently enters `COMPLETE` when its own waypoint is
  reached, then holds/station-keeps at that endpoint. Neither vehicle can
  complete or authorize the other.

Default geometry (local metres, origin at the MIT Sailing Pavilion datum):

| Vehicle | Lane x | Start y | Heading | Lane end y | Speed |
| --- | --- | --- | --- | --- | --- |
| UAV | 15 | -40 | south | -190 | 1.5 m/s |
| PEARL | -15 | -190 | north | -40 | 0.5 m/s |

A vehicle that reaches its lane end before DEPLOY holds there; the independent
start still works from a hover/hold.

## Running

```bash
./launch.sh            # SITL + uav + pearl + shoreside, then uMAC
./launch.sh --help     # geometry, speeds, ports, time warp
./launch.sh -j         # only generate targ_* files
./clean.sh
```

`launch.sh` owns the SITL container and all three communities. Quitting uMAC or
interrupting the launcher stops everything. With `-x`, it skips uMAC and runs
until SIGINT/SIGTERM. The launcher passes the computed SITL home, time warp, and
`--airborne` home and time warp through CLI options; airborne startup enables
no-RC defaults itself. This mission needs no parameter-file overrides.
Simulator output is in `LOG_sitl/`, and pAntler output is in `LOG_antler/`.

Requirements: the `ardupilot-sitl:local` image and wrapper, plus built
`pRendezvous`, `pLandingCoordinator`, `pMediator`, `pThrustMixer`, and
`pArduBridge`.

## What to watch

In each vehicle's local MOOSDB, `RENDEZVOUS_OWN_STATE`, `RENDEZVOUS_OWN_PHASE`,
`RENDEZVOUS_OWN_SESSION`, `RENDEZVOUS_OWN_POINT`, and `RENDEZVOUS_OWN_SPEED`
refer to that vehicle. `RENDEZVOUS_PEER_PHASE`, `RENDEZVOUS_PEER_SESSION`, and
`RENDEZVOUS_PEER_SPEED` refer to its peer. Session IDs contain both lowercase
vehicle names in alphabetical order, followed by a timestamp.

`RENDEZVOUS_MOTION` carries position, timestamp, phase, session, rendezvous
point, and freeze metadata through `pMediator`; each vehicle computes both speed
estimates locally from position samples. Each app also publishes its outgoing
message as `RENDEZVOUS_OWN_MOTION`, bridged to shoreside as
`RENDEZVOUS_UAV_MOTION` or `RENDEZVOUS_PEARL_MOTION` for monitoring. Direct peer
pShare routes are limited to `MEDIATED_MESSAGE` and its acknowledgements. Only
`OWN` status is bridged to shoreside under aliases such as
`RENDEZVOUS_UAV_PHASE`; peer status is not bridged there. The `RENDEZVOUS_`
prefix is hardcoded by `pRendezvous` and the corresponding Helm conditions.

Map display hides vehicle names, waypoint labels, station-keeping labels, and
loiter/hold markers. Each vehicle keeps its own rendezvous point visible in its
color (PEARL `dodger_blue`, UAV `yellow`). The display is configured by the
`point_*` options in the two `pRendezvous` blocks:

| Option | Meaning |
| --- | --- |
| `point_shape` | `point`, `circle` outline, or filled `disc` |
| `point_radius` | Circle/disc radius in metres |
| `point_color` | Any MOOS color name |
| `point_transparency` | Disc fill: 0 opaque to 1 clear |
| `point_label` | Whether to show the rendezvous label; false hides it |
