#include "RallyPoint.h"
#include "MBUtils.h"
#include "NodeMessage.h"
#include <chrono>
#include <cmath>
#include <iomanip>

using namespace std;

namespace
{
bool number(const string &s, double &value)
{
  if (!isNumber(s))
    return false;
  value = atof(s.c_str());
  return isfinite(value);
}
bool point(const string &s, char separator, double &x, double &y)
{
  return number(tokStringParse(s, "x", separator, '='), x) &&
         number(tokStringParse(s, "y", separator, '='), y) && fabs(x) <= 100000 &&
         fabs(y) <= 100000;
}
bool altitude(const string &s, double &alt)
{
  // ArduPilot AP_Rally's persisted RallyLocation uses int16 metres.
  // Require whole metres rather than silently changing the committed altitude.
  return number(s, alt) && alt >= 1 && alt <= 1000 && floor(alt) == alt;
}
} // namespace

bool RallyPoint::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();
  m_started = MOOSTime();
  STRING_LIST lines;
  m_MissionReader.GetConfiguration(GetAppName(), lines);
  for (string line : lines) {
    const string original = line;
    const string key = tolower(stripBlankEnds(biteString(line, '=')));
    const string value = stripBlankEnds(line);
    bool handled = true;
    if (key == "role" && (value == "shoreside" || value == "vehicle"))
      m_role = value;
    else if (key == "destination_node" && !value.empty())
      m_destination = value;
    else if (key == "initial_point")
      m_have_point = point(value, ',', m_x, m_y);
    else if (key == "initial_altitude")
      m_have_altitude = altitude(value, m_altitude);
    else
      handled = false;
    if (!handled)
      reportUnhandledConfigWarning(original);
  }
  registerVariables();
  if (m_role == "shoreside")
    Notify("RALLY_ALTITUDE", m_altitude);
  publish();
  return true;
}

bool RallyPoint::OnConnectToServer()
{
  registerVariables();
  return true;
}
void RallyPoint::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  if (m_role == "shoreside") {
    Register("RALLY_POINT", 0);
    Register("RALLY_ALTITUDE", 0);
    Register("RALLY_TASK_COMMIT", 0);
  } else {
    Register("RALLY_TASK", 0);
    Register("UAV_RALLY_RESULT", 0);
    for (const auto &key : {"UAV_FSM_OPERATION_STATE", "UAV_FSM_TAKEOFF_OPERATIONS_STATE",
                            "UAV_IS_ARMED", "UAV_LANDED_STATE",
                            "UAV_LANDED_STATE_AVAILABLE", "UAV_LANDED_STATE_AGE"})
      Register(key, 0);
  }
}

bool RallyPoint::idleGrounded(bool checking) const
{
  const auto input = [this](const string &key, const string &expected, bool numeric) {
    const auto it = m_inputs.find(key);
    return it != m_inputs.end() && it->second.numeric == numeric &&
           it->second.time <= m_curr_time + 1 && m_curr_time - it->second.time <= 3 &&
           it->second.value == expected;
  };
  const auto age = m_inputs.find("UAV_LANDED_STATE_AGE");
  double seconds = -1;
  const bool state_ok = input("UAV_FSM_OPERATION_STATE", "IDLE", false) ||
      (checking && input("UAV_FSM_OPERATION_STATE", "TAKEOFF_OPERATIONS", false) &&
       input("UAV_FSM_TAKEOFF_OPERATIONS_STATE", "ESTABLISHING_TAKEOFF_READINESS", false));
  return state_ok && input("UAV_IS_ARMED", "0", true) &&
         input("UAV_LANDED_STATE_AVAILABLE", "1", true) &&
         input("UAV_LANDED_STATE", "ON_GROUND", false) && age != m_inputs.end() &&
         age->second.numeric && m_curr_time - age->second.time <= 3 &&
         age->second.time <= m_curr_time + 1 && number(age->second.value, seconds) &&
         seconds >= 0 &&
         seconds + fmax(0.0, m_curr_time - age->second.time) / fmax(1e-6, GetMOOSTimeWarp()) <= 2;
}

void RallyPoint::status(const string &state, const string &reason)
{
  m_state = state;
  m_reason = reason;
  reportEvent(state + ": " + reason);
  publish();
}

void RallyPoint::task()
{
  if (!m_have_point || !m_have_altitude) {
    status("REJECTED", "select_valid_point_and_whole_metre_altitude");
    return;
  }
  const auto stamp =
      chrono::duration_cast<chrono::microseconds>(chrono::system_clock::now().time_since_epoch())
          .count();
  m_id = "rally_" + to_string(stamp);
  const string spec = "id=" + m_id + "#x=" + doubleToStringX(m_x, 6) +
                      "#y=" + doubleToStringX(m_y, 6) +
                      "#altitude=" + doubleToStringX(m_altitude, 0);
  NodeMessage message(m_host_community, m_destination, "RALLY_TASK");
  message.setSourceApp(GetAppName());
  message.setStringVal(spec);
  if (!message.valid()) {
    status("REJECTED", "invalid_destination");
    return;
  }
  Notify("NODE_MESSAGE_LOCAL", message.getSpec());
  status("SENT", "task_snapshot_sent");
}

void RallyPoint::receive(const string &spec)
{
  const string id = tokStringParse(spec, "id", '#', '=');
  double x, y, alt;
  if (id.empty() || !point(spec, '#', x, y) ||
      !altitude(tokStringParse(spec, "altitude", '#', '='), alt)) {
    reportRunWarning("Invalid rally task transaction");
    return;
  }
  if (m_seen_ids.count(id))
    return; // Any delayed redelivery must never retask/reupload.
  if (!m_pending.empty() || !idleGrounded()) {
    reportRunWarning("Rally task rejected: require fresh IDLE, grounded and disarmed");
    return;
  }
  m_seen_ids.insert(id);
  m_id = id;
  m_pending = id;
  m_pending_since = m_curr_time;
  m_active_x = x;
  m_active_y = y;
  m_active_altitude = alt;
  Notify("RALLY_UPLOAD", spec);
  status("VERIFYING", "waiting_for_flight_controller_readback");
  // Admission starts the FSM; verification blocks its readiness checks, not
  // the IDLE -> TAKEOFF_OPERATIONS transition. Never retrigger on a late ACK.
  Notify("TASK_EXECUTE", "true");
}

bool RallyPoint::OnNewMail(MOOSMSG_LIST &mail)
{
  AppCastingMOOSApp::OnNewMail(mail);
  // Cache the full batch before processing requests; delivery order must not
  // decide whether fresh native telemetry permits an otherwise valid task.
  for (const auto &msg : mail)
    m_inputs[msg.GetKey()] = {msg.IsDouble() ? doubleToStringX(msg.GetDouble()) : msg.GetString(),
                              msg.GetTime(), msg.IsDouble()};
  for (const auto &msg : mail) {
    const string key = msg.GetKey();
    // Never turn a retained true trigger/old task into a new task on restart.
    if ((key == "RALLY_TASK_COMMIT" || key == "RALLY_TASK") &&
        (msg.GetTime() < m_started || msg.GetTime() > m_curr_time + 1 ||
         m_curr_time - msg.GetTime() > 3))
      continue;
    if (m_role == "shoreside" && key == "RALLY_POINT") {
      m_have_point = msg.IsString() && point(msg.GetString(), ',', m_x, m_y);
      status(m_have_point ? "STAGED" : "INVALID", "point_edited_for_next_task");
    } else if (m_role == "shoreside" && key == "RALLY_ALTITUDE") {
      const double value = msg.GetDouble();
      m_have_altitude =
          msg.IsDouble() && isfinite(value) && value >= 1 && value <= 1000 && floor(value) == value;
      if (m_have_altitude)
        m_altitude = value;
      status(m_have_altitude ? "STAGED" : "INVALID", "altitude_edited_for_next_task");
    } else if (m_role == "shoreside" && key == "RALLY_TASK_COMMIT" &&
               ((msg.IsDouble() && msg.GetDouble() == 1) || msg.GetString() == "true"))
      task();
    else if (m_role == "vehicle" && key == "RALLY_TASK" && msg.IsString())
      receive(msg.GetString());
    else if (m_role == "vehicle" && key == "UAV_RALLY_RESULT" && !m_pending.empty() &&
             tokStringParse(msg.GetString(), "id", '#', '=') == m_pending) {
      const string result = tokStringParse(msg.GetString(), "status", '#', '=');
      if (result == "READY") {
        if (m_curr_time - m_pending_since > 60 || msg.GetTime() > m_curr_time + 1 ||
            m_curr_time - msg.GetTime() > 3 || !idleGrounded(true)) {
          m_pending.clear();
          status("REJECTED", "vehicle_changed_during_upload");
          continue;
        }
        Notify("RALLY_WAYPOINT_UPDATE",
               "points=" + doubleToStringX(m_active_x, 6) + "," + doubleToStringX(m_active_y, 6));
        Notify("RALLY_ALTITUDE_UPDATE", "altitude=" + doubleToStringX(m_active_altitude, 0));
        Notify("RALLY_ACTIVE", "id=" + m_id + "#x=" + doubleToStringX(m_active_x, 6) +
                                   "#y=" + doubleToStringX(m_active_y, 6) +
                                   "#altitude=" + doubleToStringX(m_active_altitude, 0));
        m_pending.clear();
        status("READY", "verified_rally_committed_to_task");
      } else if (result == "FAILED" || result == "REJECTED") {
        m_pending.clear();
        status("FAILED", tokStringParse(msg.GetString(), "detail", '#', '='));
      }
    }
  }
  return true;
}

void RallyPoint::publish()
{
  const string prefix = m_role == "shoreside" ? "RALLY_STAGED" : "RALLY_TASK";
  Notify(prefix + "_STATE", m_state);
  Notify(prefix + "_REASON", m_reason);
  Notify(prefix + "_ID", m_id.empty() ? "none" : m_id);
  if (m_role == "shoreside") {
    // Different name from editable inputs avoids pShare's import/export trap.
    Notify("RALLY_STAGED_POINT",
           m_have_point ? "x=" + doubleToStringX(m_x, 6) + ",y=" + doubleToStringX(m_y, 6)
                        : "invalid");
    Notify(
        "VIEW_POINT",
        "x=" + doubleToStringX(m_x, 6) + ",y=" + doubleToStringX(m_y, 6) +
            ",label=staged_rally,vertex_color=orange,vertex_size=8,label_color=invisible,active=" +
            string(m_have_point ? "true" : "false"));
  }
}

bool RallyPoint::Iterate()
{
  AppCastingMOOSApp::Iterate();
  if (!m_pending.empty() && m_curr_time - m_pending_since > 60) {
    m_pending.clear();
    status("FAILED", "verification_timeout_readiness_blocked");
  }
  publish();
  AppCastingMOOSApp::PostReport();
  return true;
}

bool RallyPoint::buildReport()
{
  m_msgs << fixed << setprecision(2);
  m_msgs << "Role:            " << m_role << '\n'
         << "State:           " << m_state << '\n'
         << "Reason:          " << m_reason << '\n'
         << "Transaction:     " << (m_id.empty() ? "--" : m_id) << '\n';
  if (m_role == "shoreside")
    m_msgs << "Staged XY:       " << m_x << ", " << m_y << " m "
           << (m_have_point ? "known" : "INVALID") << '\n'
           << "Staged altitude: " << m_altitude << " m above Home "
           << (m_have_altitude ? "known" : "INVALID") << '\n';
  else
    m_msgs << "Committed XY:    " << m_active_x << ", " << m_active_y << " m\n"
           << "Rally altitude:  " << m_active_altitude << " m above Home\n"
           << "Task admission:  "
           << (idleGrounded() ? "fresh idle/grounded/disarmed"
                              : "BLOCKED: missing/stale/not idle-grounded")
           << '\n';
  return true;
}
