# PEARL/UAV landing integration test

A small SITL recovery mission, with rendezvous, landing coordination, visual
observations, and physical deck simulation kept separate.

## Recovery sequence

1. SITL startup returns with the UAV armed and airborne at 8 m; it then flies south along x=15;
   simulated PEARL loiters clockwise on a 15 m radius circle centered at
   (-15,-115), entirely west (left on the map) of the drone's lane. PEARL starts
   at (-15,-130), heading west. The circle is approximated by 12 loiter vertices.
   Rendezvous interrupts this loiter; it does not resume during landing.
2. The shoreside **RECOVER** button posts `RECOVER_UAV=true`. Only the drone
   receives the local `RECOVER` command.
3. The UAV's `pLandingCoordinator` creates a new session and requests recovery
   from PEARL through `pMediator` (`LANDING_EXCHANGE`). PEARL agrees.
4. After agreement, each coordinator starts its local `pRendezvous` instance.
   Rendezvous planning remains independent of landing coordination.
5. PEARL releases landing clearance only after both report completion of the
   new rendezvous. PEARL holds its position during recovery.
6. The UAV coordinator waits for clearance and fresh bridge-reported health,
   armed/airborne state, and a fresh valid BODY_FRD landing target, then sends
   `ARDU_COMMAND=AUTOLAND`. It handles `UAV_COMMAND_RESULT` with `command=LAND`.
7. ArduPilot owns precision guidance, descent, target-loss handling, and
   disarming. The coordinator completes after confirmed touchdown and disarm.
   The bridge parks Helm after LAND acceptance; hold/altitude behaviors remain
   available until that park, avoiding a missing-decision-variable handoff race.

There is no separate `PRECISION_LAND` bridge command or precision-specific
bridge policy. `pArduBridge` relays landing-target telemetry and executes its
existing generic `AUTOLAND` command. This mission's `sitl.parm` enables the
MAVLink precision backend (`PLND_ENABLED=1`, `PLND_TYPE=1`) and strict target-loss
handling (`PLND_STRICT=2`). These parameters are mission configuration, not
runtime parameter checks performed by the bridge.

## Target and elevated deck

`pMavlinkLandingTarget` runs on the UAV. It broadcasts `LANDING_TARGET` whenever
fresh PEARL/UAV geometry is within `--target_range` (default 12 m, **3-D**).
It does not subscribe to recovery sessions or clearance. Native UAV attitude yaw,
not ground course, is used to construct the body-frame observation. PEARL's
simulation position is bridged directly for this geometry; coordination is
mediated. The coordinator checks the target received over MAVLink by
`pArduBridge`, never the emulator's own active flag.

`--deck_altitude` defaults to **1 m MSL**; SITL home is 0 m MSL. The same
configured height feeds both target geometry and `pMavlinkShipBridge`. The
patched simulator uses PEARL's external poses to establish the physical deck
surface. Deck-pose streaming is enabled at clearance, not during airborne
startup, so it cannot move the UAV to PEARL prematurely. It remains enabled
following touchdown so the recovered drone moves with the deck.

The external deck patch applies its surface without a proximity gate; enable
it only for this cleared on-deck recovery, not an off-deck contingency landing.
MOOS does not force UAV altitude, navigation, touchdown, or disarm.

With `show_visuals=true` and conditions satisfied (true by default),
pMarineViewer receives:

- An outer circle of radius `target_range` centered on PEARL.
- A horizontal sphere cross-section of radius
  `sqrt(target_range^2 - (UAV_MSL - deck_MSL)^2)`.
- A lime target point centered on PEARL, replacing the former square.

Visuals persist outside detection range and with stale observation geometry;
only configured conditions gate them, once local target x/y are known. With no
conditions in this mission, they stay visible even when broadcasting stops.
The projected circle collapses to a 0.001 m drawing minimum above its sphere.
Rendezvous points retain
PEARL's blue and UAV's yellow styling; labels remain hidden.

## Running

Put shared `scripts`, PEARL binaries, UAV-base binaries, and MOOS binaries on
`PATH`. Build `pLandingCoordinator`, `pRendezvous`, `pMavlinkLandingTarget`,
`pMavlinkShipBridge`, `pMediator`, `pThrustMixer`, and `pArduBridge`. Use the
patched `ardupilot-sitl:local` image.

```bash
./launch.sh
./launch.sh --deck_altitude=1.5 --target_range=15
./launch.sh --pearl_x=-25 --pearl_radius=20
./launch.sh --help
./launch.sh -j               # generate configs only
./launch.sh -ng -x           # headless; stop with SIGINT
./clean.sh
```

`--pearl_x` sets the loiter center x, and `--pearl_radius` sets its radius
(minimum 5 m). Center y is midway between `--north` and `--south`; these remain
UAV lane endpoints. The launcher requires the entire circle to lie west of
`--uav_x` and PEARL speed to remain within (0, 0.5] m/s.

The launcher owns SITL and all three communities. It passes `--airborne`,
`--home`, `--speedup`, ports (`--port`, `--sim-port`, `--diagnostic-port`), and
`--param-file` through CLI options; airborne startup enables no-RC defaults.
`--sitl_diagnostic_port` allows isolated
observer connections without claiming the default diagnostics port. Logs are
in `LOG_sitl`, `LOG_antler`, and each vehicle's `LOG_*` directory.

Watch `LANDING_UAV_PHASE`, `LANDING_PEARL_PHASE`, their `*_SESSION` aliases,
`LANDING_PEARL_CLEARANCE`, `LANDING_UAV_CHECKS_READY`,
`UAV_LANDING_TARGET_AVAILABLE`, `UAV_COMMAND_RESULT`, and the existing
`RENDEZVOUS_UAV_*` / `RENDEZVOUS_PEARL_*` aliases on shoreside.

## Automated real-SITL validation

```bash
python3 tests/test_recovery_sitl.py
python3 tests/test_recovery_sitl.py --deck-altitude=1.5
```

This opt-in, headless test copies the mission into a fresh `/tmp` directory,
allocates separate controller, simulation, diagnostics, MOOS and pShare ports,
and uses its own Compose project. It does not restart or poke existing missions.
It sends `RECOVER_UAV` only through its isolated shoreside community and checks:

- Shared landing session, agreement before rendezvous, identical winning freeze,
  and both rendezvous completions before clearance.
- Target streaming before clearance, 3-D range gating, and a fresh received
  BODY_FRD target before exactly one `AUTOLAND`.
- ArduPilot's own `PrecLand: Target Found` / `PrecLand: Init Complete` messages,
  LAND acceptance, and Helm parking without a handoff guidance-hole warning.
- Elevated-deck touchdown, automatic disarm, and both coordinator completions.
- All three visualization messages reaching shoreside, the circle radii and
  point position (not GUI rendering).
- After disarm, deliberately stopping **only the test's** PEARL geometry link:
  observations stop, the bridge reports the target unavailable, and all three
  visuals persist because conditions remain true. This is not an airborne
  target-loss/retry test.

The 1 m and 1.5 m deck runs passed on 2026-10-09 at 2x speed. Final UAV MSL
heights were 1.08 m and 1.58 m respectively, with horizontal offsets from PEARL
of 0.031 m and 0.014 m. Logs, MAVLink diagnostics, generated configs, and
`result.json` remain at the artifact path printed by the test, including on
failure. Use `--output=/path/to/new/directory` to retain them elsewhere.

This establishes the simple recovery path, not exhaustive fault coverage.
Airborne target loss, cancellation, repeated sessions in one running mission,
and off-deck contingency behavior still require separate tests. The existing
parked-in-Guided monitor warning during planned takeoff is not changed here.
