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

  MOOSMSG_LIST::iterator p;
  for(p=NewMail.begin(); p!=NewMail.end(); p++) {
    CMOOSMsg &msg = *p;
    string key    = msg.GetKey();

    bool handled = false;
    if((m_role == "shoreside") && (key == m_point_var))
      handled = handleMailPoint(msg);
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
      handled = true;
    }
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
      string command = m_pending_commands.front();
      m_pending_commands.pop_front();
      processVehicleCommand(command);
    }

    if((m_role == "vehicle") && m_vehicle_deploy_pending) {
      double staged_time = m_curr_time - m_vehicle_deploy_pending_since;
      bool release_route = false;
      if((m_deploy_delay > 0) && (staged_time >= m_deploy_delay)) {
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
          postState("DEPLOY_ACCEPTED");
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

    if(!handled)
      reportUnhandledConfigWarning(orig);
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
    Register(m_deploy_request_var, 0);
    Register(m_clear_request_var, 0);
    Register(m_goto_request_var, 0);
    Register(m_node_report_var, 0);
  }
  else if(m_role == "vehicle") {
    Register(m_command_var, 0);
    Register(m_route_ready_var, 0);
  }
}

//---------------------------------------------------------
// Procedure: handleMailPoint()

bool RouteBuffer::handleMailPoint(const CMOOSMsg& msg)
{
  if(!msg.IsString())
    return(false);

  XYPoint point = string2Point(msg.GetString());
  if(!point.valid()) {
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
  postSegList(true);

  if(m_live_append && m_route_deployed) {
    string point_spec = doubleToStringX(point.x(), 6) + ",";
    point_spec += doubleToStringX(point.y(), 6);
    if(!sendMediatedCommand("action=append # point=" + point_spec))
      return(false);
    postState("APPEND_SUBMITTED");
  }
  else
    postState("EDITING");

  return(true);
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

  m_pending_commands.push_back(msg.GetString());
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
    m_points.push_back(m_contact_points[target]);
    postPoint(m_points.front(), 1, true);
    postSegList(true);

    string points = getPointsSpec();
    string command = "action=deploy # points=" + points;
    if(!sendMediatedCommand(command))
      return(false);

    m_last_submitted_points = points;
    m_route_deployed = true;
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
  postState("SUBMITTED");
  return(true);
}

//---------------------------------------------------------
// Procedure: processVehicleCommand()

bool RouteBuffer::processVehicleCommand(const string& command)
{
  m_commands_received++;

  vector<string> fields = parseStringQ(command, '#');
  string action;
  string points;
  string point;
  for(unsigned int i=0; i<fields.size(); i++) {
    string field = stripBlankEnds(fields[i]);
    string param = tolower(biteStringX(field, '='));
    string value = stripBlankEnds(field);

    if(param == "action")
      action = tolower(value);
    else if(param == "points")
      points = value;
    else if(param == "point")
      point = value;
  }

  if(action == "sync") {
    postState("SYNCED");
    return(true);
  }

  if(action == "clear") {
    m_vehicle_deploy_pending = false;
    m_vehicle_route_replay_pending = false;
    m_vehicle_pending_route_update.clear();
    Notify(m_route_clear_var, "true");
    postState("CLEAR_ACCEPTED");
    reportEvent("Accepted mediated CLEAR");
    return(true);
  }

  if(action == "deploy")
    return(postRouteSnapshot(points));

  if(action == "append") {
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

bool RouteBuffer::sendMediatedCommand(const string& command)
{
  NodeMessage message(m_source_node, m_destination_node, m_command_var);
  message.setSourceApp(GetAppName());
  message.setStringVal(command);

  if(!message.valid()) {
    reportRunWarning("Unable to form mediated route command");
    return(false);
  }

  Notify("NODE_MESSAGE_LOCAL", message.getSpec());
  m_commands_sent++;
  return(true);
}

//---------------------------------------------------------
// Procedure: postRouteSnapshot()

bool RouteBuffer::postRouteSnapshot(const string& points)
{
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

  string update = "name=" + m_route_name;
  update += " # order=normal";
  update += " # points=" + seglist.get_spec_pts();
  update += " # duration=-1";

  if(m_deploy_delay > 0) {
    if(m_manual_override_var != "")
      Notify(m_manual_override_var, "true");
    Notify(m_route_deploy_var, "false");
  }
  Notify(m_route_clear_var, "false");
  Notify(m_route_update_var, update);
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
  view_point.set_label_color("aqua");
  view_point.set_vertex_color("yellow");
  view_point.set_vertex_size(8);
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

  seglist.set_label("route_buffer_path");
  seglist.set_edge_color("yellow");
  seglist.set_vertex_color("yellow");
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
  m_msgs << "State:            " << m_state << endl;
  m_msgs << "Source node:      " << m_source_node << endl;
  m_msgs << "Destination node: " << m_destination_node << endl;
  m_msgs << "Route points:     " << m_points.size()
         << "/" << m_max_points << endl;
  m_msgs << "Contacts cached:  " << m_contact_points.size() << endl;
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
