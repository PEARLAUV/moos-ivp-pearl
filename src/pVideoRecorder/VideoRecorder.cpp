#include "VideoRecorder.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <netdb.h>
#include <set>
#include <sstream>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "ACTable.h"
#include "MBUtils.h"

using namespace std;

namespace
{

const size_t MAX_HTTP_RESPONSE = 1024 * 1024;

bool connectWithTimeout(int fd, const struct sockaddr *address, socklen_t address_length,
                        double timeout)
{
  const int original_flags = fcntl(fd, F_GETFL, 0);
  if (original_flags < 0 || fcntl(fd, F_SETFL, original_flags | O_NONBLOCK) < 0)
    return false;

  int result = connect(fd, address, address_length);
  if (result < 0 && errno != EINPROGRESS)
    return false;

  if (result < 0) {
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(fd, &write_fds);

    struct timeval tv;
    tv.tv_sec = static_cast<int>(timeout);
    tv.tv_usec = static_cast<int>((timeout - tv.tv_sec) * 1000000.0);
    result = select(fd + 1, NULL, &write_fds, NULL, &tv);
    if (result <= 0)
      return false;

    int socket_error = 0;
    socklen_t error_length = sizeof(socket_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_length) < 0 ||
        socket_error != 0) {
      errno = socket_error;
      return false;
    }
  }

  return fcntl(fd, F_SETFL, original_flags) == 0;
}

bool sendAll(int fd, const string &data)
{
  size_t sent = 0;
  while (sent < data.size()) {
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    const ssize_t count = send(fd, data.data() + sent, data.size() - sent, flags);
    if (count <= 0)
      return false;
    sent += static_cast<size_t>(count);
  }
  return true;
}

bool setPositiveDouble(double &target, const string &value)
{
  double parsed = 0;
  if (!setDoubleOnString(parsed, value) || parsed <= 0)
    return false;
  target = parsed;
  return true;
}

bool validMediaDuration(const string &value)
{
  if (value.empty())
    return false;
  size_t index = 0;
  bool digit_seen = false;
  bool decimal_seen = false;
  while (index < value.size() && (isdigit(static_cast<unsigned char>(value[index])) ||
                                  value[index] == '.')) {
    if (value[index] == '.') {
      if (decimal_seen)
        return false;
      decimal_seen = true;
    } else {
      digit_seen = true;
    }
    ++index;
  }
  if (!digit_seen || index == 0 || value[index - 1] == '.' ||
      atof(value.substr(0, index).c_str()) <= 0)
    return false;
  const string unit = value.substr(index);
  return unit == "ns" || unit == "us" || unit == "ms" || unit == "s" || unit == "m" ||
         unit == "h";
}

} // namespace

VideoRecorder::VideoRecorder()
    : m_api_port("9997"), m_proxy_rtsp_address("127.0.0.1:8554"),
      m_command_var("VIDEO_RECORD_COMMAND"),
      m_logger_directory_var("LOGGER_DIRECTORY"), m_prefix("VIDEO_RECORD"), m_state("IDLE"),
      m_path_index(0), m_http_timeout(2.0), m_valid(true), m_logger_received(false),
      m_recording_id_received(true), m_cleanup_after_start_failure(false),
      m_api_reachable(false), m_last_http_status(0), m_start_count(0), m_stop_count(0),
      m_recording_sequence(0)
{}

bool VideoRecorder::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();

  STRING_LIST params;
  vector<string> config_errors;
  m_MissionReader.EnableVerbatimQuoting(false);
  if (!m_MissionReader.GetConfiguration(GetAppName(), params)) {
    config_errors.push_back("missing ProcessConfig block");
    m_valid = false;
  }

  set<string> path_names;
  set<string> api_path_names;
  for (const string &original : params) {
    string line = original;
    const string key = tolower(stripBlankEnds(biteStringX(line, '=')));
    const string value = stripBlankEnds(line);
    bool handled = true;

    if (key == "api_url")
      m_api_url = value;
    else if (key == "api_user")
      m_api_user = value;
    else if (key == "api_password_file")
      m_api_password_file = value;
    else if (key == "record_root")
      m_record_root = trimTrailingSlashes(value);
    else if (key == "record_host")
      m_record_host = value;
    else if (key == "segment_duration") {
      handled = validMediaDuration(value);
      if (handled)
        m_segment_duration = value;
    } else if (key == "path") {
      PathState path;
      const size_t separator = value.find(':');
      path.name = stripBlankEnds(value.substr(0, separator));
      path.api_name = separator == string::npos
                          ? path.name
                          : stripBlankEnds(value.substr(separator + 1));
      path.status = "IDLE";
      handled = validDirectoryName(path.name) && !path.api_name.empty() &&
                path.api_name.find_first_of(" \t\r\n") == string::npos &&
                path_names.insert(path.name).second &&
                api_path_names.insert(path.api_name).second;
      if (handled)
        m_paths.push_back(path);
    } else if (key == "activation") {
      string activation = value;
      const string path_name = stripBlankEnds(biteStringX(activation, ':'));
      activation = stripBlankEnds(activation);
      handled = !path_name.empty() && !activation.empty() && activation[0] == '/' &&
                m_activation_commands.emplace(path_name, activation).second;
    } else if (key == "record_proxy") {
      string proxy = value;
      const string path_name = stripBlankEnds(biteStringX(proxy, ':'));
      proxy = stripBlankEnds(proxy);
      handled = validDirectoryName(path_name) && validDirectoryName(proxy) &&
                m_record_proxies.emplace(path_name, proxy).second;
    } else if (key == "proxy_rtsp_address") {
      handled = !value.empty() && value.find_first_of("/ \t\r\n") == string::npos &&
                value.find("://") == string::npos;
      if (handled)
        m_proxy_rtsp_address = value;
    } else if (key == "command_var")
      handled = setNonWhiteVarOnString(m_command_var, value);
    else if (key == "logger_directory_var")
      handled = setNonWhiteVarOnString(m_logger_directory_var, value);
    else if (key == "recording_id_var")
      handled = setNonWhiteVarOnString(m_recording_id_var, value);
    else if (key == "recording_id_prefix") {
      handled = validDirectoryName(value) && tolower(value) != "none";
      if (handled)
        m_recording_id_prefix = value;
    } else if (key == "status_prefix")
      handled = setNonWhiteVarOnString(m_prefix, value);
    else if (key == "http_timeout")
      handled = setPositiveDouble(m_http_timeout, value);
    else if (key != "apptick" && key != "commstick" && key != "term_report_interval")
      handled = false;

    if (!handled) {
      reportUnhandledConfigWarning(original);
      config_errors.push_back("invalid setting: " + original);
      m_valid = false;
    }
  }

  string error;
  if (m_api_url.empty() || !parseApiUrl(error)) {
    const string detail = error.empty() ? "api_url is required" : error;
    reportConfigWarning(detail);
    config_errors.push_back(detail);
    m_valid = false;
  }
  if (m_record_root.empty() || m_record_root[0] != '/') {
    const string detail = "record_root must be an absolute path on the MediaMTX host";
    reportConfigWarning(detail);
    config_errors.push_back(detail);
    m_valid = false;
  }
  if (m_record_host.empty())
    m_record_host = m_api_host;
  if (m_record_host.empty() || m_record_host.find_first_of(" \t\r\n,") != string::npos) {
    const string detail = "record_host must be a nonempty host name without whitespace or commas";
    reportConfigWarning(detail);
    config_errors.push_back(detail);
    m_valid = false;
  }
  if (!m_recording_id_var.empty() && !m_recording_id_prefix.empty()) {
    const string detail = "recording_id_var and recording_id_prefix are mutually exclusive";
    reportConfigWarning(detail);
    config_errors.push_back(detail);
    m_valid = false;
  }
  m_recording_id_received = m_recording_id_var.empty();
  if (m_paths.empty()) {
    const string detail = "At least one MediaMTX path is required";
    reportConfigWarning(detail);
    config_errors.push_back(detail);
    m_valid = false;
  }
  for (const auto &activation : m_activation_commands) {
    auto path = find_if(m_paths.begin(), m_paths.end(), [&](const PathState &candidate) {
      return candidate.name == activation.first;
    });
    if (path == m_paths.end()) {
      const string detail = "activation references unknown path: " + activation.first;
      reportConfigWarning(detail);
      config_errors.push_back(detail);
      m_valid = false;
    } else {
      path->activation_command = activation.second;
    }
  }
  set<string> proxy_path_names;
  for (const auto &proxy : m_record_proxies) {
    auto path = find_if(m_paths.begin(), m_paths.end(), [&](const PathState &candidate) {
      return candidate.name == proxy.first;
    });
    string detail;
    if (path == m_paths.end())
      detail = "record_proxy references unknown path: " + proxy.first;
    else if (proxy.second == path->api_name)
      detail = "record_proxy must differ from source path: " + proxy.first;
    else if (!proxy_path_names.insert(proxy.second).second || api_path_names.count(proxy.second) != 0)
      detail = "record_proxy path is not unique: " + proxy.second;
    else if (!path->activation_command.empty())
      detail = "record_proxy and activation are mutually exclusive: " + proxy.first;
    else
      path->proxy_api_name = proxy.second;
    if (!detail.empty()) {
      reportConfigWarning(detail);
      config_errors.push_back(detail);
      m_valid = false;
    }
  }
  if (!loadPassword(error)) {
    reportConfigWarning(error);
    config_errors.push_back(error);
    m_valid = false;
  }
  if (m_api_user.empty() != m_api_password_file.empty()) {
    const string detail = "api_user and api_password_file must be configured together";
    reportConfigWarning(detail);
    config_errors.push_back(detail);
    m_valid = false;
  }

  registerVariables();
  if (!m_valid) {
    m_last_error.clear();
    for (const string &detail : config_errors) {
      if (!m_last_error.empty())
        m_last_error += "; ";
      m_last_error += detail;
    }
    if (m_last_error.empty())
      m_last_error = "invalid configuration";
    setState("ERROR");
    Notify(m_prefix + "_LAST_ERROR", m_last_error);
  } else {
    setState("IDLE");
  }
  publishConfiguration();
  return true;
}

bool VideoRecorder::OnConnectToServer()
{
  registerVariables();
  setState(m_state);
  publishConfiguration();
  Notify(m_prefix + "_LAST_ERROR", m_last_error);
  if (!m_log_directory_name.empty())
    Notify(m_prefix + "_LOG_DIRECTORY", m_log_directory_name);
  if (!m_recording_id_latched.empty())
    Notify(m_prefix + "_ID", m_recording_id_latched);
  if (!m_record_directory.empty()) {
    Notify(m_prefix + "_DIRECTORY", m_record_directory);
    Notify(m_prefix + "_LOCATION", "host=" + m_record_host + ",path=" + m_record_directory);
  }
  return true;
}

void VideoRecorder::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  Register(m_command_var, 0);
  Register(m_logger_directory_var, 0);
  if (!m_recording_id_var.empty())
    Register(m_recording_id_var, 0);
}

bool VideoRecorder::OnNewMail(MOOSMSG_LIST &mail)
{
  AppCastingMOOSApp::OnNewMail(mail);
  for (auto &msg : mail) {
    const string key = msg.GetKey();
    if (key == m_logger_directory_var) {
      if (!msg.IsString()) {
        reportRunWarning(m_logger_directory_var + " must be a string");
        continue;
      }
      m_logger_directory = msg.GetString();
      m_logger_received = !stripBlankEnds(m_logger_directory).empty();
      Notify(m_prefix + "_LOGGER_DIRECTORY_INPUT", m_logger_directory);
      if (m_state == "WAITING_LOGGER" && m_logger_received && m_recording_id_received)
        beginStart();
    } else if (!m_recording_id_var.empty() && key == m_recording_id_var) {
      if (!msg.IsString()) {
        reportRunWarning(m_recording_id_var + " must be a string");
        continue;
      }
      m_recording_id = stripBlankEnds(msg.GetString());
      m_recording_id_received = validDirectoryName(m_recording_id) &&
                                tolower(m_recording_id) != "none";
      Notify(m_prefix + "_ID_INPUT", m_recording_id);
      if (m_state == "WAITING_LOGGER" && m_logger_received && m_recording_id_received)
        beginStart();
    } else if (key == m_command_var) {
      if (!msg.IsString()) {
        reportRunWarning(m_command_var + " must be a string");
        publishResult("UNKNOWN", "FAILED", "command must be a string");
        continue;
      }
      handleCommand(msg.GetString());
    } else if (key != "APPCAST_REQ") {
      reportRunWarning("Unhandled Mail: " + key);
    }
  }
  return true;
}

bool VideoRecorder::Iterate()
{
  AppCastingMOOSApp::Iterate();
  if (m_state == "STARTING" || m_state == "STOPPING")
    processOperation();
  AppCastingMOOSApp::PostReport();
  return true;
}

void VideoRecorder::handleCommand(const string &raw_command)
{
  const string command = toupper(stripBlankEnds(raw_command));
  if (command == "START")
    beginStart();
  else if (command == "STOP")
    beginStop();
  else {
    m_last_error = "unsupported command: " + raw_command;
    reportRunWarning(m_last_error);
    publishResult(command.empty() ? "EMPTY" : command, "FAILED", m_last_error);
  }
}

void VideoRecorder::beginStart()
{
  if (!m_valid) {
    publishResult("START", "FAILED", "invalid configuration");
    return;
  }
  if (m_state == "RECORDING") {
    publishResult("START", "ACCEPTED", "already recording");
    return;
  }
  if (m_state == "STARTING") {
    publishResult("START", "ACCEPTED", "start already pending");
    return;
  }
  if (m_state == "STOPPING") {
    publishResult("START", "FAILED", "stop in progress");
    return;
  }
  if (!m_logger_received || !m_recording_id_received) {
    m_operation = "START";
    setState("WAITING_LOGGER");
    string waiting_for;
    if (!m_logger_received)
      waiting_for = m_logger_directory_var;
    if (!m_recording_id_received) {
      if (!waiting_for.empty())
        waiting_for += "+";
      waiting_for += m_recording_id_var;
    }
    publishResult("START", "PENDING", waiting_for);
    return;
  }

  string error;
  if (!prepareRecordingDirectory(error)) {
    m_last_error = error;
    setState("ERROR");
    publishResult("START", "FAILED", error);
    return;
  }

  m_operation = "START";
  m_terminal_error.clear();
  m_last_error.clear();
  m_cleanup_after_start_failure = false;
  m_path_index = 0;
  for (auto &path : m_paths) {
    path.status = "PENDING";
    path.http_status = 0;
  }
  setState("STARTING");
  publishResult("START", "SUBMITTED", m_log_directory_name);
}

void VideoRecorder::beginStop()
{
  if (!m_valid) {
    publishResult("STOP", "FAILED", "invalid configuration");
    return;
  }
  if (m_state == "STOPPING") {
    publishResult("STOP", "ACCEPTED", "stop already pending");
    return;
  }
  m_operation = "STOP";
  m_terminal_error.clear();
  m_last_error.clear();
  m_cleanup_after_start_failure = false;
  m_path_index = 0;
  for (auto &path : m_paths)
    path.status = "STOP_PENDING";
  setState("STOPPING");
  publishResult("STOP", "SUBMITTED");
}

void VideoRecorder::processOperation()
{
  if (m_path_index >= m_paths.size()) {
    finishOperation();
    return;
  }

  PathState &path = m_paths[m_path_index];
  const bool starting = m_state == "STARTING";
  HttpResponse response;
  const bool success = patchPath(path, starting, response);
  path.http_status = response.status;
  m_last_http_status = response.status;
  m_api_reachable = response.error.empty();
  Notify(m_prefix + "_API_REACHABLE", m_api_reachable ? 1.0 : 0.0);
  Notify(m_prefix + "_LAST_HTTP_STATUS", static_cast<double>(response.status));

  if (success) {
    path.status = starting ? "RECORDING" : "STOPPED";
    ++m_path_index;
    if (m_path_index >= m_paths.size())
      finishOperation();
    return;
  }

  string detail = response.error;
  if (detail.empty()) {
    detail = "HTTP " + intToString(response.status);
    if (!response.body.empty())
      detail += ": " + response.body.substr(0, 240);
  }
  path.status = "ERROR";
  m_last_error = path.name + ": " + detail;
  if (!m_terminal_error.empty())
    m_terminal_error += "; ";
  m_terminal_error += m_last_error;

  if (starting) {
    m_cleanup_after_start_failure = true;
    m_operation = "START";
    m_path_index = 0;
    for (auto &cleanup_path : m_paths)
      if (cleanup_path.status != "ERROR")
        cleanup_path.status = "STOP_PENDING";
    setState("STOPPING");
  } else {
    ++m_path_index;
    if (m_path_index >= m_paths.size())
      finishOperation();
  }
}

void VideoRecorder::finishOperation()
{
  if (m_state == "STARTING") {
    ++m_start_count;
    setState("RECORDING");
    publishResult("START", "ACCEPTED", m_log_directory_name);
    return;
  }

  ++m_stop_count;
  if (m_terminal_error.empty()) {
    setState("STOPPED");
    publishResult(m_cleanup_after_start_failure ? "START" : "STOP",
                  m_cleanup_after_start_failure ? "FAILED" : "ACCEPTED",
                  m_cleanup_after_start_failure ? "start rolled back" : "");
  } else {
    setState("ERROR");
    publishResult(m_cleanup_after_start_failure ? "START" : "STOP", "FAILED", m_terminal_error);
  }
  m_cleanup_after_start_failure = false;
}

void VideoRecorder::setState(const string &state)
{
  m_state = state;
  Notify(m_prefix + "_STATE", state);
  Notify(m_prefix + "ING", state == "RECORDING" ? 1.0 : 0.0);
}

void VideoRecorder::publishConfiguration()
{
  string paths;
  for (const auto &path : m_paths) {
    if (!paths.empty())
      paths += ",";
    paths += path.name;
  }
  Notify(m_prefix + "_STREAMS", paths);
  Notify(m_prefix + "_API_URL", m_api_url);
  Notify(m_prefix + "_HOST", m_record_host);
  if (!m_segment_duration.empty())
    Notify(m_prefix + "_SEGMENT_DURATION", m_segment_duration);
}

void VideoRecorder::publishResult(const string &action, const string &result, const string &detail)
{
  string value = "action=" + action + ",result=" + result;
  if (!detail.empty())
    value += ",detail=" + detail;
  Notify(m_prefix + "_COMMAND_RESULT", value);
  Notify(m_prefix + "_HOST", m_record_host);
  if (!m_log_directory_name.empty())
    Notify(m_prefix + "_LOG_DIRECTORY", m_log_directory_name);
  if (!m_recording_id_latched.empty())
    Notify(m_prefix + "_ID", m_recording_id_latched);
  if (!m_record_directory.empty()) {
    Notify(m_prefix + "_DIRECTORY", m_record_directory);
    Notify(m_prefix + "_LOCATION", "host=" + m_record_host + ",path=" + m_record_directory);
  }
  Notify(m_prefix + "_LAST_ERROR", m_last_error);
}

bool VideoRecorder::prepareRecordingDirectory(string &error)
{
  const string name = directoryBasename(m_logger_directory);
  if (!validDirectoryName(name)) {
    error = "invalid " + m_logger_directory_var + " basename";
    return false;
  }
  m_log_directory_name = name;
  m_recording_id_latched.clear();
  m_record_directory = m_record_root + "/" + name;
  if (!m_recording_id_var.empty()) {
    if (!m_recording_id_received || !validDirectoryName(m_recording_id)) {
      error = "invalid " + m_recording_id_var;
      return false;
    }
    m_recording_id_latched = m_recording_id;
    m_record_directory += "/" + m_recording_id_latched;
  } else if (!m_recording_id_prefix.empty()) {
    ++m_recording_sequence;
    ostringstream generated_id;
    generated_id << m_recording_id_prefix << "-" << setfill('0') << setw(3)
                 << m_recording_sequence;
    m_recording_id_latched = generated_id.str();
    m_record_directory += "/" + m_recording_id_latched;
  }
  Notify(m_prefix + "_LOG_DIRECTORY", m_log_directory_name);
  Notify(m_prefix + "_HOST", m_record_host);
  if (!m_recording_id_latched.empty())
    Notify(m_prefix + "_ID", m_recording_id_latched);
  Notify(m_prefix + "_DIRECTORY", m_record_directory);
  Notify(m_prefix + "_LOCATION", "host=" + m_record_host + ",path=" + m_record_directory);
  return true;
}

bool VideoRecorder::patchPath(const PathState &path, bool record, HttpResponse &response)
{
  const string record_pattern =
      m_record_directory + "/" + path.name + "/%path_%Y-%m-%d_%H-%M-%S-%f";

  // A MediaMTX configuration PATCH destroys and recreates the affected path,
  // interrupting its publishers and WebRTC readers. A recording proxy avoids
  // touching the live path: it reads the live path over loopback and is added
  // only for the lifetime of the recording.
  if (!path.proxy_api_name.empty()) {
    const string encoded_proxy = urlEncode(path.proxy_api_name);
    const string delete_target =
        m_api_base_path + "/v3/config/paths/delete/" + encoded_proxy;
    if (!record) {
      response = request("DELETE", delete_target, "");
      return response.error.empty() &&
             ((response.status >= 200 && response.status < 300) || response.status == 404);
    }

    // Remove a proxy left by a crashed prior run. Missing proxies are normal.
    response = request("DELETE", delete_target, "");
    if (!response.error.empty() ||
        !((response.status >= 200 && response.status < 300) || response.status == 404))
      return false;

    const string source =
        "rtsp://" + m_proxy_rtsp_address + "/" + urlEncode(path.api_name);
    string body = "{\"source\":\"" + jsonEscape(source) +
                  "\",\"sourceOnDemand\":false,\"recordPath\":\"" +
                  jsonEscape(record_pattern) + "\",\"record\":true";
    if (!m_segment_duration.empty())
      body += ",\"recordSegmentDuration\":\"" + jsonEscape(m_segment_duration) + "\"";
    body += "}";
    const string add_target = m_api_base_path + "/v3/config/paths/add/" + encoded_proxy;
    response = request("POST", add_target, body);
    return response.error.empty() && response.status >= 200 && response.status < 300;
  }

  const string target = m_api_base_path + "/v3/config/paths/patch/" + urlEncode(path.api_name);
  string body;
  if (record) {
    body = "{\"recordPath\":\"" + jsonEscape(record_pattern) + "\",\"record\":true";
    if (!m_segment_duration.empty())
      body += ",\"recordSegmentDuration\":\"" + jsonEscape(m_segment_duration) + "\"";
    if (!path.activation_command.empty())
      body += ",\"runOnInit\":\"" + jsonEscape(path.activation_command) +
              "\",\"runOnInitRestart\":true";
    body += "}";
  } else {
    body = "{\"record\":false";
    if (!path.activation_command.empty())
      body += ",\"runOnInit\":\"\"";
    body += "}";
  }
  response = request("PATCH", target, body);
  return response.error.empty() && response.status >= 200 && response.status < 300;
}

bool VideoRecorder::loadPassword(string &error)
{
  if (m_api_password_file.empty())
    return true;

  ifstream input(m_api_password_file.c_str());
  if (!input) {
    error = "Unable to open api_password_file";
    return false;
  }
  getline(input, m_api_password);
  if (!m_api_password.empty() && m_api_password.back() == '\r')
    m_api_password.pop_back();
  if (m_api_password.empty()) {
    error = "api_password_file is empty";
    return false;
  }
  return true;
}

bool VideoRecorder::parseApiUrl(string &error)
{
  const string scheme = "http://";
  string remainder = m_api_url;
  if (strBegins(remainder, scheme))
    remainder = remainder.substr(scheme.size());
  else if (remainder.find("://") != string::npos) {
    error = "api_url supports HTTP only (Tailscale provides transport encryption)";
    return false;
  }
  const size_t slash = remainder.find('/');
  string authority = slash == string::npos ? remainder : remainder.substr(0, slash);
  m_api_base_path = slash == string::npos ? "" : remainder.substr(slash);
  m_api_base_path = trimTrailingSlashes(m_api_base_path);
  if (authority.empty()) {
    error = "api_url has no host";
    return false;
  }

  if (authority[0] == '[') {
    const size_t close = authority.find(']');
    if (close == string::npos) {
      error = "api_url has invalid IPv6 authority";
      return false;
    }
    m_api_host = authority.substr(1, close - 1);
    if (close + 1 < authority.size()) {
      if (authority[close + 1] != ':' || close + 2 >= authority.size()) {
        error = "api_url has invalid port";
        return false;
      }
      m_api_port = authority.substr(close + 2);
    }
  } else {
    const size_t colon = authority.rfind(':');
    if (colon != string::npos) {
      m_api_host = authority.substr(0, colon);
      m_api_port = authority.substr(colon + 1);
    } else {
      m_api_host = authority;
    }
  }

  unsigned int port = 0;
  if (m_api_host.empty() || !setUIntOnString(port, m_api_port) || port == 0 || port > 65535) {
    error = "api_url has invalid host or port";
    return false;
  }
  return true;
}

VideoRecorder::HttpResponse VideoRecorder::request(const string &method, const string &target,
                                                   const string &body) const
{
  HttpResponse response;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *addresses = NULL;
  const int lookup = getaddrinfo(m_api_host.c_str(), m_api_port.c_str(), &hints, &addresses);
  if (lookup != 0) {
    response.error = "DNS lookup failed: " + string(gai_strerror(lookup));
    return response;
  }

  int fd = -1;
  for (struct addrinfo *address = addresses; address; address = address->ai_next) {
    fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (fd < 0)
      continue;
    if (connectWithTimeout(fd, address->ai_addr, address->ai_addrlen, m_http_timeout))
      break;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(addresses);

  if (fd < 0) {
    response.error = "connection failed: " + string(strerror(errno));
    return response;
  }

  struct timeval timeout;
  timeout.tv_sec = static_cast<int>(m_http_timeout);
  timeout.tv_usec = static_cast<int>((m_http_timeout - timeout.tv_sec) * 1000000.0);
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  ostringstream request_stream;
  request_stream << method << " " << target << " HTTP/1.1\r\n"
                 << "Host: " << m_api_host << ":" << m_api_port << "\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Accept: application/json\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n";
  if (!m_api_user.empty())
    request_stream << "Authorization: Basic " << base64Encode(m_api_user + ":" + m_api_password)
                   << "\r\n";
  request_stream << "\r\n" << body;

  if (!sendAll(fd, request_stream.str())) {
    response.error = "request send failed: " + string(strerror(errno));
    close(fd);
    return response;
  }

  string raw;
  char buffer[4096];
  while (raw.size() < MAX_HTTP_RESPONSE) {
    const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
    if (count == 0)
      break;
    if (count < 0) {
      response.error = "response read failed: " + string(strerror(errno));
      close(fd);
      return response;
    }
    raw.append(buffer, static_cast<size_t>(count));
  }
  close(fd);

  if (raw.size() >= MAX_HTTP_RESPONSE) {
    response.error = "HTTP response exceeded size limit";
    return response;
  }

  const size_t line_end = raw.find("\r\n");
  if (line_end == string::npos) {
    response.error = "invalid HTTP response";
    return response;
  }
  istringstream status_line(raw.substr(0, line_end));
  string version;
  status_line >> version >> response.status;
  if (!strBegins(version, "HTTP/") || response.status <= 0) {
    response.error = "invalid HTTP status line";
    return response;
  }
  const size_t body_start = raw.find("\r\n\r\n");
  if (body_start != string::npos)
    response.body = raw.substr(body_start + 4);
  return response;
}

string VideoRecorder::trimTrailingSlashes(const string &path)
{
  string result = stripBlankEnds(path);
  while (result.size() > 1 && result.back() == '/')
    result.pop_back();
  return result;
}

string VideoRecorder::directoryBasename(const string &path)
{
  const string clean = trimTrailingSlashes(path);
  const size_t slash = clean.find_last_of('/');
  return slash == string::npos ? clean : clean.substr(slash + 1);
}

bool VideoRecorder::validDirectoryName(const string &name)
{
  if (name.empty() || name == "." || name == "..")
    return false;
  for (const unsigned char c : name)
    if (!isalnum(c) && c != '.' && c != '_' && c != '-')
      return false;
  return true;
}

string VideoRecorder::urlEncode(const string &value)
{
  ostringstream output;
  output << uppercase << hex << setfill('0');
  for (const unsigned char c : value) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      output << static_cast<char>(c);
    else
      output << '%' << setw(2) << static_cast<unsigned int>(c);
  }
  return output.str();
}

string VideoRecorder::jsonEscape(const string &value)
{
  ostringstream output;
  for (const unsigned char c : value) {
    switch (c) {
    case '\\':
      output << "\\\\";
      break;
    case '"':
      output << "\\\"";
      break;
    case '\b':
      output << "\\b";
      break;
    case '\f':
      output << "\\f";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      if (c < 0x20)
        output << "\\u" << hex << setw(4) << setfill('0') << static_cast<unsigned int>(c) << dec;
      else
        output << static_cast<char>(c);
    }
  }
  return output.str();
}

string VideoRecorder::base64Encode(const string &value)
{
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  string output;
  unsigned int accumulator = 0;
  int bits = -6;
  for (const unsigned char c : value) {
    accumulator = (accumulator << 8) | c;
    bits += 8;
    while (bits >= 0) {
      output.push_back(alphabet[(accumulator >> bits) & 0x3f]);
      bits -= 6;
    }
  }
  if (bits > -6)
    output.push_back(alphabet[((accumulator << 8) >> (bits + 8)) & 0x3f]);
  while (output.size() % 4)
    output.push_back('=');
  return output;
}

bool VideoRecorder::buildReport()
{
  m_msgs << "Configuration" << endl;
  m_msgs << "  API URL:          " << m_api_url << endl;
  m_msgs << "  Record root:      " << m_record_root << endl;
  m_msgs << "  Record host:      " << m_record_host << endl;
  m_msgs << "  Segment duration: "
         << (m_segment_duration.empty() ? "server default" : m_segment_duration) << endl;
  m_msgs << "  Proxy RTSP:       " << m_proxy_rtsp_address << endl;
  m_msgs << "  Command variable: " << m_command_var << endl;
  m_msgs << "  Logger variable:  " << m_logger_directory_var << endl;
  m_msgs << "  Recording ID var: "
         << (m_recording_id_var.empty() ? "disabled" : m_recording_id_var) << endl;
  m_msgs << "  Auto ID prefix:   "
         << (m_recording_id_prefix.empty() ? "disabled" : m_recording_id_prefix) << endl;
  m_msgs << "  HTTP timeout:     " << doubleToStringX(m_http_timeout, 2) << endl;
  m_msgs << "  Authentication:   " << (m_api_user.empty() ? "none" : m_api_user) << endl << endl;

  m_msgs << "Session" << endl;
  m_msgs << "  State:            " << m_state << endl;
  m_msgs << "  Operation:        " << (m_operation.empty() ? "none" : m_operation) << endl;
  m_msgs << "  Logger directory: " << (m_logger_received ? m_logger_directory : "n/a") << endl;
  m_msgs << "  Latched log ID:   " << (m_log_directory_name.empty() ? "n/a" : m_log_directory_name)
         << endl;
  m_msgs << "  Recording ID:     "
         << (m_recording_id_latched.empty() ? "n/a" : m_recording_id_latched) << endl;
  m_msgs << "  Record directory: " << (m_record_directory.empty() ? "n/a" : m_record_directory)
         << endl;
  m_msgs << "  API reachable:    " << (m_api_reachable ? "true" : "false") << endl;
  m_msgs << "  Last HTTP status: " << m_last_http_status << endl;
  m_msgs << "  Starts / stops:   " << m_start_count << " / " << m_stop_count << endl;
  m_msgs << "  Last error:       " << (m_last_error.empty() ? "none" : m_last_error) << endl
         << endl;

  ACTable table(5);
  table << "Camera | Source path | Record path | State | HTTP";
  table.addHeaderLines();
  for (const auto &path : m_paths)
    table << path.name << path.api_name
          << (path.proxy_api_name.empty() ? path.api_name : path.proxy_api_name)
          << path.status << path.http_status;
  m_msgs << table.getFormattedString();
  return true;
}
