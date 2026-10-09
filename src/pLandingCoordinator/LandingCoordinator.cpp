#include "LandingCoordinator.h"
#include "MBUtils.h"
#include "NodeMessage.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace std;

LandingCoordinator::~LandingCoordinator()
{
  m_Comms.Close();
}

bool LandingCoordinator::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();
  STRING_LIST params;
  m_MissionReader.EnableVerbatimQuoting(false);
  m_MissionReader.GetConfiguration(GetAppName(), params);
  for (auto raw : params) {
    string line = raw, param = tolower(biteStringX(line, '=')), data = stripBlankEnds(line);
    bool ok = true;
    if (param == "ownship")
      ok = setNonWhiteVarOnString(m_ownship, data);
    else if (param == "peer_node")
      ok = setNonWhiteVarOnString(m_peer, data);
    else if (param == "role")
      m_role = tolower(data);
    else if (param == "landing_timeout")
      ok = setPosDoubleOnString(m_timeout, data);
    else if (param == "input_timeout")
      ok = setPosDoubleOnString(m_input_timeout, data);
    else if (param != "apptick" && param != "commstick")
      ok = false;
    if (!ok) {
      reportUnhandledConfigWarning(raw);
      m_valid = false;
    }
  }
  if (m_ownship.empty())
    m_ownship = m_host_community;
  if (m_peer.empty() || (m_role != "platform" && m_role != "aircraft")) {
    reportConfigWarning("Require peer_node and role=platform|aircraft");
    m_valid = false;
  }
  registerVariables();
  Notify("LANDING_DECK_ENABLED", "false");
  publish();
  return true;
}

bool LandingCoordinator::OnConnectToServer()
{
  registerVariables();
  return true;
}

void LandingCoordinator::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  for (const string key :
       {"RECOVER", "LANDING_CANCEL", "LANDING_EXCHANGE", "RENDEZVOUS_OWN_PHASE",
        "RENDEZVOUS_OWN_SESSION", "NAV_X", "NAV_Y", "UAV_LANDING_TARGET_AVAILABLE",
        "UAV_LANDING_TARGET_POSITION_VALID", "UAV_LANDING_TARGET_FRAME", "UAV_LANDING_TARGET_AGE",
        "UAV_HEALTH_ALL_OK", "UAV_HEALTH_AGE", "UAV_IS_ARMED", "UAV_LANDED_STATE",
        "UAV_LANDED_STATE_AVAILABLE", "UAV_LANDED_STATE_AGE", "UAV_COMMAND_RESULT"})
    Register(key, 0);
}

bool LandingCoordinator::OnNewMail(MOOSMSG_LIST &mail)
{
  AppCastingMOOSApp::OnNewMail(mail);
  for (const auto &msg : mail) {
    const string key = msg.GetKey();
    if (key == "RECOVER") {
      // RECOVER is sent only to the aircraft; PEARL receives a mediated request.
      if (m_valid && m_role == "aircraft" && isTrue(msg) &&
          (m_state == "IDLE" || m_state == "COMPLETE" || m_state == "ABORTED"))
        begin();
    } else if (key == "LANDING_CANCEL") {
      if (isTrue(msg))
        abort("operator_cancel");
    } else if (key == "LANDING_EXCHANGE" && msg.IsString())
      handleExchange(msg.GetString());
    else if (key != "APPCAST_REQ") {
      m_inputs[key].value = msg.IsDouble() ? doubleToStringX(msg.GetDouble(), 6) : msg.GetString();
      m_inputs[key].time = msg.GetTime();
      if (key == "RENDEZVOUS_OWN_SESSION" && m_rv_started && value(key) != m_old_rv_session &&
          msg.GetTime() >= m_state_time)
        m_rv_session = value(key);
      if (key == "RENDEZVOUS_OWN_PHASE" && m_rv_started && !m_rv_session.empty())
        m_rv_phase = value(key);
      if (key == "UAV_COMMAND_RESULT" && m_state == "COMMANDING" &&
          msg.GetTime() >= m_command_time) {
        const string command = tokStringParse(msg.GetString(), "command");
        const string status = tokStringParse(msg.GetString(), "status");
        // AUTOLAND is the request; pArduBridge reports it as command=LAND.
        if (command == "LAND") {
          if (status == "ACCEPTED") {
            m_command_accepted = true;
            state("LANDING", "precision_land_accepted");
          } else if (status == "FAILED" || status == "REJECTED")
            abort("precision_land_rejected");
        }
      }
    }
  }
  return true;
}

void LandingCoordinator::begin()
{
  const auto micros =
      chrono::duration_cast<chrono::microseconds>(chrono::system_clock::now().time_since_epoch())
          .count();
  m_session = "landing_" + tolower(m_ownship) + "_" + tolower(m_peer) + "_" + to_string(micros);
  m_session_time = m_curr_time;
  m_peer_phase.clear();
  m_peer_rv_phase.clear();
  m_peer_time = -1;
  m_rv_started = false;
  m_rv_phase = "IDLE";
  m_rv_session.clear();
  m_command_accepted = false;
  Notify("LANDING_DECK_ENABLED", "false");
  state("REQUESTED", "recover_received");
  send();
}

void LandingCoordinator::startRendezvous()
{
  if (m_rv_started)
    return;
  m_old_rv_session = value("RENDEZVOUS_OWN_SESSION");
  m_rv_session.clear();
  m_rv_phase = "STARTING";
  m_rv_started = true;
  state("RENDEZVOUS", "landing_agreed");
  Notify("RENDEZVOUS_START", "true");
}

void LandingCoordinator::handleExchange(const string &data)
{
  if (!m_valid || tolower(tokStringParse(data, "node", '#', '=')) != tolower(m_peer))
    return;
  const string previous_state = m_state;
  const string id = tokStringParse(data, "session", '#', '=');
  const string phase = tokStringParse(data, "phase", '#', '=');
  const string stamp = tokStringParse(data, "time", '#', '=');
  const string start = tokStringParse(data, "started", '#', '=');
  if (id.empty() || !isNumber(stamp) || !isNumber(start))
    return;
  const double time = atof(stamp.c_str()), started = atof(start.c_str());
  if (!isfinite(time) || !isfinite(started) || time > m_curr_time + 1 ||
      m_curr_time - time > m_input_timeout || started > time)
    return;
  if (id != m_session) {
    if (m_role != "platform" || phase != "REQUESTED" || started <= m_session_time ||
        (m_state != "IDLE" && m_state != "COMPLETE" && m_state != "ABORTED"))
      return;
    m_session = id;
    m_session_time = started;
    m_peer_time = -1;
    m_rv_started = false;
    m_rv_phase = "IDLE";
    m_rv_session.clear();
    m_command_accepted = false;
    Notify("LANDING_DECK_ENABLED", "false");
    state("AGREED", "aircraft_recovery_request_accepted");
  }
  if (time < m_peer_time)
    return;
  m_peer_time = time;
  m_peer_phase = phase;
  m_peer_rv_phase = tokStringParse(data, "rv_phase", '#', '=');
  if (phase == "ABORTED") {
    abort("peer_aborted");
    return;
  }
  if (m_role == "aircraft" && m_state == "REQUESTED" && phase == "AGREED")
    startRendezvous();
  if (m_role == "platform" && m_state == "AGREED" && phase == "RENDEZVOUS")
    startRendezvous();
  if (m_role == "platform" && (phase == "LANDING" || phase == "COMPLETE"))
    m_command_accepted = true;
  if (m_state != previous_state)
    send();
}

bool LandingCoordinator::fresh(const string &key) const
{
  const auto it = m_inputs.find(key);
  return it != m_inputs.end() && it->second.time <= m_curr_time + 1 &&
         m_curr_time - it->second.time <= m_input_timeout;
}
string LandingCoordinator::value(const string &key) const
{
  const auto it = m_inputs.find(key);
  return it == m_inputs.end() ? "" : it->second.value;
}
bool LandingCoordinator::flag(const string &key) const
{
  const string data = tolower(value(key));
  return fresh(key) && (data == "true" || (isNumber(data) && atof(data.c_str()) == 1));
}

bool LandingCoordinator::checksReady() const
{
  // Recovery policy belongs in MOOS, not in the generic AUTOLAND bridge.
  // PLND configuration is supplied by the mission's SITL parameter file.
  // Use actual MAVLink observations relayed by pArduBridge, never emulator-active.
  const auto age_ok = [this](const string &key, double limit) {
    const string data = value(key);
    if (!fresh(key) || !isNumber(data))
      return false;
    const double age = atof(data.c_str());
    return isfinite(age) && age >= 0 && age <= limit;
  };
  const string frame = value("UAV_LANDING_TARGET_FRAME");
  return flag("UAV_LANDING_TARGET_AVAILABLE") && flag("UAV_LANDING_TARGET_POSITION_VALID") &&
         fresh("UAV_LANDING_TARGET_FRAME") && isNumber(frame) && atof(frame.c_str()) == 12 &&
         age_ok("UAV_LANDING_TARGET_AGE", 0.5) && flag("UAV_HEALTH_ALL_OK") &&
         age_ok("UAV_HEALTH_AGE", m_input_timeout) && flag("UAV_IS_ARMED") &&
         flag("UAV_LANDED_STATE_AVAILABLE") && fresh("UAV_LANDED_STATE") &&
         value("UAV_LANDED_STATE") == "IN_AIR" && age_ok("UAV_LANDED_STATE_AGE", 2.0);
}

bool LandingCoordinator::Iterate()
{
  AppCastingMOOSApp::Iterate();
  if (m_valid && !m_session.empty() && m_state != "IDLE" && m_state != "ABORTED" &&
      m_state != "COMPLETE") {
    const bool peer_fresh = m_peer_time >= 0 && m_curr_time - m_peer_time <= m_input_timeout;
    if (m_curr_time - m_session_time > m_timeout)
      abort("landing_timeout");
    else if (m_rv_phase == "ABORT")
      abort("rendezvous_aborted");
    else if (m_role == "platform" && m_state == "RENDEZVOUS" && peer_fresh &&
             m_rv_phase == "COMPLETE" && m_peer_rv_phase == "COMPLETE" && fresh("NAV_X") &&
             fresh("NAV_Y")) {
      Notify("LANDING_DECK_ENABLED", "true");
      state("CLEARED", "both_rendezvous_complete");
    } else if (m_role == "aircraft" && m_state == "RENDEZVOUS" && peer_fresh &&
               m_peer_phase == "CLEARED" && m_peer_rv_phase == "COMPLETE" &&
               m_rv_phase == "COMPLETE" && checksReady()) {
      state("COMMANDING", "clearance_and_checks_ready");
      m_command_time = m_curr_time;
      Notify("ARDU_COMMAND", "AUTOLAND");
    } else if (m_role == "aircraft" && m_state == "COMMANDING" && m_curr_time - m_command_time > 10)
      abort("landing_command_timeout");
    else if (m_role == "aircraft" && m_state == "LANDING" && m_command_accepted &&
             flag("UAV_LANDED_STATE_AVAILABLE") && fresh("UAV_LANDED_STATE") &&
             value("UAV_LANDED_STATE") == "ON_GROUND" && fresh("UAV_IS_ARMED") &&
             !flag("UAV_IS_ARMED"))
      state("COMPLETE", "landed_and_disarmed");
    else if (m_role == "platform" && (m_state == "CLEARED" || m_state == "LANDING") && peer_fresh) {
      if (m_peer_phase == "LANDING")
        state("LANDING", "aircraft_landing");
      else if (m_peer_phase == "COMPLETE")
        state("COMPLETE", "aircraft_recovered");
    }
  }
  if (!m_session.empty() && (m_last_send < 0 || m_curr_time - m_last_send >= 1))
    send();
  publish();
  AppCastingMOOSApp::PostReport();
  return true;
}

void LandingCoordinator::send()
{
  if (m_session.empty())
    return;
  NodeMessage message(m_ownship, m_peer, "LANDING_EXCHANGE");
  message.setSourceApp(GetAppName());
  message.setStringVal("session=" + m_session + "#node=" + m_ownship + "#phase=" + m_state +
                       "#started=" + doubleToStringX(m_session_time, 6) +
                       "#time=" + doubleToStringX(m_curr_time, 6) + "#rv_phase=" + m_rv_phase);
  if (message.valid())
    Notify("NODE_MESSAGE_LOCAL", message.getSpec());
  m_last_send = m_curr_time;
}

void LandingCoordinator::state(const string &phase, const string &reason)
{
  if (m_state == phase && m_reason == reason)
    return;
  m_state = phase;
  m_reason = reason;
  m_state_time = m_curr_time;
  reportEvent(phase + ": " + reason);
  publish();
}

void LandingCoordinator::abort(const string &reason)
{
  if (m_state == "IDLE" || m_state == "ABORTED" || m_state == "COMPLETE")
    return;
  // Never compete with an accepted ArduPilot landing or force-disarm it.
  if (m_rv_started && m_rv_phase != "COMPLETE" && !m_command_accepted)
    Notify("RENDEZVOUS_ABORT", "true");
  if (!m_command_accepted)
    Notify("LANDING_DECK_ENABLED", "false");
  state("ABORTED", reason);
  send();
}

void LandingCoordinator::publish()
{
  Notify("LANDING_OWN_PHASE", m_state);
  Notify("LANDING_OWN_REASON", m_reason);
  Notify("LANDING_OWN_SESSION", m_session.empty() ? "none" : m_session);
  Notify("LANDING_OWN_CLEARANCE", m_state == "CLEARED" || m_state == "LANDING" ? "true" : "false");
  Notify("LANDING_OWN_CHECKS_READY", checksReady() ? "true" : "false");
}

bool LandingCoordinator::isTrue(const CMOOSMsg &msg) const
{
  if (msg.IsDouble())
    return msg.GetDouble() == 1;
  const string data = tolower(stripBlankEnds(msg.GetString()));
  return data == "true" || data == "1" || data == "on";
}

bool LandingCoordinator::buildReport()
{
  const auto label = [this](const string &name) { m_msgs << right << setw(22) << name << ": "; };
  const auto show = [](const string &data) { return data.empty() ? "--" : data; };
  const auto input_flag = [this](const string &key) {
    return value(key).empty() ? "missing" : !fresh(key) ? "stale" : flag(key) ? "yes" : "no";
  };
  const auto input_age = [this](const string &key, double limit) {
    const string data = value(key);
    if (data.empty())
      return string("missing");
    if (!fresh(key))
      return string("stale input");
    if (!isNumber(data) || !isfinite(atof(data.c_str())) || atof(data.c_str()) < 0)
      return string("invalid");
    const double age = atof(data.c_str());
    ostringstream text;
    text << fixed << setprecision(2) << age << " s (" << (age <= limit ? "OK" : "too old")
         << "; max " << limit << " s)";
    return text.str();
  };
  const bool peer_fresh = m_peer_time >= 0 && m_curr_time - m_peer_time <= m_input_timeout;
  const bool clearance =
      m_role == "platform" ? (m_state == "CLEARED" || m_state == "LANDING")
                           : peer_fresh && (m_peer_phase == "CLEARED" || m_peer_phase == "LANDING");
  string reason = m_reason;
  replace(reason.begin(), reason.end(), '_', ' ');
  m_msgs << fixed << setprecision(2);

  m_msgs << "Landing session\n----------------------\n";
  label("Ownship / peer");
  m_msgs << show(m_ownship) << " / " << show(m_peer) << '\n';
  label("Role");
  m_msgs << show(m_role) << '\n';
  label("Configuration");
  m_msgs << (m_valid ? "valid" : "invalid") << '\n';
  label("Phase");
  m_msgs << m_state << '\n';
  label("Reason");
  m_msgs << show(reason) << '\n';
  label("Phase age");
  if (m_state_time > 0)
    m_msgs << max(0.0, m_curr_time - m_state_time) << " s\n";
  else
    m_msgs << "--\n";
  label("Session");
  m_msgs << (m_session.empty() ? "none" : m_session) << '\n';
  label("Session age");
  if (!m_session.empty())
    m_msgs << max(0.0, m_curr_time - m_session_time) << " s\n";
  else
    m_msgs << "--\n";
  label("Landing timeout");
  m_msgs << m_timeout << " s\n";

  m_msgs << "\nCoordination\n----------------------\n";
  label("Peer phase");
  m_msgs << show(m_peer_phase) << '\n';
  label("Peer updates");
  m_msgs << (m_peer_time < 0 ? "not received" : peer_fresh ? "fresh" : "stale") << '\n';
  label("Peer update age");
  if (m_peer_time >= 0)
    m_msgs << max(0.0, m_curr_time - m_peer_time) << " s\n";
  else
    m_msgs << "--\n";
  label("Input timeout");
  m_msgs << m_input_timeout << " s\n";
  label("Rendezvous started");
  m_msgs << (m_rv_started ? "yes" : "no") << '\n';
  label("Own rendezvous");
  m_msgs << m_rv_phase << '\n';
  label("Peer rendezvous");
  m_msgs << show(m_peer_rv_phase) << '\n';
  label("Rendezvous session");
  m_msgs << (m_rv_session.empty() ? "none" : m_rv_session) << '\n';
  label("Landing clearance");
  m_msgs << (clearance ? "granted" : "not granted") << '\n';
  label("Command accepted");
  m_msgs << (m_command_accepted ? "yes" : "no") << '\n';

  if (m_role == "aircraft") {
    m_msgs << "\nAircraft readiness (before LAND)\n----------------------\n";
    label("Overall checks");
    m_msgs << (m_command_accepted ? "not required (LAND already accepted)"
               : checksReady()    ? "ready"
                                  : "blocked")
           << '\n';
    label("Target available");
    m_msgs << input_flag("UAV_LANDING_TARGET_AVAILABLE") << '\n';
    label("Target position valid");
    m_msgs << input_flag("UAV_LANDING_TARGET_POSITION_VALID") << '\n';
    label("Target frame");
    const string frame = value("UAV_LANDING_TARGET_FRAME");
    if (frame.empty())
      m_msgs << "missing\n";
    else if (!fresh("UAV_LANDING_TARGET_FRAME"))
      m_msgs << "stale\n";
    else if (isNumber(frame) && atof(frame.c_str()) == 12)
      m_msgs << "BODY_FRD (12)\n";
    else {
      if (isNumber(frame))
        m_msgs << atof(frame.c_str());
      else
        m_msgs << frame;
      m_msgs << " (requires BODY_FRD / 12)\n";
    }
    label("Target age");
    m_msgs << input_age("UAV_LANDING_TARGET_AGE", 0.5) << '\n';
    label("Health OK");
    m_msgs << input_flag("UAV_HEALTH_ALL_OK") << '\n';
    label("Health age");
    m_msgs << input_age("UAV_HEALTH_AGE", m_input_timeout) << '\n';
    label("Armed");
    m_msgs << input_flag("UAV_IS_ARMED") << '\n';
    label("Landed state available");
    m_msgs << input_flag("UAV_LANDED_STATE_AVAILABLE") << '\n';
    label("Landed state");
    m_msgs << (value("UAV_LANDED_STATE").empty() ? "missing"
               : fresh("UAV_LANDED_STATE")       ? value("UAV_LANDED_STATE")
                                                 : "stale")
           << '\n';
    label("Landed state age");
    m_msgs << input_age("UAV_LANDED_STATE_AGE", 2.0) << '\n';
  } else if (m_role == "platform") {
    m_msgs << "\nPlatform readiness\n----------------------\n";
    label("Platform navigation");
    m_msgs << (fresh("NAV_X") && fresh("NAV_Y") ? "fresh" : "missing or stale") << '\n';
    label("Aircraft checks");
    m_msgs << "evaluated by aircraft coordinator\n";
  }
  return true;
}
