#!/usr/bin/env python3
"""Contract checks for the independent rendezvous architecture.

The filename is retained for compatibility with existing test runners; landing
coordination is no longer implemented by pRendezvous.
"""

import pathlib
import subprocess
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
MISSION = REPO_ROOT / "missions/pearl_uav_integration_tests/rendezvous"
RENDEZVOUS_CPP = REPO_ROOT / "src/pRendezvous/Rendezvous.cpp"
LANDING_CPP = REPO_ROOT / "src/pLandingCoordinator/LandingCoordinator.cpp"


class IndependentRendezvousContract(unittest.TestCase):
    def test_rendezvous_has_no_role_or_authority_protocol(self):
        source = RENDEZVOUS_CPP.read_text()
        self.assertNotIn('param == "role"', source)
        self.assertNotIn("RENDEZVOUS_REQUEST", source)
        self.assertNotIn("RENDEZVOUS_RESPONSE", source)
        self.assertNotIn("LANDING_CLEARANCE", source)

    def test_helm_gate_notifications_are_string_booleans(self):
        source = RENDEZVOUS_CPP.read_text()
        self.assertIn('Notify(ownVar("ACTIVE"), "true")', source)
        self.assertIn('Notify(ownVar("WAYPOINT_READY"), "true")', source)
        self.assertIn('Notify("MOOS_MANUAL_OVERRIDE", "false")', source)
        self.assertNotIn('Notify(ownVar("ACTIVE"), true)', source)
        self.assertNotIn('Notify(ownVar("ACTIVE"), false)', source)

    def test_both_mission_instances_use_the_same_start_contract(self):
        uav = (MISSION / "meta_uav.moos").read_text()
        pearl = (MISSION / "meta_pearl.moos").read_text()
        for config in (uav, pearl):
            self.assertIn("RENDEZVOUS_START", config)
            self.assertNotIn("role      =", config)
        shoreside = (MISSION / "meta_shoreside.moos").read_text()
        self.assertIn("RENDEZVOUS_START_ALL=true", shoreside)
        self.assertIn("scope = RENDEZVOUS_UAV_MOTION", shoreside)
        self.assertIn("scope = RENDEZVOUS_PEARL_MOTION", shoreside)

    def test_mediated_motion_carries_phase_without_direct_peer_status_routes(self):
        source = RENDEZVOUS_CPP.read_text()
        self.assertIn('motion += "#phase=" + m_state', source)
        for field in ("point_x", "point_y", "freeze_time", "freeze_node"):
            self.assertIn(f'motion += "#{field}="', source)
        self.assertIn("freeze_time < m_freeze_time - timestamp_tolerance", source)
        self.assertIn("candidate_node < current_node", source)
        self.assertIn("m_planner.adoptFrozenPoint(m_freeze_x, m_freeze_y)", source)
        self.assertIn('Notify(ownVar("MOTION"), motion)', source)
        start_handler = source.split("bool Rendezvous::handleStart", 1)[1].split(
            "bool Rendezvous::handleAbort", 1
        )[0]
        self.assertIn("m_session = makeSession();", start_handler)
        self.assertIn('Notify(peerVar("PHASE"), peer_phase)', source)
        self.assertIn('Notify(peerVar("SESSION"), peer_session)', source)
        for name, peer_port, vname in (("meta_uav.moos", "PEARL_PSHARE", "UAV"),
                                       ("meta_pearl.moos", "UAV_PSHARE", "PEARL")):
            moos = (MISSION / name).read_text()
            pshare = moos.split("ProcessConfig = pShare", 1)[1].split("}", 1)[0]
            self.assertIn(f"route=localhost:$({peer_port})", pshare)
            self.assertIn("MEDIATED_MESSAGE_LOCAL", pshare)
            self.assertIn("ACK_MESSAGE_LOCAL", pshare)
            self.assertNotIn("NODE_REPORT_LOCAL", pshare)
            self.assertNotIn("RENDEZVOUS_PHASE", pshare)
            self.assertNotIn("RENDEZVOUS_SESSION", pshare)
            broker = moos.split("ProcessConfig = uFldNodeBroker", 1)[1].split("}", 1)[0]
            self.assertNotIn("RENDEZVOUS_PEER_", broker)
            self.assertIn(f"alias=RENDEZVOUS_{vname}_PHASE", broker)
            self.assertIn(
                f"src=RENDEZVOUS_OWN_MOTION, alias=RENDEZVOUS_{vname}_MOTION", broker
            )
            self.assertNotIn(f"RENDEZVOUS_{vname}_OWN_", broker)
        self.assertIn('return "RENDEZVOUS_OWN_" + suffix', source)
        self.assertIn('return "RENDEZVOUS_PEER_" + suffix', source)
        self.assertNotIn("rendezvous_prefix", source)
        self.assertIn('return (own < peer ? own + "_" + peer : peer + "_" + own) + "_" + to_string(now)', source)

    def test_each_vehicle_estimates_peer_speed_from_positions(self):
        source = RENDEZVOUS_CPP.read_text()
        self.assertIn("m_peer_speed_estimator.sample(stamp_s, x, y, m_nav_stale_thresh)", source)
        self.assertIn("m_peer_speed_estimator.sample(stamp, x, y, m_nav_stale_thresh)", source)
        self.assertIn("m_peer_speed_estimator.speed()", source)
        self.assertNotIn('tokStringParse(value, "speed"', source)

    def test_sitl_launch_uses_cli_runtime_options_and_no_rc_profile(self):
        launch = (MISSION / "launch.sh").read_text()
        self.assertIn("--no-rc", launch)
        self.assertIn('--home "$SITL_HOME"', launch)
        self.assertIn('--speedup "$TIME_WARP"', launch)
        self.assertNotIn("--param-file", launch)
        self.assertFalse((MISSION / "sitl.parm").exists())

    def test_sitl_auto_start_waits_for_startup_gate_before_arming(self):
        uav = (MISSION / "meta_uav.moos").read_text()
        self.assertIn("Run = uTimerScript       @ NewConsole = false ~ uAutoStartReady", uav)
        self.assertIn("event = var=UAV_AUTOSTART_READY, val=true, time=45", uav)
        auto_arm = uav.split("ProcessConfig = uAutoArm", 1)[1].split("}", 1)[0]
        self.assertIn("condition   = UAV_AUTOSTART_READY = true", auto_arm)
        behavior = (MISSION / "meta_uav.bhv").read_text()
        self.assertIn("initialize UAV_AUTOSTART_READY = false", behavior)

    def test_landing_is_a_separate_application(self):
        self.assertTrue(LANDING_CPP.exists())
        cmake = (REPO_ROOT / "src/pLandingCoordinator/CMakeLists.txt").read_text()
        self.assertIn("pLandingCoordinator", cmake)

    def test_built_binary_describes_independent_contract(self):
        binary = REPO_ROOT / "bin/pRendezvous"
        if not binary.exists():
            self.skipTest("pRendezvous has not been built")
        output = subprocess.run(
            [str(binary), "--example"], capture_output=True, text=True, check=True
        ).stdout
        self.assertIn("ownship", output)
        self.assertIn("peer_node", output)
        self.assertNotIn("role", output.lower())
        self.assertNotIn("landing_enabled", output)


if __name__ == "__main__":
    unittest.main()
