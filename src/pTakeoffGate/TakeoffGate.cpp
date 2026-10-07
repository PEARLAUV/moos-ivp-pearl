/************************************************************/
/*    NAME: Charles Benjamin                                */
/*    ORGN: MIT, Cambridge MA                               */
/*    FILE: TakeoffGate.cpp                                 */
/*    DATE: August 17th, 2026                               */
/************************************************************/

#include <algorithm>
#include <cmath>
#include "ACTable.h"
#include "MBUtils.h"
#include "NodeMessage.h"
#include "TakeoffGate.h"

using namespace std;

TakeoffGate::TakeoffGate()
  : m_role("uav"),
    m_peer_node("pearl"),
    m_request_timeout(15.0),
    m_coordination_step_delay(1.0),
    m_min_uav_soc(30.0),
    m_min_pearl_soc(15.0),
    m_max_wind_speed(4.0),
    m_input_max_age(3.0),
    m_fsm_state_max_age(2.5),
    m_request_pending(false),
    m_awaiting_clearance(false),
    m_request_time(0.0),
    m_coordination_state("IDLE"),
    m_coordination_state_time(0.0),
    m_uav_operation_state("none"),
    m_pearl_operation_state("none"),
    m_pearl_support_activity_state("none"),
    m_pearl_takeoff_support_state("none"),
    m_pearl_fsm_runtime_state("NOT_READY"),
    m_uav_operation_state_time(-1.0),
    m_pearl_operation_state_time(-1.0),
    m_pearl_support_activity_state_time(-1.0),
    m_pearl_takeoff_support_state_time(-1.0),
    m_pearl_fsm_runtime_state_time(-1.0),
    m_pearl_fsm_heartbeat_time(-1.0),
    m_pearl_deploy(false),
    m_pearl_return(false),
    m_pearl_station_keep(false),
    m_pearl_manual_override(true),
    m_pearl_deploy_set(false),
    m_pearl_return_set(false),
    m_pearl_station_keep_set(false),
    m_pearl_manual_override_set(false),
    m_pearl_control_saved(false),
    m_saved_pearl_deploy(false),
    m_saved_pearl_return(false),
    m_saved_pearl_station_keep(false),
    m_saved_pearl_manual_override(true),
    m_last_status_post(-1.0),
    m_last_ready(false),
    m_last_reason("INPUTS_UNAVAILABLE"),
    m_requests_approved(0),
    m_requests_rejected(0)
{
}

bool TakeoffGate::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  for(auto &msg : NewMail) {
    const string key = msg.GetKey();
    bool handled = true;

    if((m_role == "uav") && (key == "UAV_TAKEOFF_REQUEST")) {
      bool requested = false;
      handled = readBool(msg, requested);
      if(handled && requested)
        m_request_pending = true;
    }
    else if((m_role == "pearl") && (key == "TAKEOFF_REQUEST"))
      handled = handleTakeoffRequest(msg);
    else if((m_role == "uav") && (key == "TAKEOFF_CLEARANCE"))
      handled = handleTakeoffClearance(msg);
    else if((m_role == "pearl") && (key == "UAV_FSM_OPERATION_STATE")) {
      if(msg.IsString()) {
        m_uav_operation_state = toupper(stripBlankEnds(msg.GetString()));
        m_uav_operation_state_time = MOOSTime();
        handled = true;
      }
    }
    else if((m_role == "pearl") && (key == "PEARL_FSM_OPERATION_STATE")) {
      if(msg.IsString()) {
        m_pearl_operation_state = toupper(stripBlankEnds(msg.GetString()));
        m_pearl_operation_state_time = MOOSTime();
        handled = true;
      }
    }
    else if((m_role == "pearl") &&
            (key == "PEARL_FSM_SUPPORT_ACTIVITY_STATE")) {
      if(msg.IsString()) {
        m_pearl_support_activity_state =
          toupper(stripBlankEnds(msg.GetString()));
        m_pearl_support_activity_state_time = MOOSTime();
        handled = true;
      }
    }
    else if((m_role == "pearl") &&
            (key == "PEARL_FSM_TAKEOFF_SUPPORT_STATE")) {
      if(msg.IsString()) {
        m_pearl_takeoff_support_state =
          toupper(stripBlankEnds(msg.GetString()));
        m_pearl_takeoff_support_state_time = MOOSTime();
        handled = true;
      }
    }
    else if((m_role == "pearl") &&
            (key == "PEARL_FSM_RUNTIME_STATE")) {
      if(msg.IsString()) {
        m_pearl_fsm_runtime_state =
          toupper(stripBlankEnds(msg.GetString()));
        m_pearl_fsm_runtime_state_time = MOOSTime();
        handled = true;
      }
    }
    else if((m_role == "pearl") &&
            (key == "PEARL_FSM_RUNTIME_HEARTBEAT")) {
      handled = msg.IsDouble();
      if(handled)
        m_pearl_fsm_heartbeat_time = MOOSTime();
    }
    else if((m_role == "pearl") && (key == "DEPLOY")) {
      handled = readBool(msg, m_pearl_deploy);
      m_pearl_deploy_set = handled;
    }
    else if((m_role == "pearl") && (key == "RETURN")) {
      handled = readBool(msg, m_pearl_return);
      m_pearl_return_set = handled;
    }
    else if((m_role == "pearl") && (key == "STATION_KEEP")) {
      handled = readBool(msg, m_pearl_station_keep);
      m_pearl_station_keep_set = handled;
    }
    else if((m_role == "pearl") && (key == "MOOS_MANUAL_OVERRIDE")) {
      handled = readBool(msg, m_pearl_manual_override);
      m_pearl_manual_override_set = handled;
    }
    else if(key == "UAV_BATTERY_SOC")
      handled = readDouble(msg, m_uav_soc);
    else if(key == "UAV_BATTERY_DATA_VALID")
      handled = readBoolValue(msg, m_uav_battery_valid);
    else if(key == "PEARL_BATTERY_SOC")
      handled = readDouble(msg, m_pearl_soc);
    else if(key == "PEARL_BATTERY_DATA_VALID")
      handled = readBoolValue(msg, m_pearl_battery_valid);
    else if(key == "PEARL_WIND_SPEED")
      handled = readDouble(msg, m_wind_speed);
    else if(key == "PEARL_WIND_DATA_VALID")
      handled = readBoolValue(msg, m_wind_valid);
    else if(key != "APPCAST_REQ")
      handled = false;

    if(!handled && key != "APPCAST_REQ")
      reportRunWarning("Unhandled or invalid mail: " + key);
  }

  return true;
}

bool TakeoffGate::OnConnectToServer()
{
  registerVariables();
  return true;
}

bool TakeoffGate::Iterate()
{
  AppCastingMOOSApp::Iterate();

  if(m_role == "uav")
    iterateUAV();
  else
    iteratePearl();

  AppCastingMOOSApp::PostReport();
  return true;
}

bool TakeoffGate::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();

  STRING_LIST sParams;
  m_MissionReader.EnableVerbatimQuoting(false);
  if(!m_MissionReader.GetConfiguration(GetAppName(), sParams))
    reportConfigWarning("No config block found for " + GetAppName());

  for(const string &orig : sParams) {
    string line = orig;
    const string param = tolower(biteStringX(line, '='));
    const string value = line;
    bool handled = false;

    if(param == "role") {
      string role = tolower(stripBlankEnds(value));
      handled = ((role == "uav") || (role == "pearl"));
      if(handled)
        m_role = role;
    }
    else if(param == "ownship")
      handled = setNonWhiteVarOnString(m_ownship, value);
    else if(param == "peer_node")
      handled = setNonWhiteVarOnString(m_peer_node, value);
    else if(param == "request_timeout")
      handled = setPosDoubleOnString(m_request_timeout, value);
    else if(param == "coordination_step_delay")
      handled = setNonNegDoubleOnString(m_coordination_step_delay, value);
    else if(param == "min_uav_soc")
      handled = setPercentConfig(value, m_min_uav_soc);
    else if(param == "min_pearl_soc")
      handled = setPercentConfig(value, m_min_pearl_soc);
    else if(param == "max_wind_speed")
      handled = setNonNegDoubleOnString(m_max_wind_speed, value);
    else if(param == "input_max_age")
      handled = setPosDoubleOnString(m_input_max_age, value);
    else if(param == "fsm_state_max_age")
      handled = setPosDoubleOnString(m_fsm_state_max_age, value);

    if(!handled)
      reportUnhandledConfigWarning(orig);
  }

  if(m_ownship == "")
    m_ownship = m_host_community;

  registerVariables();
  return true;
}

void TakeoffGate::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  Register("PEARL_BATTERY_SOC", 0);
  Register("PEARL_BATTERY_DATA_VALID", 0);
  Register("PEARL_WIND_SPEED", 0);
  Register("PEARL_WIND_DATA_VALID", 0);

  if(m_role == "uav") {
    Register("UAV_TAKEOFF_REQUEST", 0);
    Register("TAKEOFF_CLEARANCE", 0);
    Register("UAV_BATTERY_SOC", 0);
    Register("UAV_BATTERY_DATA_VALID", 0);
  }
  else {
    Register("TAKEOFF_REQUEST", 0);
    Register("UAV_FSM_OPERATION_STATE", 0);
    Register("PEARL_FSM_OPERATION_STATE", 0);
    Register("PEARL_FSM_SUPPORT_ACTIVITY_STATE", 0);
    Register("PEARL_FSM_TAKEOFF_SUPPORT_STATE", 0);
    Register("PEARL_FSM_RUNTIME_STATE", 0);
    Register("PEARL_FSM_RUNTIME_HEARTBEAT", 0);
    Register("DEPLOY", 0);
    Register("RETURN", 0);
    Register("STATION_KEEP", 0);
    Register("MOOS_MANUAL_OVERRIDE", 0);
  }
}

void TakeoffGate::iterateUAV()
{
  const string reason = gateReason();
  const bool ready = (reason == "READY");

  if(m_request_pending) {
    m_request_pending = false;
    Notify("UAV_TAKEOFF_REQUEST", 0.0);
    Notify("UAV_TAKEOFF_APPROVED", 0.0);

    if(!ready) {
      ++m_requests_rejected;
      Notify("UAV_TAKEOFF_RESULT", "status=REJECTED,reason=" + reason);
      reportEvent("Takeoff rejected locally: " + reason);
    }
    else if(m_awaiting_clearance) {
      Notify("UAV_TAKEOFF_RESULT",
             "status=PENDING,reason=PEARL_CLEARANCE_PENDING");
    }
    else {
      m_session_id = m_ownship + "_" + doubleToStringX(MOOSTime(), 3);
      string request = "id=" + m_session_id + "#status=requested";
      if(sendMessage(m_peer_node, "TAKEOFF_REQUEST", request)) {
        m_awaiting_clearance = true;
        m_request_time = MOOSTime();
        Notify("UAV_TAKEOFF_RESULT",
               "status=PENDING,reason=PEARL_CLEARANCE_PENDING");
        reportEvent("Requested takeoff clearance from PEARL");
      }
      else {
        ++m_requests_rejected;
        Notify("UAV_TAKEOFF_RESULT",
               "status=REJECTED,reason=REQUEST_SEND_FAILED");
      }
    }
  }

  if(m_awaiting_clearance &&
     ((MOOSTime() - m_request_time) > m_request_timeout)) {
    m_awaiting_clearance = false;
    ++m_requests_rejected;
    Notify("UAV_TAKEOFF_APPROVED", 0.0);
    Notify("UAV_TAKEOFF_RESULT",
           "status=REJECTED,reason=PEARL_CLEARANCE_TIMEOUT");
    reportEvent("Takeoff rejected: PEARL clearance timeout");
  }

  if((MOOSTime() - m_last_status_post) >= 1.0 ||
     ready != m_last_ready || reason != m_last_reason) {
    Notify("UAV_TAKEOFF_GATE_READY", ready ? 1.0 : 0.0);
    Notify("UAV_TAKEOFF_GATE_REASON", reason);
    m_last_status_post = MOOSTime();
    m_last_ready = ready;
    m_last_reason = reason;
  }
}

void TakeoffGate::iteratePearl()
{
  const string reason = pearlGateReason();
  const bool ready = (reason == "READY");
  const double now = MOOSTime();
  const double elapsed = now - m_coordination_state_time;
  string fsm_reason;
  const bool fsm_available = pearlFSMAvailable(fsm_reason);

  if(m_coordination_state == "REQUESTED") {
    if(elapsed > m_request_timeout)
      rejectPearlRequest("FSM_PRECHECK_TIMEOUT");
    else if(fsm_available &&
            fsmValueFresh(m_pearl_support_activity_state_time) &&
            (m_pearl_support_activity_state ==
             "TAKEOFF_PRE_INTERRUPTION_CHECKS") &&
            (elapsed >= m_coordination_step_delay)) {
      if(!ready)
        rejectPearlRequest(reason);
      else {
        Notify("TAKEOFF_PRECHECKS_STATE", "passed");
        Notify("TAKEOFF_SUPPORT_READY", "true");
        Notify("TAKEOFF_SUPPORT_ACTIVE", "false");
        Notify("TAKEOFF_POSTCHECKS_STATE", "idle");
        Notify("TAKEOFF_SUPPORT_OUTCOME", "none");
        m_coordination_state = "PRECHECKS_PASSED";
        m_coordination_state_time = now;
        reportEvent("PEARL takeoff prechecks passed");
      }
    }
  }
  else if(m_coordination_state == "PRECHECKS_PASSED") {
    if(elapsed > m_request_timeout)
      rejectPearlRequest("FSM_TAKEOFF_SUPPORT_TIMEOUT");
    else if(fsm_available &&
            fsmValueFresh(m_pearl_takeoff_support_state_time) &&
            (m_pearl_takeoff_support_state ==
             "ESTABLISHING_TAKEOFF_CONDITIONS")) {
      if(!savePearlControlState())
        requestPearlCancellation("PEARL_CONTROLS_UNAVAILABLE");
      else {
        Notify("TAKEOFF_SUPPORT_ACTIVE", "true");
        Notify("TAKEOFF_SUPPORT_READY", "true");
        Notify("TAKEOFF_POSTCHECKS_STATE", "pending");
        Notify("TAKEOFF_CANCEL_STATE", "none");
        Notify("TAKEOFF_SUPPORT_OUTCOME", "active");
        m_coordination_state = "SUPPORTING";
        m_coordination_state_time = now;
        reportEvent("Activated PEARL takeoff support in authoritative state");
      }
    }
  }
  else if(m_coordination_state == "SUPPORTING") {
    if(fsm_available &&
       fsmValueFresh(m_pearl_takeoff_support_state_time) &&
       (m_pearl_takeoff_support_state == "CANCELLING_BEFORE_CLEARANCE")) {
      completePearlCancellation();
    }
    else if(fsm_available &&
            fsmValueFresh(m_pearl_takeoff_support_state_time) &&
            (m_pearl_takeoff_support_state == "POST_INTERRUPTION_CHECKS") &&
            (elapsed >= m_coordination_step_delay)) {
      if(!ready) {
        Notify("TAKEOFF_POSTCHECKS_STATE", "failed");
        m_coordination_state = "CANCEL_PENDING";
        m_coordination_state_time = now;
        reportEvent("PEARL takeoff postchecks failed: " + reason);
      }
      else {
        Notify("TAKEOFF_POSTCHECKS_STATE", "passed");
        Notify("TAKEOFF_CLEARANCE_STATE", "granted");
        string clearance = "id=" + m_session_id + "#status=granted";
        sendMessage(m_peer_node, "TAKEOFF_CLEARANCE", clearance);
        ++m_requests_approved;
        m_coordination_state = "CLEARED";
        m_coordination_state_time = now;
        reportEvent("Granted takeoff clearance from POST_INTERRUPTION_CHECKS");
      }
    }
    else if(elapsed > m_request_timeout)
      requestPearlCancellation("FSM_POSTCHECK_TIMEOUT");
  }
  else if(m_coordination_state == "CLEARED") {
    const bool takeoff_in_progress =
      fsm_available && fsmValueFresh(m_pearl_takeoff_support_state_time) &&
      (m_pearl_takeoff_support_state == "TAKEOFF_IN_PROGRESS");
    const bool uav_departed =
      fsmValueFresh(m_uav_operation_state_time) &&
      (m_uav_operation_state == "MISSION_EXECUTION");
    if(takeoff_in_progress && uav_departed) {
      Notify("TAKEOFF_SUPPORT_ACTIVE", "false");
      Notify("TAKEOFF_SUPPORT_OUTCOME", "completed");
      Notify("BASE_RESTORE_STATE", "restoring");
      m_coordination_state = "COMPLETING";
      m_coordination_state_time = now;
      reportEvent("Completed PEARL takeoff support from authoritative state");
    }
    else if(fsm_available &&
            fsmValueFresh(m_pearl_takeoff_support_state_time) &&
            ((m_pearl_takeoff_support_state ==
              "CANCELLING_BEFORE_CLEARANCE") ||
             (m_pearl_takeoff_support_state ==
              "DISENGAGING_TAKEOFF_SUPPORT"))) {
      completePearlCancellation();
    }
    else if((m_uav_operation_state == "IDLE") &&
            (elapsed > m_request_timeout)) {
      requestPearlCancellation("UAV_DEPARTURE_TIMEOUT");
    }
  }
  else if(m_coordination_state == "CANCEL_PENDING") {
    if(fsm_available &&
       fsmValueFresh(m_pearl_takeoff_support_state_time) &&
       ((m_pearl_takeoff_support_state == "CANCELLING_BEFORE_CLEARANCE") ||
        (m_pearl_takeoff_support_state ==
         "DISENGAGING_TAKEOFF_SUPPORT")))
      completePearlCancellation();
  }
  else if((m_coordination_state == "COMPLETING") &&
          fsm_available && fsmValueFresh(m_pearl_operation_state_time) &&
          (m_pearl_operation_state == "NORMAL_OPERATION")) {
    restorePearlControlState();
    resetPearlCoordination();
  }
  else if((m_coordination_state == "REJECTING") &&
          (elapsed >= m_coordination_step_delay))
    resetPearlCoordination();

  const string reported_reason = fsm_available ? reason : fsm_reason;
  const bool reported_ready = fsm_available && ready;
  if((now - m_last_status_post) >= 1.0 ||
     reported_ready != m_last_ready || reported_reason != m_last_reason) {
    Notify("PEARL_TAKEOFF_GATE_READY", reported_ready ? 1.0 : 0.0);
    Notify("PEARL_TAKEOFF_GATE_REASON", reported_reason);
    m_last_status_post = now;
    m_last_ready = reported_ready;
    m_last_reason = reported_reason;
  }
}

bool TakeoffGate::handleTakeoffRequest(const CMOOSMsg &msg)
{
  if(!msg.IsString())
    return false;

  const string request = msg.GetString();
  const string id = tokStringParse(request, "id", '#', '=');
  if(id == "") {
    reportRunWarning("Rejected takeoff request without an ID");
    return true;
  }

  if(m_coordination_state != "IDLE") {
    sendMessage(m_peer_node, "TAKEOFF_CLEARANCE",
                "id=" + id + "#status=rejected#reason=busy");
    return true;
  }

  string fsm_reason;
  if(!pearlFSMAvailable(fsm_reason) ||
     !fsmValueFresh(m_pearl_support_activity_state_time)) {
    if(fsm_reason == "READY")
      fsm_reason = "FSM_SUPPORT_STATE_STALE";
    sendMessage(m_peer_node, "TAKEOFF_CLEARANCE",
                "id=" + id + "#status=rejected#reason=" + fsm_reason);
    ++m_requests_rejected;
    reportEvent("Rejected UAV takeoff request: " + fsm_reason);
    return true;
  }
  if(m_pearl_support_activity_state != "NO_ACTIVE_REQUEST") {
    sendMessage(m_peer_node, "TAKEOFF_CLEARANCE",
                "id=" + id + "#status=rejected#reason=FSM_BUSY");
    ++m_requests_rejected;
    reportEvent("Rejected UAV takeoff request outside NO_ACTIVE_REQUEST");
    return true;
  }

  m_session_id = id;
  const string reason = pearlGateReason();
  if(reason != "READY") {
    rejectPearlRequest(reason);
    return true;
  }

  publishPearlRequestPending();
  m_coordination_state = "REQUESTED";
  m_coordination_state_time = MOOSTime();
  return true;
}

bool TakeoffGate::handleTakeoffClearance(const CMOOSMsg &msg)
{
  if(!msg.IsString())
    return false;

  const string clearance = msg.GetString();
  const string id = tokStringParse(clearance, "id", '#', '=');
  const string status = tolower(tokStringParse(clearance, "status", '#', '='));
  const string reason = tokStringParse(clearance, "reason", '#', '=');
  if(!m_awaiting_clearance || (id != m_session_id)) {
    reportRunWarning("Ignored stale takeoff clearance");
    return true;
  }

  m_awaiting_clearance = false;
  if(status == "granted") {
    ++m_requests_approved;
    Notify("UAV_TAKEOFF_APPROVED", 1.0);
    Notify("UAV_TAKEOFF_RESULT", "status=APPROVED,reason=PEARL_CLEARED");
    reportEvent("Takeoff approved by PEARL");
  }
  else {
    ++m_requests_rejected;
    Notify("UAV_TAKEOFF_APPROVED", 0.0);
    Notify("UAV_TAKEOFF_RESULT", "status=REJECTED,reason=" +
           ((reason == "") ? "PEARL_REJECTED" : reason));
    reportEvent("Takeoff rejected by PEARL");
  }
  return true;
}

bool TakeoffGate::sendMessage(const string &destination,
                              const string &variable,
                              const string &value)
{
  NodeMessage message(m_ownship, destination, variable);
  message.setSourceApp(GetAppName());
  message.setStringVal(value);
  if(!message.valid()) {
    reportRunWarning("Unable to form takeoff coordination message");
    return false;
  }
  Notify("NODE_MESSAGE_LOCAL", message.getSpec());
  return true;
}

void TakeoffGate::publishPearlRequestPending()
{
  Notify("TAKEOFF_REQUEST_ID", m_session_id);
  Notify("TAKEOFF_REQUEST_STATE", "open");
  Notify("TAKEOFF_PRECHECKS_STATE", "pending");
  Notify("TAKEOFF_SUPPORT_ACTIVE", "false");
  Notify("TAKEOFF_SUPPORT_READY", "false");
  Notify("TAKEOFF_POSTCHECKS_STATE", "idle");
  Notify("TAKEOFF_CLEARANCE_STATE", "none");
  Notify("TAKEOFF_CANCEL_STATE", "none");
  Notify("TAKEOFF_SUPPORT_OUTCOME", "none");
  Notify("BASE_RESTORE_STATE", "idle");
  reportEvent("Received UAV takeoff request");
}

void TakeoffGate::rejectPearlRequest(const string &reason)
{
  Notify("TAKEOFF_REQUEST_ID", m_session_id);
  Notify("TAKEOFF_REQUEST_STATE", "rejected");
  Notify("TAKEOFF_PRECHECKS_STATE", "failed");
  Notify("TAKEOFF_SUPPORT_ACTIVE", "false");
  Notify("TAKEOFF_SUPPORT_READY", "false");
  Notify("TAKEOFF_POSTCHECKS_STATE", "idle");
  Notify("TAKEOFF_CLEARANCE_STATE", "rejected");
  Notify("TAKEOFF_CANCEL_STATE", "none");
  Notify("TAKEOFF_SUPPORT_OUTCOME", "none");
  sendMessage(m_peer_node, "TAKEOFF_CLEARANCE",
              "id=" + m_session_id + "#status=rejected#reason=" + reason);
  ++m_requests_rejected;
  m_coordination_state = "REJECTING";
  m_coordination_state_time = MOOSTime();
  reportEvent("Rejected UAV takeoff request: " + reason);
}

void TakeoffGate::requestPearlCancellation(const string &reason)
{
  string cancel_state = "disengaging";
  if((m_pearl_takeoff_support_state ==
      "ESTABLISHING_TAKEOFF_CONDITIONS") ||
     (m_pearl_takeoff_support_state == "POST_INTERRUPTION_CHECKS"))
    cancel_state = "before_clearance";

  Notify("TAKEOFF_CANCEL_STATE", cancel_state);
  if(cancel_state == "before_clearance")
    Notify("TAKEOFF_POSTCHECKS_STATE", "failed");
  Notify("TAKEOFF_CLEARANCE_STATE", "rejected");
  sendMessage(m_peer_node, "TAKEOFF_CLEARANCE",
              "id=" + m_session_id + "#status=rejected#reason=" + reason);
  m_coordination_state = "CANCEL_PENDING";
  m_coordination_state_time = MOOSTime();
  reportEvent("Requested PEARL takeoff cancellation: " + reason);
}

void TakeoffGate::completePearlCancellation()
{
  Notify("TAKEOFF_SUPPORT_ACTIVE", "false");
  Notify("TAKEOFF_SUPPORT_OUTCOME", "cancelled");
  Notify("BASE_RESTORE_STATE", "restoring");
  m_coordination_state = "COMPLETING";
  m_coordination_state_time = MOOSTime();
  reportEvent("Completed PEARL takeoff cancellation in authoritative state");
}

bool TakeoffGate::fsmValueFresh(double value_time) const
{
  return value_time >= 0.0 &&
         (MOOSTime() - value_time) <= m_fsm_state_max_age;
}

bool TakeoffGate::pearlFSMAvailable(string &reason) const
{
  if(m_pearl_fsm_runtime_state != "READY") {
    reason = "FSM_NOT_READY";
    return false;
  }
  if(!fsmValueFresh(m_pearl_fsm_heartbeat_time)) {
    reason = "FSM_HEARTBEAT_STALE";
    return false;
  }
  reason = "READY";
  return true;
}

bool TakeoffGate::savePearlControlState()
{
  if(m_role != "pearl")
    return false;
  if(m_pearl_control_saved)
    return true;

  if(!m_pearl_deploy_set || !m_pearl_return_set ||
     !m_pearl_station_keep_set || !m_pearl_manual_override_set) {
    reportRunWarning("Cannot save incomplete PEARL takeoff control state");
    return false;
  }

  m_saved_pearl_deploy = m_pearl_deploy;
  m_saved_pearl_return = m_pearl_return;
  m_saved_pearl_station_keep = m_pearl_station_keep;
  m_saved_pearl_manual_override = m_pearl_manual_override;
  m_pearl_control_saved = true;

  Notify("PEARL_TAKEOFF_CONTROL_RESTORED", "false");
  Notify("PEARL_TAKEOFF_CONTROL_HISTORY",
         "deploy=" + boolToString(m_saved_pearl_deploy) +
         ",return=" + boolToString(m_saved_pearl_return) +
         ",station_keep=" + boolToString(m_saved_pearl_station_keep) +
         ",manual_override=" + boolToString(m_saved_pearl_manual_override));

  // Suspend the active mission while PEARL establishes takeoff support.
  Notify("DEPLOY", "true");
  Notify("RETURN", "false");
  Notify("STATION_KEEP", "true");
  Notify("MOOS_MANUAL_OVERRIDE", "false");
  reportEvent("Suspended PEARL mission for UAV takeoff support");
  return true;
}

void TakeoffGate::restorePearlControlState()
{
  if((m_role != "pearl") || !m_pearl_control_saved)
    return;

  Notify("DEPLOY", boolToString(m_saved_pearl_deploy));
  Notify("RETURN", boolToString(m_saved_pearl_return));
  Notify("STATION_KEEP", boolToString(m_saved_pearl_station_keep));
  Notify("MOOS_MANUAL_OVERRIDE", boolToString(m_saved_pearl_manual_override));
  Notify("PEARL_TAKEOFF_CONTROL_RESTORED", "true");
  m_pearl_control_saved = false;
  reportEvent("Restored pre-takeoff PEARL control state");
}

void TakeoffGate::resetPearlCoordination()
{
  Notify("TAKEOFF_REQUEST_ID", "none");
  Notify("TAKEOFF_REQUEST_STATE", "inactive");
  Notify("TAKEOFF_PRECHECKS_STATE", "idle");
  Notify("TAKEOFF_SUPPORT_ACTIVE", "false");
  Notify("TAKEOFF_SUPPORT_READY", "false");
  Notify("TAKEOFF_POSTCHECKS_STATE", "idle");
  Notify("TAKEOFF_CLEARANCE_STATE", "none");
  Notify("TAKEOFF_CANCEL_STATE", "none");
  Notify("TAKEOFF_SUPPORT_OUTCOME", "none");
  Notify("BASE_RESTORE_STATE", "idle");
  m_session_id = "";
  m_coordination_state = "IDLE";
  m_coordination_state_time = MOOSTime();
}

bool TakeoffGate::readDouble(const CMOOSMsg &msg, TimedValue &target)
{
  if(!msg.IsDouble() || !std::isfinite(msg.GetDouble()))
    return false;

  target.value = msg.GetDouble();
  target.time = MOOSTime();
  return true;
}

bool TakeoffGate::readBool(const CMOOSMsg &msg, bool &value) const
{
  if(msg.IsDouble()) {
    value = (msg.GetDouble() != 0.0);
    return true;
  }

  if(!msg.IsString())
    return false;

  return setBooleanOnString(value, msg.GetString());
}

bool TakeoffGate::readBoolValue(const CMOOSMsg &msg, TimedValue &target)
{
  bool value = false;
  if(!readBool(msg, value))
    return false;

  target.value = value ? 1.0 : 0.0;
  target.time = MOOSTime();
  return true;
}

bool TakeoffGate::setPercentConfig(const string &value, double &target)
{
  double parsed = 0.0;
  if(!setNonNegDoubleOnString(parsed, value) || parsed > 100.0)
    return false;

  target = parsed;
  return true;
}

bool TakeoffGate::isFresh(const TimedValue &value) const
{
  return value.time >= 0.0 && (MOOSTime() - value.time) <= m_input_max_age;
}

string TakeoffGate::gateReason() const
{
  if(!isFresh(m_uav_soc) || !isFresh(m_uav_battery_valid))
    return "UAV_BATTERY_STALE";
  if(m_uav_battery_valid.value < 0.5)
    return "UAV_BATTERY_INVALID";
  if(m_uav_soc.value < m_min_uav_soc)
    return "UAV_BATTERY_LOW";

  if(!isFresh(m_pearl_soc) || !isFresh(m_pearl_battery_valid))
    return "PEARL_BATTERY_STALE";
  if(m_pearl_battery_valid.value < 0.5)
    return "PEARL_BATTERY_INVALID";
  if(m_pearl_soc.value < m_min_pearl_soc)
    return "PEARL_BATTERY_LOW";

  if(!isFresh(m_wind_speed) || !isFresh(m_wind_valid))
    return "WIND_STALE";
  if(m_wind_valid.value < 0.5)
    return "WIND_INVALID";
  if(m_wind_speed.value < 0.0 || m_wind_speed.value > m_max_wind_speed)
    return "WIND_HIGH";

  return "READY";
}

bool TakeoffGate::buildReport()
{
  m_msgs << "Thresholds" << endl;
  m_msgs << "  UAV SOC:   >= " << doubleToStringX(m_min_uav_soc, 1) << "%" << endl;
  m_msgs << "  PEARL SOC: >= " << doubleToStringX(m_min_pearl_soc, 1) << "%" << endl;
  m_msgs << "  Wind:      <= " << doubleToStringX(m_max_wind_speed, 1) << " m/s" << endl;
  m_msgs << "  Input age: <= " << doubleToStringX(m_input_max_age, 1) << " s" << endl;
  m_msgs << "  FSM age:   <= " << doubleToStringX(m_fsm_state_max_age, 1) << " s" << endl;
  const string reason = (m_role == "uav") ? gateReason() : pearlGateReason();
  m_msgs << "Role: " << m_role << endl;
  m_msgs << "Gate: " << reason << endl;
  m_msgs << "Coordination: " << m_coordination_state << endl;
  m_msgs << "Session: " << ((m_session_id == "") ? "none" : m_session_id)
         << endl;
  m_msgs << "Requests approved/rejected: " << m_requests_approved
         << "/" << m_requests_rejected << endl << endl;

  ACTable table(4);
  table << "Input | Value | Age | Valid";
  table.addHeaderLines();
  table << "UAV battery" << doubleToStringX(m_uav_soc.value, 1)
        << ageString(m_uav_soc) << boolString(m_uav_battery_valid);
  table << "PEARL battery" << doubleToStringX(m_pearl_soc.value, 1)
        << ageString(m_pearl_soc) << boolString(m_pearl_battery_valid);
  table << "Apparent wind" << doubleToStringX(m_wind_speed.value, 1)
        << ageString(m_wind_speed) << boolString(m_wind_valid);
  m_msgs << table.getFormattedString();

  if(m_role == "pearl") {
    string fsm_reason;
    const bool fsm_available = pearlFSMAvailable(fsm_reason);
    const auto state_age = [](double time) {
      return (time < 0.0) ? string("n/a") :
        doubleToStringX(max(0.0, MOOSTime() - time), 1);
    };
    m_msgs << endl << endl;
    ACTable states(3);
    states << "FSM input" << "Value" << "Age";
    states.addHeaderLines();
    states << "Runtime" << m_pearl_fsm_runtime_state
           << state_age(m_pearl_fsm_runtime_state_time);
    states << "Heartbeat" << (fsm_available ? "fresh" : fsm_reason)
           << state_age(m_pearl_fsm_heartbeat_time);
    states << "Operation" << m_pearl_operation_state
           << state_age(m_pearl_operation_state_time);
    states << "Support activity" << m_pearl_support_activity_state
           << state_age(m_pearl_support_activity_state_time);
    states << "Takeoff support" << m_pearl_takeoff_support_state
           << state_age(m_pearl_takeoff_support_state_time);
    states << "UAV operation" << m_uav_operation_state
           << state_age(m_uav_operation_state_time);
    m_msgs << states.getFormattedString() << endl << endl;

    ACTable authorization(5);
    authorization << "Action" << "Required state" << "Current state"
                  << "Age" << "Authorized";
    authorization.addHeaderLines();
    authorization << "Run prechecks" << "TAKEOFF_PRE_INTERRUPTION_CHECKS"
                  << m_pearl_support_activity_state
                  << state_age(m_pearl_support_activity_state_time)
                  << boolToString(fsm_available &&
                       fsmValueFresh(m_pearl_support_activity_state_time) &&
                       (m_pearl_support_activity_state ==
                        "TAKEOFF_PRE_INTERRUPTION_CHECKS"));
    authorization << "Apply controls" << "ESTABLISHING_TAKEOFF_CONDITIONS"
                  << m_pearl_takeoff_support_state
                  << state_age(m_pearl_takeoff_support_state_time)
                  << boolToString(fsm_available &&
                       fsmValueFresh(m_pearl_takeoff_support_state_time) &&
                       (m_pearl_takeoff_support_state ==
                        "ESTABLISHING_TAKEOFF_CONDITIONS"));
    authorization << "Grant clearance" << "POST_INTERRUPTION_CHECKS"
                  << m_pearl_takeoff_support_state
                  << state_age(m_pearl_takeoff_support_state_time)
                  << boolToString(fsm_available &&
                       fsmValueFresh(m_pearl_takeoff_support_state_time) &&
                       (m_pearl_takeoff_support_state ==
                        "POST_INTERRUPTION_CHECKS"));
    authorization << "Complete support" << "TAKEOFF_IN_PROGRESS"
                  << m_pearl_takeoff_support_state
                  << state_age(m_pearl_takeoff_support_state_time)
                  << boolToString(fsm_available &&
                       fsmValueFresh(m_pearl_takeoff_support_state_time) &&
                       (m_pearl_takeoff_support_state ==
                        "TAKEOFF_IN_PROGRESS") &&
                       fsmValueFresh(m_uav_operation_state_time) &&
                       (m_uav_operation_state == "MISSION_EXECUTION"));
    authorization << "Restore controls" << "NORMAL_OPERATION"
                  << m_pearl_operation_state
                  << state_age(m_pearl_operation_state_time)
                  << boolToString(fsm_available &&
                       fsmValueFresh(m_pearl_operation_state_time) &&
                       (m_pearl_operation_state == "NORMAL_OPERATION"));
    m_msgs << authorization.getFormattedString();
  }
  return true;
}

string TakeoffGate::pearlGateReason() const
{
  if(!isFresh(m_pearl_soc) || !isFresh(m_pearl_battery_valid))
    return "PEARL_BATTERY_STALE";
  if(m_pearl_battery_valid.value < 0.5)
    return "PEARL_BATTERY_INVALID";
  if(m_pearl_soc.value < m_min_pearl_soc)
    return "PEARL_BATTERY_LOW";

  if(!isFresh(m_wind_speed) || !isFresh(m_wind_valid))
    return "WIND_STALE";
  if(m_wind_valid.value < 0.5)
    return "WIND_INVALID";
  if(m_wind_speed.value < 0.0 || m_wind_speed.value > m_max_wind_speed)
    return "WIND_HIGH";

  return "READY";
}

string TakeoffGate::ageString(const TimedValue &value) const
{
  if(value.time < 0.0)
    return "n/a";
  return doubleToStringX(max(0.0, MOOSTime() - value.time), 1);
}

string TakeoffGate::boolString(const TimedValue &value) const
{
  if(value.time < 0.0)
    return "unknown";
  return value.value >= 0.5 ? "true" : "false";
}
