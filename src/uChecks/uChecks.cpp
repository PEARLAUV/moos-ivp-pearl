#include "uChecks.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <unistd.h>

#include "MBUtils.h"

using namespace std;

namespace {
bool finiteNumber(const string& text, double& result)
{
  return setDoubleOnString(result, text) && std::isfinite(result);
}

bool validName(const string& name)
{
  return !name.empty() && name.find_first_not_of(
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") == string::npos;
}

void jsonString(ostream& out, const string& value)
{
  const char* hex = "0123456789abcdef";
  out << '"';
  for(unsigned char byte : value) {
    if(byte == '"' || byte == '\\')
      out << '\\' << static_cast<char>(byte);
    else if(byte < 0x20)
      out << "\\u00" << hex[byte >> 4] << hex[byte & 0x0f];
    else
      out << static_cast<char>(byte);
  }
  out << '"';
}

void jsonNumber(ostream& out, double value)
{
  if(std::isfinite(value)) out << setprecision(17) << value;
  else out << "null";
}

void jsonInput(ostream& out, const CheckInput* input, double now)
{
  out << "{\"type\":";
  if(input && input->received)
    jsonString(out, input->numeric ? "double" : "string");
  else
    out << "null";
  out << ",\"value\":";
  if(!input || !input->received) out << "null";
  else if(input->numeric) jsonNumber(out, input->number);
  else jsonString(out, input->text);
  out << ",\"received\":" << (input && input->received ? "true" : "false")
      << ",\"age_s\":";
  if(input && input->received) jsonNumber(out, std::max(0.0, now - input->time));
  else out << "null";
  out << '}';
}

bool ruleMatches(const CheckRule& rule, const CheckInput& input)
{
  if(rule.numeric) {
    if(!input.numeric || !std::isfinite(input.number)) return false;
    return rule.has_equal ? input.number == rule.equal :
      (!rule.has_min || input.number >= rule.minimum) &&
      (!rule.has_max || input.number <= rule.maximum);
  }
  if(input.numeric) return false;
  return rule.string_options.empty() ? input.text == rule.string_equal :
    std::find(rule.string_options.begin(), rule.string_options.end(), input.text) !=
      rule.string_options.end();
}
}

uChecks::uChecks() : m_condition_configured(false), m_config_valid(true) {}

bool uChecks::parseCheck(const string& config)
{
  NamedCheck check;
  set<string> seen;
  for(string field : parseStringQ(config, ',')) {
    const string key = tolower(biteStringX(field, '='));
    const string value = stripBlankEnds(field);
    if(key.empty() || value.empty() || !seen.insert(key).second)
      return false;
    if(key == "name")
      check.name = value;
    else if(key == "timeout") {
      if(!setPosDoubleOnString(check.timeout, value) || !std::isfinite(check.timeout))
        return false;
    }
    else if(key == "max_age") {
      if(!setPosDoubleOnString(check.max_age, value) || !std::isfinite(check.max_age))
        return false;
    }
    else if(key == "success_hold") {
      if(!setNonNegDoubleOnString(check.success_hold, value) ||
         !std::isfinite(check.success_hold))
        return false;
    }
    else
      return false;
  }
  if(!validName(check.name) || check.timeout <= 0 ||
     check.success_hold >= check.timeout || m_checks.count(check.name))
    return false;
  m_checks.emplace(check.name, check);
  return true;
}

bool uChecks::parseRule(const string& config)
{
  CheckRule rule;
  string check_name;
  bool has_type = false;
  bool has_policy = false;
  set<string> seen;
  for(string field : parseStringQ(config, ',')) {
    const string key = tolower(biteStringX(field, '='));
    const string value = stripBlankEnds(field);
    if(key.empty() || value.empty() || !seen.insert(key).second)
      return false;
    if(key == "check")
      check_name = value;
    else if(key == "var")
      rule.var = value;
    else if(key == "type") {
      has_type = true;
      if(value == "double")
        rule.numeric = true;
      else if(value != "string")
        return false;
    }
    else if(key == "equals" || key == "one_of") {
      if(rule.has_equal) return false;
      rule.has_equal = true;
      rule.string_equal = value;
      if(key == "one_of") {
        for(const string& option : parseStringQ(value, '|')) {
          if(option.empty()) return false;
          rule.string_options.push_back(option);
        }
        if(rule.string_options.size() < 2) return false;
      }
    }
    else if(key == "min") {
      rule.has_min = finiteNumber(value, rule.minimum);
      if(!rule.has_min)
        return false;
    }
    else if(key == "max") {
      rule.has_max = finiteNumber(value, rule.maximum);
      if(!rule.has_max)
        return false;
    }
    else if(key == "on_mismatch") {
      has_policy = true;
      if(value == "fail")
        rule.immediate = true;
      else if(value != "wait")
        return false;
    }
    else
      return false;
  }
  if(!validName(check_name) || rule.var.empty() ||
     rule.var.find_first_of(" \t,=") != string::npos ||
     !has_type || !has_policy)
    return false;
  if(rule.numeric) {
    if(rule.has_equal == (rule.has_min || rule.has_max))
      return false;
    if(!rule.string_options.empty() ||
       (rule.has_equal && !finiteNumber(rule.string_equal, rule.equal)))
      return false;
    if(rule.has_min && rule.has_max && rule.minimum > rule.maximum)
      return false;
  }
  else if(!rule.has_equal || rule.has_min || rule.has_max)
    return false;
  // Configuration lines may be returned in any order; validate check names
  // only after all definitions have been parsed.
  NamedCheck& check = m_checks[check_name];
  check.name = check_name;
  if(std::any_of(check.rules.begin(), check.rules.end(), [&](const CheckRule& other) {
       return other.var == rule.var;
     }))
    return false;
  check.rules.push_back(rule);
  return true;
}

bool uChecks::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();
  STRING_LIST params;
  m_MissionReader.EnableVerbatimQuoting(false);
  if(!m_MissionReader.GetConfiguration(GetAppName(), params))
    m_config_valid = false;
  // The MOOS mission reader does not promise configuration-line order.
  for(const string& original : params) {
    string line = original;
    const string key = tolower(biteStringX(line, '='));
    const string value = stripBlankEnds(line);
    if(key == "result_var")
      m_report_var = value;
    else if(key == "state_var")
      m_state_var = value;
    else if(key == "result_prefix")
      m_result_prefix = value;
    else if(key == "session_var")
      m_session_var = value;
    else if(key == "snapshot_var")
      m_snapshot_var = value;
    else if(key == "result_interval") {
      if(!finiteNumber(value, m_result_interval) || m_result_interval < 0) {
        reportUnhandledConfigWarning(original);
        m_config_valid = false;
      }
    }
    else if(key == "snapshot_interval") {
      if(!setPosDoubleOnString(m_snapshot_interval, value) ||
         !std::isfinite(m_snapshot_interval)) {
        reportUnhandledConfigWarning(original);
        m_config_valid = false;
      }
    }
    else if(key == "condition") {
      if(m_condition_configured) {
        reportUnhandledConfigWarning(original);
        m_config_valid = false;
      }
      else {
        m_condition.setAllowDoubleEquals(true);
        m_condition_configured = m_condition.setCondition(value);
        if(!m_condition_configured)
          m_config_valid = false;
      }
    }
  }
  if(m_state_var.empty())
    m_state_var = m_report_var + "_STATE";
  if(m_snapshot_var.empty())
    m_snapshot_var = m_report_var + "_SNAPSHOT";
  for(const string& original : params) {
    string line = original;
    const string key = tolower(biteStringX(line, '='));
    bool handled = true;
    if(key == "check")
      handled = parseCheck(stripBlankEnds(line));
    else if(key != "require" && key != "condition" && key != "result_var" &&
            key != "state_var" && key != "result_prefix" &&
            key != "session_var" && key != "snapshot_var" &&
            key != "snapshot_interval" && key != "result_interval" &&
            key != "apptick" && key != "commstick" &&
            key != "term_report_interval")
      handled = false;
    if(!handled) {
      reportUnhandledConfigWarning(original);
      m_config_valid = false;
    }
  }
  for(const string& original : params) {
    string line = original;
    if(tolower(biteStringX(line, '=')) == "require" &&
       !parseRule(stripBlankEnds(line))) {
      reportUnhandledConfigWarning(original);
      m_config_valid = false;
    }
  }
  if(!validName(m_report_var) || !validName(m_state_var) ||
     !validName(m_snapshot_var) || !validName(m_result_prefix) ||
     (!m_session_var.empty() && !validName(m_session_var)) ||
     m_report_var == m_state_var ||
     !m_condition_configured || m_checks.empty()) {
    reportConfigWarning("result_var, result_prefix, condition and named checks are required");
    m_config_valid = false;
  }
  m_condition_vars = m_condition.getVarNames();
  set<string> outputs = {m_report_var, m_state_var};
  if(!outputs.insert(m_snapshot_var).second) {
    reportConfigWarning("snapshot_var cannot overlap another output");
    m_config_valid = false;
  }
  for(const auto& entry : m_checks) {
    const NamedCheck& check = entry.second;
    const string var = m_result_prefix + entry.first;
    if(!outputs.insert(var).second || !outputs.insert(var + "_STATE").second ||
       check.timeout <= 0 || check.rules.empty()) {
      reportConfigWarning("Duplicate output, missing check definition, or empty check: " + entry.first);
      m_config_valid = false;
    }
  }
  if(outputs.count(m_session_var)) {
    reportConfigWarning("session_var cannot be an output");
    m_config_valid = false;
  }
  for(const string& var : m_condition_vars) {
    if(outputs.count(var)) {
      reportConfigWarning("Output cannot be an activation input: " + var);
      m_config_valid = false;
    }
  }
  for(const auto& entry : m_checks) {
    for(const CheckRule& rule : entry.second.rules) {
      if(outputs.count(rule.var)) {
        reportConfigWarning("Output cannot be a checked input: " + rule.var);
        m_config_valid = false;
      }
    }
  }
  if(m_config_valid) {
    ostringstream epoch;
    epoch.imbue(locale::classic());
    epoch << getpid() << '-' << setprecision(17) << MOOSTime();
    m_snapshot_epoch = epoch.str();
    for(auto& entry : m_checks)
      publish(m_result_prefix + entry.first, entry.second.status,
              entry.second.info, "inactive", "reason=condition_not_met");
    publish(m_report_var, m_status, m_info, "inactive", "reason=condition_not_met");
  }
  registerVariables();
  return true;
}

bool uChecks::OnConnectToServer()
{
  registerVariables();
  return true;
}

void uChecks::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  for(const string& var : m_condition_vars)
    Register(var, 0);
  if(!m_session_var.empty()) Register(m_session_var, 0);
  for(const auto& entry : m_checks)
    for(const CheckRule& rule : entry.second.rules)
      Register(rule.var, 0);
}

bool uChecks::OnNewMail(MOOSMSG_LIST& mail)
{
  AppCastingMOOSApp::OnNewMail(mail);
  for(const CMOOSMsg& msg : mail) {
    const string key = msg.GetKey();
    if(key == m_session_var && msg.IsString() &&
       msg.GetString() != m_session) {
      m_session = msg.GetString();
      m_session_changed = true;
    }
    if(find(m_condition_vars.begin(), m_condition_vars.end(), key) !=
       m_condition_vars.end()) {
      if(msg.IsDouble() && std::isfinite(msg.GetDouble()))
        m_condition.setVarVal(key, msg.GetDouble());
      else if(msg.IsString())
        m_condition.setVarVal(key, msg.GetString());
      else
        continue;
      m_condition_seen.insert(key);
      CheckInput& condition_input = m_condition_inputs[key];
      condition_input.received = true;
      condition_input.numeric = msg.IsDouble();
      if(condition_input.numeric) {
        condition_input.number = msg.GetDouble();
        condition_input.text.clear();
      }
      else
        condition_input.text = msg.GetString();
      condition_input.time = MOOSTime();
    }
    if(m_inputs.find(key) == m_inputs.end()) {
      bool checked = false;
      for(const auto& entry : m_checks)
        for(const CheckRule& rule : entry.second.rules)
          checked = checked || rule.var == key;
      if(!checked)
        continue;
    }
    CheckInput& input = m_inputs[key];
    if(msg.IsDouble()) {
      input.numeric = true;
      input.number = msg.GetDouble();
      input.text.clear();
    }
    else if(msg.IsString()) {
      input.numeric = false;
      input.text = msg.GetString();
    }
    else
      continue;
    input.received = true;
    input.time = MOOSTime();
  }
  return true;
}

bool uChecks::active() const
{
  for(const string& var : m_condition_vars)
    if(!m_condition_seen.count(var))
      return false;
  return m_condition.eval();
}

double uChecks::expiryDeadline(const NamedCheck& check, double now) const
{
  double deadline = std::numeric_limits<double>::infinity();
  for(const CheckRule& rule : check.rules) {
    const auto found = m_inputs.find(rule.var);
    if(found == m_inputs.end() || !found->second.received)
      return now;
    if(check.max_age > 0)
      deadline = std::min(deadline, found->second.time + check.max_age);
  }
  return deadline;
}

string uChecks::evaluate(const NamedCheck& check, string& info, double now) const
{
  string pending;
  string observations;
  for(const CheckRule& rule : check.rules) {
    const auto found = m_inputs.find(rule.var);
    if(found == m_inputs.end() || !found->second.received ||
       (check.max_age > 0 && (now - found->second.time) > check.max_age)) {
      if(pending.empty())
        pending = "var=" + rule.var + ",reason=missing_or_stale";
      continue;
    }
    const CheckInput& input = found->second;
    const bool match = ruleMatches(rule, input);
    const string observed =
      (rule.numeric && input.numeric && std::isfinite(input.number)) ?
        doubleToStringX(input.number, 3) :
      (!rule.numeric && !input.numeric) ? input.text : "wrong_type";
    if(match) {
      if(!observations.empty())
        observations += ";";
      observations += rule.var + ":" + observed;
      continue;
    }
    string detail = "var=" + rule.var + ",observed=" + observed + ",expected=";
    if(!rule.numeric)
      detail += rule.string_equal;
    else if(rule.has_equal)
      detail += doubleToStringX(rule.equal, 3);
    else
      detail += "[" + (rule.has_min ? doubleToStringX(rule.minimum, 3) : "-inf") +
                ":" + (rule.has_max ? doubleToStringX(rule.maximum, 3) : "inf") + "]";
    if(rule.immediate) {
      info = detail;
      return "fail";
    }
    if(pending.empty())
      pending = detail;
  }
  if(!pending.empty()) {
    info = pending;
    return "pending";
  }
  info = "values=" + observations;
  return "success";
}

void uChecks::publish(const string& var, string& status_cache,
                      string& info_cache, const string& status, const string& info,
                      bool refresh)
{
  if(!refresh && status == status_cache && info == info_cache)
    return;
  if(refresh || status != status_cache)
    Notify(var, status);
  Notify(var == m_report_var ? m_state_var : var + "_STATE",
         "status=" + status +
         (m_session_var.empty() ? "" : ",session=" +
           (m_session.empty() ? "none" : m_session)) + "," + info);
  status_cache = status;
  info_cache = info;
}

void uChecks::updateCheck(NamedCheck& check, bool enabled, double now, bool refresh)
{
  const string var = m_result_prefix + check.name;
  if(!enabled) {
    if(check.deactivated_at < 0 && check.active_since >= 0) {
      check.deactivated_at = now;
      check.inactive_deadline = expiryDeadline(check, now);
    }
    if(check.active_since < 0 || now >= check.inactive_deadline) {
      check.active_since = -1;
      check.pass_since = -1;
      check.deactivated_at = -1;
      check.inactive_deadline = -1;
      check.failed = false;
      publish(var, check.status, check.info,
              check.has_run ? "expired" : "inactive",
              check.has_run ? "reason=first_requirement_expired" :
                              "reason=condition_not_met");
    }
    return;
  }
  check.has_run = true;
  if(check.deactivated_at >= 0) {
    const double paused = now - check.deactivated_at;
    check.active_since += paused;
    if(check.pass_since >= 0)
      check.pass_since += paused;
    check.deactivated_at = -1;
    check.inactive_deadline = -1;
  }
  if(check.active_since < 0)
    check.active_since = now;
  if(check.failed) {
    publish(var, check.status, check.info, "fail", check.info, refresh);
    return;
  }
  string info;
  string status = evaluate(check, info, now);
  if(status == "success") {
    if(check.pass_since < 0)
      check.pass_since = now;
    if((now - check.pass_since) < check.success_hold) {
      status = "pending";
      info = "reason=holding,elapsed=" + doubleToStringX(now - check.pass_since, 1) +
             ",required=" + doubleToStringX(check.success_hold, 1);
    }
  }
  else
    check.pass_since = -1;
  if(status == "pending" && now - check.active_since >= check.timeout) {
    status = "fail";
    info = "reason=timeout,waited=" + doubleToStringX(check.timeout, 1) +
           ",last=" + info;
  }
  if(status == "fail")
    check.failed = true;
  publish(var, check.status, check.info, status, info, refresh);
}

string uChecks::snapshotSignature() const
{
  ostringstream signature;
  signature << m_status << '|' << m_info;
  for(const auto& entry : m_checks)
    signature << '|' << entry.first << '=' << entry.second.status
              << ':' << entry.second.info;
  return signature.str();
}

void uChecks::publishSnapshot(double now, bool enabled)
{
  ostringstream out;
  out.imbue(locale::classic());
  out << "{\"schema_version\":1,\"instance\":";
  jsonString(out, m_report_var);
  out << ",\"epoch\":";
  jsonString(out, m_snapshot_epoch);
  out << ",\"sequence\":" << ++m_snapshot_sequence
      << ",\"published_at_moos\":";
  jsonNumber(out, now);
  out << ",\"active\":" << (enabled ? "true" : "false")
      << ",\"session\":{\"variable\":";
  if(m_session_var.empty()) out << "null";
  else jsonString(out, m_session_var);
  out << ",\"id\":";
  if(m_session.empty() || m_session == "none") out << "null";
  else jsonString(out, m_session);
  out << "},\"activation\":{\"expression\":";
  jsonString(out, m_condition.getRawCondition());
  out << ",\"matched\":" << (active() ? "true" : "false")
      << ",\"inputs\":{";
  set<string> condition_vars(m_condition_vars.begin(), m_condition_vars.end());
  bool first = true;
  for(const string& var : condition_vars) {
    if(!first) out << ',';
    first = false;
    jsonString(out, var);
    out << ':';
    const auto found = m_condition_inputs.find(var);
    jsonInput(out, found == m_condition_inputs.end() ? nullptr : &found->second, now);
  }
  out << "}},\"aggregate\":{\"variable\":";
  jsonString(out, m_report_var);
  out << ",\"status\":";
  jsonString(out, m_status);
  out << "},\"inputs\":{";
  set<string> input_vars;
  for(const auto& entry : m_checks)
    for(const CheckRule& rule : entry.second.rules)
      input_vars.insert(rule.var);
  first = true;
  for(const string& var : input_vars) {
    if(!first) out << ',';
    first = false;
    jsonString(out, var);
    out << ':';
    const auto found = m_inputs.find(var);
    jsonInput(out, found == m_inputs.end() ? nullptr : &found->second, now);
  }
  out << "},\"checks\":{";
  first = true;
  for(const auto& entry : m_checks) {
    const NamedCheck& check = entry.second;
    if(!first) out << ',';
    first = false;
    jsonString(out, entry.first);
    out << ":{\"status\":";
    jsonString(out, check.status);
    out << ",\"reason\":";
    if(check.status == "success" || check.info.empty()) out << "null";
    else jsonString(out, check.info);
    out << ",\"detail\":";
    jsonString(out, check.info);
    out << ",\"active_elapsed_s\":";
    const double active_end = check.deactivated_at >= 0 ? check.deactivated_at : now;
    if(check.active_since < 0) out << "null";
    else jsonNumber(out, std::max(0.0, active_end - check.active_since));
    out << ",\"timeout_s\":";
    jsonNumber(out, check.timeout);
    out << ",\"success_hold_s\":";
    jsonNumber(out, check.success_hold);
    out << ",\"hold_elapsed_s\":";
    if(check.pass_since < 0) out << "null";
    else jsonNumber(out, std::max(0.0, active_end - check.pass_since));
    out << ",\"failure_latched\":" << (check.failed ? "true" : "false")
        << ",\"requirements\":[";
    for(size_t i=0; i<check.rules.size(); ++i) {
      const CheckRule& rule = check.rules[i];
      if(i) out << ',';
      out << "{\"variable\":";
      jsonString(out, rule.var);
      out << ",\"expected\":{";
      if(!rule.string_options.empty()) {
        out << "\"one_of\":[";
        for(size_t j=0; j<rule.string_options.size(); ++j) {
          if(j) out << ',';
          jsonString(out, rule.string_options[j]);
        }
        out << ']';
      }
      else if(rule.has_equal) {
        out << "\"equals\":";
        if(rule.numeric) jsonNumber(out, rule.equal);
        else jsonString(out, rule.string_equal);
      }
      else {
        if(rule.has_min) {
          out << "\"min\":";
          jsonNumber(out, rule.minimum);
        }
        if(rule.has_min && rule.has_max) out << ',';
        if(rule.has_max) {
          out << "\"max\":";
          jsonNumber(out, rule.maximum);
        }
      }
      out << "},\"on_mismatch\":";
      jsonString(out, rule.immediate ? "fail" : "wait");
      out << ",\"max_age_s\":";
      if(check.max_age > 0) jsonNumber(out, check.max_age);
      else out << "null";
      const auto found = m_inputs.find(rule.var);
      const bool received = found != m_inputs.end() && found->second.received;
      const bool fresh = received &&
        (check.max_age <= 0 || now - found->second.time <= check.max_age);
      out << ",\"fresh\":" << (fresh ? "true" : "false")
          << ",\"matched\":";
      if(!fresh) out << "null";
      else out << (ruleMatches(rule, found->second) ? "true" : "false");
      out << ",\"reason\":";
      if(!received) jsonString(out, "missing");
      else if(!fresh) jsonString(out, "stale");
      else if(rule.numeric != found->second.numeric ||
              (rule.numeric && !std::isfinite(found->second.number)))
        jsonString(out, "wrong_type");
      else if(!ruleMatches(rule, found->second)) jsonString(out, "mismatch");
      else out << "null";
      out << '}';
    }
    out << "]}";
  }
  out << "}}";
  Notify(m_snapshot_var, out.str());
  m_last_snapshot_time = now;
  m_snapshot_active = enabled;
  m_snapshot_session = m_session;
  m_snapshot_signature = snapshotSignature();
}

bool uChecks::Iterate()
{
  AppCastingMOOSApp::Iterate();
  if(!m_config_valid) {
    AppCastingMOOSApp::PostReport();
    return true;
  }
  const double now = MOOSTime();
  if(m_session_changed) {
    m_last_result_time = -1;
    for(auto& entry : m_checks) {
      NamedCheck& check = entry.second;
      check.active_since = -1;
      check.pass_since = -1;
      check.deactivated_at = -1;
      check.inactive_deadline = -1;
      check.failed = false;
      check.has_run = false;
      publish(m_result_prefix + entry.first, check.status, check.info,
              "inactive", "reason=session_changed");
    }
    publish(m_report_var, m_status, m_info, "inactive",
            "reason=session_changed");
    m_session_changed = false;
  }
  const bool enabled = active() &&
                       (m_session_var.empty() ||
                        (!m_session.empty() && m_session != "none"));
  // Only an active producer can renew its authorization lease, and only
  // after updateCheck re-evaluates current inputs (including age and holds).
  // Never echo a retained success while inactive, or postpone input expiry.
  if(!enabled)
    m_last_result_time = -1;
  const bool refresh = enabled && m_result_interval > 0 &&
                       (m_last_result_time < 0 ||
                        now - m_last_result_time >= m_result_interval);
  bool any_fail = false;
  bool any_expired = false;
  bool all_success = true;
  bool all_inactive = true;
  string detail;
  for(auto& entry : m_checks) {
    NamedCheck& check = entry.second;
    updateCheck(check, enabled, now, refresh);
    if(check.status == "fail") any_fail = true;
    if(check.status == "expired") any_expired = true;
    if(check.status != "success") all_success = false;
    if(check.status != "inactive") all_inactive = false;
    if(!detail.empty()) detail += ";";
    detail += check.name + ":" + check.status;
  }
  const string status = any_fail ? "fail" :
                        all_success ? "success" :
                        any_expired ? "expired" :
                        all_inactive ? "inactive" : "pending";
  publish(m_report_var, m_status, m_info, status, "checks=" + detail, refresh);
  if(refresh)
    m_last_result_time = now;
  if(m_snapshot_sequence == 0 || enabled != m_snapshot_active ||
     m_session != m_snapshot_session ||
     snapshotSignature() != m_snapshot_signature ||
     (enabled && now - m_last_snapshot_time >= m_snapshot_interval))
    publishSnapshot(now, enabled);
  AppCastingMOOSApp::PostReport();
  return true;
}

bool uChecks::buildReport()
{
  m_msgs << "Aggregate: " << m_status << "  " << m_info << endl;
  m_msgs << "Condition: " << m_condition.getRawCondition() << endl;
  m_msgs << "Result: " << m_report_var
         << " active refresh=" << m_result_interval << "s (0=change-only)" << endl;
  m_msgs << "Snapshot: " << m_snapshot_var
         << " interval=" << m_snapshot_interval
         << "s sequence=" << m_snapshot_sequence << endl;
  for(const auto& entry : m_checks) {
    const NamedCheck& check = entry.second;
    m_msgs << check.name << ": " << check.status << " (" << check.info << ")"
           << " timeout=" << check.timeout << " max_age="
           << (check.max_age == 0 ? "unlimited" : doubleToStringX(check.max_age, 1))
           << " hold=" << check.success_hold << endl;
  }
  return true;
}
