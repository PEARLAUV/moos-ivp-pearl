#include "LandingCoordinator.h"
#include "BodyFrame.h"
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
    else if (param == "align_yaw")
      ok = setBooleanOnString(m_align_yaw, data);
    else if (param == "yaw_tolerance")
      ok = setPosDoubleOnString(m_yaw_tolerance, data) && isfinite(m_yaw_tolerance) && m_yaw_tolerance <= 45;
    else if (param == "yaw_rate")
      ok = setPosDoubleOnString(m_yaw_rate, data) && isfinite(m_yaw_rate) && m_yaw_rate <= 90;
    else if (param == "yaw_timeout")
      ok = setPosDoubleOnString(m_yaw_timeout, data) && isfinite(m_yaw_timeout);
    else if (param == "yaw_stability_time")
      ok = setPosDoubleOnString(m_yaw_stability_time, data) && isfinite(m_yaw_stability_time);
    else if (param == "yaw_hold_time")
      ok = setPosDoubleOnString(m_yaw_hold_time, data) && isfinite(m_yaw_hold_time);
    else if (param == "yaw_stability_tolerance")
      ok = setPosDoubleOnString(m_yaw_stability_tolerance, data) &&
           isfinite(m_yaw_stability_tolerance) && m_yaw_stability_tolerance <= 45;
    else if (param == "yaw_max_tilt")
      ok = setPosDoubleOnString(m_yaw_max_tilt, data) && isfinite(m_yaw_max_tilt) &&
           m_yaw_max_tilt <= 45;
    else if (param == "agreement_condition" || param == "rendezvous_condition" ||
             param == "clearance_condition" || param == "command_condition") {
      const string stage = param.substr(0, param.find('_'));
      LogicCondition condition;
      ok = condition.setCondition(data);
      if (ok) {
        m_conditions[stage] = condition;
        m_condition_specs[stage] = data;
      }
    } else if (param != "apptick" && param != "commstick")
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
  if (m_align_yaw && m_role != "aircraft") {
    reportConfigWarning("align_yaw is aircraft-only");
    m_valid = false;
  }
  m_yaw_state = m_align_yaw ? "WAITING" : "DISABLED";
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
  if (m_align_yaw)
    for (const auto &key : {"UAV_ROLL", "UAV_PITCH", "UAV_ATTITUDE_AVAILABLE",
                            "UAV_ATTITUDE_AGE", "UAV_GUIDED_MODE_ACTIVE",
                            "UAV_LANDING_TARGET_TIME_USEC", "UAV_LANDING_TARGET_TARGET_NUM",
                            "UAV_YAW_ALIGN_RESULT",
                            "UAV_LANDING_TARGET_Q_W", "UAV_LANDING_TARGET_Q_X",
                            "UAV_LANDING_TARGET_Q_Y", "UAV_LANDING_TARGET_Q_Z"})
      Register(key, 0);
  for (const auto &entry : m_conditions)
    for (const auto &key : entry.second.getVarNames())
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
      m_inputs[key].numeric = msg.IsDouble();
      if (key == "RENDEZVOUS_OWN_SESSION" && m_rv_started && value(key) != m_old_rv_session &&
          msg.GetTime() >= m_state_time)
        m_rv_session = value(key);
      if (key == "RENDEZVOUS_OWN_PHASE" && m_rv_started && !m_rv_session.empty())
        m_rv_phase = value(key);
      if (key == "UAV_YAW_ALIGN_RESULT" && m_align_yaw && m_state == "RENDEZVOUS" &&
          !m_yaw_request_id.empty() && msg.IsString() &&
          msg.GetTime() >= m_yaw_request_time && fresh(key) &&
          tokStringParse(msg.GetString(), "id", '#', '=') == m_yaw_request_id) {
        const string status = tokStringParse(msg.GetString(), "status", '#', '=');
        const string angle = tokStringParse(msg.GetString(), "angle", '#', '=');
        if (isNumber(angle) && isfinite(atof(angle.c_str())) &&
            fabs(atof(angle.c_str()) - m_yaw_goal) < 1e-4) {
          if (status == "ACCEPTED") {
            m_yaw_accepted = true;
            m_yaw_ack_time = msg.GetTime();
            m_yaw_state = "ACCEPTED";
          } else if (status == "REJECTED" || status == "FAILED")
            abort("yaw_alignment_rejected");
        }
      }
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
  m_yaw_started = m_yaw_request_time = m_yaw_ack_time = -1;
  m_yaw_stable_since = m_yaw_confirm_since = -1;
  m_yaw_last_stamp = m_yaw_request_stamp = m_yaw_confirm_stamp = 0;
  m_yaw_target_num = -1;
  m_yaw_stable_samples = m_yaw_confirm_samples = 0;
  m_yaw_request_id.clear();
  m_yaw_accepted = false;
  m_yaw_state = m_align_yaw ? "WAITING" : "DISABLED";
  m_yaw_input_reason = "not_evaluated";
  Notify("LANDING_DECK_ENABLED", "false");
  state("REQUESTED", "recover_received");
  send();
}

void LandingCoordinator::startRendezvous()
{
  if (m_rv_started || !permits("rendezvous"))
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
    // Expose a pending session for admission checks before guarded agreement.
    // Ungated missions retain their immediate agreement behavior.
    state(m_conditions.count("agreement") ? "REQUESTED" : "AGREED",
          m_conditions.count("agreement") ? "awaiting_platform_permission"
                                          : "aircraft_recovery_request_accepted");
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

bool LandingCoordinator::permits(const string &stage) const
{
  const auto it = m_conditions.find(stage);
  if (it == m_conditions.end())
    return true;
  LogicCondition condition = it->second;
  for (const auto &key : condition.getVarNames()) {
    // Periodic FSM/check facts provide a lease: never reuse stale permission.
    if (!fresh(key))
      return false;
    const auto &input = m_inputs.at(key);
    if (input.numeric)
      condition.setVarVal(key, atof(input.value.c_str()));
    else
      condition.setVarVal(key, input.value);
  }
  return condition.eval();
}

bool LandingCoordinator::targetFresh() const
{
  const string age = value("UAV_LANDING_TARGET_AGE");
  const string frame = value("UAV_LANDING_TARGET_FRAME");
  if (!fresh("UAV_LANDING_TARGET_AGE") || !isNumber(age))
    return false;
  const double reported_age = atof(age.c_str());
  const double elapsed = max(0.0, m_curr_time - m_inputs.at("UAV_LANDING_TARGET_AGE").time) /
                         max(0.001, GetMOOSTimeWarp());
  return isfinite(reported_age) && reported_age >= 0 && reported_age + elapsed <= 0.5 &&
         flag("UAV_LANDING_TARGET_AVAILABLE") && flag("UAV_LANDING_TARGET_POSITION_VALID") &&
         fresh("UAV_LANDING_TARGET_FRAME") && isNumber(frame) && atof(frame.c_str()) == 12;
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
  return targetFresh() && flag("UAV_HEALTH_ALL_OK") && age_ok("UAV_HEALTH_AGE", m_input_timeout) &&
         flag("UAV_IS_ARMED") && flag("UAV_LANDED_STATE_AVAILABLE") && fresh("UAV_LANDED_STATE") &&
         value("UAV_LANDED_STATE") == "IN_AIR" && age_ok("UAV_LANDED_STATE_AGE", 2.0);
}

bool LandingCoordinator::landedAndDisarmed() const
{
  const string age = value("UAV_LANDED_STATE_AGE");
  return flag("UAV_LANDED_STATE_AVAILABLE") && fresh("UAV_LANDED_STATE") &&
         value("UAV_LANDED_STATE") == "ON_GROUND" && fresh("UAV_IS_ARMED") &&
         m_inputs.at("UAV_IS_ARMED").numeric && isNumber(value("UAV_IS_ARMED")) &&
         atof(value("UAV_IS_ARMED").c_str()) == 0 && fresh("UAV_LANDED_STATE_AGE") &&
         isNumber(age) && isfinite(atof(age.c_str())) && atof(age.c_str()) >= 0 &&
         atof(age.c_str()) <= 2.0;
}

bool LandingCoordinator::number(const string &key, double &data) const
{
  const auto it = m_inputs.find(key);
  if (!fresh(key) || it == m_inputs.end() || !it->second.numeric || !isNumber(it->second.value))
    return false;
  data = atof(it->second.value.c_str());
  return isfinite(data);
}

bool LandingCoordinator::targetRelativeYaw(double &error, double &sample_time, uint64_t &stamp)
{
  double roll = 0, pitch = 0, available = 0, age = 0;
  if (!flag("UAV_GUIDED_MODE_ACTIVE")) {
    m_yaw_input_reason = "guided_mode_missing_stale_or_inactive";
    return false;
  }
  if (!targetFresh()) {
    m_yaw_input_reason = "target_missing_or_stale";
    return false;
  }
  for (const auto &entry : {make_pair("UAV_ROLL", &roll), make_pair("UAV_PITCH", &pitch),
                            make_pair("UAV_ATTITUDE_AVAILABLE", &available),
                            make_pair("UAV_ATTITUDE_AGE", &age)}) {
    if (!number(entry.first, *entry.second)) {
      m_yaw_input_reason = string(entry.first) + "_missing_stale_or_not_numeric";
      return false;
    }
  }
  if (available != 1 || age < 0 ||
      age + max(0.0, m_curr_time - m_inputs.at("UAV_ATTITUDE_AGE").time) /
                max(0.001, GetMOOSTimeWarp()) > 0.5) {
    m_yaw_input_reason = "native_attitude_unavailable_or_old";
    return false;
  }
  if (hypot(roll, pitch) > m_yaw_max_tilt) {
    m_yaw_input_reason = "aircraft_tilt_exceeds_relative_yaw_limit";
    return false;
  }

  double target_num = 0;
  if (!number("UAV_LANDING_TARGET_TARGET_NUM", target_num) || target_num < 0 ||
      target_num > 255 || floor(target_num) != target_num) {
    m_yaw_input_reason = "target_number_missing_stale_or_invalid";
    return false;
  }
  if (m_yaw_target_num >= 0 && static_cast<int>(target_num) != m_yaw_target_num) {
    m_yaw_target_num = static_cast<int>(target_num);
    m_yaw_input_reason = "target_number_changed";
    return false;
  }
  m_yaw_target_num = static_cast<int>(target_num);

  bodyframe::Quaternion q{};
  const char *fields[] = {"W", "X", "Y", "Z"};
  double norm = 0;
  sample_time = m_curr_time + 1;
  for (unsigned i = 0; i < 4; ++i) {
    const string key = string("UAV_LANDING_TARGET_Q_") + fields[i];
    if (!number(key, q[i])) {
      m_yaw_input_reason = string("quaternion_") + fields[i] + "_missing_stale_or_not_numeric";
      return false;
    }
    norm += q[i] * q[i];
    sample_time = min(sample_time, m_inputs.at(key).time);
  }
  if (!isfinite(norm) || norm <= 1e-12) {
    m_yaw_input_reason = "quaternion_invalid_or_unavailable";
    return false;
  }
  for (auto &component : q)
    component /= sqrt(norm);

  double observation_stamp = 0;
  if (!number("UAV_LANDING_TARGET_TIME_USEC", observation_stamp) || observation_stamp <= 0 ||
      observation_stamp >= 9007199254740992.0 || floor(observation_stamp) != observation_stamp) {
    m_yaw_input_reason = "observation_timestamp_missing_or_invalid";
    return false;
  }
  stamp = static_cast<uint64_t>(observation_stamp);
  sample_time = min(sample_time, m_inputs.at("UAV_LANDING_TARGET_TIME_USEC").time);
  const auto forward = bodyframe::rotate(q, {{1, 0, 0}});
  if (hypot(forward[0], forward[1]) <= 1e-6) {
    m_yaw_input_reason = "target_forward_axis_vertical";
    return false;
  }
  error = remainder(atan2(forward[1], forward[0]) / bodyframe::radians, 360.0);
  m_yaw_input_reason = "ready";
  return true;
}

bool LandingCoordinator::yawReady()
{
  if (!m_align_yaw)
    return true;
  if (m_yaw_started < 0)
    m_yaw_started = m_curr_time;

  double relative_error = 0, sample_time = -1;
  uint64_t stamp = 0;
  if (!targetRelativeYaw(relative_error, sample_time, stamp)) {
    if (!m_yaw_request_id.empty() && m_yaw_input_reason == "target_number_changed") {
      abort("yaw_target_changed_after_command");
      return false;
    }
    m_yaw_state = "WAITING_FOR_ORIENTATION";
    m_yaw_stable_since = m_yaw_confirm_since = -1;
    m_yaw_stable_samples = m_yaw_confirm_samples = 0;
    return false; // Never manufacture orientation or alignment from elapsed time.
  }
  m_yaw_error = relative_error;
  if (!m_yaw_request_id.empty() && m_yaw_last_stamp > 0 && stamp < m_yaw_last_stamp) {
    abort("yaw_observation_clock_rewind_after_command");
    return false;
  }
  const bool new_sample = stamp != m_yaw_last_stamp;
  if (new_sample) {
    if (stamp < m_yaw_last_stamp || m_yaw_stable_samples == 0 ||
        fabs(remainder(relative_error - m_yaw_stable_reference, 360.0)) >
            m_yaw_stability_tolerance) {
      m_yaw_stable_reference = relative_error;
      m_yaw_stable_since = sample_time;
      m_yaw_stable_samples = 1;
    } else {
      m_yaw_stable_reference += remainder(relative_error - m_yaw_stable_reference, 360.0) /
                                static_cast<double>(m_yaw_stable_samples + 1);
      m_yaw_stable_reference = remainder(m_yaw_stable_reference, 360.0);
      ++m_yaw_stable_samples;
    }
    m_yaw_last_stamp = stamp;
  }

  if (m_yaw_request_id.empty() && fabs(relative_error) > m_yaw_tolerance) {
    if (m_yaw_stable_samples < 3 || m_yaw_stable_since < 0 ||
        sample_time - m_yaw_stable_since < m_yaw_stability_time) {
      m_yaw_state = "STABILIZING_TARGET_ORIENTATION";
      return false;
    }
    m_yaw_goal = relative_error;
    m_yaw_request_stamp = stamp;
    m_yaw_request_id = m_session + "_yaw";
    m_yaw_request_time = m_curr_time;
    m_yaw_state = "COMMANDING";
    Notify("YAW_ALIGN_REQUEST", "id=" + m_yaw_request_id +
        "#angle=" + doubleToStringX(m_yaw_goal, 6) + "#rate=" + doubleToStringX(m_yaw_rate, 3));
    return false;
  }
  if (!m_yaw_request_id.empty() && !m_yaw_accepted) {
    m_yaw_state = "WAITING_FOR_FC_ACK";
    return false;
  }
  if (!m_yaw_request_id.empty() &&
      (sample_time < m_yaw_ack_time || sample_time > m_curr_time + 1 ||
       stamp == m_yaw_request_stamp)) {
    m_yaw_state = "WAITING_FOR_POST_ACK_TARGET";
    return false;
  }
  if (fabs(relative_error) > m_yaw_tolerance) {
    m_yaw_confirm_since = -1;
    m_yaw_confirm_stamp = 0;
    m_yaw_confirm_samples = 0;
    m_yaw_state = "ALIGNING";
    return false;
  }
  if (new_sample && stamp != m_yaw_confirm_stamp) {
    if (m_yaw_confirm_since < 0) {
      m_yaw_confirm_since = sample_time;
      m_yaw_confirm_samples = 1;
    } else
      ++m_yaw_confirm_samples;
    m_yaw_confirm_stamp = stamp;
  }
  if (m_yaw_confirm_samples < 3 || m_yaw_confirm_since < 0 ||
      sample_time - m_yaw_confirm_since < m_yaw_hold_time) {
    m_yaw_state = "CONFIRMING_ALIGNMENT";
    return false;
  }
  m_yaw_state = "ALIGNED";
  return true;
}

bool LandingCoordinator::Iterate()
{
  AppCastingMOOSApp::Iterate();
  if (m_valid && !m_session.empty() && m_state != "IDLE" && m_state != "ABORTED" &&
      m_state != "COMPLETE") {
    const bool peer_fresh = m_peer_time >= 0 && m_curr_time - m_peer_time <= m_input_timeout;
    if (m_curr_time - m_session_time > m_timeout)
      abort("landing_timeout");
    else if (m_role == "aircraft" && !m_command_accepted && landedAndDisarmed())
      // An RTL/manual landing is not a successful coordinated recovery.
      // Allow the supervisory FSM to follow its existing off-nominal edges.
      abort("landed_without_authorized_autoland");
    else if (m_align_yaw && m_role == "aircraft" && m_yaw_started >= 0 &&
             m_state == "RENDEZVOUS" && m_curr_time - m_yaw_started > m_yaw_timeout)
      abort("yaw_alignment_timeout");
    else if (m_align_yaw && !m_yaw_request_id.empty() && m_state == "RENDEZVOUS" &&
             (!peer_fresh || m_peer_phase != "CLEARED" || !permits("command") ||
              !flag("UAV_GUIDED_MODE_ACTIVE")))
      abort("yaw_alignment_permission_lost");
    else if (m_rv_phase == "ABORT")
      abort("rendezvous_aborted");
    else if (m_role == "platform" && m_state == "REQUESTED" && peer_fresh && permits("agreement")) {
      state("AGREED", "platform_permission_granted");
      send();
    } else if (peer_fresh &&
               ((m_role == "aircraft" && m_state == "REQUESTED" && m_peer_phase == "AGREED") ||
                (m_role == "platform" && m_state == "AGREED" && m_peer_phase == "RENDEZVOUS")))
      startRendezvous();
    else if (m_role == "platform" && m_state == "RENDEZVOUS" && peer_fresh &&
             m_rv_phase == "COMPLETE" && m_peer_rv_phase == "COMPLETE" && fresh("NAV_X") &&
             fresh("NAV_Y") && permits("clearance")) {
      Notify("LANDING_DECK_ENABLED", "true");
      state("CLEARED", "both_rendezvous_complete");
    } else if (m_role == "aircraft" && m_state == "RENDEZVOUS" && peer_fresh &&
               m_peer_phase == "CLEARED" && m_peer_rv_phase == "COMPLETE" &&
               m_rv_phase == "COMPLETE" && permits("command")) {
      if (!checksReady()) {
        if (m_align_yaw && m_yaw_started >= 0) {
          m_yaw_state = "WAITING_FOR_ORIENTATION";
          m_yaw_input_reason = "required_landing_check_missing_or_stale";
          m_yaw_stable_since = m_yaw_confirm_since = -1;
          m_yaw_stable_samples = m_yaw_confirm_samples = 0;
        }
      } else if (yawReady()) {
        state("COMMANDING", m_align_yaw ? "clearance_checks_and_yaw_ready" : "clearance_and_checks_ready");
        m_command_time = m_curr_time;
        Notify("ARDU_COMMAND", "AUTOLAND");
      }
    } else if (m_role == "aircraft" && m_state == "COMMANDING" && m_curr_time - m_command_time > 10)
      abort("landing_command_timeout");
    else if (m_role == "aircraft" && m_state == "LANDING" && m_command_accepted &&
             landedAndDisarmed())
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
  if (m_align_yaw && !m_command_accepted)
    m_yaw_state = "ABORTED";
  state("ABORTED", reason);
  send();
}

void LandingCoordinator::publish()
{
  Notify("LANDING_OWN_PHASE", m_state);
  Notify("LANDING_OWN_REASON", m_reason);
  Notify("LANDING_OWN_SESSION", m_session.empty() ? "none" : m_session);
  Notify("LANDING_OWN_RENDEZVOUS_PHASE", m_rv_phase);
  Notify("LANDING_OWN_RENDEZVOUS_SESSION", m_rv_session.empty() ? "none" : m_rv_session);
  const bool peer_fresh = m_peer_time >= 0 && m_curr_time - m_peer_time <= m_input_timeout;
  Notify("LANDING_PEER_PHASE", peer_fresh ? m_peer_phase : "STALE");
  Notify("LANDING_PEER_CLEARANCE",
         peer_fresh && (m_peer_phase == "CLEARED" || m_peer_phase == "LANDING") ? "true" : "false");
  Notify("LANDING_OWN_CLEARANCE", m_state == "CLEARED" || m_state == "LANDING" ? "true" : "false");
  Notify("LANDING_OWN_CHECKS_READY", checksReady() ? "true" : "false");
  Notify("LANDING_OWN_TARGET_FRESH", targetFresh() ? "true" : "false");
  Notify("LANDING_OWN_YAW_STATE", m_yaw_state);
  Notify("LANDING_OWN_YAW_INPUT_REASON", m_yaw_input_reason);
  Notify("LANDING_OWN_YAW_STABLE_SAMPLES", static_cast<double>(m_yaw_stable_samples));
  Notify("LANDING_OWN_YAW_CONFIRM_SAMPLES", static_cast<double>(m_yaw_confirm_samples));
  if (!m_yaw_request_id.empty())
    Notify("LANDING_OWN_YAW_GOAL", m_yaw_goal);
  if (m_yaw_started >= 0 && m_yaw_input_reason == "ready")
    Notify("LANDING_OWN_YAW_ERROR", m_yaw_error);
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

  for (const auto &entry : m_condition_specs) {
    label(entry.first + " permission");
    m_msgs << (permits(entry.first) ? "allowed" : "BLOCKED") << " (" << entry.second << ")\n";
  }
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
    label("Yaw alignment");
    m_msgs << m_yaw_state << " (option " << (m_align_yaw ? "enabled" : "disabled") << ")\n";
    if (m_align_yaw) {
      label("Yaw input status");
      m_msgs << m_yaw_input_reason << '\n';
      label("Yaw tolerance / rate");
      m_msgs << m_yaw_tolerance << " deg / " << m_yaw_rate << " deg/s\n";
      label("Stability / hold");
      m_msgs << m_yaw_stability_time << " / " << m_yaw_hold_time << " MOOS s; "
             << m_yaw_stability_tolerance << " deg jitter\n";
      label("Maximum tilt");
      m_msgs << m_yaw_max_tilt << " deg\n";
      label("Yaw timeout");
      m_msgs << m_yaw_timeout << " MOOS s\n";
      label("Yaw request / FC ACK");
      m_msgs << show(m_yaw_request_id) << " / " << (m_yaw_accepted ? "accepted" : "not accepted") << '\n';
      label("Command / error");
      if (m_yaw_request_id.empty())
        m_msgs << "none / " << m_yaw_error << " deg target-relative\n";
      else
        m_msgs << m_yaw_goal << " / " << m_yaw_error << " deg target-relative\n";
    }
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
