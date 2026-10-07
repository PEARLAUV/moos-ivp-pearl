#include "TakeoffCoordinator.h"

#include <cmath>
#include "ACTable.h"
#include "MBUtils.h"
#include "NodeMessage.h"

using namespace std;

TakeoffCoordinator::TakeoffCoordinator()
  : m_timeout(100), m_request_interval(5), m_completion_retry_time(60),
    m_sent_at(0), m_last_request_at(0), m_last_progress_at(-1),
    m_completed_at(0), m_sequence(0), m_request_transmissions(0),
    m_complete_transmissions(0),
    m_phase(IDLE), m_pending(false), m_grant_received(false),
    m_was_met(false), m_valid(true), m_invalidated_pending(false),
    m_armed_seen(false), m_armed(false) {}

bool TakeoffCoordinator::parseFlag(const string& spec, string& var, string& value)
{
  string rest = spec;
  string name = stripBlankEnds(biteStringX(rest, '='));
  const string data = stripBlankEnds(rest);
  if(!setNonWhiteVarOnString(name, name) || data.empty() ||
     data.front() == '=' || data.find('=') != string::npos)
    return false;
  var = name;
  value = data;
  return true;
}

bool TakeoffCoordinator::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();
  STRING_LIST params;
  m_MissionReader.EnableVerbatimQuoting(false);
  if(!m_MissionReader.GetConfiguration(GetAppName(), params))
    m_valid = false;
  for(const string& orig : params) {
    string line = orig;
    const string key = tolower(biteStringX(line, '='));
    const string value = stripBlankEnds(line);
    bool handled = true;
    if(key == "request_condition") {
      LogicCondition condition;
      condition.setAllowDoubleEquals(true);
      handled = condition.setCondition(value);
      if(handled) {
        for(const string& var : condition.getVarNames()) m_vars.insert(var);
        m_request_conditions.push_back(condition);
      }
    }
    else if(key == "checks_result_var") {
      string name;
      handled = setNonWhiteVarOnString(name, value) &&
                m_checks.emplace(name, CheckStatus()).second;
    }
    else if(key == "checks_session_var")
      handled = setNonWhiteVarOnString(m_checks_session_var, value);
    else if(key == "arming_flag")
      handled = parseFlag(value, m_arming_var, m_arming_value);
    else if(key == "takeoff_flag")
      handled = parseFlag(value, m_takeoff_var, m_takeoff_value);
    else if(key == "armed_var")
      handled = setNonWhiteVarOnString(m_armed_var, value);
    else if(key == "landed_var")
      handled = setNonWhiteVarOnString(m_landed_var, value);
    else if(key == "result_var")
      handled = setNonWhiteVarOnString(m_result_var, value);
    else if(key == "receipt_var")
      handled = setNonWhiteVarOnString(m_receipt_var, value);
    else if(key == "ownship")
      handled = setNonWhiteVarOnString(m_ownship, value);
    else if(key == "authority")
      handled = setNonWhiteVarOnString(m_authority, value);
    else if(key == "state_var")
      handled = setNonWhiteVarOnString(m_state_var, value);
    else if(key == "reason_var")
      handled = setNonWhiteVarOnString(m_reason_var, value);
    else if(key == "timeout")
      handled = setPosDoubleOnString(m_timeout, value) && std::isfinite(m_timeout);
    else if(key == "request_interval")
      handled = setPosDoubleOnString(m_request_interval, value) &&
                std::isfinite(m_request_interval);
    else if(key == "completion_retry_time")
      handled = setPosDoubleOnString(m_completion_retry_time, value) &&
                std::isfinite(m_completion_retry_time);
    else if(key != "apptick" && key != "commstick" &&
            key != "term_report_interval")
      handled = false;
    if(!handled) {
      reportUnhandledConfigWarning(orig);
      m_valid = false;
    }
  }
  if(m_ownship.empty()) m_ownship = m_host_community;
  if(m_state_var.empty()) m_state_var = m_result_var + "_STATE";
  if(m_request_conditions.empty() || m_vars.empty() || m_checks.empty() ||
     m_result_var.empty() || m_receipt_var.empty() ||
     m_armed_var.empty() || m_landed_var.empty() ||
     m_arming_var.empty() || m_takeoff_var.empty() ||
     m_authority.empty() || m_ownship.empty() || m_timeout <= 0 ||
     m_request_interval <= 0 || m_completion_retry_time <= 0 ||
     m_result_var == m_state_var ||
     m_result_var == m_reason_var ||
     (!m_reason_var.empty() && m_reason_var == m_state_var) ||
     m_vars.count(m_result_var) || m_vars.count(m_state_var) ||
     (!m_reason_var.empty() && m_vars.count(m_reason_var)) ||
     m_vars.count("TAKEOFF_CLEARANCE") ||
     m_receipt_var == "TAKEOFF_CLEARANCE" ||
     m_receipt_var == "TAKEOFF_REQUEST" ||
     m_receipt_var == m_result_var || m_receipt_var == m_state_var ||
     m_receipt_var == m_reason_var || m_vars.count(m_receipt_var) ||
     m_receipt_var == m_armed_var || m_receipt_var == m_landed_var ||
     m_receipt_var == m_arming_var || m_receipt_var == m_takeoff_var ||
     m_checks.count(m_receipt_var) ||
     (!m_checks_session_var.empty() &&
      (m_checks_session_var == m_receipt_var ||
       m_checks_session_var == m_result_var ||
       m_checks_session_var == m_state_var ||
       m_checks_session_var == m_reason_var ||
       m_checks.count(m_checks_session_var))) ||
     m_arming_var == m_takeoff_var ||
     m_arming_var == m_result_var || m_takeoff_var == m_result_var ||
     m_arming_var == m_state_var || m_takeoff_var == m_state_var) {
    reportConfigWarning("Require request_condition(s), checks_result_var(s), telemetry vars, "
                        "arming_flag, takeoff_flag, result_var and positive timing");
    m_valid = false;
  }
  for(const auto& entry : m_checks) {
    if(m_checks.count(entry.first + "_STATE") ||
       entry.first == m_result_var || entry.first == m_state_var ||
       entry.first == m_reason_var ||
       entry.first + "_STATE" == m_result_var ||
       entry.first + "_STATE" == m_state_var ||
       entry.first + "_STATE" == m_reason_var ||
       entry.first + "_STATE" == m_receipt_var) {
      reportConfigWarning("Check output collision: " + entry.first);
      m_valid = false;
    }
  }
  if(m_valid) {
    Notify(m_receipt_var, "none");
    Notify(m_result_var, "UNKNOWN");
    Notify(m_state_var, "status=UNKNOWN");
    if(!m_reason_var.empty()) Notify(m_reason_var, "NONE");
  }
  registerVariables();
  return true;
}

bool TakeoffCoordinator::OnConnectToServer()
{
  registerVariables();
  return true;
}

void TakeoffCoordinator::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  for(const string& var : m_vars) Register(var, 0);
  for(const auto& entry : m_checks) {
    Register(entry.first, 0);
    Register(entry.first + "_STATE", 0);
  }
  if(!m_checks_session_var.empty()) Register(m_checks_session_var, 0);
  if(!m_armed_var.empty()) Register(m_armed_var, 0);
  if(!m_landed_var.empty()) Register(m_landed_var, 0);
  Register("TAKEOFF_CLEARANCE", 0);
}

bool TakeoffCoordinator::requestConditionsMet() const
{
  if(!m_checks_session_var.empty() &&
     (m_checks_session.empty() || m_checks_session == "none")) return false;
  for(const string& var : m_vars)
    if(!m_seen.count(var)) return false;
  for(const LogicCondition& condition : m_request_conditions)
    if(!condition.eval()) return false;
  return true;
}

bool TakeoffCoordinator::checksMet() const
{
  const string& expected = m_checks_session_var.empty() ?
    m_request_id : m_request_checks_session;
  if(!m_checks_session_var.empty() && m_checks_session != expected)
    return false;
  for(const auto& entry : m_checks)
    if(entry.second.result != "success" ||
       entry.second.state_result != "success" ||
       entry.second.session != expected) return false;
  return !m_checks.empty();
}

bool TakeoffCoordinator::postFlag(const string& var, const string& value)
{
  return Notify(var, value);
}

bool TakeoffCoordinator::send(const string& status, const string& reason)
{
  NodeMessage message(m_ownship, m_authority, "TAKEOFF_REQUEST");
  message.setSourceApp(GetAppName());
  string spec = "id=" + m_request_id + "#status=" + status;
  if(!reason.empty()) spec += "#reason=" + reason;
  message.setStringVal(spec);
  if(!message.valid()) return false;
  if(!Notify("NODE_MESSAGE_LOCAL", message.getSpec())) return false;
  if(status == "requested") ++m_request_transmissions;
  if(status == "takeoff_complete") ++m_complete_transmissions;
  return true;
}

void TakeoffCoordinator::updateState(const string& status, const string& reason)
{
  m_status = status;
  Notify(m_result_var, status);
  Notify(m_state_var, "id=" + m_request_id + ",status=" + status +
                      ",reason=" + reason);
  if(!m_reason_var.empty()) Notify(m_reason_var, reason);
  recordEvent(status, reason);
}

void TakeoffCoordinator::recordEvent(const string& status, const string& reason)
{
  if(m_request_id.empty()) return;
  const string phase = phaseName();
  if(!m_events.empty()) {
    const TakeoffEvent& previous = m_events.back();
    if(previous.phase == phase && previous.status == status &&
       previous.reason == reason) return;
  }
  m_events.push_back(TakeoffEvent{MOOSTime() - m_sent_at, phase, status, reason});
}

void TakeoffCoordinator::finish(const string& status, const string& reason)
{
  m_pending = false;
  m_grant_received = false;
  m_invalidated_pending = false;
  m_phase = IDLE;
  Notify(m_receipt_var, "none");
  updateState(status, reason);
}

bool TakeoffCoordinator::OnNewMail(MOOSMSG_LIST& mail)
{
  AppCastingMOOSApp::OnNewMail(mail);
  // Apply telemetry/check updates before processing clearance replies.
  for(const CMOOSMsg& msg : mail) {
    const string key = msg.GetKey();
    if(!m_checks_session_var.empty() && key == m_checks_session_var &&
       msg.IsString()) {
      m_checks_session = msg.GetString();
      if(m_pending && m_checks_session != m_request_checks_session)
        m_invalidated_pending = true;
    }
    if(m_vars.count(key)) {
      if(msg.IsDouble() && std::isfinite(msg.GetDouble())) {
        for(LogicCondition& condition : m_request_conditions)
          condition.setVarVal(key, msg.GetDouble());
        m_seen.insert(key);
      }
      else if(msg.IsString()) {
        for(LogicCondition& condition : m_request_conditions)
          condition.setVarVal(key, msg.GetString());
        m_seen.insert(key);
      }
      if(m_pending && !requestConditionsMet()) m_invalidated_pending = true;
    }
    const auto check = m_checks.find(key);
    if(check != m_checks.end() && msg.IsString())
      check->second.result = msg.GetString();
    if(strEnds(key, "_STATE") && msg.IsString()) {
      const auto parent = m_checks.find(key.substr(0, key.size() - 6));
      if(parent != m_checks.end()) {
        parent->second.state_result =
          tokStringParse(msg.GetString(), "status", ',', '=');
        parent->second.session =
          tokStringParse(msg.GetString(), "session", ',', '=');
      }
    }
    if(key == m_armed_var) {
      if(msg.IsDouble() && std::isfinite(msg.GetDouble())) {
        m_armed_seen = true;
        m_armed = msg.GetDouble() == 1.0;
      }
      else if(msg.IsString()) {
        m_armed_seen = true;
        m_armed = msg.GetString() == "1" || tolower(msg.GetString()) == "true";
      }
    }
    if(key == m_landed_var && msg.IsString())
      m_landed_state = tolower(msg.GetString());
  }
  for(const CMOOSMsg& msg : mail) {
    if(msg.GetKey() != "TAKEOFF_CLEARANCE" || !msg.IsString() || !m_pending)
      continue;
    if(m_invalidated_pending || !requestConditionsMet()) {
      send("cancelled", "OWN_CONDITIONS_CHANGED");
      finish("CANCELLED", "OWN_CONDITIONS_CHANGED");
      continue;
    }
    if(MOOSTime() - m_sent_at >= m_timeout) {
      send("cancelled", "AUTHORITY_TIMEOUT");
      finish("TIMEOUT", "AUTHORITY_TIMEOUT");
      continue;
    }
    const string id = tokStringParse(msg.GetString(), "id", '#', '=');
    const string status = tolower(tokStringParse(msg.GetString(), "status", '#', '='));
    if(id != m_request_id) {
      reportEvent("Ignored stale takeoff response: " + id);
      continue;
    }
    if(status == "granted") {
      m_grant_received = true;
      Notify(m_state_var, "id=" + m_request_id +
                          ",status=PENDING,reason=AUTHORITY_GRANTED_CHECKING");
      if(!m_reason_var.empty()) Notify(m_reason_var, "AUTHORITY_GRANTED_CHECKING");
      recordEvent("PENDING", "AUTHORITY_GRANTED_CHECKING");
    }
    else if(status == "denied" || status == "rejected") {
      const string remote_reason =
        tokStringParse(msg.GetString(), "reason", '#', '=');
      finish("REJECTED", remote_reason.empty() ? "AUTHORITY_DENIED" :
             "AUTHORITY_" + toupper(remote_reason));
    }
    else if(status == "busy") finish("REJECTED", "AUTHORITY_BUSY");
    else reportRunWarning("Ignored malformed takeoff response");
  }
  return true;
}

bool TakeoffCoordinator::Iterate()
{
  AppCastingMOOSApp::Iterate();
  if(m_valid) {
    const bool met = requestConditionsMet();
    const double now = MOOSTime();
    if(m_pending) {
      if(m_invalidated_pending || !met) {
        send("cancelled", "OWN_CONDITIONS_CHANGED");
        finish("CANCELLED", "OWN_CONDITIONS_CHANGED");
      }
      else if(now - m_sent_at >= m_timeout) {
        send("cancelled", "AUTHORITY_TIMEOUT");
        finish("TIMEOUT", "AUTHORITY_TIMEOUT");
      }
      else if(m_grant_received && checksMet()) {
        if(postFlag(m_arming_var, m_arming_value)) {
          m_pending = false;
          m_phase = ARMING;
          m_last_progress_at = -1;
          updateState("APPROVED", "ARMING");
        }
        else reportRunWarning("Could not post arming flag; remaining pending");
      }
      else if(!m_grant_received && now - m_last_request_at >= m_request_interval) {
        if(send("requested")) m_last_request_at = now;
        else reportRunWarning("Could not resend takeoff request");
      }
    }
    else if(m_phase == ARMING) {
      if(m_landed_state == "in_air") {
        reportRunWarning("Airborne before arming feedback; skipping takeoff command");
        if(send("takeoff_complete")) {
          m_phase = COMPLETE;
          m_completed_at = m_last_progress_at = now;
          updateState("APPROVED", "TAKEOFF_COMPLETE");
        }
      }
      else if(m_armed_seen && m_armed) {
        if(m_last_progress_at < 0 || now - m_last_progress_at >= m_request_interval) {
          if(send("armed")) m_last_progress_at = now;
        }
        if(m_last_progress_at >= 0 && postFlag(m_takeoff_var, m_takeoff_value)) {
          m_phase = WAITING_TAKEOFF;
          updateState("APPROVED", "WAITING_TAKEOFF");
        }
      }
    }
    else if(m_phase == WAITING_TAKEOFF && m_armed_seen && !m_armed &&
            m_landed_state == "on_ground") {
      // Honor a pilot's disarm after arming feedback; never command takeoff again.
      send("cancelled");
      finish("CANCELLED", "DISARMED_BEFORE_TAKEOFF");
    }
    else if(m_phase == WAITING_TAKEOFF || m_phase == TAKING_OFF) {
      const string progress = m_landed_state == "in_air" ? "takeoff_complete" :
                              m_landed_state == "taking_off" ? "taking_off" :
                              m_phase == TAKING_OFF ? "taking_off" : "armed";
      if(progress == "takeoff_complete" ||
         (progress == "taking_off" && m_phase != TAKING_OFF) ||
         m_last_progress_at < 0 || now - m_last_progress_at >= m_request_interval) {
        if(send(progress)) {
          m_last_progress_at = now;
          if(progress == "takeoff_complete") {
            m_phase = COMPLETE;
            m_completed_at = now;
            updateState("APPROVED", "TAKEOFF_COMPLETE");
          }
          else if(progress == "taking_off") {
            m_phase = TAKING_OFF;
            updateState("APPROVED", "TAKING_OFF");
          }
        }
      }
    }
    else if(m_phase == COMPLETE && !(met && !m_was_met) &&
            now - m_completed_at <= m_completion_retry_time &&
            now - m_last_progress_at >= m_request_interval) {
      if(send("takeoff_complete")) m_last_progress_at = now;
      else reportRunWarning("Could not repeat takeoff completion");
    }
    else if(met && !m_was_met && (m_phase == IDLE || m_phase == COMPLETE)) {
      m_request_checks_session = m_checks_session;
      m_request_id = m_ownship + "_" + doubleToStringX(now, 6) +
                     "_" + uintToString(++m_sequence);
      m_events.clear();
      m_sent_at = now;
      m_request_transmissions = 0;
      m_complete_transmissions = 0;
      if(Notify(m_receipt_var, m_request_id) && send("requested")) {
        m_pending = true;
        m_grant_received = false;
        m_invalidated_pending = false;
        m_last_request_at = now;
        m_phase = WAITING_CLEARANCE;
        updateState("PENDING", "AWAITING_AUTHORITY");
      }
      else finish("REJECTED", "REQUEST_SEND_FAILED");
    }
    m_was_met = met;
  }
  AppCastingMOOSApp::PostReport();
  return true;
}

string TakeoffCoordinator::phaseName() const
{
  switch(m_phase) {
  case WAITING_CLEARANCE: return "WAITING_CLEARANCE";
  case ARMING: return "ARMING";
  case WAITING_TAKEOFF: return "WAITING_TAKEOFF";
  case TAKING_OFF: return "TAKING_OFF";
  case COMPLETE: return "COMPLETE";
  default: return "IDLE";
  }
}

bool TakeoffCoordinator::buildReport()
{
  m_msgs << "Authority: " << m_authority << "  Ownship: " << m_ownship << endl;
  m_msgs << "Request conditions: " << m_request_conditions.size() << "  Met: "
         << boolToString(requestConditionsMet()) << endl;
  for(const LogicCondition& condition : m_request_conditions) {
    string missing;
    for(const string& var : condition.getVarNames()) {
      if(m_seen.count(var)) continue;
      if(!missing.empty()) missing += ",";
      missing += var;
    }
    m_msgs << "  " << condition.getRawCondition() << " => "
           << boolToString(condition.eval());
    if(!missing.empty()) m_msgs << " (unseen: " << missing << ")";
    m_msgs << endl;
  }
  m_msgs << "Checks:" << endl;
  for(const auto& entry : m_checks)
    m_msgs << "  " << entry.first << "=" << entry.second.result
           << " diagnostic=" << entry.second.state_result
           << " session=" << entry.second.session << endl;
  m_msgs << "Pending: " << boolToString(m_pending)
         << "  Grant received: " << boolToString(m_grant_received)
         << "  ID: " << m_request_id << endl;
  m_msgs << "Request transmissions: " << m_request_transmissions
         << "  Completion transmissions: " << m_complete_transmissions << endl;
  m_msgs << "Phase: " << phaseName() << "  Armed: "
         << (m_armed_seen ? boolToString(m_armed) : "unseen")
         << "  Landed: " << m_landed_state << endl;
  m_msgs << "Status: " << m_status << "  Timeout: " << m_timeout
         << "  Interval: " << m_request_interval << endl;
  m_msgs << endl << "Takeoff events (" <<
    (m_request_id.empty() ? "none" : m_request_id) << "):" << endl;
  if(m_events.empty()) {
    m_msgs << "  No takeoff events yet" << endl;
  }
  else {
    ACTable events(4);
    events << "Elapsed (s)" << "Phase" << "Status" << "Reason";
    events.addHeaderLines();
    for(const TakeoffEvent& event : m_events)
      events << doubleToStringX(event.elapsed, 2) << event.phase
             << event.status << event.reason;
    m_msgs << events.getFormattedString() << endl;
  }
  return true;
}
