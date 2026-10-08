/************************************************************/
/*    NAME: Charles Benjamin                               */
/*    FILE: Rendezvous.cpp                                 */
/************************************************************/

#include <chrono>
#include <cmath>
#include <cstdint>
#include "MBUtils.h"
#include "ColorParse.h"
#include "NodeMessage.h"
#include "Rendezvous.h"

using namespace std;

Rendezvous::Rendezvous()
  : m_state("IDLE"), m_reason("startup")
{
}

Rendezvous::~Rendezvous() = default;

bool Rendezvous::OnNewMail(MOOSMSG_LIST &mail)
{
  AppCastingMOOSApp::OnNewMail(mail);
  const string waypoint = toupper(m_ownship) + "_RENDEZVOUS_WAYPOINT_REACHED";
  for(auto &msg : mail) {
    const string key = msg.GetKey();
    bool handled = false;
    if(key == "RENDEZVOUS_START") handled = handleStart(msg);
    else if(key == "RENDEZVOUS_ABORT") handled = handleAbort(msg);
    else if(key == "NODE_REPORT") handled = handleNodeReport(msg);
    else if(key == "RENDEZVOUS_MOTION") handled = handleMotion(msg);
    else if(key == "NAV_X") handled = handleDouble(msg, m_nav_x, m_nav_x_time) && (m_nav_x_set=true);
    else if(key == "NAV_Y") handled = handleDouble(msg, m_nav_y, m_nav_y_time) && (m_nav_y_set=true);
    else if(key == waypoint) {
      m_waypoint_reached = isTrue(msg);
      handled = true;
    }
    else if(key == "APPCAST_REQ") handled = true;
    if(!handled && key != "APPCAST_REQ")
      reportRunWarning("Unhandled Mail: " + key);
  }
  return true;
}

bool Rendezvous::Iterate()
{
  AppCastingMOOSApp::Iterate();
  if(m_state_enter_time <= 0) m_state_enter_time = m_curr_time;

  if(m_state == "STARTING") {
    if(navFresh() && peerFresh()) {
      m_session = makeSession();
      m_planner.reset(m_curr_time, m_nav_x, m_nav_y);
      m_peer_speed_estimator.reset(m_peer_time, m_peer_x, m_peer_y);
      m_planner.update(m_curr_time, m_nav_x, m_nav_y, m_peer_x, m_peer_y,
                       RendezvousPlanner::Speed(), m_point_freeze_radius);
      m_target_x = m_planner.x();
      m_target_y = m_planner.y();
      m_waypoint_reached = false;
      publishPoint(true);
      publishRoute(true);
      m_state = "RENDEZVOUS";
      m_reason = "point_ready";
      m_state_enter_time = m_curr_time;
      m_sessions++;
    }
  }
  else if(m_state == "RENDEZVOUS") {
    if(!navFresh()) abort("navigation_stale");
    else if(!peerFresh()) abort("peer_navigation_stale");
    else if(m_curr_time - m_state_enter_time > m_transit_timeout)
      abort("transit_timeout");
    else {
      updatePlanner();
      const double distance = hypot(m_nav_x - m_target_x, m_nav_y - m_target_y);
      Notify(toupper(m_ownship) + "_RENDEZVOUS_DISTANCE", distance);
      if(m_waypoint_reached || distance <= m_arrival_radius) {
        if(m_arrival_start <= 0) m_arrival_start = m_curr_time;
        if(m_curr_time - m_arrival_start >= m_arrival_dwell)
          complete(m_waypoint_reached ? "waypoint_reached" : "arrival_radius");
      } else m_arrival_start = 0;
    }
  }
  publishState(false);
  AppCastingMOOSApp::PostReport();
  return true;
}

bool Rendezvous::OnConnectToServer()
{
  registerVariables();
  return true;
}

bool Rendezvous::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();
  STRING_LIST params;
  m_MissionReader.EnableVerbatimQuoting(false);
  m_MissionReader.GetConfiguration(GetAppName(), params);
  for(auto raw : params) {
    string line = raw;
    string param = tolower(biteStringX(line, '='));
    string value = stripBlankEnds(line);
    bool ok = true;
    if(param == "ownship") ok = setNonWhiteVarOnString(m_ownship, value);
    else if(param == "peer_node") ok = setNonWhiteVarOnString(m_peer_node, value);
    else if(param == "nav_stale_thresh") ok = setPosDoubleOnString(m_nav_stale_thresh, value);
    else if(param == "transit_timeout") ok = setPosDoubleOnString(m_transit_timeout, value);
    else if(param == "point_freeze_radius") ok = setNonNegDoubleOnString(m_point_freeze_radius, value);
    else if(param == "rendezvous_distance" || param == "arrival_radius") ok = setPosDoubleOnString(m_arrival_radius, value);
    else if(param == "arrival_dwell") ok = setNonNegDoubleOnString(m_arrival_dwell, value);
    else if(param == "state_post_interval") ok = setPosDoubleOnString(m_state_post_interval, value);
    else if(param == "point_color") { value=tolower(value); ok=isColor(value); if(ok)m_point_color=value; }
    else if(param == "point_shape") { value=tolower(value); ok=(value=="point"||value=="circle"||value=="disc"); if(ok)m_point_shape=value; }
    else if(param == "point_radius") ok=setPosDoubleOnString(m_point_radius,value);
    else if(param == "point_transparency") ok=setNonNegDoubleOnString(m_point_transparency,value) && m_point_transparency<=1;
    else if(param == "point_label") ok=setBooleanOnString(m_point_label,value);
    else if(param == "landing_enabled" || param == "secondary_landing_enabled" ||
            param == "require_health" || param == "require_battery" || param == "require_flight_state" ||
            param == "require_landing_target" || param == "require_platform_ready" ||
            param == "require_platform_health") {
      reportConfigWarning(param + " is obsolete; pRendezvous is independent and does not coordinate landing");
    }
    else if(param == "") ok=true;
    else { reportUnhandledConfigWarning(raw); ok=true; }
    if(!ok) { reportConfigWarning("Invalid configuration: " + raw); m_config_valid=false; }
  }
  if(m_ownship.empty()) m_ownship=m_host_community;
  if(m_peer_node.empty()) { reportConfigWarning("peer_node is required"); m_config_valid=false; }
  registerVariables();
  return true;
}

void Rendezvous::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  Register("RENDEZVOUS_START", 0);
  Register("RENDEZVOUS_ABORT", 0);
  Register("NAV_X", 0);
  Register("NAV_Y", 0);
  Register("NODE_REPORT", 0);
  Register("RENDEZVOUS_MOTION", 0);
  Register(toupper(m_ownship) + "_RENDEZVOUS_WAYPOINT_REACHED", 0);
}

bool Rendezvous::handleStart(const CMOOSMsg& msg)
{
  if(!isTrue(msg)) return true;
  if(m_state == "RENDEZVOUS" || m_state == "STARTING") return true;
  m_session.clear();
  m_waypoint_reached=false;
  m_arrival_start=0;
  m_state="STARTING";
  m_reason="start_received";
  m_state_enter_time=m_curr_time;
  return true;
}

bool Rendezvous::handleAbort(const CMOOSMsg& msg)
{
  if(isTrue(msg)) abort("operator_abort");
  return true;
}

bool Rendezvous::handleNodeReport(const CMOOSMsg& msg)
{
  if(!msg.IsString()) return false;
  const string report=msg.GetString();
  string name=tolower(tokStringParse(report,"NAME"));
  if(name.empty()) name=tolower(tokStringParse(report,"name"));
  if(name != tolower(m_peer_node)) return true;
  string sx=tokStringParse(report,"X"), sy=tokStringParse(report,"Y");
  if(sx.empty()) sx=tokStringParse(report,"x");
  if(sy.empty()) sy=tokStringParse(report,"y");
  if(!isNumber(sx)||!isNumber(sy)) return true;
  const double stamp_s=tokStringParse(report,"TIME").empty() ? msg.GetTime() : atof(tokStringParse(report,"TIME").c_str());
  const double x=atof(sx.c_str()), y=atof(sy.c_str());
  if(!isfinite(x)||!isfinite(y)||stamp_s < m_peer_time) return true;
  m_peer_speed_estimator.sample(stamp_s,x,y,m_nav_stale_thresh);
  m_peer_x=x; m_peer_y=y; m_peer_time=stamp_s; m_peer_set=true;
  return true;
}

bool Rendezvous::handleMotion(const CMOOSMsg& msg)
{
  if(!msg.IsString()) return false;
  const string value=msg.GetString();
  if(tolower(tokStringParse(value,"node",'#','=')) != tolower(m_peer_node)) return true;
  const string sx=tokStringParse(value,"x",'#','='), sy=tokStringParse(value,"y",'#','='),
               st=tokStringParse(value,"time",'#','=');
  if(!isNumber(sx)||!isNumber(sy)||!isNumber(st)) return true;
  const double stamp=atof(st.c_str());
  if(stamp < m_peer_time || stamp > m_curr_time+1 || m_curr_time-stamp > m_nav_stale_thresh) return true;
  const double x=atof(sx.c_str()), y=atof(sy.c_str());
  m_peer_speed_estimator.sample(stamp,x,y,m_nav_stale_thresh);
  m_peer_x=x; m_peer_y=y; m_peer_time=stamp; m_peer_set=true;
  return true;
}

bool Rendezvous::handleDouble(const CMOOSMsg& msg,double& value,double& stamp)
{
  if(!msg.IsDouble() || !isfinite(msg.GetDouble())) return false;
  value=msg.GetDouble(); stamp=m_curr_time; return true;
}

bool Rendezvous::handleBool(const CMOOSMsg& msg,bool& value)
{
  value=isTrue(msg); return true;
}

void Rendezvous::updatePlanner()
{
  const double stamp=min(m_nav_x_time,m_nav_y_time);
  m_planner.sample(stamp,m_nav_x,m_nav_y,m_nav_stale_thresh);
  publishMotion();
  const RendezvousPlanner::Speed peer_speed=m_peer_speed_estimator.speed();
  Notify(toupper(m_ownship)+"_RENDEZVOUS_PEER_SPEED",peer_speed.value);
  Notify(toupper(m_ownship)+"_RENDEZVOUS_PEER_SPEED_VALID",peer_speed.valid?1.0:0.0);
  Notify(toupper(m_ownship)+"_RENDEZVOUS_PEER_SPEED_WINDOW",peer_speed.window);
  const bool own_captured=m_waypoint_reached ||
    hypot(m_nav_x-m_target_x,m_nav_y-m_target_y)<=m_arrival_radius;
  if(m_planner.update(m_curr_time,m_nav_x,m_nav_y,m_peer_x,m_peer_y,
                      peer_speed,m_point_freeze_radius,own_captured)) {
    m_target_x=m_planner.x(); m_target_y=m_planner.y(); publishPoint(true); publishRoute(false);
  }
  Notify(toupper(m_ownship)+"_RENDEZVOUS_POINT_FROZEN",m_planner.frozen()?1.0:0.0);
}

void Rendezvous::publishMotion()
{
  if(m_curr_time-m_last_motion_time<1) return;
  const auto speed=m_planner.speed();
  const string prefix=toupper(m_ownship)+"_RENDEZVOUS_";
  Notify(prefix+"SPEED",speed.value); Notify(prefix+"SPEED_VALID",speed.valid?1.0:0.0);
  Notify(prefix+"SPEED_WINDOW",speed.window);
  string motion="session="+m_session+"#node="+m_ownship;
  motion+="#x="+doubleToStringX(m_nav_x,3)+"#y="+doubleToStringX(m_nav_y,3);
  motion+="#time="+doubleToStringX(m_curr_time,6);
  sendMessage(m_peer_node,"RENDEZVOUS_MOTION",motion);
  m_last_motion_time=m_curr_time;
}

void Rendezvous::publishRoute(bool initial)
{
  Notify(toupper(m_ownship)+"_RENDEZVOUS_UPDATE","points="+doubleToStringX(m_target_x,2)+","+doubleToStringX(m_target_y,2));
  if(initial) activateRoute();
}

void Rendezvous::activateRoute()
{
  const string prefix=toupper(m_ownship);
  // Helm conditions compare these variables to string literals (`=true` /
  // `=false`). Use string Notify overloads; bool converts to numeric 1/0 and
  // does not satisfy those string conditions.
  Notify(prefix+"_RENDEZVOUS_WAYPOINT_REACHED","false");
  Notify(prefix+"_RENDEZVOUS_WAYPOINT_READY","true");
  Notify(prefix+"_RENDEZVOUS_ACTIVE","true");
  Notify("MOOS_MANUAL_OVERRIDE","false");
  // The UAV waypoint behavior consumes this; other vehicle types simply
  // ignore the variable. No vehicle role is encoded in pRendezvous.
  Notify("UAV_RENDEZVOUS_HANDOFF_REQUEST","true");
}

void Rendezvous::complete(const string& reason)
{
  m_state="COMPLETE"; m_reason=reason; m_state_enter_time=m_curr_time;
  // Publish the terminal phase before disabling the waypoint so the hold
  // behavior becomes eligible in the same MOOS mail cycle.
  publishState(true);
  Notify(toupper(m_ownship)+"_RENDEZVOUS_ACTIVE","false");
  Notify(toupper(m_ownship)+"_RENDEZVOUS_WAYPOINT_READY","false");
  Notify("UAV_RENDEZVOUS_HANDOFF_REQUEST","false");
  Notify("ARDU_COMMAND","HOLD_POSITION");
  Notify("MOOS_MANUAL_OVERRIDE","false");
  // Keep the independently selected endpoint visible while the vehicle
  // station-keeps/loiters there.
  publishPoint(true);
}

void Rendezvous::abort(const string& reason)
{
  m_state="ABORT"; m_reason=reason; m_state_enter_time=m_curr_time; ++m_aborts;
  publishState(true);
  Notify(toupper(m_ownship)+"_RENDEZVOUS_ACTIVE","false");
  Notify(toupper(m_ownship)+"_RENDEZVOUS_WAYPOINT_READY","false");
  Notify("UAV_RENDEZVOUS_HANDOFF_REQUEST","false");
}

void Rendezvous::publishState(bool force)
{
  if(!force && m_curr_time-m_last_state_time<m_state_post_interval) return;
  const string prefix=toupper(m_ownship);
  Notify(prefix+"_RENDEZVOUS_STATE",m_state);
  Notify(prefix+"_RENDEZVOUS_PHASE",m_state);
  Notify(prefix+"_RENDEZVOUS_SESSION",m_session);
  Notify(prefix+"_RENDEZVOUS_ARRIVED",m_state=="COMPLETE"?1.0:0.0);
  Notify(prefix+"_RENDEZVOUS_POINT",pointString());
  m_last_state_time=m_curr_time;
}

void Rendezvous::publishPoint(bool active)
{
  string spec="x="+doubleToStringX(m_target_x,2)+",y="+doubleToStringX(m_target_y,2);
  spec+=",label=rendezvous_"+tolower(m_ownship)+",label_color="+(m_point_label?m_point_color:"invisible");
  spec+=",active="+(active?string("true"):string("false"));
  if(m_point_shape=="point") { spec+=",vertex_color="+m_point_color+",vertex_size=10"; Notify("VIEW_POINT",spec); }
  else { spec+=",radius="+doubleToStringX(m_point_radius,2)+",edge_color="+m_point_color+",edge_size=2,vertex_size=0";
    if(m_point_shape=="disc") spec+=",fill_color="+m_point_color+",fill_transparency="+doubleToStringX(m_point_transparency,2);
    Notify("VIEW_CIRCLE",spec);
  }
  Notify("RENDEZVOUS_POINT",pointString());
  Notify(toupper(m_ownship)+"_RENDEZVOUS_POINT",pointString());
}

bool Rendezvous::sendMessage(const string& destination,const string& variable,const string& value)
{
  NodeMessage message(m_ownship,destination,variable); message.setSourceApp(GetAppName()); message.setStringVal(value);
  if(!message.valid()) return false;
  Notify("NODE_MESSAGE_LOCAL",message.getSpec()); return true;
}

bool Rendezvous::navFresh() const { return m_nav_x_set&&m_nav_y_set&&m_curr_time-min(m_nav_x_time,m_nav_y_time)<=m_nav_stale_thresh; }
bool Rendezvous::peerFresh() const { return m_peer_set&&m_peer_time>0&&m_curr_time-m_peer_time<=m_nav_stale_thresh; }
bool Rendezvous::isTrue(const CMOOSMsg& msg) const
{
  if(msg.IsDouble()) return isfinite(msg.GetDouble())&&msg.GetDouble()!=0;
  const string v=tolower(stripBlankEnds(msg.GetString())); return v=="true"||v=="1"||v=="on";
}
string Rendezvous::pointString() const { return "{"+doubleToStringX(m_target_x,2)+","+doubleToStringX(m_target_y,2)+"}"; }
string Rendezvous::makeSession() const
{
  const uint64_t now=chrono::duration_cast<chrono::microseconds>(chrono::system_clock::now().time_since_epoch()).count();
  return tolower(m_ownship)+"_"+to_string(now);
}

bool Rendezvous::buildReport()
{
  m_msgs<<"Ownship / peer: "<<m_ownship<<" / "<<m_peer_node<<"\n";
  m_msgs<<"State: "<<m_state<<" ("<<m_reason<<")\n";
  m_msgs<<"Session: "<<m_session<<"\n";
  m_msgs<<"Ownship fresh: "<<(navFresh()?"true":"false")<<"  Peer fresh: "<<(peerFresh()?"true":"false")<<"\n";
  const auto own_speed=m_planner.speed();
  const auto peer_speed=m_peer_speed_estimator.speed();
  m_msgs<<"Target: "<<pointString()<<"  Frozen: "<<(m_planner.frozen()?"true":"false")<<"\n";
  m_msgs<<"Measured speeds (own/peer): "<<own_speed.value<<" / "<<peer_speed.value
       <<" m/s (valid: "<<(own_speed.valid?"true":"false")<<" / "
       <<(peer_speed.valid?"true":"false")<<")\n";
  m_msgs<<"Completed locally: "<<(m_state=="COMPLETE"?"true":"false")<<"\n";
  m_msgs<<"Landing coordination: owned by pLandingCoordinator\n";
  return true;
}
