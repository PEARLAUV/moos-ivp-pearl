#!/usr/bin/env python3
"""Real MOOS permission guards without changing coordinator default behavior."""

import os
import time
import unittest

import test_landing_checks as fixture


class GuardTests(fixture.LandingChecksTest):
    def setUp(self):
        self.role = (
            "platform"
            if self._testMethodName.startswith("test_platform")
            else "aircraft"
        )
        if self.role == "platform":
            self.extra_config = """agreement_condition = FSM_STAGE=CALCULATING
  rendezvous_condition = FSM_STAGE=CALCULATING
  clearance_condition = (FSM_STAGE=POSTCHECKS) and (POSTCHECKS=success)"""
        elif self._testMethodName.startswith("test_aircraft"):
            self.extra_config = """rendezvous_condition = FSM_STAGE=CALCULATING
  command_condition = FSM_STAGE=APPROACHING"""
        else:
            self.extra_config = ""
        if self._testMethodName == "test_platform_late_peer_completion_keeps_live_check_permission":
            self.input_timeout = 3
            self.extra_config = "clearance_condition = (FSM_STAGE=POSTCHECKS) and (POSTCHECKS=success)"
            self.extra_apps = f"""Run = {fixture.ROOT / "bin/uChecks"} @ NewConsole = false ~ uPostchecks
  Run = {fixture.MOOS_BIN / "uTimerScript"} @ NewConsole = false ~ uPostcheckInputs"""
            # Empty override lets the same regression reproduce the original,
            # change-only producer's stale-success failure before rebuilding.
            interval = os.environ.get("UCHECKS_TEST_RESULT_INTERVAL", "result_interval = 0.25")
            self.extra_process_config = f"""
ProcessConfig = uPostchecks
{{
  AppTick = 20
  CommsTick = 20
  condition = FSM_STAGE=POSTCHECKS
  session_var = LANDING_OWN_SESSION
  result_var = POSTCHECKS
  result_prefix = POSTCHECK_
  {interval}
  check = name=READY,timeout=30,max_age=1
  require = check=READY,var=CHECK_INPUT,type=double,equals=1,on_mismatch=fail
}}
ProcessConfig = uPostcheckInputs
{{
  AppTick = 20
  CommsTick = 20
  condition = REFRESH_POSTCHECK_INPUTS=true
  paused = false
  reset_max = unlimited
  reset_time = 0.1
  event = var=CHECK_INPUT,val=1,time=0
  event = var=FSM_STAGE,val=POSTCHECKS,time=0
  event = var=NAV_X,val=0,time=0
  event = var=NAV_Y,val=0,time=0
}}
"""
        super().setUp()

    def test_platform_checks_gate_agreement_and_clearance(self):
        session, started = "platform-test", time.time() - 1
        self.poke("FSM_STAGE:=PRECHECKS", "POSTCHECKS:=inactive", "NAV_X=0", "NAV_Y=0")
        self.peer(session, started, "REQUESTED", "IDLE")
        self.query("LANDING_OWN_PHASE=REQUESTED", "LANDING_DECK_ENABLED=false")
        self.poke("FSM_STAGE:=none")
        self.query("LANDING_OWN_PHASE=REQUESTED")
        self.poke("FSM_STAGE:=CALCULATING")
        self.query("LANDING_OWN_PHASE=AGREED")
        self.peer(session, started, "RENDEZVOUS", "IDLE")
        self.query("LANDING_OWN_PHASE=RENDEZVOUS")
        self.poke("RENDEZVOUS_OWN_SESSION:=new-rv")
        self.poke("RENDEZVOUS_OWN_PHASE:=COMPLETE")
        self.peer(session, started, "RENDEZVOUS")
        self.poke("FSM_STAGE:=POSTCHECKS")
        self.query("LANDING_OWN_PHASE=RENDEZVOUS", "LANDING_DECK_ENABLED=false")
        self.poke("POSTCHECKS:=success")
        self.query("LANDING_OWN_PHASE=CLEARED", "LANDING_DECK_ENABLED=true")

    def test_platform_late_peer_completion_keeps_live_check_permission(self):
        session, started = "late-peer", time.time() - 1
        self.poke("FSM_STAGE:=CALCULATING", "NAV_X=0", "NAV_Y=0")
        self.peer(session, started, "REQUESTED", "IDLE")
        self.query("LANDING_OWN_PHASE=AGREED")
        self.peer(session, started, "RENDEZVOUS", "IDLE")
        self.query("LANDING_OWN_PHASE=RENDEZVOUS")
        self.poke(
            "RENDEZVOUS_OWN_SESSION:=late-peer-rv",
            "RENDEZVOUS_OWN_PHASE:=COMPLETE",
            "REFRESH_POSTCHECK_INPUTS:=true",
        )
        self.query("POSTCHECKS=success", "LANDING_DECK_ENABLED=false")
        # The aircraft arrives after the original success's three-second lease,
        # while the real checks producer keeps re-evaluating fresh local input.
        time.sleep(4)
        self.poke("RENDEZVOUS_OWN_PHASE:=COMPLETE")
        self.peer(session, started, "RENDEZVOUS", "COMPLETE")
        self.query("LANDING_OWN_PHASE=CLEARED", "LANDING_DECK_ENABLED=true")

    def test_aircraft_fsm_and_frozen_age_cannot_bypass_landing_guards(self):
        self.poke("ARDU_COMMAND:=NONE", "FSM_STAGE:=REQUESTING", "RECOVER:=true")
        session = self.query(
            "LANDING_OWN_PHASE=REQUESTED", variable="LANDING_OWN_SESSION"
        )
        started = time.time() - 1
        self.peer(session, started, "AGREED", "IDLE")
        self.query("LANDING_OWN_PHASE=REQUESTED")
        self.poke("FSM_STAGE:=CALCULATING")
        self.query("LANDING_OWN_PHASE=RENDEZVOUS")
        self.poke("RENDEZVOUS_OWN_SESSION:=new-rv")
        self.poke("RENDEZVOUS_OWN_PHASE:=COMPLETE")
        self.peer(session, started, "CLEARED")
        self.poke(
            "UAV_HEALTH_ALL_OK=1",
            "UAV_HEALTH_AGE=0.1",
            "UAV_IS_ARMED=1",
            "UAV_LANDED_STATE_AVAILABLE=1",
            "UAV_LANDED_STATE:=IN_AIR",
            "UAV_LANDED_STATE_AGE=0.1",
            "UAV_LANDING_TARGET_AVAILABLE=1",
            "UAV_LANDING_TARGET_POSITION_VALID=1",
            "UAV_LANDING_TARGET_FRAME=12",
            "UAV_LANDING_TARGET_AGE=0.1",
        )
        self.query("LANDING_OWN_PHASE=RENDEZVOUS", "ARDU_COMMAND=NONE")
        time.sleep(0.6)
        self.poke("FSM_STAGE:=APPROACHING")
        self.query("LANDING_OWN_TARGET_FRESH=false", "ARDU_COMMAND=NONE")
        self.poke("TEST_REFRESH_TARGET:=true")
        self.query("LANDING_OWN_PHASE=COMMANDING", "ARDU_COMMAND=AUTOLAND")


if __name__ == "__main__":
    unittest.main()
