/************************************************************/
/*    FILE: MavlinkShipBridge.cpp                           */
/************************************************************/

#include "MavlinkShipBridge.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "MBUtils.h"

namespace
{
constexpr uint8_t MAVLINK_STX_V2 = 0xFD;
constexpr uint32_t MAVLINK_MSG_HEARTBEAT = 0;
constexpr uint32_t MAVLINK_MSG_GLOBAL_POSITION_INT = 33;
constexpr uint32_t MAVLINK_MSG_LANDING_TARGET = 149;
constexpr uint8_t MAV_TYPE_SURFACE_BOAT = 11;
constexpr uint8_t MAV_AUTOPILOT_INVALID = 8;
constexpr uint8_t MAV_STATE_ACTIVE = 4;
constexpr uint8_t MAV_FRAME_BODY_FRD = 12;
constexpr uint8_t LANDING_TARGET_TYPE_VISION_FIDUCIAL = 2;
constexpr double PI_VALUE = 3.14159265358979323846;

uint8_t crcExtra(uint32_t message_id)
{
  switch(message_id) {
  case MAVLINK_MSG_HEARTBEAT:
    return 50;
  case MAVLINK_MSG_GLOBAL_POSITION_INT:
    return 104;
  case MAVLINK_MSG_LANDING_TARGET:
    return 200;
  default:
    return 0;
  }
}

double wrapHeading(double heading)
{
  heading = std::fmod(heading, 360.0);
  return heading < 0 ? heading + 360.0 : heading;
}

uint64_t unixMicros()
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}

uint32_t steadyMillis()
{
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}
} // namespace

MavlinkShipBridge::MavlinkShipBridge()
  : m_mavlink_host("127.0.0.1"),
    m_mavlink_port(5763),
    m_ship_system_id(17),
    m_source_component_id(191),
    m_target_number(0),
    m_publish_rate_hz(10),
    m_input_timeout(1),
    m_detection_range(12),
    m_ship_altitude_msl(0),
    m_socket_fd(-1),
    m_sequence(0),
    m_last_connect_attempt(-1),
    m_last_heartbeat(-1),
    m_last_publish(-1),
    m_ship_messages_sent(0),
    m_target_messages_sent(0),
    m_ship_latitude(0),
    m_ship_longitude(0),
    m_ship_heading(0),
    m_ship_speed(0),
    m_ship_latitude_time(0),
    m_ship_longitude_time(0),
    m_ship_heading_time(0),
    m_ship_speed_time(0),
    m_have_ship_latitude(false),
    m_have_ship_longitude(false),
    m_have_ship_heading(false),
    m_have_ship_speed(false),
    m_uav_heading(0),
    m_uav_altitude(0),
    m_uav_heading_time(0),
    m_uav_altitude_time(0),
    m_have_uav_heading(false),
    m_have_uav_altitude(false),
    m_target_visible(false),
    m_target_distance(0),
    m_target_forward(0),
    m_target_right(0),
    m_target_down(0)
{
}

MavlinkShipBridge::~MavlinkShipBridge()
{
  closeMavlink();
}

bool MavlinkShipBridge::OnNewMail(MOOSMSG_LIST &new_mail)
{
  AppCastingMOOSApp::OnNewMail(new_mail);

  for(const CMOOSMsg &msg : new_mail) {
    const std::string key = msg.GetKey();
    if(key == m_ship_pose_enable_var) {
      const std::string value = msg.IsDouble() ? "" : tolower(msg.GetString());
      const bool valid = msg.IsDouble()
          ? (msg.GetDouble() == 0 || msg.GetDouble() == 1)
          : (value == "true" || value == "false");
      m_ship_pose_enabled = valid &&
          (msg.IsDouble() ? msg.GetDouble() == 1 : value == "true");
      if(!valid)
        reportRunWarning("Invalid boolean on " + m_ship_pose_enable_var);
      if(!m_ship_pose_enabled) {
        m_ship_pose_active = false;
        m_previous_pose_time = -1;
      }
    }
    else if(!m_ship_altitude_var.empty() && key == m_ship_altitude_var) {
      m_ship_altitude_msl = msg.GetDouble();
      m_ship_altitude_time = msg.GetTime();
      m_have_ship_altitude = msg.IsDouble() && std::isfinite(m_ship_altitude_msl);
    }
    else if(key == m_ship_nav_prefix + "_LAT") {
      m_ship_latitude = msg.GetDouble();
      m_ship_latitude_time = msg.GetTime();
      m_have_ship_latitude = msg.IsDouble();
    }
    else if(key == m_ship_nav_prefix + "_LONG") {
      m_ship_longitude = msg.GetDouble();
      m_ship_longitude_time = msg.GetTime();
      m_have_ship_longitude = msg.IsDouble();
    }
    else if(key == m_ship_nav_prefix + "_HEADING") {
      m_ship_heading = msg.GetDouble();
      m_ship_heading_time = msg.GetTime();
      m_have_ship_heading = msg.IsDouble();
    }
    else if(key == m_ship_nav_prefix + "_SPEED") {
      m_ship_speed = msg.GetDouble();
      m_ship_speed_time = msg.GetTime();
      m_have_ship_speed = msg.IsDouble();
    }
    else if(key == "UAV_NAV_LAT") {
      m_uav_latitude = msg.GetDouble();
      m_uav_latitude_time = msg.GetTime();
      m_have_uav_latitude = msg.IsDouble();
    }
    else if(key == "UAV_NAV_LONG") {
      m_uav_longitude = msg.GetDouble();
      m_uav_longitude_time = msg.GetTime();
      m_have_uav_longitude = msg.IsDouble();
    }
    else if(key == "UAV_NAV_HEADING") {
      m_uav_heading = msg.GetDouble();
      m_uav_heading_time = msg.GetTime();
      m_have_uav_heading = msg.IsDouble();
    }
    else if(key == "UAV_NAV_ALTITUDE_MSL") {
      m_uav_altitude = msg.GetDouble();
      m_uav_altitude_time = msg.GetTime();
      m_have_uav_altitude = msg.IsDouble();
    }
    else if(key != "APPCAST_REQ") {
      reportRunWarning("Unhandled Mail: " + key);
    }
  }
  return true;
}

bool MavlinkShipBridge::Iterate()
{
  AppCastingMOOSApp::Iterate();

  if(m_socket_fd < 0 &&
     (m_last_connect_attempt < 0 || m_curr_time - m_last_connect_attempt >= 1)) {
    m_last_connect_attempt = m_curr_time;
    connectMavlink();
  }

  if(m_socket_fd >= 0) {
    drainMavlink();
    if(m_last_heartbeat < 0 || m_curr_time - m_last_heartbeat >= 1) {
      if(sendHeartbeat())
        m_last_heartbeat = m_curr_time;
    }

    const double publish_period = 1.0 / m_publish_rate_hz;
    if(m_last_publish < 0 || m_curr_time - m_last_publish >= publish_period) {
      m_last_publish = m_curr_time;
      m_ship_pose_active = m_ship_pose_enabled && shipNavigationFresh() &&
                           sendShipPosition();
      updateTargetGeometry();
      if(m_target_visible)
        sendLandingTarget();
    }
  }
  else {
    m_target_visible = false;
    m_ship_pose_active = false;
  }
  if(!m_ship_pose_enabled || !shipNavigationFresh())
    m_ship_pose_active = false;

  Notify("MAVLINK_SHIP_POSE_ACTIVE", m_ship_pose_active ? 1.0 : 0.0);
  Notify("MAVLINK_SHIP_BRIDGE_CONNECTED", m_socket_fd >= 0 ? 1.0 : 0.0);
  Notify("LANDMARK_EMULATOR_ACTIVE", m_target_visible ? 1.0 : 0.0);
  if(uavNavigationFresh())
    Notify("LANDMARK_EMULATOR_DISTANCE", m_target_distance);

  AppCastingMOOSApp::PostReport();
  return true;
}

bool MavlinkShipBridge::OnConnectToServer()
{
  registerVariables();
  return true;
}

bool MavlinkShipBridge::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();

  STRING_LIST config_lines;
  if(!m_MissionReader.GetConfiguration(GetAppName(), config_lines))
    reportConfigWarning("No config block found for " + GetAppName());

  for(std::string line : config_lines) {
    const std::string original = line;
    const std::string param = tolower(biteStringX(line, '='));
    const std::string value = stripBlankEnds(line);
    bool handled = false;

    if(param == "ship_nav_prefix")
      handled = setNonWhiteVarOnString(m_ship_nav_prefix, value);
    else if(param == "ship_pose_enable_var")
      handled = setNonWhiteVarOnString(m_ship_pose_enable_var, value);
    else if(param == "ship_altitude_var")
      handled = setNonWhiteVarOnString(m_ship_altitude_var, value);
    else if(param == "mavlink_host")
      handled = setNonWhiteVarOnString(m_mavlink_host, value);
    else if(param == "mavlink_port") {
      int parsed = 0;
      handled = setIntOnString(parsed, value) && parsed > 0 && parsed <= 65535;
      if(handled)
        m_mavlink_port = static_cast<unsigned int>(parsed);
    }
    else if(param == "ship_system_id") {
      int parsed = 0;
      handled = setIntOnString(parsed, value) && parsed > 0 && parsed <= 255;
      if(handled)
        m_ship_system_id = static_cast<unsigned int>(parsed);
    }
    else if(param == "source_component_id") {
      int parsed = 0;
      handled = setIntOnString(parsed, value) && parsed > 0 && parsed <= 255;
      if(handled)
        m_source_component_id = static_cast<unsigned int>(parsed);
    }
    else if(param == "target_system_id") {
      int parsed = 0;
      handled = setIntOnString(parsed, value) && parsed > 0 && parsed <= 255;
      if(handled)
        m_target_system_id = static_cast<unsigned int>(parsed);
    }
    else if(param == "target_number") {
      int parsed = 0;
      handled = setIntOnString(parsed, value) && parsed >= 0 && parsed <= 255;
      if(handled)
        m_target_number = static_cast<unsigned int>(parsed);
    }
    else if((param == "publish_rate_hz" || param == "input_timeout" ||
             param == "detection_range" || param == "ship_altitude_msl") &&
            isNumber(value)) {
      const double parsed = std::atof(value.c_str());
      handled = std::isfinite(parsed) &&
                (param == "ship_altitude_msl" || parsed > 0);
      if(handled) {
        if(param == "publish_rate_hz") m_publish_rate_hz = parsed;
        else if(param == "input_timeout") m_input_timeout = parsed;
        else if(param == "detection_range") m_detection_range = parsed;
        else m_ship_altitude_msl = parsed;
      }
    }

    if(!handled)
      reportUnhandledConfigWarning(original);
  }

  m_configuration_loaded.store(true);
  registerVariables();
  return true;
}

void MavlinkShipBridge::registerVariables()
{
  // OnConnectToServer can run before OnStartUp reads configured variable names.
  // Do not leave default NAV_* subscriptions behind in a prefixed community.
  if(!m_configuration_loaded.load())
    return;
  AppCastingMOOSApp::RegisterVariables();
  Register(m_ship_pose_enable_var, 0);
  if(!m_ship_altitude_var.empty())
    Register(m_ship_altitude_var, 0);
  Register(m_ship_nav_prefix + "_LAT", 0);
  Register(m_ship_nav_prefix + "_LONG", 0);
  Register(m_ship_nav_prefix + "_HEADING", 0);
  Register(m_ship_nav_prefix + "_SPEED", 0);
  Register("UAV_NAV_LAT", 0);
  Register("UAV_NAV_LONG", 0);
  Register("UAV_NAV_HEADING", 0);
  Register("UAV_NAV_ALTITUDE_MSL", 0);
}

bool MavlinkShipBridge::connectMavlink()
{
  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *addresses = nullptr;
  const std::string port = std::to_string(m_mavlink_port);
  if(getaddrinfo(m_mavlink_host.c_str(), port.c_str(), &hints, &addresses) != 0)
    return false;

  int connected_socket = -1;
  for(struct addrinfo *address = addresses; address != nullptr;
      address = address->ai_next) {
    connected_socket = socket(address->ai_family, address->ai_socktype,
                              address->ai_protocol);
    if(connected_socket < 0)
      continue;
    const int flags = fcntl(connected_socket, F_GETFL, 0);
    if(flags >= 0 && fcntl(connected_socket, F_SETFL, flags | O_NONBLOCK) == 0) {
      int result = connect(connected_socket, address->ai_addr, address->ai_addrlen);
      if(result < 0 && errno == EINPROGRESS) {
        struct pollfd poll_socket { connected_socket, POLLOUT, 0 };
        if(poll(&poll_socket, 1, 100) > 0) {
          int socket_error = 0;
          socklen_t size = sizeof(socket_error);
          if(getsockopt(connected_socket, SOL_SOCKET, SO_ERROR,
                        &socket_error, &size) == 0 && socket_error == 0)
            result = 0;
        }
      }
      if(result == 0)
        break;
    }
    close(connected_socket);
    connected_socket = -1;
  }
  freeaddrinfo(addresses);

  if(connected_socket < 0)
    return false;

  m_socket_fd = connected_socket;
  m_last_heartbeat = -1;
  m_last_publish = -1;
  reportEvent("Connected to MAVLink endpoint " + m_mavlink_host + ":" + port);
  return true;
}

void MavlinkShipBridge::closeMavlink()
{
  if(m_socket_fd >= 0)
    close(m_socket_fd);
  m_socket_fd = -1;
}

void MavlinkShipBridge::drainMavlink()
{
  uint8_t buffer[4096];
  while(m_socket_fd >= 0) {
    const ssize_t received = recv(m_socket_fd, buffer, sizeof(buffer), MSG_DONTWAIT);
    if(received > 0)
      continue;
    if(received == 0) {
      closeMavlink();
      return;
    }
    if(errno == EAGAIN || errno == EWOULDBLOCK)
      return;
    if(errno == EINTR)
      continue;
    closeMavlink();
    return;
  }
}

bool MavlinkShipBridge::sendMessage(uint32_t message_id,
                                    const std::vector<uint8_t> &payload,
                                    uint8_t system_id,
                                    uint8_t component_id)
{
  if(m_socket_fd < 0 || payload.size() > 255)
    return false;

  std::vector<uint8_t> header;
  header.reserve(9);
  appendUint8(header, static_cast<uint8_t>(payload.size()));
  appendUint8(header, 0);
  appendUint8(header, 0);
  appendUint8(header, m_sequence++);
  appendUint8(header, system_id);
  appendUint8(header, component_id);
  appendUint8(header, static_cast<uint8_t>(message_id & 0xFF));
  appendUint8(header, static_cast<uint8_t>((message_id >> 8) & 0xFF));
  appendUint8(header, static_cast<uint8_t>((message_id >> 16) & 0xFF));

  std::vector<uint8_t> checksum_data = header;
  checksum_data.insert(checksum_data.end(), payload.begin(), payload.end());
  appendUint8(checksum_data, crcExtra(message_id));
  const uint16_t checksum = mavlinkChecksum(checksum_data);

  std::vector<uint8_t> packet;
  packet.reserve(1 + header.size() + payload.size() + 2);
  appendUint8(packet, MAVLINK_STX_V2);
  packet.insert(packet.end(), header.begin(), header.end());
  packet.insert(packet.end(), payload.begin(), payload.end());
  appendUint16(packet, checksum);

  size_t sent_total = 0;
  while(sent_total < packet.size()) {
    const ssize_t sent = send(m_socket_fd, packet.data() + sent_total,
                              packet.size() - sent_total, MSG_NOSIGNAL);
    if(sent > 0) {
      sent_total += static_cast<size_t>(sent);
      continue;
    }
    if(sent < 0 && errno == EINTR)
      continue;
    closeMavlink();
    return false;
  }
  return true;
}

bool MavlinkShipBridge::sendHeartbeat()
{
  std::vector<uint8_t> payload;
  payload.reserve(9);
  appendUint32(payload, 0);
  appendUint8(payload, MAV_TYPE_SURFACE_BOAT);
  appendUint8(payload, MAV_AUTOPILOT_INVALID);
  appendUint8(payload, 0);
  appendUint8(payload, MAV_STATE_ACTIVE);
  appendUint8(payload, 3);
  return sendMessage(MAVLINK_MSG_HEARTBEAT, payload,
                     static_cast<uint8_t>(m_ship_system_id),
                     static_cast<uint8_t>(m_source_component_id));
}

bool MavlinkShipBridge::sendShipPosition()
{
  const double pose_time = std::min(m_ship_latitude_time, m_ship_longitude_time);
  if(m_previous_pose_time < 0 || pose_time - m_previous_pose_time > m_input_timeout) {
    const double heading_rad = wrapHeading(m_ship_heading) * PI_VALUE / 180.0;
    m_north_velocity = m_ship_speed * std::cos(heading_rad);
    m_east_velocity = m_ship_speed * std::sin(heading_rad);
    m_have_ship_velocity = false;
  }
  else if(pose_time > m_previous_pose_time) {
    // Derive geographic velocity from GPS samples, not MOOS grid heading:
    // local map north can differ from true north, and a platform may drift.
    const double dt = pose_time - m_previous_pose_time;
    const double mean_lat = (m_ship_latitude + m_previous_latitude) * PI_VALUE / 360.0;
    const double north = (m_ship_latitude - m_previous_latitude) * PI_VALUE / 180.0 * 6378137.0;
    const double east = (m_ship_longitude - m_previous_longitude) * PI_VALUE / 180.0 *
                        6378137.0 * std::cos(mean_lat);
    const double weight = m_have_ship_velocity ? dt / (0.2 + dt) : 1.0;
    m_north_velocity += weight * (north / dt - m_north_velocity);
    m_east_velocity += weight * (east / dt - m_east_velocity);
    m_have_ship_velocity = true;
  }
  if(pose_time != m_previous_pose_time) {
    m_previous_pose_time = pose_time;
    m_previous_latitude = m_ship_latitude;
    m_previous_longitude = m_ship_longitude;
  }

  const auto velocity_cm_s = [](double speed) {
    return static_cast<int16_t>(std::lround(
        std::max(-327.68, std::min(327.67, speed)) * 100.0));
  };

  std::vector<uint8_t> payload;
  payload.reserve(28);
  appendUint32(payload, steadyMillis());
  appendInt32(payload, static_cast<int32_t>(std::llround(m_ship_latitude * 1.0e7)));
  appendInt32(payload, static_cast<int32_t>(std::llround(m_ship_longitude * 1.0e7)));
  appendInt32(payload, static_cast<int32_t>(std::llround(m_ship_altitude_msl * 1000.0)));
  appendInt32(payload, 0);
  appendInt16(payload, velocity_cm_s(m_north_velocity));
  appendInt16(payload, velocity_cm_s(m_east_velocity));
  appendInt16(payload, 0);
  appendUint16(payload, static_cast<uint16_t>(
      std::lround(wrapHeading(m_ship_heading) * 100.0) % 36000));

  const bool sent = sendMessage(MAVLINK_MSG_GLOBAL_POSITION_INT, payload,
                                static_cast<uint8_t>(m_ship_system_id),
                                static_cast<uint8_t>(m_source_component_id));
  if(sent)
    ++m_ship_messages_sent;
  return sent;
}

bool MavlinkShipBridge::sendLandingTarget()
{
  const float angle_x = std::atan2(m_target_right,
                                   std::max(std::fabs(m_target_down), 1.0e-6));
  const float angle_y = std::atan2(m_target_forward,
                                   std::max(std::fabs(m_target_down), 1.0e-6));

  std::vector<uint8_t> payload;
  payload.reserve(60);
  appendUint64(payload, unixMicros());
  appendFloat(payload, angle_x);
  appendFloat(payload, angle_y);
  appendFloat(payload, static_cast<float>(m_target_distance));
  appendFloat(payload, 0);
  appendFloat(payload, 0);
  appendUint8(payload, static_cast<uint8_t>(m_target_number));
  appendUint8(payload, MAV_FRAME_BODY_FRD);
  appendFloat(payload, static_cast<float>(m_target_forward));
  appendFloat(payload, static_cast<float>(m_target_right));
  appendFloat(payload, static_cast<float>(m_target_down));
  appendFloat(payload, 1);
  appendFloat(payload, 0);
  appendFloat(payload, 0);
  appendFloat(payload, 0);
  appendUint8(payload, LANDING_TARGET_TYPE_VISION_FIDUCIAL);
  appendUint8(payload, 1);

  // Vision is a component of the UAV system. Keeping it on that system also
  // lets MAVSDK's UAV MavlinkPassthrough expose the native target observations.
  const bool sent = sendMessage(MAVLINK_MSG_LANDING_TARGET, payload,
                                static_cast<uint8_t>(m_target_system_id),
                                static_cast<uint8_t>(m_source_component_id));
  if(sent)
    ++m_target_messages_sent;
  return sent;
}

bool MavlinkShipBridge::shipNavigationFresh() const
{
  if(!(m_have_ship_latitude && m_have_ship_longitude && m_have_ship_heading &&
       m_have_ship_speed))
    return false;
  if(!m_ship_altitude_var.empty() &&
     (!m_have_ship_altitude || m_curr_time - m_ship_altitude_time > m_input_timeout))
    return false;
  if(!std::isfinite(m_ship_altitude_msl) || m_ship_altitude_msl < -1000 ||
     m_ship_altitude_msl > 10000 || !std::isfinite(m_ship_latitude) || std::fabs(m_ship_latitude) > 90 ||
     !std::isfinite(m_ship_longitude) || std::fabs(m_ship_longitude) > 180 ||
     !std::isfinite(m_ship_heading) || !std::isfinite(m_ship_speed))
    return false;
  return m_curr_time - m_ship_latitude_time <= m_input_timeout &&
         m_curr_time - m_ship_longitude_time <= m_input_timeout &&
         m_curr_time - m_ship_heading_time <= m_input_timeout &&
         m_curr_time - m_ship_speed_time <= m_input_timeout;
}

bool MavlinkShipBridge::uavNavigationFresh() const
{
  if(!(shipNavigationFresh() && m_have_uav_latitude && m_have_uav_longitude &&
       m_have_uav_heading && m_have_uav_altitude))
    return false;
  if(!std::isfinite(m_uav_latitude) || std::fabs(m_uav_latitude) > 90 ||
     !std::isfinite(m_uav_longitude) || std::fabs(m_uav_longitude) > 180 ||
     !std::isfinite(m_uav_heading) || !std::isfinite(m_uav_altitude))
    return false;
  return m_curr_time - m_uav_latitude_time <= m_input_timeout &&
         m_curr_time - m_uav_longitude_time <= m_input_timeout &&
         m_curr_time - m_uav_heading_time <= m_input_timeout &&
         m_curr_time - m_uav_altitude_time <= m_input_timeout;
}

void MavlinkShipBridge::updateTargetGeometry()
{
  m_target_visible = false;
  if(!uavNavigationFresh())
    return;

  const double mean_lat = (m_ship_latitude + m_uav_latitude) * PI_VALUE / 360.0;
  const double north = (m_ship_latitude - m_uav_latitude) * PI_VALUE / 180.0 * 6378137.0;
  const double east = (m_ship_longitude - m_uav_longitude) * PI_VALUE / 180.0 *
                      6378137.0 * std::cos(mean_lat);
  const double yaw = wrapHeading(m_uav_heading) * PI_VALUE / 180.0;
  m_target_forward = std::cos(yaw) * north + std::sin(yaw) * east;
  m_target_right = -std::sin(yaw) * north + std::cos(yaw) * east;
  m_target_down = m_uav_altitude - m_ship_altitude_msl;
  m_target_distance = std::sqrt(m_target_forward * m_target_forward +
                                m_target_right * m_target_right +
                                m_target_down * m_target_down);
  m_target_visible = m_target_distance <= m_detection_range;
}

uint16_t MavlinkShipBridge::mavlinkChecksum(const std::vector<uint8_t> &data)
{
  uint16_t crc = 0xFFFF;
  for(const uint8_t byte : data) {
    uint8_t tmp = byte ^ static_cast<uint8_t>(crc & 0xFF);
    tmp ^= static_cast<uint8_t>(tmp << 4);
    crc = static_cast<uint16_t>((crc >> 8) ^ (static_cast<uint16_t>(tmp) << 8) ^
                                (static_cast<uint16_t>(tmp) << 3) ^ (tmp >> 4));
  }
  return crc;
}

void MavlinkShipBridge::appendUint8(std::vector<uint8_t> &payload, uint8_t value)
{
  payload.push_back(value);
}

void MavlinkShipBridge::appendUint16(std::vector<uint8_t> &payload, uint16_t value)
{
  payload.push_back(static_cast<uint8_t>(value & 0xFF));
  payload.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void MavlinkShipBridge::appendInt16(std::vector<uint8_t> &payload, int16_t value)
{
  appendUint16(payload, static_cast<uint16_t>(value));
}

void MavlinkShipBridge::appendUint32(std::vector<uint8_t> &payload, uint32_t value)
{
  for(unsigned int shift = 0; shift < 32; shift += 8)
    payload.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
}

void MavlinkShipBridge::appendInt32(std::vector<uint8_t> &payload, int32_t value)
{
  appendUint32(payload, static_cast<uint32_t>(value));
}

void MavlinkShipBridge::appendUint64(std::vector<uint8_t> &payload, uint64_t value)
{
  for(unsigned int shift = 0; shift < 64; shift += 8)
    payload.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
}

void MavlinkShipBridge::appendFloat(std::vector<uint8_t> &payload, float value)
{
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "Unexpected float size");
  std::memcpy(&bits, &value, sizeof(bits));
  appendUint32(payload, bits);
}

bool MavlinkShipBridge::buildReport()
{
  m_msgs << "MAVLink endpoint: " << m_mavlink_host << ':' << m_mavlink_port << '\n';
  m_msgs << "Connected:        " << (m_socket_fd >= 0 ? "yes" : "no") << '\n';
  m_msgs << "Ship pose gate:   " << m_ship_pose_enable_var << " = "
         << (m_ship_pose_enabled ? "true" : "false") << '\n';
  m_msgs << "Ship pose active: " << (m_ship_pose_active ? "yes" : "no") << '\n';
  m_msgs << "Deck altitude:    " << m_ship_altitude_msl << " m MSL\n";
  m_msgs << "Ship navigation:  " << (shipNavigationFresh() ? "fresh" : "stale") << '\n';
  m_msgs << "Ship pose:        " << std::fixed << std::setprecision(7)
         << m_ship_latitude << ", " << m_ship_longitude
         << std::setprecision(2) << ", heading=" << m_ship_heading
         << ", speed=" << m_ship_speed << '\n';
  m_msgs << "Ship messages:    " << m_ship_messages_sent << '\n';
  m_msgs << "Target visible:   " << (m_target_visible ? "yes" : "no") << '\n';
  m_msgs << "Target distance:  " << m_target_distance << " m\n";
  m_msgs << "Target messages:  " << m_target_messages_sent << '\n';
  return true;
}
