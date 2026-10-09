#include "MavlinkShipBridge.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include "MBUtils.h"

using namespace mavlink;
namespace { constexpr double PI_VALUE = 3.14159265358979323846; }

MavlinkShipBridge::~MavlinkShipBridge()
{
  m_Comms.Close();
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
    else if(key != "APPCAST_REQ") {
      reportRunWarning("Unhandled Mail: " + key);
    }
  }
  return true;
}

bool MavlinkShipBridge::Iterate()
{
  AppCastingMOOSApp::Iterate();
  if(!m_link.connected() &&
     (m_last_connect_attempt < 0 || m_curr_time - m_last_connect_attempt >= 1)) {
    m_last_connect_attempt = m_curr_time;
    if(m_link.connectTo(m_mavlink_host, m_mavlink_port)) {
      m_last_heartbeat = -1;
      m_last_publish = -1;
      reportEvent("Connected to attachment endpoint");
    }
  }
  m_link.drain();
  if(m_link.connected()) {
    if(m_last_heartbeat < 0 || m_curr_time - m_last_heartbeat >= 1) {
      if(m_link.send(HEARTBEAT, heartbeat(11), m_ship_system_id, m_source_component_id))
        m_last_heartbeat = m_curr_time;
    }
    if(m_last_publish < 0 || m_curr_time - m_last_publish >= 1.0 / m_publish_rate_hz) {
      m_last_publish = m_curr_time;
      m_ship_pose_active = m_ship_pose_enabled && shipNavigationFresh() && sendShipPosition();
    }
  }
  if(!m_link.connected() || !m_ship_pose_enabled || !shipNavigationFresh())
    m_ship_pose_active = false;
  Notify("MAVLINK_SHIP_POSE_ACTIVE", m_ship_pose_active ? 1.0 : 0.0);
  Notify("MAVLINK_SHIP_BRIDGE_CONNECTED", m_link.connected() ? 1.0 : 0.0);
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
    else if((param == "publish_rate_hz" || param == "input_timeout" ||
             param == "ship_altitude_msl") &&
            isNumber(value)) {
      const double parsed = std::atof(value.c_str());
      handled = std::isfinite(parsed) &&
                (param == "ship_altitude_msl" || parsed > 0);
      if(handled) {
        if(param == "publish_rate_hz") m_publish_rate_hz = parsed;
        else if(param == "input_timeout") m_input_timeout = parsed;
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

  const bool sent = m_link.send(GLOBAL_POSITION_INT, payload,
                                static_cast<uint8_t>(m_ship_system_id),
                                static_cast<uint8_t>(m_source_component_id));
  if(sent)
    ++m_ship_messages_sent;
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

bool MavlinkShipBridge::buildReport()
{
  m_msgs << "MAVLink endpoint: " << m_mavlink_host << ':' << m_mavlink_port << '\n';
  m_msgs << "Connected:        " << (m_link.connected() ? "yes" : "no") << '\n';
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
  return true;
}
