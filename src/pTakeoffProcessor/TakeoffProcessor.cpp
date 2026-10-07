#include "TakeoffProcessor.h"

#include <cmath>
#include <cstdlib>
#include "ACTable.h"
#include "MBUtils.h"
#include "NodeMessage.h"
#include "XYCircle.h"
#include "XYSegList.h"

using namespace std;

TakeoffProcessor::TakeoffProcessor()
  : m_timeout(100), m_received_at(0), m_released_at(0),
    m_progress_grace(120), m_view_radius(0), m_view_post_interval(1),
    m_view_last_post(-1), m_view_x(0), m_view_y(0),
    m_have_view_x(false), m_have_view_y(false), m_progress_stage(0),
    m_valid(true), m_checks_succeeded(false) {}

bool TakeoffProcessor::addReleaseCondition(const string& expression)
{
  string spec = expression;
  string var = stripBlankEnds(biteStringX(spec, '='));
  const string expected = stripBlankEnds(spec);
  if(!setNonWhiteVarOnString(var, var) || expected.empty() ||
     expected.front() == '=' || expected.find('=') != string::npos)
    return false;
  Condition condition;
  condition.expected = expected;
  return m_release_conditions.emplace(var, condition).second;
}

bool TakeoffProcessor::releaseMatched() const
{
  for(const auto& entry : m_release_conditions)
    if(!entry.second.matched) return false;
  return true;
}

void TakeoffProcessor::resetReleaseConditions()
{
  for(auto& entry : m_release_conditions)
    entry.second.matched = false;
}

bool TakeoffProcessor::matches(const Condition& condition, const CMOOSMsg& msg) const
{
  if(msg.IsString()) return msg.GetString() == condition.expected;
  if(msg.IsDouble() && std::isfinite(msg.GetDouble()) &&
     isNumber(condition.expected))
    return msg.GetDouble() == std::atof(condition.expected.c_str());
  return false;
}

bool TakeoffProcessor::OnStartUp()
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
    if(key == "receipt_var")
      handled = setNonWhiteVarOnString(m_receipt_var, value);
    else if(key == "status_var")
      handled = setNonWhiteVarOnString(m_status_var, value);
    else if(key == "progress_var")
      handled = setNonWhiteVarOnString(m_progress_var, value);
    else if(key == "view_label")
      handled = setNonWhiteVarOnString(m_view_label, value);
    else if(key == "view_x_var")
      handled = setNonWhiteVarOnString(m_view_x_var, value);
    else if(key == "view_y_var")
      handled = setNonWhiteVarOnString(m_view_y_var, value);
    else if(key == "view_radius")
      handled = setPosDoubleOnString(m_view_radius, value) &&
                std::isfinite(m_view_radius);
    else if(key == "view_post_interval")
      handled = setPosDoubleOnString(m_view_post_interval, value) &&
                std::isfinite(m_view_post_interval);
    else if(key == "checks_result_var") {
      string name;
      handled = setNonWhiteVarOnString(name, value) &&
                name != "TAKEOFF_REQUEST" && name != m_receipt_var &&
                m_checks.emplace(name, CheckStatus()).second;
    }
    else if(key == "release_condition")
      handled = addReleaseCondition(value);
    else if(key == "ownship")
      handled = setNonWhiteVarOnString(m_ownship, value);
    else if(key == "requester")
      handled = setNonWhiteVarOnString(m_requester, value);
    else if(key == "timeout")
      handled = setPosDoubleOnString(m_timeout, value) && std::isfinite(m_timeout);
    else if(key == "progress_grace")
      handled = setPosDoubleOnString(m_progress_grace, value) &&
                std::isfinite(m_progress_grace);
    else if(key != "apptick" && key != "commstick" &&
            key != "term_report_interval")
      handled = false;
    if(!handled) {
      reportUnhandledConfigWarning(orig);
      m_valid = false;
    }
  }
  if(m_ownship.empty()) m_ownship = m_host_community;
  if(m_receipt_var.empty() || m_status_var.empty() || m_checks.empty() ||
     m_requester.empty() || m_ownship.empty() || m_timeout <= 0 ||
     m_progress_grace <= 0 ||
     m_receipt_var == "TAKEOFF_REQUEST" ||
     m_receipt_var == "TAKEOFF_CLEARANCE" ||
     m_status_var == "TAKEOFF_REQUEST" ||
     m_status_var == "TAKEOFF_CLEARANCE" ||
     m_status_var == m_receipt_var ||
     (!m_progress_var.empty() &&
      (m_progress_var == m_receipt_var || m_progress_var == m_status_var ||
       m_progress_var == "TAKEOFF_REQUEST" ||
       m_progress_var == "TAKEOFF_CLEARANCE" ||
       m_checks.count(m_progress_var))) ||
     m_checks.count(m_receipt_var) || m_checks.count(m_receipt_var + "_STATE") ||
     m_checks.count(m_status_var)) {
    reportConfigWarning("Require distinct receipt_var, status_var, checks_result_var(s), requester, ownship, and positive timeout");
    m_valid = false;
  }
  for(const auto& entry : m_checks) {
    if(m_checks.count(entry.first + "_STATE") ||
       entry.first + "_STATE" == m_status_var ||
       entry.first + "_STATE" == m_progress_var) {
      reportConfigWarning("Check result collides with a diagnostic variable: " +
                          entry.first);
      m_valid = false;
    }
  }
  for(const auto& entry : m_release_conditions) {
    const string& var = entry.first;
    bool collision = var == "TAKEOFF_REQUEST" || var == m_receipt_var ||
                     var == m_status_var || var == m_progress_var ||
                     m_checks.count(var) != 0;
    for(const auto& check : m_checks)
      collision = collision || var == check.first + "_STATE";
    if(collision) {
      reportConfigWarning("Release condition cannot observe an owned input/output: " + var);
      m_valid = false;
    }
  }
  const bool any_view = !m_view_label.empty() || !m_view_x_var.empty() ||
                        !m_view_y_var.empty() || m_view_radius > 0;
  if(any_view && (m_view_label.empty() || m_view_x_var.empty() ||
                  m_view_y_var.empty() || m_view_radius <= 0 ||
                  m_view_x_var == m_view_y_var ||
                  m_view_x_var == m_receipt_var ||
                  m_view_y_var == m_receipt_var ||
                  m_view_x_var == m_status_var ||
                  m_view_y_var == m_status_var)) {
    reportConfigWarning("View needs distinct label, radius, x and y input variables");
    m_valid = false;
  }
  if(m_valid) {
    if(any_view) hideVisualization();
    Notify(m_receipt_var, "none");
    if(!m_progress_var.empty()) Notify(m_progress_var, "none");
    setStatus("INACTIVE", "");
  }
  registerVariables();
  return true;
}

bool TakeoffProcessor::OnConnectToServer()
{
  registerVariables();
  return true;
}

void TakeoffProcessor::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  Register("TAKEOFF_REQUEST", 0);
  for(const auto& entry : m_checks) {
    Register(entry.first, 0);
    Register(entry.first + "_STATE", 0);
  }
  for(const auto& entry : m_release_conditions) Register(entry.first, 0);
  if(!m_view_x_var.empty()) Register(m_view_x_var, 0);
  if(!m_view_y_var.empty()) Register(m_view_y_var, 0);
}

bool TakeoffProcessor::send(const string& id, const string& status)
{
  NodeMessage message(m_ownship, m_requester, "TAKEOFF_CLEARANCE");
  message.setSourceApp(GetAppName());
  message.setStringVal("id=" + id + "#status=" + status);
  if(!message.valid()) return false;
  return Notify("NODE_MESSAGE_LOCAL", message.getSpec());
}

void TakeoffProcessor::setStatus(const string& status, const string& reason)
{
  m_status = status;
  // Keep transport reasons in the AppCast; retain departure completion
  // across release so the PEARL FSM and observers can see it reliably.
  const string value = status == "COMPLETE" ?
                       (m_progress_stage == 3 ? "complete" : "inactive") :
                       status == "TIMEOUT" ? "denied" :
                       status == "CANCELLED" ? "canceled" : tolower(status);
  Notify(m_status_var, value);
  recordEvent(reason);
}

void TakeoffProcessor::recordEvent(const string& reason)
{
  if(m_event_id.empty()) return;
  if(!m_events.empty()) {
    const TakeoffEvent& previous = m_events.back();
    if(previous.status == m_status &&
       previous.progress == m_progress_status &&
       previous.reason == reason) return;
  }
  m_events.push_back(TakeoffEvent{MOOSTime() - m_received_at, m_status,
                                  m_progress_status, reason});
}

void TakeoffProcessor::finish(const string& status, const string& reason)
{
  // A mediated progress update can arrive after the direct FSM release fact.
  // Retain only the just-granted ID for a bounded late-completion window.
  if(status == "COMPLETE" && m_status == "GRANTED") {
    m_released_grant_id = m_active_id;
    m_released_at = MOOSTime();
  }
  else m_released_grant_id.clear();
  setStatus(status, reason);
  // Internal release stays COMPLETE; setStatus retains confirmed departure.
  m_active_id.clear();
  m_checks_succeeded = false;
  for(auto& entry : m_checks) entry.second.succeeded = false;
  resetReleaseConditions();
  Notify(m_receipt_var, "none");
}

void TakeoffProcessor::hideVisualization()
{
  if(m_view_label.empty()) return;
  XYCircle circle;
  circle.set(m_view_x, m_view_y, m_view_radius);
  circle.set_label(m_view_label);
  circle.set_active(false);
  Notify("VIEW_CIRCLE", circle.get_spec());
  for(unsigned int i=0; i<12; ++i)
    Notify("VIEW_SEGLIST", "label=" + m_view_label + "-dash-" +
                            uintToString(i) + ",active=false");
  m_view_mode = "none";
  m_view_last_post = -1;
}

void TakeoffProcessor::publishVisualization()
{
  if(m_view_label.empty()) return;
  const string desired = m_active_id.empty() ? "none" :
                         m_status == "GRANTED" ? "granted" : "pending";
  if(desired == "none" || !m_have_view_x || !m_have_view_y) {
    if(m_view_mode != "none") hideVisualization();
    return;
  }
  const double now = MOOSTime();
  if(desired == m_view_mode && m_view_last_post >= 0 &&
     now - m_view_last_post < m_view_post_interval) return;
  if(desired != m_view_mode) hideVisualization();

  if(desired == "granted") {
    XYCircle circle;
    circle.set(m_view_x, m_view_y, m_view_radius);
    circle.set_label(m_view_label);
    circle.set_label_color("off");
    circle.set_edge_color("red");
    circle.set_edge_size(2);
    Notify("VIEW_CIRCLE", circle.get_spec());
  }
  else {
    // VIEW_CIRCLE has no dashed-edge hint in pMarineViewer. Each of these
    // short arc polylines is independently retractable on grant or release.
    const double pi = std::acos(-1.0);
    for(unsigned int i=0; i<12; ++i) {
      XYSegList dash;
      for(unsigned int j=0; j<3; ++j) {
        const double angle = (i * 30.0 + j * 10.0) * pi / 180.0;
        dash.add_vertex(m_view_x + m_view_radius * std::cos(angle),
                        m_view_y + m_view_radius * std::sin(angle));
      }
      dash.set_label(m_view_label + "-dash-" + uintToString(i));
      dash.set_label_color("off");
      dash.set_vertex_color("off");
      dash.set_edge_color("red");
      dash.set_edge_size(2);
      Notify("VIEW_SEGLIST", dash.get_spec());
    }
  }
  m_view_mode = desired;
  m_view_last_post = now;
}

bool TakeoffProcessor::OnNewMail(MOOSMSG_LIST& mail)
{
  AppCastingMOOSApp::OnNewMail(mail);
  if(!m_valid) return true;
  for(const CMOOSMsg& msg : mail) {
    const string key = msg.GetKey();
    if(key == m_view_x_var || key == m_view_y_var) {
      const double value = msg.IsDouble() ? msg.GetDouble() :
                           msg.IsString() && isNumber(msg.GetString()) ?
                           std::atof(msg.GetString().c_str()) : NAN;
      if(std::isfinite(value)) {
        if(key == m_view_x_var) {
          m_view_x = value;
          m_have_view_x = true;
        }
        else {
          m_view_y = value;
          m_have_view_y = true;
        }
      }
    }
    if(!m_active_id.empty() && m_status == "GRANTED") {
      const auto release = m_release_conditions.find(key);
      if(release != m_release_conditions.end())
        release->second.matched = matches(release->second, msg);
    }
    if(!msg.IsString()) continue;
    const auto check = m_checks.find(key);
    if(check != m_checks.end()) {
      check->second.result = msg.GetString();
      continue;
    }
    if(strEnds(key, "_STATE")) {
      const auto parent = m_checks.find(key.substr(0, key.size() - 6));
      if(parent != m_checks.end()) {
        parent->second.state_result =
          tokStringParse(msg.GetString(), "status", ',', '=');
        parent->second.session =
          tokStringParse(msg.GetString(), "session", ',', '=');
        continue;
      }
    }
    if(key == "TAKEOFF_REQUEST") {
      const string spec = msg.GetString();
      const string id = tokStringParse(spec, "id", '#', '=');
      const string status = tolower(tokStringParse(spec, "status", '#', '='));
      if(id.empty() || id.find_first_of("#,= \t\r\n") != string::npos) {
        reportRunWarning("Ignored malformed request ID");
        continue;
      }
      if(status == "cancelled") {
        if(id == m_active_id) {
          m_cancel_reason = tokStringParse(spec, "reason", '#', '=');
          if(m_cancel_reason.empty()) m_cancel_reason = "unspecified";
          finish("CANCELLED", "REQUESTER_" + toupper(m_cancel_reason));
        }
        continue;
      }
      unsigned int stage = status == "armed" ? 1 :
                           status == "taking_off" ? 2 :
                           status == "takeoff_complete" ? 3 : 0;
      if(stage) {
        const bool active_grant = id == m_active_id && m_status == "GRANTED";
        const bool recently_released = m_active_id.empty() &&
          m_status == "COMPLETE" && id == m_released_grant_id &&
          MOOSTime() - m_released_at <= m_progress_grace;
        if((active_grant || recently_released) && stage > m_progress_stage) {
          m_progress_stage = stage;
          m_progress_status = status;
          if(!m_progress_var.empty())
            Notify(m_progress_var, "id=" + id + ",status=" + status);
          if(stage == 3) Notify(m_status_var, "complete");
          recordEvent(recently_released ? "REQUESTER_PROGRESS_AFTER_RELEASE" :
                      "REQUESTER_PROGRESS");
        }
        continue; // Ignore stale, duplicate, and out-of-order progress.
      }
      if(status != "requested") {
        reportRunWarning("Ignored malformed request status");
        continue;
      }
      if(id == m_active_id) continue; // Idempotent mediated retransmission.
      if(!m_active_id.empty()) {
        send(id, "busy");
        continue;
      }
      m_active_id = id;
      m_released_grant_id.clear();
      m_event_id = id;
      m_events.clear();
      m_cancel_reason.clear();
      m_progress_stage = 0;
      m_progress_status = "none";
      if(!m_progress_var.empty()) Notify(m_progress_var, "none");
      m_received_at = MOOSTime();
      setStatus("PENDING", "REQUEST_RECEIVED");
      m_checks_succeeded = false;
      for(auto& entry : m_checks) entry.second.succeeded = false;
      resetReleaseConditions();
      Notify(m_receipt_var, id);
    }
  }
  return true;
}

bool TakeoffProcessor::Iterate()
{
  AppCastingMOOSApp::Iterate();
  if(m_valid && !m_active_id.empty()) {
    if(m_status == "GRANTED") {
      if(releaseMatched())
        finish("COMPLETE", "RELEASE_CONDITIONS_MET");
    }
    else if(MOOSTime() - m_received_at >= m_timeout) {
      send(m_active_id, "denied");
      finish("TIMEOUT", "CHECKS_TIMEOUT");
    }
    else {
      bool all_success = true;
      bool any_failure = false;
      for(auto& entry : m_checks) {
        CheckStatus& check = entry.second;
        const bool current = check.session == m_active_id &&
                             check.state_result == check.result;
        if(current && check.result == "success") check.succeeded = true;
        if(!check.succeeded) all_success = false;
        if(current && (check.result == "fail" ||
                       (check.result == "expired" && !check.succeeded)))
          any_failure = true;
      }
      m_checks_succeeded = all_success;
      if(any_failure) {
        send(m_active_id, "denied");
        finish("DENIED", "CHECKS_FAILED");
      }
      else if(m_checks_succeeded) {
        if(send(m_active_id, "granted")) {
          setStatus("GRANTED", "CHECKS_SUCCEEDED");
          resetReleaseConditions();
          if(m_release_conditions.empty())
            finish("COMPLETE", "NO_RELEASE_CONDITIONS");
        }
        else
          reportRunWarning("Could not send takeoff clearance; remaining pending");
      }
    }
  }
  if(m_valid) publishVisualization();
  AppCastingMOOSApp::PostReport();
  return true;
}

bool TakeoffProcessor::buildReport()
{
  m_msgs << "Ownship: " << m_ownship << "  Requester: " << m_requester << endl;
  m_msgs << "Receipt: " << m_receipt_var << "  Local status: "
         << m_status_var << "  Check results:" << endl;
  for(const auto& entry : m_checks)
    m_msgs << "  " << entry.first << "=" << entry.second.result
           << " session=" << entry.second.session
           << " succeeded=" << boolToString(entry.second.succeeded) << endl;
  m_msgs << "Active ID: " << m_active_id << "  Status: " << m_status;
  if(m_status == "CANCELLED") m_msgs << "  Cancellation reason: " << m_cancel_reason;
  m_msgs << "  Progress: " << m_progress_status
         << "  All checks succeeded: " << boolToString(m_checks_succeeded) << endl;
  m_msgs << "Release conditions:" << endl;
  for(const auto& entry : m_release_conditions)
    m_msgs << "  " << entry.first << "=" << entry.second.expected
           << " matched=" << boolToString(entry.second.matched) << endl;
  m_msgs << "Timeout: " << m_timeout << "  Progress grace: "
         << m_progress_grace << "  Recent grant: " << m_released_grant_id << endl;
  if(!m_view_label.empty())
    m_msgs << "Takeoff view: " << m_view_mode << " at "
           << (m_have_view_x ? doubleToStringX(m_view_x, 2) : "unseen") << ","
           << (m_have_view_y ? doubleToStringX(m_view_y, 2) : "unseen")
           << " radius=" << m_view_radius << endl;
  m_msgs << endl << "Takeoff events ("
         << (m_event_id.empty() ? "none" : m_event_id) << "):" << endl;
  if(m_events.empty()) {
    m_msgs << "  No takeoff events yet" << endl;
  }
  else {
    ACTable events(4);
    events << "Elapsed (s)" << "Status" << "Progress" << "Reason";
    events.addHeaderLines();
    for(const TakeoffEvent& event : m_events)
      events << doubleToStringX(event.elapsed, 2) << event.status
             << event.progress << event.reason;
    m_msgs << events.getFormattedString() << endl;
  }
  return true;
}
