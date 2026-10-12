#!/usr/bin/env python3
"""Protect the approved FSM topology and integration ownership contract."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import tomllib

MISSION = Path(__file__).resolve().parents[1]


def topology(value):
    if isinstance(value, dict):
        return {
            key: topology(item)
            for key, item in value.items()
            if key not in ("when", "entry_effects", "exit_effects", "entry_epoch")
        }
    return value


class IntegrationContract(unittest.TestCase):
    def test_states_transitions_targets_and_layout_are_unchanged(self):
        baseline = json.loads((MISSION / "tests/fsm_graph_baseline.json").read_text())
        for name, expected in baseline.items():
            model = tomllib.loads((MISSION / name).read_text())
            self.assertEqual(
                {key: topology(model[key]) for key in ("machine", "states")},
                expected,
                name,
            )

    def test_fsm_owns_lifecycle_without_tasking_timers(self):
        config = (MISSION / "meta_uav.moos").read_text()
        self.assertNotIn("ProcessConfig = uTasking", config)
        self.assertNotIn("ProcessConfig = uOperatorPrecLand", config)
        model = tomllib.loads((MISSION / "uav_fsm.toml").read_text())
        self.assertTrue(
            model["states"]["mission_execution"]["entry_effects"]["route_execute"]
        )
        recovery = model["states"]["landing_operations"]["states"]
        self.assertTrue(recovery["requesting_recovery"]["entry_effects"]["recover"])
        self.assertTrue(recovery["approaching"]["entry_effects"]["sim_uav_on_ship"])
        self.assertNotIn("ardu_command", recovery["approaching"]["entry_effects"])
        self.assertIn(
            "command_condition = UAV_FSM_LANDING_OPERATIONS_STATE=APPROACHING", config
        )
        coordinator = config.split("ProcessConfig = pLandingCoordinator", 1)[1].split("}", 1)[0]
        self.assertIn("align_yaw = $(YAW_ALIGNMENT)", coordinator)
        self.assertIn("yaw_stability_time = 0.5", coordinator)
        self.assertNotIn("observation_pose_samples_var", coordinator)
        self.assertIn("Run = uChecks        @ NewConsole = false ~ uLandingYawChecks", config)
        yaw_check = config.split("ProcessConfig = uLandingYawChecks", 1)[1].split("}", 1)[0]
        self.assertIn("condition = UAV_FSM_LANDING_OPERATIONS_STATE=APPROACHING", yaw_check)
        self.assertIn("var=LANDING_OWN_YAW_STATE,type=string,equals=ALIGNED", yaw_check)
        self.assertIn("var=LANDING_OWN_YAW_STATE,type=string,equals=DISABLED", yaw_check)
        committed = recovery["approaching"]["transitions"]["landing_committed"]["when"]
        self.assertIn("check_result(landing_yaw_checks, success)", committed)

    def test_uav_fsm_releases_route_buffer_ownership_without_topology_changes(self):
        model = tomllib.loads((MISSION / "uav_fsm.toml").read_text())
        self.assertFalse(model["facts"]["route_buffer_command"]["initialize"])
        for effects in (model["states"]["idle"]["entry_effects"],
                        model["states"]["mission_execution"]["exit_effects"]):
            self.assertEqual(effects["route_buffer_command"], "action=clear")
            self.assertFalse(effects["route_execute"])
            self.assertFalse(effects["route_deploy"])
        self.assertIn("route_identity_var = UAV_ROUTE_ACTIVE_ID",
                      (MISSION / "meta_uav.moos").read_text())

    def test_normal_route_lead_and_final_slowdown_are_scoped(self):
        def parameters(block):
            result = {}
            for line in block.splitlines():
                line = line.split("//", 1)[0].strip()
                key, separator, value = line.partition("=")
                if separator:
                    result[key.strip()] = value.strip()
            return result

        def behaviors(source):
            return {
                fields["name"]: fields
                for block in source.split("Behavior = ")[1:]
                for fields in (parameters(block.split("}", 1)[0]),)
            }

        source = (MISSION / "meta_uav.bhv").read_text()
        with tempfile.TemporaryDirectory(prefix="route-guidance-config-") as directory:
            target = Path(directory) / "uav.bhv"
            subprocess.run(
                [
                    "nsplug", str(MISSION / "meta_uav.bhv"), str(target),
                    "--strict", "--force", "-x", "VNAME=uav",
                    "SECONDARY_LANDING_X=10", "SECONDARY_LANDING_Y=15",
                ],
                check=True, capture_output=True, text=True,
            )
            for config in (source, target.read_text()):
                blocks = behaviors(config)
                route = blocks["operator_route_"]
                for key, value in (
                    ("lead", "5"), ("lead_to_start", "true"),
                    ("end_spd", "slow_dist=8, stop_dist=0"),
                    ("speed", "3"), ("speed_alt", "3"),
                    ("capture_radius", "4"), ("slip_radius", "5"),
                ):
                    self.assertEqual(route[key], value, key)
                self.assertNotIn("lead_damper", route)
                self.assertEqual(route["endflag"], "ARDU_COMMAND=BRAKE_AND_HOLD")
                for name in ("uav_rendezvous", "uav_secondary_transit"):
                    self.assertEqual(blocks[name]["endflag"], "ARDU_COMMAND=HOLD_POSITION")
                    self.assertNotIn("lead", blocks[name])
                    self.assertNotIn("lead_to_start", blocks[name])
                    self.assertEqual(
                        blocks[name]["end_spd"], "slow_dist=5, stop_dist=0"
                    )

    def test_both_rendezvous_planners_gate_freeze_on_stable_fresh_history(self):
        for name in ("uav", "pearl"):
            source = (MISSION / f"meta_{name}.moos").read_text()
            block = source.split("ProcessConfig = pRendezvous", 1)[1].split("}", 1)[0]
            self.assertIn("planning_history_window = 3", block)
            self.assertIn("planning_max_positive_accel = 0.5", block)
            self.assertIn("point_freeze_radius = 6", block)

    def test_only_approved_shore_policies_are_configured(self):
        common = (
            "IVPHELM_STATE",
            "LANDING_OWN_PHASE",
            "LANDING_OWN_SESSION",
            "LANDING_OWN_REASON",
        )
        expected = {
            "uav": dict.fromkeys(
                common
                + (
                    "LANDING_OWN_CHECKS_READY",
                    "LANDING_OWN_TARGET_FRESH",
                    "LANDING_OWN_YAW_STATE",
                    "LANDING_OWN_YAW_INPUT_REASON",
                    "LANDING_YAW_CHECKS",
                    "LANDING_OWN_RENDEZVOUS_PHASE",
                    "LANDING_TARGET_VIEW_ACTIVE",
                    "LANDMARK_EMULATOR_ACTIVE",
                    "LANDMARK_EMULATOR_CONDITIONS_MET",
                    "MAVLINK_LANDING_TARGET_CONNECTED",
                    "MAVLINK_SHIP_BRIDGE_CONNECTED",
                    "MAVLINK_SHIP_POSE_ACTIVE",
                    "TAKEOFF_PRECHECKS_SNAPSHOT",
                    "TAKEOFF_AIRBORNE_SNAPSHOT",
                ),
                ("1", "coalesce"),
            ),
            "pearl": dict.fromkeys(
                common
                + (
                    "LANDING_OWN_CLEARANCE",
                    "TAKEOFF_PRECHECKS_STATE_SNAPSHOT",
                    "TAKEOFF_POSTCHECKS_STATE_SNAPSHOT",
                    "RECOVERY_PRECHECKS_STATE_SNAPSHOT",
                    "RECOVERY_POSTCHECKS_STATE_SNAPSHOT",
                ),
                ("1", "coalesce"),
            ),
        }
        expected["uav"].update(
            {
                "NODE_REPORT_LOCAL": ("4", "latest"),
                "LANDMARK_EMULATOR_DISTANCE": ("2", "latest"),
                "LANDING_OWN_YAW_ERROR": ("2", "latest"),
                "NAV_ALTITUDE": ("5", "latest"),
                "UAV_ALTITUDE_MSL": ("5", "latest"),
            }
        )
        for suffix in (
            "AVAILABLE",
            "AGE",
            "TIME_USEC",
            "SYSTEM_ID",
            "COMPONENT_ID",
            "TARGET_NUM",
            "FRAME",
            "TYPE",
            "POSITION_VALID",
            "ANGLE_X",
            "ANGLE_Y",
            "DISTANCE",
            "SIZE_X",
            "SIZE_Y",
            "X",
            "Y",
            "Z",
            "Q_W",
            "Q_X",
            "Q_Y",
            "Q_Z",
        ):
            expected["uav"]["UAV_LANDING_TARGET_" + suffix] = ("2", "latest")
        for topic in (
            "ALFA_SIGNAL_DBM",
            "ALFA_SIGNAL_AVG_DBM",
            "ALFA_TX_BITRATE_MBPS",
            "ALFA_RX_BITRATE_MBPS",
            "ALFA_TX_RETRIES_TOTAL",
            "ALFA_TX_FAILED_TOTAL",
            "ALFA_INACTIVE_MS",
        ):
            expected["pearl"][topic] = ("1", "latest")
        for name, graphics in (
            ("uav", ("POINT", "CIRCLE", "POLYGON", "SEGLIST", "MARKER", "VECTOR")),
            ("pearl", ("POINT", "CIRCLE", "POLYGON", "SEGLIST")),
        ):
            expected[name].update(
                {"VIEW_" + suffix: ("1", "coalesce") for suffix in graphics}
            )
            actual = {}
            config = (MISSION / f"meta_{name}.moos").read_text()
            for line in config.splitlines():
                directive, _, body = line.strip().partition("=")
                directive = directive.strip()
                if directive not in ("bridge", "output"):
                    continue
                options = {
                    key.strip(): value.strip()
                    for item in body.split(",")
                    for key, _, value in (item.partition("="),)
                }
                if "frequency" not in options and "behavior" not in options:
                    continue
                src = options.get("src", options.get("src_name"))
                self.assertIsNotNone(src, line)
                self.assertNotIn(src, actual, line)
                actual[src] = (options.get("frequency"), options.get("behavior"))
                if directive == "output":
                    self.assertEqual(
                        options["route"], "$(SHORE_IP):$(SHORE_PSHARE)", line
                    )
            self.assertEqual(actual, expected[name], name)

    def test_protocol_routes_not_coalesced_and_peer_authority_routes_intact(self):
        for name in ("uav", "pearl"):
            config = (MISSION / f"meta_{name}.moos").read_text()
            pshare = config.split("ProcessConfig = pShare", 1)[1].split("}", 1)[0]
            self.assertNotIn("coalesce_route", config)
            self.assertNotIn("behavior=drop", config)
            for line in pshare.splitlines():
                if "src_name=MEDIATED_TX_" in line or "src_name=ACK_TX_" in line:
                    self.assertNotIn("frequency=", line)
                    self.assertNotIn("behavior=", line)
            self.assertIn("output = src_name=MEDIATED_TX_SHORESIDE", pshare)
            self.assertIn("output = src_name=ACK_TX_SHORESIDE", pshare)
            self.assertNotIn("src_name=MEDIATED_MESSAGE_LOCAL", pshare)
            self.assertNotIn("src_name=ACK_MESSAGE_LOCAL", pshare)
            self.assertNotIn("bridge = src=MEDIATED_MESSAGE_LOCAL", config)
            self.assertNotIn("bridge = src=ACK_MESSAGE_LOCAL", config)
            self.assertIn("Run = pMediatorRouter", config)
            router = config.split("ProcessConfig = pMediatorRouter", 1)[1].split(
                "}", 1
            )[0]
            self.assertIn("allow_relay = false", router)
        shore = (MISSION / "meta_shoreside.moos").read_text()
        self.assertNotIn("coalesce_route", shore)
        self.assertIn("qbridge = HEARTBEAT", shore)
        self.assertNotIn("bridge = src=MEDIATED_TX_", shore)
        self.assertNotIn("bridge = src=ACK_TX_", shore)
        self.assertNotIn("qbridge = MEDIATED_MESSAGE", shore)
        self.assertNotIn("qbridge = ACK_MESSAGE", shore)
        self.assertNotIn("bridge = src=MEDIATED_MESSAGE_LOCAL", shore)
        self.assertNotIn("bridge = src=ACK_MESSAGE_LOCAL", shore)
        self.assertNotIn("ACK_MESSAGE_ALL", shore)
        self.assertNotIn("MEDIATED_MESSAGE_ALL", shore)
        self.assertNotIn("uFieldRelay", shore)
        self.assertIn("allow_relay = false", shore)
        for node, peers in (
            ("uav", ("pearl", "shoreside")),
            ("pearl", ("uav", "shoreside")),
            ("shoreside", ("uav", "pearl")),
        ):
            config = (MISSION / f"meta_{node}.moos").read_text()
            pshare = config.split("ProcessConfig = pShare", 1)[1].split("}", 1)[0]
            self.assertNotIn("MEDIATED_MESSAGE_IN", config)
            self.assertNotIn("ACK_MESSAGE_IN", config)
            self.assertNotIn("#ifdef DIRECT_PEER false", pshare)
            for peer in peers:
                prefix = "SHORE" if peer == "shoreside" else peer.upper()
                route = f"route=$({prefix}_IP):$({prefix}_PSHARE)"
                self.assertIn(
                    f"output = src_name=MEDIATED_TX_{peer.upper()}, dest_name=MEDIATED_MESSAGE, {route}",
                    pshare,
                )
                self.assertIn(
                    f"output = src_name=ACK_TX_{peer.upper()}, dest_name=ACK_MESSAGE, {route}",
                    pshare,
                )

    def test_sender_only_protocol_in_strict_generated_configs(self):
        root = MISSION.parents[2]
        addresses = {
            "shoreside": ("192.0.2.10", "13200"),
            "uav": ("192.0.2.11", "13201"),
            "pearl": ("192.0.2.12", "13202"),
        }
        with tempfile.TemporaryDirectory(prefix="sender-only-config-") as directory:
            for node in addresses:
                for mode in ("SITL", "REAL"):
                    for direct in ("true", "false"):
                        with self.subTest(node=node, mode=mode, direct=direct):
                            values = {
                                "WARP": "1",
                                "MOOS_PORT": "19000",
                                "IP_ADDR": addresses[node][0],
                                "PSHARE_PORT": addresses[node][1],
                                "SHORE_IP": addresses["shoreside"][0],
                                "SHORE_PSHARE": addresses["shoreside"][1],
                                "UAV_IP": addresses["uav"][0],
                                "UAV_PSHARE": addresses["uav"][1],
                                "PEARL_IP": addresses["pearl"][0],
                                "PEARL_PSHARE": addresses["pearl"][1],
                                "DIRECT_PEER": direct,
                                "LAUNCH_GUI": "no",
                                "VNAME": node,
                                "COLOR": "yellow",
                                "PLATFORM_LENGTH": "1.5",
                                "START_POS": "0,0,0",
                                "SPEED": "0.5",
                                "MIN_STATE_TIME": "0",
                                "XMODE": "SIM"
                                if node == "pearl" and mode == "SITL"
                                else mode,
                                "IS_SIM": str(mode == "SITL").lower(),
                                "AP_URL": "127.0.0.1:5760",
                                "AP_PROTOCOL": "tcp",
                                "SITL_ROUTER_PORT": "5763",
                                "DECK_ALTITUDE": "1",
                                "TARGET_RANGE": "12",
                                "YAW_ALIGNMENT": "true",
                                "SECONDARY_LANDING_X": "10",
                                "SECONDARY_LANDING_Y": "15",
                                "RALLY_X": "10",
                                "RALLY_Y": "15",
                                "PEARL_LIB_DIR": str(root / "lib"),
                                "SHERLOCK_METRICS_HOST": "127.0.0.1",
                                "SHERLOCK_METRICS_PORT": "9273",
                            }
                            target = Path(directory) / f"{node}-{mode}-{direct}.moos"
                            subprocess.run(
                                [
                                    "nsplug",
                                    str(MISSION / f"meta_{node}.moos"),
                                    str(target),
                                    "--strict",
                                    "--force",
                                    "-x",
                                    f"--path={MISSION}:{root.parent / 'moos-ivp-uav-base/missions/uav_solo/briggs_test'}",
                                    *(
                                        f"{key}={value}"
                                        for key, value in values.items()
                                    ),
                                ],
                                check=True,
                                capture_output=True,
                                text=True,
                            )
                            config = target.read_text()
                            pshare = config.split("ProcessConfig = pShare", 1)[1].split(
                                "}", 1
                            )[0]
                            self.assertNotIn("MEDIATED_MESSAGE_IN", config)
                            self.assertNotIn("ACK_MESSAGE_IN", config)
                            self.assertNotIn("allow_relay = true", config)
                            for peer, (host, port) in addresses.items():
                                if peer == node:
                                    continue
                                for prefix, inbox in (
                                    ("MEDIATED_TX", "MEDIATED_MESSAGE"),
                                    ("ACK_TX", "ACK_MESSAGE"),
                                ):
                                    self.assertIn(
                                        f"output = src_name={prefix}_{peer.upper()}, dest_name={inbox}, route={host}:{port}",
                                        pshare,
                                    )

    def test_target_shore_rate_does_not_change_native_producer(self):
        uav = (MISSION / "meta_uav.moos").read_text()
        target = uav.split("ProcessConfig = pMavlinkLandingTarget", 1)[1].split("}", 1)[
            0
        ]
        self.assertIn("publish_rate_hz     = 10", target)
        pshare = uav.split("ProcessConfig = pShare", 1)[1].split("}", 1)[0]
        self.assertNotIn("frequency", pshare)
        self.assertIn("bridge = src=VIEW_POLYGON,frequency=1,behavior=coalesce", uav)

    def test_target_viewer_and_independent_components(self):
        uav = (MISSION / "meta_uav.moos").read_text()
        pearl = (MISSION / "meta_pearl.moos").read_text()
        for config in (uav, pearl):
            self.assertIn("Run = pLandingCoordinator", config)
            planner = config.split("ProcessConfig = pRendezvous", 1)[1].split("}", 1)[0]
            self.assertNotIn("role", planner)
            self.assertNotIn("landing", planner)
            self.assertIn("point_label = false", planner)
        self.assertIn("Run = pLandingTargetViewer", uav)
        self.assertIn("bridge = src=VIEW_POLYGON", uav)
        self.assertIn(
            "agreement_condition = PEARL_FSM_SUPPORT_ACTIVITY_STATE=CALCULATING_RENDEZVOUS",
            pearl,
        )
        self.assertIn("session_var = LANDING_OWN_SESSION", pearl)
        postchecks = pearl.split("ProcessConfig = uChecksRecoveryPost", 1)[1].split("}", 1)[0]
        self.assertIn("session_var = LANDING_OWN_SESSION", postchecks)
        self.assertIn("result_interval = 1", postchecks)
        self.assertNotIn("output = src_name=UAV_RENDEZVOUS_PHASE", uav)
        self.assertIn("show_visuals = true", uav)

    def test_drone_hold_graphics_hidden_without_disabling_guidance(self):
        source = (MISSION / "meta_uav.bhv").read_text()
        hold = source.split("name = uav_recovery_hold", 1)[1].split("}", 1)[0]
        self.assertIn("edge_color=invisible", hold)
        self.assertIn("vertex_color=invisible", hold)
        self.assertIn("label_color=invisible", hold)
        self.assertIn("edge_size=0", hold)
        self.assertIn("center_activate = true", hold)
        bridge = (MISSION / "plug_uav_ardupilot.moos").read_text()
        self.assertIn("show_loiter_visuals            = false", bridge)
        self.assertNotIn(
            "show_loiter_visuals", (MISSION / "meta_pearl.moos").read_text()
        )

    def test_shoreside_control_arrows_and_link_pulses_hidden(self):
        config = (MISSION / "meta_shoreside.moos").read_text()
        self.assertIn("vector_viewable_all = false", config)
        self.assertIn("comms_pulse_viewable_all = false", config)
        self.assertIn("pulse_duration = 0", config)

    def test_background_images_match_each_site_datum(self):
        config = (MISSION / "meta_shoreside.moos").read_text()
        origin_config = (MISSION / "plug_origin_warp.moos").read_text()

        def site_branches(source):
            charles, briggs = source.split("#ifdef SITE charles", 1)[1].split(
                "#else", 1
            )
            return {"charles": charles, "briggs": briggs.split("#endif", 1)[0]}

        def parameters(source):
            return {
                key.strip(): value.strip()
                for line in source.splitlines()
                if "=" in line and not line.strip().startswith("//")
                for key, value in (line.strip().split("=", 1),)
            }

        image_branches = site_branches(config)
        origin_branches = site_branches(origin_config)
        for site in ("charles", "briggs"):
            line = next(
                line for line in image_branches[site].splitlines()
                if line.strip().startswith("tiff_file")
            )
            image = (MISSION / line.split("=", 1)[1].strip()).resolve()
            self.assertTrue(image.is_file(), f"{site}: {image}")
            info = image.with_suffix(".info")
            self.assertTrue(info.is_file(), f"{site}: {info}")
            origin = parameters(origin_branches[site])
            datum = parameters(info.read_text())
            self.assertAlmostEqual(
                float(origin["LatOrigin"]), float(datum["datum_lat"]), places=9
            )
            self.assertAlmostEqual(
                float(origin["LongOrigin"]), float(datum["datum_lon"]), places=9
            )

    def test_pearl_default_start_matches_requested_latitude_longitude(self):
        positions = []
        for file, variable in (
            ("launch.sh", "PEARL_START"),
            ("launch_pearl.sh", "START_POS"),
        ):
            line = next(
                line
                for line in (MISSION / file).read_text().splitlines()
                if line.startswith(variable + "=")
            )
            positions.append(line.split("=", 1)[1].strip('"'))
        self.assertEqual(positions[0], positions[1])
        x, y, heading = positions[0].split(",")
        self.assertEqual(float(heading), 0)
        origin = {}
        for line in (MISSION / "plug_origin_warp.moos").read_text().splitlines():
            if "=" in line and line.strip().startswith(("LatOrigin", "LongOrigin")):
                key, value = line.split("=", 1)
                origin[key.strip()] = value.strip()
        result = subprocess.check_output(
            [
                sys.executable,
                str(MISSION.parents[2] / "scripts/local_to_latlon.py"),
                origin["LatOrigin"],
                origin["LongOrigin"],
                x,
                y,
            ],
            text=True,
        )
        latitude, longitude = map(float, result.split())
        self.assertAlmostEqual(latitude, 42.356424, places=7)
        self.assertAlmostEqual(longitude, -71.098393, places=7)

    def test_rally_commit_gate_and_shared_offnominal_guidance(self):
        shore = (MISSION / "meta_shoreside.moos").read_text()
        uav = (MISSION / "meta_uav.moos").read_text()
        behaviors = (MISSION / "meta_uav.bhv").read_text()
        self.assertIn(
            'left_context[uav-rally] = RALLY_POINT = "x=$[XPOS],y=$[YPOS]"', shore
        )
        self.assertIn(
            "button_2 = TASK-UAV # ROUTE_BUFFER_TASK=uav",
            shore,
        )
        self.assertIn("task_submit_flag = uav:RALLY_TASK_COMMIT=true", shore)
        self.assertIn("check=FLIGHT_PLAN,var=RALLY_TASK_STATE,type=string,equals=READY",
                      (MISSION / "meta_uav.moos").read_text())
        self.assertNotIn("button_2 = TASK-UAV # TASK_EXECUTE_UAV=true", shore)
        self.assertIn("ProcessConfig = pRallyPoint", shore)
        self.assertIn("ProcessConfig = pRallyPoint", uav)
        self.assertIn("check=FLIGHT_PLAN,var=UAV_RALLY_READY,type=double,equals=1", uav)
        self.assertNotIn("check = name=RALLY", uav)
        self.assertIn("helm_state_var = IVPHELM_STATE", uav)
        self.assertIn(
            "require_verified_rally         = true",
            (MISSION / "plug_uav_ardupilot.moos").read_text(),
        )
        self.assertIn("updates = RALLY_WAYPOINT_UPDATE", behaviors)
        self.assertIn("updates = RALLY_ALTITUDE_UPDATE", behaviors)
        self.assertIn("altitude = 15", behaviors)
        for variable in ("RALLY_ACTIVE", "RALLY_TASK_STATE", "UAV_RALLY_READY"):
            self.assertIn(f"bridge = src={variable}", uav)

    def test_shared_drawing_and_pearl_route_lifecycle(self):
        shore = (MISSION / "meta_shoreside.moos").read_text()
        pearl = (MISSION / "meta_pearl.moos").read_text()
        bhv = (MISSION / "meta_pearl.bhv").read_text()
        self.assertEqual(shore.count("ProcessConfig = pRouteBuffer"), 1)
        self.assertIn("left_context[route] = ROUTE_POINT", shore)
        self.assertNotIn("left_context[uav-route]", shore)
        self.assertIn("button_1 = TASK-PEARL # ROUTE_BUFFER_TASK=pearl", shore)
        self.assertNotIn("button_1 = TASK-PEARL # TASK_EXECUTE_PEARL", shore)
        self.assertIn("right_context[route-clear] = ROUTE_BUFFER_CLEAR_STAGED=true", shore)
        self.assertIn("Run = pRouteBuffer", pearl)
        self.assertIn("task_execute_var = TASK_EXECUTE", pearl)
        self.assertIn("route_ready_name_var = ROUTE_READY_NAME", pearl)
        self.assertNotIn("Behavior = BHV_Loiter", bhv)
        self.assertNotIn("duration = 240", bhv)
        self.assertIn("name = pearl_main_task_", bhv)
        self.assertIn("spawnflag = ROUTE_READY_NAME=$[BHVNAME]", bhv)
        self.assertIn("endflag = MAIN_TASK_STATUS=completed", bhv)
        self.assertIn("points         = 10.6,4.8", bhv)

    def test_native_navigation_not_simulator_coordinates(self):
        config = (MISSION / "meta_uav.moos").read_text()
        self.assertIn("Echo = UAV_X        -> NAV_X", config)
        self.assertNotIn("PEARL_NAV_X -> NAV_X", config)
        self.assertNotIn("GPSGLITCH", (MISSION / "sitl.parm").read_text())
        self.assertIn(
            "geodesy = utm", (MISSION / "plug_uav_ardupilot.moos").read_text()
        )


if __name__ == "__main__":
    unittest.main()
