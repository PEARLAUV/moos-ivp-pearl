/************************************************************/
/*    NAME: Charles Benjamin                               */
/*    FILE: Rendezvous.cpp                                 */
/************************************************************/

#include "Rendezvous.h"
#include "ColorParse.h"
#include "MBUtils.h"
#include "NodeMessage.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>

using namespace std;

Rendezvous::Rendezvous() : m_state("IDLE"), m_reason("startup") {}

Rendezvous::~Rendezvous() = default;

bool Rendezvous::OnNewMail(MOOSMSG_LIST &mail)
{
  AppCastingMOOSApp::OnNewMail(mail);
  const string waypoint = ownVar("WAYPOINT_REACHED");
  for (auto &msg : mail) {
    const string key = msg.GetKey();
    bool handled = false;
    if (key == "RENDEZVOUS_START")
      handled = handleStart(msg);
    else if (key == "RENDEZVOUS_ABORT")
      handled = handleAbort(msg);
    else if (key == "NODE_REPORT")
      handled = handleNodeReport(msg);
    else if (key == "RENDEZVOUS_MOTION")
      handled = handleMotion(msg);
    else if (key == "NAV_X")
      handled = handleDouble(msg, m_nav_x, m_nav_x_time) && (m_nav_x_set = true);
    else if (key == "NAV_Y")
      handled = handleDouble(msg, m_nav_y, m_nav_y_time) && (m_nav_y_set = true);
    else if (key == waypoint) {
      m_waypoint_reached = isTrue(msg);
      handled = true;
    } else if (key == "APPCAST_REQ")
      handled = true;
    if (!handled && key != "APPCAST_REQ")
      reportRunWarning("Unhandled Mail: " + key);
  }
  return true;
}

bool Rendezvous::Iterate()
{
  AppCastingMOOSApp::Iterate();
  if (m_state_enter_time <= 0)
    m_state_enter_time = m_curr_time;

  if (m_state == "STARTING") {
    publishMotion();
    if (navFresh() && peerFresh()) {
      m_planner.reset(m_curr_time, m_nav_x, m_nav_y);
      m_peer_speed_estimator.reset(m_peer_time, m_peer_x, m_peer_y);
      m_planner.update(m_curr_time, m_nav_x, m_nav_y, m_peer_x, m_peer_y,
                       RendezvousPlanner::Speed(), m_point_freeze_radius);
      if (m_freeze_time > 0 && tolower(m_freeze_node) != tolower(m_ownship))
        m_planner.adoptFrozenPoint(m_freeze_x, m_freeze_y);
      m_target_x = m_planner.x();
      m_target_y = m_planner.y();
      m_waypoint_reached = false;
      publishPoint(true);
      publishRoute(true);
      m_state = "RENDEZVOUS";
      m_reason = "point_ready";
      m_state_enter_time = m_curr_time;
      m_sessions++;
      publishMotion(true);
    }
  } else if (m_state == "RENDEZVOUS") {
    if (!navFresh())
      abort("navigation_stale");
    else if (!peerFresh())
      abort("peer_navigation_stale");
    else if (m_curr_time - m_state_enter_time > m_transit_timeout)
      abort("transit_timeout");
    else {
      updatePlanner();
      const double distance = hypot(m_nav_x - m_target_x, m_nav_y - m_target_y);
      Notify(ownVar("DISTANCE"), distance);
      if (m_waypoint_reached || distance <= m_arrival_radius) {
        if (m_arrival_start <= 0)
          m_arrival_start = m_curr_time;
        if (m_curr_time - m_arrival_start >= m_arrival_dwell)
          complete(m_waypoint_reached ? "waypoint_reached" : "arrival_radius");
      } else
        m_arrival_start = 0;
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
  for (auto raw : params) {
    string line = raw;
    string param = tolower(biteStringX(line, '='));
    string value = stripBlankEnds(line);
    bool ok = true;
    if (param == "ownship")
      ok = setNonWhiteVarOnString(m_ownship, value);
    else if (param == "peer_node")
      ok = setNonWhiteVarOnString(m_peer_node, value);
    else if (param == "nav_stale_thresh")
      ok = setPosDoubleOnString(m_nav_stale_thresh, value);
    else if (param == "transit_timeout")
      ok = setPosDoubleOnString(m_transit_timeout, value);
    else if (param == "point_freeze_radius")
      ok = setNonNegDoubleOnString(m_point_freeze_radius, value);
    else if (param == "rendezvous_distance" || param == "arrival_radius")
      ok = setPosDoubleOnString(m_arrival_radius, value);
    else if (param == "arrival_dwell")
      ok = setNonNegDoubleOnString(m_arrival_dwell, value);
    else if (param == "state_post_interval")
      ok = setPosDoubleOnString(m_state_post_interval, value);
    else if (param == "point_color") {
      value = tolower(value);
      ok = isColor(value);
      if (ok)
        m_point_color = value;
    } else if (param == "point_shape") {
      value = tolower(value);
      ok = (value == "point" || value == "circle" || value == "disc");
      if (ok)
        m_point_shape = value;
    } else if (param == "point_radius")
      ok = setPosDoubleOnString(m_point_radius, value);
    else if (param == "point_transparency")
      ok = setNonNegDoubleOnString(m_point_transparency, value) && m_point_transparency <= 1;
    else if (param == "point_label")
      ok = setBooleanOnString(m_point_label, value);
    else if (param == "landing_enabled" || param == "secondary_landing_enabled" ||
             param == "require_health" || param == "require_battery" ||
             param == "require_flight_state" || param == "require_landing_target" ||
             param == "require_platform_ready" || param == "require_platform_health") {
      reportConfigWarning(
          param + " is obsolete; pRendezvous is independent and does not coordinate landing");
    } else if (param == "")
      ok = true;
    else {
      reportUnhandledConfigWarning(raw);
      ok = true;
    }
    if (!ok) {
      reportConfigWarning("Invalid configuration: " + raw);
      m_config_valid = false;
    }
  }
  if (m_ownship.empty())
    m_ownship = m_host_community;
  if (m_peer_node.empty()) {
    reportConfigWarning("peer_node is required");
    m_config_valid = false;
  }
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
  Register(ownVar("WAYPOINT_REACHED"), 0);
}

bool Rendezvous::handleStart(const CMOOSMsg &msg)
{
  if (!isTrue(msg))
    return true;
  if (m_state == "RENDEZVOUS" || m_state == "STARTING")
    return true;
  // STARTING motion packets are the peer's initial position source. Keep the
  // first key=value token non-empty so tokStringParse can parse later fields.
  m_session = makeSession();
  m_freeze_x = 0;
  m_freeze_y = 0;
  m_freeze_time = 0;
  m_freeze_node.clear();
  m_waypoint_reached = false;
  m_arrival_start = 0;
  m_state = "STARTING";
  m_reason = "start_received";
  m_state_enter_time = m_curr_time;
  return true;
}

bool Rendezvous::handleAbort(const CMOOSMsg &msg)
{
  if (isTrue(msg))
    abort("operator_abort");
  return true;
}

bool Rendezvous::handleNodeReport(const CMOOSMsg &msg)
{
  if (!msg.IsString())
    return false;
  const string report = msg.GetString();
  string name = tolower(tokStringParse(report, "NAME"));
  if (name.empty())
    name = tolower(tokStringParse(report, "name"));
  if (name != tolower(m_peer_node))
    return true;
  string sx = tokStringParse(report, "X"), sy = tokStringParse(report, "Y");
  if (sx.empty())
    sx = tokStringParse(report, "x");
  if (sy.empty())
    sy = tokStringParse(report, "y");
  if (!isNumber(sx) || !isNumber(sy))
    return true;
  const double stamp_s = tokStringParse(report, "TIME").empty()
                             ? msg.GetTime()
                             : atof(tokStringParse(report, "TIME").c_str());
  const double x = atof(sx.c_str()), y = atof(sy.c_str());
  if (!isfinite(x) || !isfinite(y) || stamp_s < m_peer_time)
    return true;
  m_peer_speed_estimator.sample(stamp_s, x, y, m_nav_stale_thresh);
  m_peer_x = x;
  m_peer_y = y;
  m_peer_time = stamp_s;
  m_peer_set = true;
  return true;
}

bool Rendezvous::handleMotion(const CMOOSMsg &msg)
{
  if (!msg.IsString())
    return false;
  const string value = msg.GetString();
  if (tolower(tokStringParse(value, "node", '#', '=')) != tolower(m_peer_node))
    return true;
  const string peer_phase = tokStringParse(value, "phase", '#', '=');
  const string peer_session = tokStringParse(value, "session", '#', '=');
  const string st = tokStringParse(value, "time", '#', '=');
  const bool message_fresh = isNumber(st) && atof(st.c_str()) <= m_curr_time + 1 &&
                             m_curr_time - atof(st.c_str()) <= m_nav_stale_thresh;
  if (message_fresh && !peer_phase.empty()) {
    const double phase_stamp = atof(st.c_str());
    if (phase_stamp >= m_peer_phase_time) {
      Notify(peerVar("PHASE"), peer_phase);
      Notify(peerVar("SESSION"), peer_session);
      m_peer_phase_time = phase_stamp;
    }
  }

  const string freeze_time_s = tokStringParse(value, "freeze_time", '#', '=');
  const string freeze_node = tokStringParse(value, "freeze_node", '#', '=');
  const string freeze_x_s = tokStringParse(value, "point_x", '#', '=');
  const string freeze_y_s = tokStringParse(value, "point_y", '#', '=');
  if (message_fresh && isNumber(freeze_time_s) && isNumber(freeze_x_s) && isNumber(freeze_y_s) &&
      atof(freeze_time_s.c_str()) > 0 && freeze_node != "none") {
    adoptPeerFreeze(atof(freeze_x_s.c_str()), atof(freeze_y_s.c_str()), atof(freeze_time_s.c_str()),
                    freeze_node);
  }

  const string sx = tokStringParse(value, "x", '#', '='), sy = tokStringParse(value, "y", '#', '='),
               position_time = tokStringParse(value, "position_time", '#', '=');
  if (!isNumber(sx) || !isNumber(sy) || !isNumber(position_time))
    return true;
  const double stamp = atof(position_time.c_str());
  if (stamp < m_peer_time || stamp > m_curr_time + 1 || m_curr_time - stamp > m_nav_stale_thresh)
    return true;
  const double x = atof(sx.c_str()), y = atof(sy.c_str());
  m_peer_speed_estimator.sample(stamp, x, y, m_nav_stale_thresh);
  m_peer_x = x;
  m_peer_y = y;
  m_peer_time = stamp;
  m_peer_set = true;
  return true;
}

bool Rendezvous::handleDouble(const CMOOSMsg &msg, double &value, double &stamp)
{
  if (!msg.IsDouble() || !isfinite(msg.GetDouble()))
    return false;
  value = msg.GetDouble();
  stamp = m_curr_time;
  return true;
}

bool Rendezvous::handleBool(const CMOOSMsg &msg, bool &value)
{
  value = isTrue(msg);
  return true;
}

void Rendezvous::updatePlanner()
{
  const double stamp = min(m_nav_x_time, m_nav_y_time);
  m_planner.sample(stamp, m_nav_x, m_nav_y, m_nav_stale_thresh);
  publishMotion();
  const RendezvousPlanner::Speed peer_speed = m_peer_speed_estimator.speed();
  Notify(peerVar("SPEED"), peer_speed.value);
  Notify(peerVar("SPEED_VALID"), peer_speed.valid ? 1.0 : 0.0);
  Notify(peerVar("SPEED_WINDOW"), peer_speed.window);
  const bool own_captured =
      m_waypoint_reached || hypot(m_nav_x - m_target_x, m_nav_y - m_target_y) <= m_arrival_radius;
  const bool was_frozen = m_planner.frozen();
  if (m_planner.update(m_curr_time, m_nav_x, m_nav_y, m_peer_x, m_peer_y, peer_speed,
                       m_point_freeze_radius, own_captured)) {
    m_target_x = m_planner.x();
    m_target_y = m_planner.y();
    publishPoint(true);
    publishRoute(false);
  }
  if (!was_frozen && m_planner.frozen()) {
    m_freeze_x = m_planner.x();
    m_freeze_y = m_planner.y();
    m_freeze_time = m_curr_time;
    m_freeze_node = tolower(m_ownship);
    publishMotion(true);
  }
  Notify(ownVar("POINT_FROZEN"), m_planner.frozen() ? 1.0 : 0.0);
}

void Rendezvous::adoptPeerFreeze(double x, double y, double freeze_time, const string &freeze_node)
{
  if (m_state == "IDLE" || m_state == "ABORT" || !isfinite(x) || !isfinite(y) ||
      !isfinite(freeze_time) || freeze_time <= 0 || freeze_node.empty() || freeze_node == "none")
    return;

  const string candidate_node = tolower(freeze_node);
  const string current_node = tolower(m_freeze_node);
  constexpr double timestamp_tolerance = 1e-6;
  const bool earlier = m_freeze_time <= 0 || freeze_time < m_freeze_time - timestamp_tolerance;
  const bool tie_wins = fabs(freeze_time - m_freeze_time) <= timestamp_tolerance &&
                        (current_node.empty() || candidate_node < current_node);
  if (!earlier && !tie_wins)
    return;

  m_freeze_x = x;
  m_freeze_y = y;
  m_freeze_time = freeze_time;
  m_freeze_node = candidate_node;
  if (m_state == "STARTING")
    return; // Apply it after the planner is initialized from fresh positions.

  const bool reopening = m_state == "COMPLETE";
  m_planner.adoptFrozenPoint(m_freeze_x, m_freeze_y);
  m_target_x = m_freeze_x;
  m_target_y = m_freeze_y;
  m_waypoint_reached = false;
  m_arrival_start = 0;
  if (reopening) {
    m_state = "RENDEZVOUS";
    m_reason = "earlier_peer_freeze_adopted";
    m_state_enter_time = m_curr_time;
  }
  publishState(true);
  publishPoint(true);
  publishRoute(true);
  publishMotion(true);
}

void Rendezvous::publishMotion(bool force)
{
  if (!force && !navFresh())
    return;
  if (!force && m_curr_time - m_last_motion_time < 1)
    return;
  const auto speed = m_planner.speed();
  Notify(ownVar("SPEED"), speed.value);
  Notify(ownVar("SPEED_VALID"), speed.valid ? 1.0 : 0.0);
  Notify(ownVar("SPEED_WINDOW"), speed.window);
  string motion = "session=" + m_session + "#node=" + m_ownship;
  motion += "#x=" + doubleToStringX(m_nav_x, 3) + "#y=" + doubleToStringX(m_nav_y, 3);
  motion += "#time=" + doubleToStringX(m_curr_time, 6);
  motion += "#position_time=" + doubleToStringX(min(m_nav_x_time, m_nav_y_time), 6);
  const double exchanged_x = m_freeze_time > 0 ? m_freeze_x : m_target_x;
  const double exchanged_y = m_freeze_time > 0 ? m_freeze_y : m_target_y;
  motion += "#point_x=" + doubleToStringX(exchanged_x, 6);
  motion += "#point_y=" + doubleToStringX(exchanged_y, 6);
  motion += "#freeze_time=" + doubleToStringX(m_freeze_time, 6);
  motion += "#freeze_node=" + (m_freeze_node.empty() ? string("none") : m_freeze_node);
  motion += "#phase=" + m_state;
  Notify(ownVar("MOTION"), motion);
  sendMessage(m_peer_node, "RENDEZVOUS_MOTION", motion);
  m_last_motion_time = m_curr_time;
}

void Rendezvous::publishRoute(bool initial)
{
  Notify(ownVar("UPDATE"),
         "points=" + doubleToStringX(m_target_x, 2) + "," + doubleToStringX(m_target_y, 2));
  if (initial)
    activateRoute();
}

void Rendezvous::activateRoute()
{
  // Helm conditions compare these variables to string literals (`=true` /
  // `=false`). Use string Notify overloads; bool converts to numeric 1/0 and
  // does not satisfy those string conditions.
  Notify(ownVar("WAYPOINT_REACHED"), "false");
  Notify(ownVar("WAYPOINT_READY"), "true");
  Notify(ownVar("ACTIVE"), "true");
  Notify("MOOS_MANUAL_OVERRIDE", "false");
  Notify(ownVar("HANDOFF_REQUEST"), "true");
}

void Rendezvous::complete(const string &reason)
{
  m_state = "COMPLETE";
  m_reason = reason;
  m_state_enter_time = m_curr_time;
  // Publish the terminal phase before disabling the waypoint so the hold
  // behavior becomes eligible in the same MOOS mail cycle.
  publishState(true);
  Notify(ownVar("ACTIVE"), "false");
  Notify(ownVar("WAYPOINT_READY"), "false");
  Notify(ownVar("HANDOFF_REQUEST"), "false");
  Notify("ARDU_COMMAND", "HOLD_POSITION");
  Notify("MOOS_MANUAL_OVERRIDE", "false");
  // Endpoint metadata remains available; publishState removes its visual.
  publishMotion(true);
}

void Rendezvous::abort(const string &reason)
{
  m_state = "ABORT";
  m_reason = reason;
  m_state_enter_time = m_curr_time;
  ++m_aborts;
  publishState(true);
  Notify(ownVar("ACTIVE"), "false");
  Notify(ownVar("WAYPOINT_READY"), "false");
  Notify(ownVar("HANDOFF_REQUEST"), "false");
  publishMotion(true);
}

void Rendezvous::publishState(bool force)
{
  if (!force && m_curr_time - m_last_state_time < m_state_post_interval)
    return;
  Notify(ownVar("STATE"), m_state);
  Notify(ownVar("PHASE"), m_state);
  Notify(ownVar("SESSION"), m_session);
  Notify(ownVar("ARRIVED"), m_state == "COMPLETE" ? 1.0 : 0.0);
  Notify(ownVar("POINT"), pointString());
  m_last_state_time = m_curr_time;
  // Repeat terminal removals so a dropped viewer/pShare notification cannot
  // leave a ghost circle. A new session or earlier peer freeze redraws it.
  if (m_state == "COMPLETE" || m_state == "ABORT")
    publishPoint(false);
}

void Rendezvous::publishPoint(bool active)
{
  string spec = "x=" + doubleToStringX(m_target_x, 2) + ",y=" + doubleToStringX(m_target_y, 2);
  spec += ",label=rendezvous_" + tolower(m_ownship) +
          ",label_color=" + (m_point_label ? m_point_color : "invisible");
  spec += ",active=" + (active ? string("true") : string("false"));
  if (m_point_shape == "point") {
    spec += ",vertex_color=" + m_point_color + ",vertex_size=10";
    Notify("VIEW_POINT", spec);
  } else {
    spec += ",radius=" + doubleToStringX(m_point_radius, 2) + ",edge_color=" + m_point_color +
            ",edge_size=2,vertex_size=0";
    if (m_point_shape == "disc")
      spec += ",fill_color=" + m_point_color +
              ",fill_transparency=" + doubleToStringX(m_point_transparency, 2);
    Notify("VIEW_CIRCLE", spec);
  }
  Notify("RENDEZVOUS_POINT", pointString());
  Notify(ownVar("POINT"), pointString());
}

bool Rendezvous::sendMessage(const string &destination, const string &variable, const string &value)
{
  NodeMessage message(m_ownship, destination, variable);
  message.setSourceApp(GetAppName());
  message.setStringVal(value);
  if (!message.valid())
    return false;
  Notify("NODE_MESSAGE_LOCAL", message.getSpec());
  return true;
}

bool Rendezvous::navFresh() const
{
  return m_nav_x_set && m_nav_y_set &&
         m_curr_time - min(m_nav_x_time, m_nav_y_time) <= m_nav_stale_thresh;
}
bool Rendezvous::peerFresh() const
{
  return m_peer_set && m_peer_time > 0 && m_curr_time - m_peer_time <= m_nav_stale_thresh;
}
bool Rendezvous::isTrue(const CMOOSMsg &msg) const
{
  if (msg.IsDouble())
    return isfinite(msg.GetDouble()) && msg.GetDouble() != 0;
  const string v = tolower(stripBlankEnds(msg.GetString()));
  return v == "true" || v == "1" || v == "on";
}
string Rendezvous::pointString() const
{
  return "{" + doubleToStringX(m_target_x, 2) + "," + doubleToStringX(m_target_y, 2) + "}";
}
string Rendezvous::ownVar(const string &suffix) const
{
  return "RENDEZVOUS_OWN_" + suffix;
}
string Rendezvous::peerVar(const string &suffix) const
{
  return "RENDEZVOUS_PEER_" + suffix;
}
string Rendezvous::makeSession() const
{
  const uint64_t now =
      chrono::duration_cast<chrono::microseconds>(chrono::system_clock::now().time_since_epoch())
          .count();
  const string own = tolower(m_ownship), peer = tolower(m_peer_node);
  return (own < peer ? own + "_" + peer : peer + "_" + own) + "_" + to_string(now);
}

bool Rendezvous::buildReport()
{
  constexpr int label_width = 22;
  const auto own_speed = m_planner.speed();
  const auto peer_speed = m_peer_speed_estimator.speed();

  m_msgs << std::right << std::setw(label_width) << "Ownship / peer:" << " " << m_ownship << " / "
         << m_peer_node << "\n";
  m_msgs << std::right << std::setw(label_width) << "State:" << " " << m_state << " (" << m_reason
         << ")\n";
  m_msgs << std::right << std::setw(label_width) << "Session:" << " " << m_session << "\n";
  m_msgs << std::right << std::setw(label_width) << "Ownship fresh:" << " "
         << (navFresh() ? "true" : "false") << "\n";
  m_msgs << std::right << std::setw(label_width) << "Peer fresh:" << " "
         << (peerFresh() ? "true" : "false") << "\n";
  m_msgs << std::right << std::setw(label_width) << "Target:" << " " << pointString() << "\n";
  m_msgs << std::right << std::setw(label_width) << "Target frozen:" << " "
         << (m_planner.frozen() ? "true" : "false") << "\n";
  m_msgs << std::right << std::setw(label_width) << "Speed own / peer:" << " " << std::fixed
         << std::setprecision(2) << own_speed.value << " / " << peer_speed.value << " m/s\n";
  m_msgs << std::right << std::setw(label_width) << "Speed valid:" << " "
         << (own_speed.valid ? "true" : "false") << " / " << (peer_speed.valid ? "true" : "false")
         << " (own / peer)\n";
  m_msgs << std::right << std::setw(label_width) << "Completed locally:" << " "
         << (m_state == "COMPLETE" ? "true" : "false") << "\n";
  m_msgs << std::right << std::setw(label_width) << "Landing owner:" << " "
         << "pLandingCoordinator\n";
  return true;
}
