# Static landing-target test

UAV-only SITL mission exercising `pMavlinkLandingTarget`, not recovery or landing.
There is no peer vehicle, mediation, rendezvous, landing coordinator, ship model,
or elevated-deck attachment. SITL startup leaves the drone armed and airborne;
it flies three north/south passes, then holds position without landing.

## Geometry and passes

The static target defaults to local **(0, -115)**, **1 m MSL**, with yaw
**30 degrees clockwise from map north**. The launcher converts its local
position to latitude/longitude using the same UTM inverse as the UAV home.
`uStaticTarget` refreshes that constant geometry for the normal freshness gate.
Target altitude is independent of UAV altitude (`target_altitude_msl` in
`meta_uav.moos`; the app also supports a live `target_altitude_var`).

Left/right refer to west/east in the map frame, not the target's rotated axes.
Each pass is 50 m long, with altitude changes and lateral repositioning at the
ends, away from the target. The drone waits for the next altitude before flying
that next pass.

| Pass | Path | Side offset | UAV altitude |
|---|---|---|---|
| 1 | North to south | 3 m west (`x=-3`) | 8 m |
| 2 | South to north | 5 m east (`x=5`) | 4 m |
| 3 | North to south | Directly over target (`x=0`) | 15 m |

The launcher requests an 8 m AGL airborne start for the first pass. The built-in
model's ground/home is 0 m MSL, and flight altitudes are relative to it. ArduPilot's
reported MSL heights may differ slightly with the simulated sensor/body offset.
The target is a point, so its yaw has no drawn orientation or effect on the
position-only observation. The BODY_FRD vector uses the **drone's native yaw**.

## Observation and display gates

The default detection range is **12 m in 3-D**. With the target at 1 m MSL,
minimum distances on the three passes are approximately 7.62 m, 5.83 m and
14 m. Thus the first two passes produce observations near the target; the
third deliberately tests range rejection.

No `condition` entries are configured: conditions default **true**. Target
visuals remain displayed even while the drone is out of range, before mission
start, or while the MAVLink connection is unavailable. They include:

- A lime target point (`VIEW_POINT`), replacing the former square.
- A lime outer detection-radius circle.
- A yellow horizontal cross-section circle, using the UAV/target MSL difference.
  Above the sphere, it collapses to the renderer's 0.001 m minimum radius.

Only conditions gate the display, after the target's finite local x/y are
known. Fresh geographic geometry, all configured conditions, distance within
range, and a connected link are required to transmit `LANDING_TARGET`.

`pLandingTargetViewer` separately draws a **magenta received-data footprint**
from `pArduBridge` telemetry only. It appears only with recent valid observations
(default maximum age 0.5 real seconds), and disappears on the third pass. The
emulator currently sends zero angular sizes, so `target_size=1` supplies a 1 m
square. Orientation comes from the received quaternion: the emulator encodes
its 30-degree local-grid target heading relative to UAV yaw, so the footprint
stays at 30 degrees on both N/S passes. The viewer does not read simulator
heading directly. See the
[viewer contract](../../../src/pLandingTargetViewer/README.md) for size estimates,
orientation conventions, and the level-attitude reconstruction limitation.

For optional gating, add one or more entries in `pMavlinkLandingTarget`:

```moos
condition = CAMERA_ENABLED=true
condition = QUALITY>=2
```

All conditions must be true. Their MOOS variables are automatically subscribed;
unknown variables and malformed conditions block both display and observations.
Condition variables retain their last received values; `input_timeout` applies
to observation geometry, not condition variables. This mission deliberately
uses no such gates to keep its target visible.

## Running

Put shared `scripts`, project binaries, UAV-base binaries, and MOOS executables
on `PATH`; use the `ardupilot-sitl` wrapper/image.

```bash
./launch.sh                    # viewer + automatic flight
./launch.sh -ng -x 2           # headless at 2x; stop with SIGINT
./launch.sh -j                 # generate configs only
./launch.sh --help
./launch.sh --target_range=20  # third pass also enters observation range
```

The wrapper receives `--airborne`, home, speedup, and separate controller/
simulation/diagnostics ports via CLI; airborne startup enables no-RC defaults.
There is no mission `.parm` overlay: this test does
not enable precision-landing guidance or the ship simulator. `pArduBridge`
relays the actual received target packets independently of landing control.
The generic Copter Helm sender uses fresh native `IN_AIR` telemetry, allowing
this mission's 4 m pass rather than the legacy 5 m altitude-based airborne flag.

Watch `TARGET_TEST_PHASE`, `UAV_ALTITUDE_MSL`, `LANDMARK_EMULATOR_ACTIVE`,
`LANDMARK_EMULATOR_DISTANCE`, `UAV_LANDING_TARGET_AVAILABLE`,
`UAV_LANDING_TARGET_FRAME`, and `LANDING_TARGET_VIEW_ACTIVE` on shoreside.
Labels remain hidden.

## Validation

```bash
python3 tests/test_passes_sitl.py
python3 ../../../src/pMavlinkLandingTarget/tests/test_target.py
python3 ../../../src/pLandingTargetViewer/tests/test_viewer.py
```

The opt-in SITL test copies this mission to `/tmp`, uses separate ports and an
owned Compose project, checks the three offsets/heights and actual bridge
observations, and confirms that visuals remain active on the third pass.
Artifacts stay at the printed path. Existing missions are neither stopped nor
poked. The native app tests capture and checksum MAVLink packets, exercise
multiple/string/numeric/invalid conditions, and verify that distance, stale
geometry and disconnected links stop observations but do not hide the target.

The 2x SITL run with the target at **1 m MSL** and the received-data viewer passed
on 2026-10-09: UAV MSL heights 8.07 m, 4.08 m and 15.08 m. Observations and the
received-data footprint appeared on passes 1 and 2 and were suppressed on pass 3;
the source point and range visuals remained visible throughout. A subsequent
run also verified that every active received polygon retained the 30-degree
heading on both passes, despite the UAV reversing direction.
