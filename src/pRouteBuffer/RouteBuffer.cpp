/************************************************************/
/*    NAME: Charles Benjamin                                 */
/*    ORGN: MIT, Cambridge MA                                */
/*    FILE: RouteBuffer.cpp                                  */
/*    DATE: July 25th, 2026                                  */
/************************************************************/

#include "ACTable.h"
#include "MBUtils.h"
#include "NodeMessage.h"
#include "NodeRecord.h"
#include "NodeRecordUtils.h"
#include "XYFormatUtilsPoint.h"
#include "XYFormatUtilsSegl.h"
#include "XYSegList.h"
#include "RouteBuffer.h"
#include <chrono>
#include <cmath>

using namespace std;

//---------------------------------------------------------
// Constructor()

RouteBuffer::RouteBuffer()
{
  m_role               = "";
  m_point_var          = "ROUTE_POINT";
  m_deploy_request_var = "ROUTE_BUFFER_DEPLOY";
  m_clear_request_var  = "ROUTE_BUFFER_CLEAR";
  m_goto_request_var   = "ROUTE_BUFFER_GOTO";
  m_node_report_var    = "NODE_REPORT";
  m_command_var        = "ROUTE_BUFFER_COMMAND";
  m_route_update_var   = "ROUTE_UPDATE";
  m_route_deploy_var   = "ROUTE_DEPLOY";
  m_route_clear_var    = "ROUTE_CLEAR";
  m_route_ready_var    = "ROUTE_READY";
  m_route_name         = "active";
  m_manual_override_var = "";
  m_max_points         = 50;
  m_contact_max_age    = 3;
  m_deploy_delay       = 0;
  m_route_replay_delay = 0;
  m_live_append        = false;

  m_state                 = "EMPTY";
  m_max_visualized_points = 0;
  m_commands_sent         = 0;
  m_commands_received     = 0;
  m_commands_rejected     = 0;
  m_config_valid          = true;
  m_sync_sent             = false;
  m_route_ready            = false;
  m_route_deployed         = false;
  m_vehicle_deploy_pending = false;
  m_vehicle_deploy_pending_since = 0;
  m_vehicle_route_replay_pending = false;
  m_vehicle_route_replay_pending_since = 0;
  m_vehicle_active_route_name = "";
}

//---------------------------------------------------------
// Destructor

RouteBuffer::~RouteBuffer()
{
}

//---------------------------------------------------------
// Procedure: OnNewMail()

bool RouteBuffer::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  // Cache guard facts before commands, without changing their producer times.
  for(const auto& msg : NewMail) {
    if(m_task_guards.count(msg.GetKey()))
      m_task_inputs[msg.GetKey()] = {
        msg.IsDouble() ? doubleToStringX(msg.GetDouble()) : msg.GetString(), msg.GetTime()};
    if(m_task_start_pending && msg.GetKey() == m_task_started_var &&
       msg.IsString() && msg.GetString() == m_task_started_value &&
       msg.GetTime() >= m_task_requested_time && msg.GetTime() <= m_curr_time + 1 &&
       m_curr_time - msg.GetTime() <= 3)
      m_task_started = true;
  }

  MOOSMSG_LIST::iterator p;
  for(p=NewMail.begin(); p!=NewMail.end(); p++) {
    CMOOSMsg &msg = *p;
    string key    = msg.GetKey();

    bool handled = false;
    if((m_role == "shoreside") && (key == m_point_var))
      handled = handleMailPoint(msg);
    else if((m_role == "shoreside") && (key == m_task_request_var))
      handled = handleMailTask(msg);
    else if((m_role == "shoreside") && !m_stage_clear_request_var.empty() &&
            key == m_stage_clear_request_var) {
      if(mailIsTrue(msg)) clearStagedDrawing();
      handled = true;
    }
    else if((m_role == "shoreside") && (key == m_deploy_request_var))
      handled = handleMailTrigger(msg, "deploy");
    else if((m_role == "shoreside") && (key == m_clear_request_var))
      handled = handleMailTrigger(msg, "clear");
    else if((m_role == "shoreside") && (key == m_goto_request_var))
      handled = handleMailGoto(msg);
    else if((m_role == "shoreside") && (key == m_node_report_var))
      handled = handleMailNodeReport(msg);
    else if((m_role == "vehicle") && (key == m_command_var))
      handled = handleMailCommand(msg);
    else if((m_role == "vehicle") && (key == m_route_ready_var)) {
      m_route_ready = mailIsTrue(msg);
      m_route_ready_time = msg.GetTime();
      handled = true;
    }
    else if((m_role == "vehicle") && !m_ready_name_var.empty() &&
            key == m_ready_name_var && msg.IsString()) {
      m_ready_name = msg.GetString();
      m_ready_name_time = msg.GetTime();
      handled = true;
    }
    else if((m_role == "vehicle") && !m_operation_state_var.empty() &&
            (key == m_operation_state_var) && msg.IsString()) {
      m_operation_state = msg.GetString();
      handled = true;
    }
    else if((m_role == "vehicle") && !m_helm_state_var.empty() &&
            (key == m_helm_state_var) && msg.IsString()) {
      m_helm_state = msg.GetString();
      handled = true;
    }
    else if(m_task_guards.count(key) || key == m_task_started_var)
      handled = true;
    else if(key == "APPCAST_REQ")
      handled = true;

    if(!handled && (key != "APPCAST_REQ"))
      reportRunWarning("Unhandled Mail: " + key);
  }

  return(true);
}

//---------------------------------------------------------
// Procedure: OnConnectToServer()

bool RouteBuffer::OnConnectToServer()
{
   registerVariables();
   return(true);
}

//---------------------------------------------------------
// Procedure: Iterate()
//            happens AppTick times per second

bool RouteBuffer::Iterate()
{
  AppCastingMOOSApp::Iterate();

  if(m_config_valid) {
    if((m_role == "shoreside") && !m_sync_sent) {
      // pMediator currently begins its sequence at zero, which receivers
      // deliberately reject. Consume that initial ID with a harmless command.
      sendMediatedCommand("action=sync");
      m_sync_sent = true;
    }
    else if((m_role == "shoreside") && !m_pending_actions.empty())
      processPendingAction();
    else if((m_role == "vehicle") && !m_pending_commands.empty()) {
      const auto command = m_pending_commands.front();
      m_pending_commands.pop_front();
      processVehicleCommand(command.first, command.second);
    }

    // Parked Helm discards update deltas before its behaviors can spawn.
    // Release the retained snapshot on actual DRIVE, never on a task timer.
    if((m_role == "vehicle") && !m_deferred_helm_update.empty() &&
       (m_helm_state == "DRIVE")) {
      Notify(m_route_update_var, m_deferred_helm_update);
      m_deferred_helm_update.clear();
      reportEvent("Staged route delivered after Helm DRIVE");
    }

    if(m_task_start_pending) {
      if(m_task_started) {
        m_task_start_pending = false;
        postState("TASK_STARTED");
      }
      else if(!taskPermitted() || m_curr_time - m_task_requested_time > 3)
        cancelPendingTask("TASK_REJECTED_START");
    }

    if((m_role == "vehicle") && m_vehicle_deploy_pending &&
       !m_task_execute_var.empty() && !taskPermitted())
      cancelPendingTask("TASK_REJECTED_CONTEXT");

    if((m_role == "vehicle") && m_vehicle_deploy_pending) {
      double staged_time = m_curr_time - m_vehicle_deploy_pending_since;
      bool release_route = false;
      if(!m_task_execute_var.empty()) {
        // A matching, post-snapshot Helm readiness publication is evidence;
        // a fixed delay or a retained true flag is not.
        if(m_route_ready && m_route_ready_time >= m_vehicle_deploy_pending_since &&
           m_route_ready_time <= m_curr_time + 1 && m_curr_time - m_route_ready_time <= 3 &&
           m_ready_name == m_ready_name_prefix + m_vehicle_active_route_name &&
           m_ready_name_time >= m_vehicle_deploy_pending_since &&
           m_ready_name_time <= m_curr_time + 1 && m_curr_time - m_ready_name_time <= 3) {
          Notify(m_route_deploy_var, "true");
          Notify(m_task_execute_var, "true");
          m_task_requested_time = m_curr_time;
          m_task_start_pending = true;
          m_task_started = false;
          release_route = true;
        }
        else if(staged_time > 5)
          cancelPendingTask("TASK_REJECTED_NOT_READY");
      }
      else if((m_deploy_delay > 0) && (staged_time >= m_deploy_delay)) {
        Notify(m_route_deploy_var, "true");
        if(m_manual_override_var != "")
          Notify(m_manual_override_var, "false");
        release_route = true;
      }
      else if((m_deploy_delay == 0) && m_route_ready) {
        Notify(m_route_deploy_var, "true");
        release_route = true;
      }
      else if((m_deploy_delay == 0) && (staged_time > 3)) {
        m_vehicle_deploy_pending = false;
        m_vehicle_pending_route_update.clear();
        m_commands_rejected++;
        postState("DEPLOY_REJECTED_NOT_READY");
        reportRunWarning("DEPLOY rejected: route did not become ready");
      }

      if(release_route) {
        m_route_deployed = true;
        m_vehicle_deploy_pending = false;
        if((m_route_replay_delay > 0) &&
           (m_vehicle_pending_route_update != "")) {
          m_vehicle_route_replay_pending = true;
          m_vehicle_route_replay_pending_since = m_curr_time;
          postState("DEPLOY_RELEASED");
        }
        else {
          m_vehicle_pending_route_update.clear();
          postState(m_task_execute_var.empty() ? "DEPLOY_ACCEPTED" : "TASK_REQUESTED");
        }
      }
    }

    if((m_role == "vehicle") && m_vehicle_route_replay_pending) {
      double replay_time =
        m_curr_time - m_vehicle_route_replay_pending_since;
      if(replay_time >= m_route_replay_delay) {
        Notify(m_route_update_var, m_vehicle_pending_route_update);
        m_vehicle_route_replay_pending = false;
        m_vehicle_pending_route_update.clear();
        postState("DEPLOY_ACCEPTED");
        reportEvent("Replayed route after helm release");
      }
    }
  }

  AppCastingMOOSApp::PostReport();
  return(true);
}

//---------------------------------------------------------
// Procedure: OnStartUp()
//            happens before connection is open

bool RouteBuffer::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();
  m_started = MOOSTime();

  STRING_LIST sParams;
  m_MissionReader.EnableVerbatimQuoting(false);
  if(!m_MissionReader.GetConfiguration(GetAppName(), sParams))
    reportConfigWarning("No config block found for " + GetAppName());

  STRING_LIST::iterator p;
  for(p=sParams.begin(); p!=sParams.end(); p++) {
    string orig  = *p;
    string line  = *p;
    string param = tolower(biteStringX(line, '='));
    string value = line;

    bool handled = false;
    if(param == "role")
      handled = handleConfigRole(value);
    else if(param == "task_request_var")
      handled = setNonWhiteVarOnString(m_task_request_var, value);
    else if(param == "stage_clear_request_var")
      handled = setNonWhiteVarOnString(m_stage_clear_request_var, value);
    else if(param == "task_destination")
      handled = !value.empty() && !strContainsWhite(value) &&
                m_task_destinations.insert(tolower(value)).second;
    else if(param == "task_submit_flag") {
      string destination = tolower(stripBlankEnds(biteStringX(value, ':')));
      string key = stripBlankEnds(biteStringX(value, '='));
      handled = !destination.empty() && !key.empty() && !value.empty() &&
                !strContainsWhite(destination) && !strContainsWhite(key);
      if(handled) m_task_submit_flags[destination].emplace_back(key, value);
    }
    else if(param == "route_ready_name_prefix")
      handled = setNonWhiteVarOnString(m_ready_name_prefix, value);
    else if(param == "route_ready_name_var")
      handled = setNonWhiteVarOnString(m_ready_name_var, value);
    else if(param == "task_execute_var")
      handled = setNonWhiteVarOnString(m_task_execute_var, value);
    else if(param == "task_started_state") {
      m_task_started_var = stripBlankEnds(biteStringX(value, '='));
      handled = setNonWhiteVarOnString(m_task_started_value, value) &&
                !m_task_started_var.empty() && !strContainsWhite(m_task_started_var);
    }
    else if(param == "task_guard") {
      string key = stripBlankEnds(biteStringX(value, '='));
      const auto alternatives = parseString(value, '|');
      handled = !key.empty() && !strContainsWhite(key) && !alternatives.empty();
      for(const auto& alternative : alternatives)
        handled = handled && !alternative.empty() && !strContainsWhite(alternative);
      if(handled) m_task_guards[key] = set<string>(alternatives.begin(), alternatives.end());
    }
    else if(param == "destination_node")
      handled = setNonWhiteVarOnString(m_destination_node, value);
    else if(param == "source_node")
      handled = setNonWhiteVarOnString(m_source_node, value);
    else if(param == "point_var")
      handled = setNonWhiteVarOnString(m_point_var, value);
    else if(param == "deploy_request_var")
      handled = setNonWhiteVarOnString(m_deploy_request_var, value);
    else if(param == "clear_request_var")
      handled = setNonWhiteVarOnString(m_clear_request_var, value);
    else if(param == "goto_request_var")
      handled = setNonWhiteVarOnString(m_goto_request_var, value);
    else if(param == "node_report_var")
      handled = setNonWhiteVarOnString(m_node_report_var, value);
    else if(param == "command_var")
      handled = setNonWhiteVarOnString(m_command_var, value);
    else if(param == "route_update_var")
      handled = setNonWhiteVarOnString(m_route_update_var, value);
    else if(param == "route_deploy_var")
      handled = setNonWhiteVarOnString(m_route_deploy_var, value);
    else if(param == "route_clear_var")
      handled = setNonWhiteVarOnString(m_route_clear_var, value);
    else if(param == "route_ready_var")
      handled = setNonWhiteVarOnString(m_route_ready_var, value);
    else if(param == "route_name")
      handled = setNonWhiteVarOnString(m_route_name, value);
    else if(param == "route_purpose_var")
      handled = setNonWhiteVarOnString(m_route_purpose_var, value);
    else if(param == "route_identity_var")
      handled = setNonWhiteVarOnString(m_route_identity_var, value);
    else if(param == "helm_state_var")
      handled = setNonWhiteVarOnString(m_helm_state_var, value);
    else if(param == "operation_state_var")
      handled = setNonWhiteVarOnString(m_operation_state_var, value);
    else if(param == "recovery_source")
      handled = setNonWhiteVarOnString(m_recovery_source, value);
    else if(param == "operator_stage_state") {
      string state;
      handled = setNonWhiteVarOnString(state, value) &&
                m_operator_states.insert(state).second;
    }
    else if(param == "recovery_stage_state") {
      string state;
      handled = setNonWhiteVarOnString(state, value) &&
                m_recovery_states.insert(state).second;
    }
    else if(param == "manual_override_var")
      handled = setNonWhiteVarOnString(m_manual_override_var, value);
    else if(param == "max_points")
      handled = setUIntOnString(m_max_points, value) && (m_max_points > 0);
    else if(param == "contact_max_age")
      handled = setPosDoubleOnString(m_contact_max_age, value);
    else if(param == "deploy_delay")
      handled = setNonNegDoubleOnString(m_deploy_delay, value);
    else if(param == "route_replay_delay")
      handled = setNonNegDoubleOnString(m_route_replay_delay, value);
    else if(param == "live_append")
      handled = setBooleanOnString(m_live_append, value);

    if(!handled) {
      reportUnhandledConfigWarning(orig);
      if(strBegins(param, "task_") || param == "stage_clear_request_var" ||
         param == "route_ready_name_var" || param == "route_ready_name_prefix" ||
         param == "route_identity_var")
        m_config_valid = false;
    }
  }

  if(m_source_node == "")
    m_source_node = m_host_community;

  if(m_role == "") {
    reportConfigWarning("role must be shoreside or vehicle");
    m_config_valid = false;
  }
  else if((m_role == "shoreside") && (m_destination_node == "")) {
    reportConfigWarning("destination_node is required for shoreside role");
    m_config_valid = false;
  }
  if(m_role == "vehicle" &&
     (!m_route_purpose_var.empty() || !m_operation_state_var.empty() ||
      !m_recovery_source.empty() || !m_operator_states.empty() ||
      !m_recovery_states.empty()) &&
     (m_route_purpose_var.empty() || m_operation_state_var.empty() ||
      m_recovery_source.empty() || m_operator_states.empty() ||
      m_recovery_states.empty())) {
    reportConfigWarning("Route purpose policy needs a purpose variable, FSM state variable, recovery source, and allowed states");
    m_config_valid = false;
  }
  for(const auto& flags : m_task_submit_flags) {
    if(!m_task_destinations.count(flags.first)) {
      reportConfigWarning("task_submit_flag needs an allowed task_destination");
      m_config_valid = false;
    }
  }
  if(!m_task_execute_var.empty() &&
     (m_role != "vehicle" || m_task_guards.empty() || m_task_started_var.empty() ||
      m_ready_name_var.empty() ||
      m_deploy_delay != 0 || m_route_replay_delay != 0)) {
    reportConfigWarning("Task execution requires guards, started state and zero deploy/replay delay");
    m_config_valid = false;
  }
  if(m_config_valid && !m_route_purpose_var.empty())
    Notify(m_route_purpose_var, "none");

  registerVariables();
  return(true);
}

//---------------------------------------------------------
// Procedure: registerVariables()

void RouteBuffer::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();

  if(m_role == "shoreside") {
    Register(m_point_var, 0);
    Register(m_task_request_var, 0);
    if(!m_stage_clear_request_var.empty()) Register(m_stage_clear_request_var, 0);
    Register(m_deploy_request_var, 0);
    Register(m_clear_request_var, 0);
    Register(m_goto_request_var, 0);
    Register(m_node_report_var, 0);
  }
  else if(m_role == "vehicle") {
    Register(m_command_var, 0);
    Register(m_route_ready_var, 0);
    if(!m_ready_name_var.empty()) Register(m_ready_name_var, 0);
    if(!m_operation_state_var.empty()) Register(m_operation_state_var, 0);
    if(!m_helm_state_var.empty()) Register(m_helm_state_var, 0);
    for(const auto& guard : m_task_guards) Register(guard.first, 0);
    if(!m_task_started_var.empty()) Register(m_task_started_var, 0);
  }
}

//---------------------------------------------------------
// Procedure: handleMailPoint()

bool RouteBuffer::handleMailPoint(const CMOOSMsg& msg)
{
  if(!msg.IsString())
    return(false);

  XYPoint point = string2Point(msg.GetString());
  if(!point.valid() || !std::isfinite(point.x()) || !std::isfinite(point.y())) {
    reportRunWarning("Invalid route point: " + msg.GetString());
    return(true);
  }

  if(m_points.size() >= m_max_points) {
    reportRunWarning("Route point limit reached");
    postState("POINT_LIMIT");
    return(true);
  }

  m_points.push_back(point);
  m_last_submitted_points = "";
  postPoint(point, m_points.size(), true);

  bool sent_append = false;
  if(m_live_append && m_route_deployed) {
    string point_spec = doubleToStringX(point.x(), 6) + ",";
    point_spec += doubleToStringX(point.y(), 6);
    sent_append = sendMediatedCommand("action=append # point=" + point_spec);
  }
  if(!sent_append) m_route_deployed = false;
  postSegList(true);
  postState(sent_append ? "APPEND_SUBMITTED" : "EDITING");
  return(true);
}

// An explicit target and immutable snapshot share one request. Consuming the
// drawing never sends a vehicle CLEAR and never cancels another vehicle.
bool RouteBuffer::handleMailTask(const CMOOSMsg& msg)
{
  const string destination = msg.IsString() ? tolower(stripBlankEnds(msg.GetString())) : "";
  if(!m_config_valid || msg.GetTime() < m_started || msg.GetTime() > m_curr_time + 1 ||
     m_curr_time - msg.GetTime() > 3 || !m_task_destinations.count(destination)) {
    postState("TASK_REJECTED_TARGET");
    return true;
  }
  if(m_points.empty()) {
    postState("TASK_REJECTED_EMPTY");
    return true;
  }
  const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const string id = "task_" + destination + "_" + to_string(stamp);
  const string command = "action=deploy # task_id=" + id +
                         " # task_time=" + doubleToStringX(m_curr_time, 6) +
                         " # points=" + getPointsSpec();
  if(!sendMediatedCommand(command, destination)) {
    postState("TASK_REJECTED_SEND");
    return true;
  }
  // pMediator owns retry copies after this post; its ACK is receipt, not execution.
  clearStagedDrawing();
  Notify("ROUTE_BUFFER_TASK_ID", id);
  Notify("ROUTE_BUFFER_TASK_DESTINATION", destination);
  for(const auto& flag : m_task_submit_flags[destination])
    Notify(flag.first, flag.second);
  postState("TASK_SUBMITTED");
  return true;
}

void RouteBuffer::clearStagedDrawing()
{
  clearVisualization();
  m_points.clear();
  m_last_submitted_points.clear();
  m_route_deployed = false;
  postState("EMPTY");
}

bool RouteBuffer::taskPermitted() const
{
  for(const auto& guard : m_task_guards) {
    const auto input = m_task_inputs.find(guard.first);
    if(input == m_task_inputs.end() || !guard.second.count(input->second.value) ||
       input->second.time > m_curr_time + 1 || m_curr_time - input->second.time > 3)
      return false;
  }
  return true;
}

void RouteBuffer::cancelPendingTask(const string& reason)
{
  // Cancel only a task not observed in the execution state; support suspension
  // must not discard an executing route or its waypoint-engine progress.
  if(!m_task_execute_var.empty()) Notify(m_task_execute_var, "false");
  processVehicleCommand("action=clear", GetAppName());
  Notify(m_route_deploy_var, "false");
  Notify(m_route_ready_var, "false");
  m_route_ready = false;
  m_commands_rejected++;
  postState(reason);
}

//---------------------------------------------------------
// Procedure: handleMailTrigger()

bool RouteBuffer::handleMailTrigger(const CMOOSMsg& msg,
                                    const string& action)
{
  if(!mailIsTrue(msg))
    return(true);

  m_pending_actions.push_back(action);
  return(true);
}

//---------------------------------------------------------
// Procedure: handleMailGoto()

bool RouteBuffer::handleMailGoto(const CMOOSMsg& msg)
{
  if(!msg.IsString())
    return(false);

  string target = tolower(stripBlankEnds(msg.GetString()));
  if(target == "") {
    reportRunWarning("GOTO rejected: empty target node");
    postState("GOTO_REJECTED_TARGET");
    return(true);
  }

  m_pending_actions.push_back("goto:" + target);
  return(true);
}

//---------------------------------------------------------
// Procedure: handleMailNodeReport()

bool RouteBuffer::handleMailNodeReport(const CMOOSMsg& msg)
{
  if(!msg.IsString())
    return(false);

  NodeRecord record = string2NodeRecord(msg.GetString());
  string target = tolower(record.getName());
  if((target == "") || !record.isSetXY())
    return(true);

  m_contact_points[target] = XYPoint(record.getX(), record.getY());
  m_contact_times[target] = MOOSTime();
  return(true);
}

//---------------------------------------------------------
// Procedure: handleMailCommand()

bool RouteBuffer::handleMailCommand(const CMOOSMsg& msg)
{
  if(!msg.IsString())
    return(false);

  m_pending_commands.emplace_back(msg.GetString(), msg.GetSource());
  return(true);
}

//---------------------------------------------------------
// Procedure: handleConfigRole()

bool RouteBuffer::handleConfigRole(const string& value)
{
  string role = tolower(stripBlankEnds(value));
  if((role != "shoreside") && (role != "vehicle"))
    return(false);

  m_role = role;
  return(true);
}

//---------------------------------------------------------
// Procedure: processPendingAction()

bool RouteBuffer::processPendingAction()
{
  string action = m_pending_actions.front();
  m_pending_actions.pop_front();

  if(action == "clear") {
    clearVisualization();
    m_points.clear();
    m_last_submitted_points = "";
    m_route_deployed = false;
    postState("EMPTY");
    return(sendMediatedCommand("action=clear"));
  }

  if(strBegins(action, "goto:")) {
    string target = action.substr(5);
    if(m_contact_points.count(target) == 0) {
      postState("GOTO_REJECTED_NO_REPORT");
      reportRunWarning("GOTO rejected: no report for " + target);
      return(true);
    }

    double contact_age = MOOSTime() - m_contact_times[target];
    if(contact_age > m_contact_max_age) {
      postState("GOTO_REJECTED_STALE");
      reportRunWarning("GOTO rejected: stale report for " + target);
      return(true);
    }

    clearVisualization();
    m_points.clear();
    m_route_deployed = false;
    m_points.push_back(m_contact_points[target]);
    postPoint(m_points.front(), 1, true);
    postSegList(true);

    string points = getPointsSpec();
    string command = "action=deploy # points=" + points;
    if(!sendMediatedCommand(command))
      return(false);

    m_last_submitted_points = points;
    m_route_deployed = true;
    postSegList(true);
    postState("GOTO_SUBMITTED");
    reportEvent("Submitted GOTO route for " + target);
    return(true);
  }

  if(action != "deploy")
    return(false);

  if(m_points.empty()) {
    postState("DEPLOY_REJECTED_EMPTY");
    reportRunWarning("DEPLOY rejected: route is empty");
    return(true);
  }

  string points = getPointsSpec();
  if(points == m_last_submitted_points) {
    postState("SUBMITTED");
    reportEvent("Ignored duplicate DEPLOY for unchanged route");
    return(true);
  }

  string command = "action=deploy # points=" + points;
  if(!sendMediatedCommand(command))
    return(false);

  m_last_submitted_points = points;
  m_route_deployed = true;
  postSegList(true);
  postState("SUBMITTED");
  return(true);
}

//---------------------------------------------------------
// Procedure: processVehicleCommand()

bool RouteBuffer::processVehicleCommand(const string& command,
                                        const string& source)
{
  m_commands_received++;

  vector<string> fields = parseStringQ(command, '#');
  string action;
  string points;
  string point;
  string speed;
  string capture_radius;
  string slip_radius;
  string route_name;
  string purpose = "operator";
  string task_id, task_time;
  for(unsigned int i=0; i<fields.size(); i++) {
    string field = stripBlankEnds(fields[i]);
    string param = tolower(biteStringX(field, '='));
    string value = stripBlankEnds(field);

    if(param == "task_id") task_id = value;
    else if(param == "task_time") task_time = value;
    else if(param == "action")
      action = tolower(value);
    else if(param == "points")
      points = value;
    else if(param == "point")
      point = value;
    else if(param == "speed")
      speed = value;
    else if(param == "capture_radius")
      capture_radius = value;
    else if(param == "slip_radius")
      slip_radius = value;
    else if(param == "route_name")
      route_name = value;
    else if(param == "purpose")
      purpose = tolower(value);
  }

  if(action == "sync") {
    postState("SYNCED");
    return(true);
  }

  if(action == "clear") {
    if(m_task_start_pending && !m_task_execute_var.empty()) Notify(m_task_execute_var, "false");
    m_task_start_pending = false;
    m_deferred_helm_update.clear();
    m_vehicle_deploy_pending = false;
    m_vehicle_route_replay_pending = false;
    m_vehicle_pending_route_update.clear();
    m_vehicle_route_purpose.clear();
    m_route_deployed = false;
    if(!m_route_purpose_var.empty()) Notify(m_route_purpose_var, "none");
    if(!m_route_identity_var.empty()) Notify(m_route_identity_var, "none");
    if(!m_vehicle_active_route_name.empty()) {
      // Repeating this update can respawn an expired Helm behavior while idle.
      Notify(m_route_update_var,
             "name=" + m_vehicle_active_route_name + "#duration=0");
      m_vehicle_active_route_name.clear();
    }
    Notify(m_route_clear_var, "true");
    postState("CLEAR_ACCEPTED");
    reportEvent("Accepted mediated CLEAR");
    return(true);
  }

  if(action == "deploy") {
    if(!task_id.empty() || !m_task_execute_var.empty()) {
      if(task_id.empty() || strContainsWhite(task_id) || !isNumber(task_time) ||
         !std::isfinite(atof(task_time.c_str())) || atof(task_time.c_str()) < m_started ||
         atof(task_time.c_str()) > m_curr_time + 1 ||
         m_curr_time - atof(task_time.c_str()) > 30) {
        postState("TASK_REJECTED_STALE");
        m_commands_rejected++;
        return false;
      }
      if(!m_seen_tasks.insert(task_id).second) {
        reportEvent("Ignored duplicate task " + task_id);
        return true;
      }
      if(purpose != "operator" || !taskPermitted() ||
         m_vehicle_deploy_pending || m_task_start_pending ||
         (!m_vehicle_active_route_name.empty() && m_vehicle_route_purpose == "operator")) {
        postState("TASK_REJECTED_CONTEXT");
        m_commands_rejected++;
        return false;
      }
      route_name = task_id; // Immutable task identity is also its Helm instance.
      m_route_ready = false;
      m_route_ready_time = -1;
      m_ready_name.clear();
      m_ready_name_time = -1;
      Notify(m_route_ready_var, "false");
    }
    if(purpose != "operator" && purpose != "recovery") {
      m_commands_rejected++;
      postState("DEPLOY_REJECTED_PURPOSE");
      return(false);
    }
    if(!m_route_purpose_var.empty()) {
      const auto& allowed = purpose == "operator" ? m_operator_states :
                                                m_recovery_states;
      if(!allowed.count(m_operation_state) ||
         (purpose == "recovery" && source != m_recovery_source)) {
        m_commands_rejected++;
        postState("DEPLOY_REJECTED_AUTHORITY");
        reportRunWarning("DEPLOY rejected: " + purpose + " route from " +
                         source + " in " + m_operation_state);
        return(false);
      }
    }
    return(postRouteSnapshot(points, speed, capture_radius, slip_radius,
                             route_name, purpose));
  }

  if(action == "append") {
    if(!m_route_purpose_var.empty() &&
       (m_vehicle_route_purpose != "operator" ||
        !m_operator_states.count(m_operation_state))) {
      m_commands_rejected++;
      postState("APPEND_REJECTED_AUTHORITY");
      return(false);
    }
    XYPoint new_point = string2Point(point);
    if(!new_point.valid()) {
      m_commands_rejected++;
      postState("APPEND_REJECTED_POINT");
      reportRunWarning("APPEND rejected: invalid route point");
      return(false);
    }

    string point_spec = doubleToStringX(new_point.x(), 6) + ",";
    point_spec += doubleToStringX(new_point.y(), 6);
    string update = "name=" + m_route_name + " # newpt=" + point_spec;
    Notify(m_route_update_var, update);
    postState("APPEND_ACCEPTED");
    reportEvent("Accepted live route append");
    return(true);
  }

  m_commands_rejected++;
  postState("COMMAND_REJECTED");
  reportRunWarning("Invalid route command: " + command);
  return(false);
}

//---------------------------------------------------------
// Procedure: sendMediatedCommand()

bool RouteBuffer::sendMediatedCommand(const string& command, const string& destination)
{
  NodeMessage message(m_source_node, destination.empty() ? m_destination_node : destination,
                      m_command_var);
  message.setSourceApp(GetAppName());
  message.setStringVal(command);

  if(!message.valid()) {
    reportRunWarning("Unable to form mediated route command");
    return(false);
  }

  if(!Notify("NODE_MESSAGE_LOCAL", message.getSpec()))
    return false;
  m_commands_sent++;
  return(true);
}

//---------------------------------------------------------
// Procedure: postRouteSnapshot()

bool RouteBuffer::postRouteSnapshot(const string& points, const string& speed,
                                    const string& capture_radius,
                                    const string& slip_radius,
                                    const string& route_name,
                                    const string& purpose)
{
  double route_speed = 0;
  if((speed != "") && !setPosDoubleOnString(route_speed, speed)) {
    m_commands_rejected++;
    postState("DEPLOY_REJECTED_SPEED");
    reportRunWarning("DEPLOY rejected: invalid route speed");
    return(false);
  }

  double route_capture_radius = 0;
  if((capture_radius != "") &&
     !setPosDoubleOnString(route_capture_radius, capture_radius)) {
    m_commands_rejected++;
    postState("DEPLOY_REJECTED_CAPTURE_RADIUS");
    reportRunWarning("DEPLOY rejected: invalid capture radius");
    return(false);
  }

  double route_slip_radius = 0;
  if((slip_radius != "") &&
     !setNonNegDoubleOnString(route_slip_radius, slip_radius)) {
    m_commands_rejected++;
    postState("DEPLOY_REJECTED_SLIP_RADIUS");
    reportRunWarning("DEPLOY rejected: invalid slip radius");
    return(false);
  }

  if((route_name != "") && strContainsWhite(route_name)) {
    m_commands_rejected++;
    postState("DEPLOY_REJECTED_ROUTE_NAME");
    reportRunWarning("DEPLOY rejected: invalid route name");
    return(false);
  }

  string parse_points = points;
  if(strBegins(parse_points, "{"))
    parse_points = "pts=" + parse_points;

  XYSegList seglist = string2SegList(parse_points);
  if((seglist.size() == 0) || (seglist.size() > m_max_points)) {
    m_commands_rejected++;
    postState("DEPLOY_REJECTED_POINTS");
    reportRunWarning("DEPLOY rejected: invalid route snapshot");
    return(false);
  }

  for(unsigned int i=0; i<seglist.size(); ++i) {
    if(!std::isfinite(seglist.get_vx(i)) || !std::isfinite(seglist.get_vy(i))) {
      m_commands_rejected++;
      postState("DEPLOY_REJECTED_POINTS");
      return false;
    }
  }

  string update_name = (route_name == "") ? m_route_name : route_name;
  // Identity is a literal in a Helm logic condition, never operator syntax.
  if(!m_route_identity_var.empty() &&
     update_name.find_first_not_of(
       "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != string::npos) {
    m_commands_rejected++;
    postState("DEPLOY_REJECTED_ROUTE_NAME");
    return false;
  }
  string update = "name=" + update_name;
  if(!m_route_identity_var.empty()) {
    // A parked Helm may discard a previous duration=0 delta. Retained
    // instances must never become runnable again for a different task.
    update += " # condition=" + m_route_identity_var + "=" + update_name;
    Notify(m_route_identity_var, update_name);
  }
  m_vehicle_active_route_name = update_name;
  m_vehicle_route_purpose = purpose;
  update += " # order=normal";
  update += " # points=" + seglist.get_spec_pts();
  if(speed != "")
    update += " # speed=" + doubleToStringX(route_speed, 2);
  if(capture_radius != "")
    update += " # capture_radius=" +
              doubleToStringX(route_capture_radius, 2);
  if(slip_radius != "")
    update += " # slip_radius=" + doubleToStringX(route_slip_radius, 2);
  update += " # duration=-1";

  if(m_deploy_delay > 0) {
    if(m_manual_override_var != "")
      Notify(m_manual_override_var, "true");
    Notify(m_route_deploy_var, "false");
  }
  Notify(m_route_clear_var, "false");
  if(!m_route_purpose_var.empty()) Notify(m_route_purpose_var, purpose);
  m_deferred_helm_update.clear();
  if(m_helm_state_var.empty() || m_helm_state == "DRIVE")
    Notify(m_route_update_var, update);
  else
    m_deferred_helm_update = update;
  m_vehicle_route_replay_pending = false;
  m_vehicle_pending_route_update = update;
  m_vehicle_deploy_pending = true;
  m_vehicle_deploy_pending_since = m_curr_time;
  postState("STAGED");
  reportEvent("Accepted mediated route with " +
              uintToString(seglist.size()) + " points");
  return(true);
}

//---------------------------------------------------------
// Procedure: postPoint()

void RouteBuffer::postPoint(const XYPoint& point, unsigned int index,
                            bool active)
{
  XYPoint view_point = point;
  view_point.set_label("route_buffer_" + uintToString(index));
  view_point.set_type("route_pending");
  // Keep the label as the stable identity used by clearVisualization(),
  // but do not draw its text over the route.
  view_point.set_label_color("off");
  view_point.set_vertex_color("yellow");
  view_point.set_vertex_size(4);
  view_point.set_active(active);
  Notify("VIEW_POINT", view_point.get_spec());

  if(index > m_max_visualized_points)
    m_max_visualized_points = index;
}

//---------------------------------------------------------
// Procedure: postSegList()

void RouteBuffer::postSegList(bool active)
{
  XYSegList seglist;
  for(unsigned int i=0; i<m_points.size(); i++)
    seglist.add_vertex(m_points[i]);

  // The label is the viewer identity; keep it stable through editing, deploy,
  // and clear. Only the staged preview needs an explanatory visible message.
  seglist.set_label("route_buffer_path");
  const bool uncommitted = active && !m_route_deployed;
  if(uncommitted) seglist.set_msg("route (uncommitted)");
  seglist.set_label_color(uncommitted ? "orange" : "off");
  seglist.set_edge_color(uncommitted ? "orange" : "yellow");
  seglist.set_vertex_color(uncommitted ? "orange" : "yellow");
  seglist.set_vertex_size(4);
  seglist.set_edge_size(1);
  seglist.set_active(active);
  Notify("VIEW_SEGLIST", seglist.get_spec());
}

//---------------------------------------------------------
// Procedure: clearVisualization()

void RouteBuffer::clearVisualization()
{
  for(unsigned int i=1; i<=m_max_visualized_points; i++) {
    string spec = "x=0,y=0,label=route_buffer_" + uintToString(i);
    spec += ",active=false";
    Notify("VIEW_POINT", spec);
  }

  postSegList(false);
  m_max_visualized_points = 0;
}

//---------------------------------------------------------
// Procedure: postState()

void RouteBuffer::postState(const string& state)
{
  m_state = state;
  string prefix = (m_role == "vehicle") ? "ROUTE_BUFFER_VEHICLE_" :
                                          "ROUTE_BUFFER_";
  Notify(prefix + "STATE", m_state);
  Notify(prefix + "COUNT", (double)(m_points.size()));
}

//---------------------------------------------------------
// Procedure: getPointsSpec()

string RouteBuffer::getPointsSpec() const
{
  XYSegList seglist;
  for(unsigned int i=0; i<m_points.size(); i++)
    seglist.add_vertex(m_points[i]);

  string points_spec = seglist.get_spec_pts();
  if(strBegins(points_spec, "pts="))
    points_spec = points_spec.substr(4);
  return(points_spec);
}

//---------------------------------------------------------
// Procedure: mailIsTrue()

bool RouteBuffer::mailIsTrue(const CMOOSMsg& msg) const
{
  if(msg.IsDouble())
    return(msg.GetDouble() != 0);

  string value = tolower(stripBlankEnds(msg.GetString()));
  return((value == "true") || (value == "1") || (value == "on"));
}

//------------------------------------------------------------
// Procedure: buildReport()

bool RouteBuffer::buildReport()
{
  m_msgs << "Role:             " << m_role << endl;
  m_msgs << "Task guards ready: " << (taskPermitted() ? "true" : "false") << endl;
  m_msgs << "Task start pending: " << (m_task_start_pending ? "true" : "false") << endl;
  m_msgs << "State:            " << m_state << endl;
  m_msgs << "Source node:      " << m_source_node << endl;
  m_msgs << "Destination node: " << m_destination_node << endl;
  m_msgs << "Route points:     " << m_points.size()
         << "/" << m_max_points << endl;
  m_msgs << "Contacts cached:  " << m_contact_points.size() << endl;
  if(!m_helm_state_var.empty()) {
    m_msgs << "Helm state:       " << (m_helm_state.empty() ? "missing" : m_helm_state) << endl;
    m_msgs << "Snapshot waiting for DRIVE: " << boolToString(!m_deferred_helm_update.empty()) << endl;
  }
  m_msgs << "Contact max age:  " << m_contact_max_age << endl;
  m_msgs << "Deploy delay:     " << m_deploy_delay << endl;
  m_msgs << "Route replay delay:" << m_route_replay_delay << endl;
  m_msgs << "Manual override:  " << m_manual_override_var << endl;
  m_msgs << "Live append:      " << boolToString(m_live_append) << endl;
  m_msgs << "Route deployed:   " << boolToString(m_route_deployed) << endl;
  m_msgs << "Replay pending:   "
         << boolToString(m_vehicle_route_replay_pending) << endl;
  m_msgs << "Commands sent:    " << m_commands_sent << endl;
  m_msgs << "Commands received:" << m_commands_received << endl;
  m_msgs << "Commands rejected:" << m_commands_rejected << endl;

  if(m_role == "shoreside") {
    ACTable actab(3);
    actab << "Index | X | Y";
    actab.addHeaderLines();
    for(unsigned int i=0; i<m_points.size(); i++)
      actab << (i+1) << m_points[i].x() << m_points[i].y();
    m_msgs << endl << actab.getFormattedString();
  }

  return(true);
}
