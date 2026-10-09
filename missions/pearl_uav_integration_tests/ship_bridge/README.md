# Ship attachment / detachment

Minimal two-vehicle SITL test of `pMavlinkShipBridge` and the external-ship patch.

- **PEARL** (`pearl`, blue) automatically loiters around `(0,-115)` on a
  12-vertex polygon of radius 15 m, at 0.5 m/s.
- **Drone** (`uav`, yellow) starts detached at `(25,-115)`, disarmed and grounded.
  Its test phase is **IDLE**, its Helm is **PARK**, and it has no behaviors,
  automatic arming, takeoff, guidance, landing, or flight-command buttons.
- **ATTACH** enables PEARL's external ship poses. SITL moves the grounded UAV
  onto PEARL and carries it while PEARL keeps loitering.
- **DETACH** stops the poses. After the simulator's **one simulated second**
  freshness deadline, the UAV stops following PEARL. Allow a little longer for
  GPS/EKF telemetry to settle. ATTACH may be repeated to reacquire the boat.

The initial state is **detached**. No mediator, rendezvous, landing coordinator,
landing-target stream, or received-target viewer is involved. Vehicle and loiter
labels are hidden; AppCasts and shoreside status scopes remain available.

## Launch

As with the other missions, project/UAV-base binaries, MOOS tools, shared scripts,
and the `moos-vnc-session` viewer wrapper must be on `PATH`. Use the patched
`ardupilot-sitl:local` image with `patches/external-ship.patch` installed.

```bash
./launch.sh 2             # GUI and uMAC; matched MOOS warp / SITL speedup
./launch.sh -ng -x 2      # headless, stop with Ctrl-C
./launch.sh -j            # strict config generation only, no simulator/apps
./launch.sh --help
```

Geometry options: `--center_x`, `--center_y`, `--loiter_radius`, `--pearl_speed`,
`--uav_x`, `--uav_y`, and `--deck_altitude`. Deck height defaults to **1 m MSL**;
SITL home/ordinary ground is **0 m MSL**. All positions are local UTM offsets from
`plug_origin_warp.moos`; home conversion uses the shared `local_to_latlon.py`.

The launcher uses the shared `launch_ardupilot_sitl.sh` with `--no-rc`, the
extensively documented four-entry `sitl.parm`, and CLI-only home/speedup/endpoints.
No precision-landing parameters are enabled by this overlay. Default community
ports are 9000/9001/9002, pShare 9200/9201/9202, controller 5760, simulation router
5763, and diagnostics 5762; every port can be overridden (see `--help`). Stop
other missions yourself before reusing their ports, or choose distinct ports.

Each launch owns a unique Compose project and its own pAntler children. Cleanup
stops only those resources, saves SITL output under `LOG_sitl`, and leaves other
missions alone. `./clean.sh` removes this directory's generated configs/logs,
not running processes or retained test artifacts in `/tmp`.

## Controls and status

The only two configured GUI buttons send addressed commands to **PEARL**, not
flight commands to the UAV:

```moos
button_1 = ATTACH # SHIP_POSE_ENABLED_PEARL=true
button_2 = DETACH # SHIP_POSE_ENABLED_PEARL=false
```

For headless testing, the same commands are:

```bash
uPokeDB targ_shoreside.moos SHIP_POSE_ENABLED_PEARL:=true
uPokeDB targ_shoreside.moos SHIP_POSE_ENABLED_PEARL:=false
```

`uFldShoreBroker` forwards this through an addressed qbridge to PEARL's local
`SHIP_POSE_ENABLED`. A distinct echoed status, `SHIP_ATTACHMENT_REQUESTED`,
returns to shore: **do not export the input command itself**, because pShare
suppresses inbound variables it also exports, even when the export has an alias.

- `SHIP_ATTACHMENT_REQUESTED`: latched requested enable state, not physical proof.
- `MAVLINK_SHIP_BRIDGE_CONNECTED`: simulation transport connected.
- `MAVLINK_SHIP_POSE_ACTIVE`: actual fresh-pose transmission, not physical proof.
- Native UAV/PEARL node reports: physical tracking and release evidence.
- `UAV_IS_ARMED`, `UAV_LANDED_STATE`, `UAV_TEST_PHASE`, `UAV_HELM_STATE`:
  aircraft remains disarmed, on ground, IDLE/PARK throughout.

No UAV position is substituted with PEARL navigation. `pEchoVar` only copies
bridge-received native UAV telemetry into normal `NAV_*` for the node reporter.
The ship app sends native MAVLink poses on its own connection; ArduPilot/SITL
owns the physical coupling.

## Limitations

**Simulation only.** The custom patch has **no horizontal proximity gate**:
ATTACH can teleport a grounded drone from anywhere onto the ship center. This
mission intentionally exercises that behavior, not a real fastening mechanism,
landing approach, or proximity check. The patch treats the deck altitude as the
available ground surface while poses are fresh. On DETACH from an elevated deck,
the disarmed aircraft can settle down to ordinary ground; this is **not hovering**
or an airborne release maneuver. Do not use these buttons as airborne recovery
controls or as evidence that a real aircraft is physically attached.

## Tests

```bash
python3 tests/test_launch.py            # config-only, no live mission
python3 tests/test_attachment_sitl.py   # opt-in real SITL, isolated ports/project
```

The real test retains `/tmp/ship-attachment-*` artifacts, uses the same shoreside
payloads as the buttons, and checks initial detachment, following a moving PEARL,
release while PEARL continues loitering, reattachment, and final release. It
checks native positions and deck height, both node reports, disarmed/grounded
telemetry throughout, a permanently parked UAV Helm, and absence of flight
command publications.

Validated on 2026-10-09 at 2x speedup: attach error **0.20 m**, reattach error
**0.34 m**, detached UAV drift **0.008 m** while PEARL continued moving.
