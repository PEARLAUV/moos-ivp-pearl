#include "MavlinkLandingTarget.h"
#include "MBUtils.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <utility>

using namespace mavlink;
namespace
{
constexpr double PI_VALUE = 3.14159265358979323846;
constexpr uint8_t MAV_FRAME_BODY_FRD = 12;
constexpr uint8_t LANDING_TARGET_TYPE_VISION_FIDUCIAL = 2;
} // namespace

MavlinkLandingTarget::~MavlinkLandingTarget()
{
  m_Comms.Close();
}

void MavlinkLandingTarget::Sample::receive(const CMOOSMsg &msg)
{
  value = msg.GetDouble();
  time = msg.GetTime();
  known = msg.IsDouble() && std::isfinite(value);
}

bool MavlinkLandingTarget::Sample::fresh(double now, double timeout) const
{
  return known && now - time <= timeout;
}

bool MavlinkLandingTarget::OnNewMail(MOOSMSG_LIST &mail)
{
  AppCastingMOOSApp::OnNewMail(mail);
  for (const CMOOSMsg &msg : mail) {
    const std::string key = msg.GetKey();
    const bool condition_input = m_condition_vars.count(key) > 0;
    if (condition_input) {
      if (msg.IsDouble())
        m_conditions.updateInfoBuffer(key, msg.GetDouble());
      else if (msg.IsString())
        m_conditions.updateInfoBuffer(key, msg.GetString());
    }
    if (key == m_target_nav_prefix + "_LAT")
      m_target_lat.receive(msg);
    else if (key == m_target_nav_prefix + "_LONG")
      m_target_lon.receive(msg);
    else if (key == m_target_nav_prefix + "_X")
      m_target_x.receive(msg);
    else if (key == m_target_nav_prefix + "_Y")
      m_target_y.receive(msg);
    else if (key == m_target_nav_prefix + "_HEADING")
      m_target_heading.receive(msg);
    else if (key == m_vehicle_nav_prefix + "_LAT")
      m_vehicle_lat.receive(msg);
    else if (key == m_vehicle_nav_prefix + "_LONG")
      m_vehicle_lon.receive(msg);
    else if (key == m_vehicle_nav_prefix + "_HEADING")
      m_vehicle_yaw.receive(msg);
    else if (key == m_vehicle_nav_prefix + "_ALTITUDE_MSL")
      m_vehicle_alt.receive(msg);
    else if (!m_target_altitude_var.empty() && key == m_target_altitude_var)
      m_target_alt.receive(msg);
    else if (key != "APPCAST_REQ" && !condition_input)
      reportRunWarning("Unhandled Mail: " + key);
  }
  return true;
}

bool MavlinkLandingTarget::Iterate()
{
  AppCastingMOOSApp::Iterate();
  m_conditions.setCurrTime(m_curr_time);
  m_conditions_met = m_conditions_valid && m_conditions.checkConditions();
  if (!m_link.connected() &&
      (m_last_connect_attempt < 0 || m_curr_time - m_last_connect_attempt >= 1)) {
    m_last_connect_attempt = m_curr_time;
    if (m_link.connectTo(m_mavlink_host, m_mavlink_port)) {
      m_last_heartbeat = -1;
      m_last_publish = -1;
      reportEvent("Connected to landing-target endpoint");
    }
  }
  m_link.drain();
  if (m_link.connected()) {
    if (m_last_heartbeat < 0 || m_curr_time - m_last_heartbeat >= 1) {
      // Advertise the vision component, never a ship or an autopilot.
      if (m_link.send(HEARTBEAT, heartbeat(18), m_target_system_id, m_source_component_id))
        m_last_heartbeat = m_curr_time;
    }
  }
  if (m_last_publish < 0 || m_curr_time - m_last_publish >= 1.0 / m_publish_rate_hz) {
    m_last_publish = m_curr_time;
    updateTargetGeometry();
    m_target_active =
        m_conditions_met && m_link.connected() && m_target_visible && sendLandingTarget();
  }
  if (!geometryFresh())
    m_target_visible = false;
  if (!m_conditions_met || !m_link.connected() || !m_target_visible)
    m_target_active = false;
  // Viewer geometry is independent of link health, vehicle freshness and range.
  publishVisuals();
  Notify("LANDMARK_EMULATOR_CONDITIONS_MET", m_conditions_met ? 1.0 : 0.0);
  Notify("MAVLINK_LANDING_TARGET_CONNECTED", m_link.connected() ? 1.0 : 0.0);
  Notify("LANDMARK_EMULATOR_ACTIVE", m_target_active ? 1.0 : 0.0);
  if (geometryFresh())
    Notify("LANDMARK_EMULATOR_DISTANCE", m_target_distance);
  AppCastingMOOSApp::PostReport();
  return true;
}

bool MavlinkLandingTarget::OnConnectToServer()
{
  registerVariables();
  return true;
}

bool MavlinkLandingTarget::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();
  m_conditions.setInfoBuffer(&m_condition_info);
  STRING_LIST lines;
  if (!m_MissionReader.GetConfiguration(GetAppName(), lines))
    reportConfigWarning("No config block found for " + GetAppName());
  for (std::string line : lines) {
    const std::string original = line;
    const std::string param = tolower(biteStringX(line, '='));
    const std::string value = stripBlankEnds(line);
    bool handled = false;
    if (param == "condition") {
      handled = m_conditions.addNewCondition(value);
      if (!handled)
        m_conditions_valid = false; // An invalid gate must never fail open.
    } else if (param == "show_visuals")
      handled = setBooleanOnString(m_show_visuals, value);
    else if (param == "target_nav_prefix")
      handled = setNonWhiteVarOnString(m_target_nav_prefix, value);
    else if (param == "vehicle_nav_prefix")
      handled = setNonWhiteVarOnString(m_vehicle_nav_prefix, value);
    else if (param == "target_altitude_var")
      handled = setNonWhiteVarOnString(m_target_altitude_var, value);
    else if (param == "mavlink_host")
      handled = setNonWhiteVarOnString(m_mavlink_host, value);
    else if (param == "mavlink_port" || param == "target_system_id" ||
             param == "source_component_id" || param == "target_number") {
      int parsed = 0;
      const int maximum = param == "mavlink_port" ? 65535 : 255;
      const int minimum = param == "target_number" ? 0 : 1;
      handled = setIntOnString(parsed, value) && parsed >= minimum && parsed <= maximum;
      if (handled) {
        if (param == "mavlink_port")
          m_mavlink_port = parsed;
        else if (param == "target_system_id")
          m_target_system_id = parsed;
        else if (param == "source_component_id")
          m_source_component_id = parsed;
        else
          m_target_number = parsed;
      }
    } else if ((param == "publish_rate_hz" || param == "input_timeout" ||
                param == "detection_range" || param == "target_altitude_msl") &&
               isNumber(value)) {
      const double parsed = std::atof(value.c_str());
      handled =
          std::isfinite(parsed) &&
          (param == "target_altitude_msl" ? (parsed >= -1000 && parsed <= 10000) : parsed > 0);
      if (handled) {
        if (param == "publish_rate_hz")
          m_publish_rate_hz = parsed;
        else if (param == "input_timeout")
          m_input_timeout = parsed;
        else if (param == "detection_range")
          m_detection_range = parsed;
        else
          m_target_altitude_msl = parsed;
      }
    }
    if (!handled)
      reportUnhandledConfigWarning(original);
  }
  double lat = 0, lon = 0;
  m_origin_valid = m_MissionReader.GetValue("LatOrigin", lat) &&
                   m_MissionReader.GetValue("LongOrigin", lon) && std::isfinite(lat) &&
                   std::isfinite(lon) && std::fabs(lat) < 90 && std::fabs(lon) <= 180 &&
                   m_geodesy.Initialise(lat, lon);
  m_condition_vars = m_conditions.getAllVarsSet();
  m_configuration_loaded.store(true);
  registerVariables();
  return true;
}

void MavlinkLandingTarget::registerVariables()
{
  if (!m_configuration_loaded.load())
    return;
  AppCastingMOOSApp::RegisterVariables();
  for (const auto &key : m_condition_vars)
    Register(key, 0);
  Register(m_target_nav_prefix + "_LAT", 0);
  Register(m_target_nav_prefix + "_LONG", 0);
  Register(m_target_nav_prefix + "_HEADING", 0);
  if (m_show_visuals) {
    Register(m_target_nav_prefix + "_X", 0);
    Register(m_target_nav_prefix + "_Y", 0);
  }
  Register(m_vehicle_nav_prefix + "_LAT", 0);
  Register(m_vehicle_nav_prefix + "_LONG", 0);
  Register(m_vehicle_nav_prefix + "_HEADING", 0);
  Register(m_vehicle_nav_prefix + "_ALTITUDE_MSL", 0);
  if (!m_target_altitude_var.empty())
    Register(m_target_altitude_var, 0);
}

bool MavlinkLandingTarget::geometryFresh() const
{
  const double altitude =
      m_target_altitude_var.empty() ? m_target_altitude_msl : m_target_alt.value;
  return m_target_lat.fresh(m_curr_time, m_input_timeout) &&
         m_target_lon.fresh(m_curr_time, m_input_timeout) &&
         m_vehicle_lat.fresh(m_curr_time, m_input_timeout) &&
         m_vehicle_lon.fresh(m_curr_time, m_input_timeout) &&
         m_vehicle_yaw.fresh(m_curr_time, m_input_timeout) &&
         m_vehicle_alt.fresh(m_curr_time, m_input_timeout) &&
         (m_target_altitude_var.empty() || m_target_alt.fresh(m_curr_time, m_input_timeout)) &&
         std::fabs(m_target_lat.value) <= 90 && std::fabs(m_vehicle_lat.value) <= 90 &&
         std::fabs(m_target_lon.value) <= 180 && std::fabs(m_vehicle_lon.value) <= 180 &&
         std::isfinite(altitude) && altitude >= -1000 && altitude <= 10000;
}

void MavlinkLandingTarget::updateTargetGeometry()
{
  m_target_visible = false;
  m_orientation_valid = false;
  if (!geometryFresh())
    return;
  const double altitude =
      m_target_altitude_var.empty() ? m_target_altitude_msl : m_target_alt.value;
  const double mean_lat = (m_target_lat.value + m_vehicle_lat.value) * PI_VALUE / 360.0;
  const double north = (m_target_lat.value - m_vehicle_lat.value) * PI_VALUE / 180.0 * 6378137.0;
  const double east = (m_target_lon.value - m_vehicle_lon.value) * PI_VALUE / 180.0 * 6378137.0 *
                      std::cos(mean_lat);
  const double yaw = wrapHeading(m_vehicle_yaw.value) * PI_VALUE / 180.0;
  m_target_forward = std::cos(yaw) * north + std::sin(yaw) * east;
  m_target_right = -std::sin(yaw) * north + std::cos(yaw) * east;
  m_target_down = m_vehicle_alt.value - altitude;
  m_target_distance = std::sqrt(m_target_forward * m_target_forward +
                                m_target_right * m_target_right + m_target_down * m_target_down);
  m_target_visible = m_target_distance <= m_detection_range;

  // Target NAV_HEADING is local-grid heading; native vehicle yaw is true north.
  // Convert grid -> true -> BODY_FRD before encoding the target quaternion.
  if (m_origin_valid && m_target_heading.fresh(m_curr_time, m_input_timeout) &&
      m_target_lat.value < 89.9999) {
    double north0 = 0, east0 = 0, north1 = 0, east1 = 0;
    if (m_geodesy.LatLong2LocalUTM(m_target_lat.value, m_target_lon.value, north0, east0) &&
        m_geodesy.LatLong2LocalUTM(m_target_lat.value + 1e-4, m_target_lon.value, north1, east1)) {
      const double grid_north = std::atan2(east1 - east0, north1 - north0) * 180 / PI_VALUE;
      m_relative_heading = wrapHeading(m_target_heading.value - grid_north - m_vehicle_yaw.value);
      m_orientation_valid = true;
    }
  }
}

bool MavlinkLandingTarget::sendLandingTarget()
{
  const float angle_x = std::atan2(m_target_right, std::max(std::fabs(m_target_down), 1.0e-6));
  const float angle_y = std::atan2(m_target_forward, std::max(std::fabs(m_target_down), 1.0e-6));

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
  // All-zero quaternion means orientation unavailable, not a fabricated
  // body-aligned target. Position observations remain usable without heading.
  const double half_yaw = m_relative_heading * PI_VALUE / 360;
  appendFloat(payload, m_orientation_valid ? static_cast<float>(std::cos(half_yaw)) : 0);
  appendFloat(payload, 0);
  appendFloat(payload, 0);
  appendFloat(payload, m_orientation_valid ? static_cast<float>(std::sin(half_yaw)) : 0);
  appendUint8(payload, LANDING_TARGET_TYPE_VISION_FIDUCIAL);
  appendUint8(payload, 1);

  // Vision is a component of the UAV system. Keeping it on that system also
  // lets MAVSDK's UAV MavlinkPassthrough expose the native target observations.
  const bool sent = m_link.send(LANDING_TARGET, payload, static_cast<uint8_t>(m_target_system_id),
                                static_cast<uint8_t>(m_source_component_id));
  if (sent)
    ++m_target_messages_sent;
  return sent;
}

void MavlinkLandingTarget::publishVisuals()
{
  if (!m_show_visuals)
    return;
  // Never invent a target at (0,0) before its coordinates arrive. Once known,
  // retain the target display even with stale geometry or no UAV observations.
  const bool active = m_conditions_met && m_target_x.known && m_target_y.known;
  const std::string center =
      "x=" + doubleToStringX(m_target_x.value, 3) + ",y=" + doubleToStringX(m_target_y.value, 3);
  const std::string style = ",edge_size=2,vertex_size=0,label_color=invisible";
  Notify("VIEW_CIRCLE", center + ",radius=" + doubleToStringX(m_detection_range, 3) +
                            ",label=landing_target_range,active=" + (active ? "true" : "false") +
                            style + ",edge_color=lime");
  const double altitude =
      m_target_altitude_var.empty() ? m_target_altitude_msl : m_target_alt.value;
  double projected = 0;
  if (m_vehicle_alt.known && (m_target_altitude_var.empty() || m_target_alt.known)) {
    const double down = m_vehicle_alt.value - altitude;
    projected = std::sqrt(std::max(0.0, m_detection_range * m_detection_range - down * down));
  }
  Notify("VIEW_CIRCLE", center + ",radius=" + doubleToStringX(std::max(0.001, projected), 3) +
                            ",label=landing_target_slice,active=" + (active ? "true" : "false") +
                            style + ",edge_color=yellow");

  Notify("VIEW_POINT", center +
                           ",label=landing_target_point,active=" + (active ? "true" : "false") +
                           ",vertex_color=lime,vertex_size=8,label_color=invisible");
}

bool MavlinkLandingTarget::buildReport()
{
  const auto label = [this](const std::string &name) {
    m_msgs << std::right << std::setw(22) << name << ": ";
  };
  const auto sample = [this, &label](const std::string &name, const Sample &input,
                                     const std::string &units, int precision = 2) {
    label(name);
    if (!input.known)
      m_msgs << "missing\n";
    else
      m_msgs << std::fixed << std::setprecision(precision) << input.value << units << " ("
             << (input.fresh(m_curr_time, m_input_timeout) ? "fresh" : "stale") << ")\n";
  };
  const bool geometry_fresh = geometryFresh();
  const std::string status = !m_conditions_met     ? "conditions not met"
                             : !m_link.connected() ? "disconnected"
                             : !geometry_fresh     ? "waiting for fresh geometry"
                             : !m_target_visible   ? "outside detection range"
                             : m_target_active     ? "streaming"
                                                   : "waiting for successful send";

  m_msgs << "Streaming\n----------------------\n";
  label("Status");
  m_msgs << status << '\n';
  label("Conditions");
  m_msgs << (!m_conditions_valid        ? "invalid (blocked)"
             : m_conditions.size() == 0 ? "met (none configured)"
             : m_conditions_met         ? "met"
                                        : "not met")
         << '\n';
  const auto conditions = m_conditions.getSpec();
  for (size_t i = 0; i < conditions.size(); ++i) {
    label("Condition " + std::to_string(i + 1));
    m_msgs << conditions[i] << '\n';
  }
  if (m_conditions_valid && !m_conditions_met) {
    label("Blocking condition");
    m_msgs << m_conditions.getNotableCondition() << '\n';
  }
  label("MAVLink endpoint");
  m_msgs << m_mavlink_host << ':' << m_mavlink_port << '\n';
  label("Connected");
  m_msgs << (m_link.connected() ? "yes" : "no") << '\n';
  label("Broadcasting");
  m_msgs << (m_target_active ? "yes" : "no") << '\n';
  label("Target messages");
  m_msgs << m_target_messages_sent << '\n';
  label("Publish rate");
  m_msgs << std::fixed << std::setprecision(2) << m_publish_rate_hz << " Hz\n";
  label("System / component");
  m_msgs << m_target_system_id << " / " << m_source_component_id << '\n';
  label("Target / frame");
  m_msgs << m_target_number << " / BODY_FRD (12)\n";
  label("Target orientation");
  m_msgs << (m_orientation_valid ? "grid heading encoded in quaternion" : "unavailable") << '\n';
  label("Target relative yaw");
  if (m_orientation_valid)
    m_msgs << m_relative_heading << " deg (BODY_FRD)\n";
  else
    m_msgs << "-- (requires fresh heading and origin)\n";

  m_msgs << "\nTarget geometry\n----------------------\n";
  label("Geometry");
  m_msgs << (geometry_fresh ? "fresh" : "missing, stale or invalid") << '\n';
  label("Detection range");
  m_msgs << m_detection_range << " m (3-D)\n";
  label("Within range");
  m_msgs << (geometry_fresh ? (m_target_visible ? "yes" : "no") : "unknown") << '\n';
  for (const auto &entry :
       {std::make_pair("Distance", m_target_distance), std::make_pair("Forward", m_target_forward),
        std::make_pair("Right", m_target_right), std::make_pair("Down", m_target_down)}) {
    label(entry.first);
    if (geometry_fresh)
      m_msgs << entry.second << " m\n";
    else
      m_msgs << "-- (no fresh geometry)\n";
  }

  m_msgs << "\nInput data\n----------------------\n";
  label("Target nav prefix");
  m_msgs << m_target_nav_prefix << '\n';
  sample("Target latitude", m_target_lat, " deg", 7);
  sample("Target longitude", m_target_lon, " deg", 7);
  sample("Target yaw", m_target_heading, " deg (grid)");
  if (m_target_altitude_var.empty()) {
    label("Target altitude");
    m_msgs << std::fixed << std::setprecision(2) << m_target_altitude_msl << " m MSL (fixed)\n";
  } else {
    label("Altitude input");
    m_msgs << m_target_altitude_var << '\n';
    sample("Target altitude", m_target_alt, " m MSL");
  }
  label("Vehicle nav prefix");
  m_msgs << m_vehicle_nav_prefix << '\n';
  sample("Vehicle latitude", m_vehicle_lat, " deg", 7);
  sample("Vehicle longitude", m_vehicle_lon, " deg", 7);
  sample("Vehicle yaw", m_vehicle_yaw, " deg");
  sample("Vehicle altitude", m_vehicle_alt, " m MSL");
  label("Input timeout");
  m_msgs << std::fixed << std::setprecision(2) << m_input_timeout << " s\n";
  label("Viewer shapes");
  m_msgs << (m_show_visuals ? "enabled" : "disabled") << '\n';
  return true;
}
