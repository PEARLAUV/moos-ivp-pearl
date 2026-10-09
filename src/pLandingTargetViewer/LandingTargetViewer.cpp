#include "LandingTargetViewer.h"
#include "ColorParse.h"
#include "MBUtils.h"
#include "MOOS/libMOOS/Utils/MOOSUtilityFunctions.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>

namespace
{
constexpr double PI_VALUE = 3.14159265358979323846;
constexpr double EARTH_RADIUS_METRES = 6378137.0;
const std::array<std::string, 15> TARGET_FIELDS = {
    "AVAILABLE", "AGE",      "POSITION_VALID", "FRAME",  "X",   "Y",
    "Z",         "DISTANCE", "SIZE_X",         "SIZE_Y", "Q_W", "Q_X",
    "Q_Y",       "Q_Z",      "TIME_USEC"};

// Quaternion maps target-local axes into the reported BODY_FRD frame.
std::array<double, 3> rotate(const std::array<double, 4> &q, double x, double y)
{
  const double w = q[0], a = q[1], b = q[2], c = q[3];
  return {{(1 - 2 * (b * b + c * c)) * x + 2 * (a * b - w * c) * y,
           2 * (a * b + w * c) * x + (1 - 2 * (a * a + c * c)) * y,
           2 * (a * c - w * b) * x + 2 * (b * c + w * a) * y}};
}
} // namespace

LandingTargetViewer::~LandingTargetViewer()
{
  m_Comms.Close();
}

bool LandingTargetViewer::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();
  STRING_LIST lines;
  m_MissionReader.GetConfiguration(GetAppName(), lines);
  bool config_ok = true;
  for (std::string line : lines) {
    const std::string original = line;
    const std::string key = tolower(biteStringX(line, '='));
    const std::string data = stripBlankEnds(line);
    bool ok = false;
    if (key == "target_prefix")
      ok = setNonWhiteVarOnString(m_target_prefix, data);
    else if (key == "vehicle_prefix")
      ok = setNonWhiteVarOnString(m_vehicle_prefix, data);
    else if (key == "label")
      ok = setNonWhiteVarOnString(m_label, data);
    else if (key == "color") {
      ok = isColor(data);
      if (ok)
        m_color = data;
    } else if (key == "use_received_size")
      ok = setBooleanOnString(m_use_received_size, data);
    else if (key == "observation_timeout")
      ok = setPosDoubleOnString(m_observation_timeout, data);
    else if (key == "input_timeout")
      ok = setPosDoubleOnString(m_input_timeout, data);
    else if (key == "target_size")
      ok = setPosDoubleOnString(m_target_size, data);
    else if (key == "fallback_heading")
      ok = setDoubleOnString(m_fallback_heading, data);
    else if (key == "apptick" || key == "commstick")
      ok = true;
    if (!ok) {
      reportUnhandledConfigWarning(original);
      config_ok = false;
    }
  }
  double lat = 0, lon = 0;
  const bool origin_ok = m_MissionReader.GetValue("LatOrigin", lat) &&
                         m_MissionReader.GetValue("LongOrigin", lon) && std::isfinite(lat) &&
                         std::isfinite(lon) && std::fabs(lat) < 90 && std::fabs(lon) <= 180 &&
                         m_geodesy.Initialise(lat, lon);
  if (!origin_ok)
    reportConfigWarning("Require valid LatOrigin/LongOrigin for UTM plotting");
  m_valid = config_ok && origin_ok && std::isfinite(m_target_size) &&
            std::isfinite(m_fallback_heading) && std::isfinite(m_observation_timeout) &&
            std::isfinite(m_input_timeout);
  registerVariables();
  Notify("VIEW_POLYGON", "label=" + m_label + ",active=false");
  return true;
}

bool LandingTargetViewer::OnConnectToServer()
{
  registerVariables();
  return true;
}

void LandingTargetViewer::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  for (const auto &field : TARGET_FIELDS)
    Register(m_target_prefix + "_" + field, 0);
  for (const auto &field : {"LAT", "LON", "YAW"})
    Register(m_vehicle_prefix + "_" + field, 0);
}

bool LandingTargetViewer::OnNewMail(MOOSMSG_LIST &mail)
{
  AppCastingMOOSApp::OnNewMail(mail);
  for (const auto &msg : mail) {
    if (msg.GetKey() == "APPCAST_REQ")
      continue;
    auto &input = m_inputs[msg.GetKey()];
    input.time = msg.GetTime();
    input.value = msg.GetDouble();
    input.known = msg.IsDouble() && std::isfinite(input.value);
    if (msg.IsString() && (msg.GetString() == "true" || msg.GetString() == "false")) {
      input.value = msg.GetString() == "true" ? 1 : 0;
      input.known = true;
    }
  }
  return true;
}

bool LandingTargetViewer::read(const std::string &key, double &value) const
{
  const auto it = m_inputs.find(key);
  if (it == m_inputs.end() || !it->second.known || it->second.time > m_curr_time + 1 ||
      m_curr_time - it->second.time > m_input_timeout)
    return false;
  value = it->second.value;
  return true;
}

bool LandingTargetViewer::project(double lat, double lon, double north, double east, double &x,
                                  double &y)
{
  const double target_lat = lat + north / EARTH_RADIUS_METRES * 180 / PI_VALUE;
  const double cosine = std::cos((lat + target_lat) * PI_VALUE / 360);
  if (std::fabs(cosine) < 1e-6 || std::fabs(target_lat) >= 90)
    return false;
  const double target_lon = lon + east / (EARTH_RADIUS_METRES * cosine) * 180 / PI_VALUE;
  return std::fabs(target_lon) <= 180 && m_geodesy.LatLong2LocalUTM(target_lat, target_lon, y, x);
}

bool LandingTargetViewer::drawTarget()
{
  m_age = -1;
  if (!m_valid) {
    m_status = "invalid configuration";
    return false;
  }
  const auto target = [this](const std::string &field, double &data) {
    return read(m_target_prefix + "_" + field, data);
  };
  double available = 0, age = 0;
  if (!target("AGE", age) || age < 0) {
    m_status = "missing/stale observation age";
    return false;
  }
  // Reposted bridge fields do not imply a new observation. AGE is the actual
  // MAVLink receive age; also expire a frozen AGE if the relay stops updating.
  m_age = age + std::max(0.0, m_curr_time - m_inputs.at(m_target_prefix + "_AGE").time) /
                    std::max(0.001, GetMOOSTimeWarp());
  if (m_age > m_observation_timeout || !target("AVAILABLE", available) || available != 1) {
    m_status = "no recent received target";
    return false;
  }
  double frame = 0, valid = 0, forward = 0, right = 0, down = 0;
  if (!target("FRAME", frame) || frame != 12) {
    m_status = "requires BODY_FRD (12)";
    return false;
  }
  if (!target("POSITION_VALID", valid) || valid != 1 || !target("X", forward) ||
      !target("Y", right) || !target("Z", down)) {
    m_status = "missing/invalid position";
    return false;
  }
  double lat = 0, lon = 0, yaw = 0;
  if (!read(m_vehicle_prefix + "_LAT", lat) || !read(m_vehicle_prefix + "_LON", lon) ||
      !read(m_vehicle_prefix + "_YAW", yaw) || std::fabs(lat) >= 90 || std::fabs(lon) > 180) {
    m_status = "missing/stale vehicle pose";
    return false;
  }
  const double yaw_rad = yaw * PI_VALUE / 180;
  const auto world = [yaw_rad](double x, double y) {
    return std::array<double, 2>{{std::cos(yaw_rad) * x - std::sin(yaw_rad) * y,
                                  std::sin(yaw_rad) * x + std::cos(yaw_rad) * y}};
  };
  const auto center = world(forward, right);
  if (!project(lat, lon, center[0], center[1], m_x, m_y)) {
    m_status = "cannot project target position";
    return false;
  }

  m_width = m_length = m_target_size;
  m_size_source = "configured physical size";
  double size_x = 0, size_y = 0, distance = 0;
  if (m_use_received_size && target("SIZE_X", size_x) && target("SIZE_Y", size_y) &&
      target("DISTANCE", distance) && distance > 0 && size_x > 0 && size_x < PI_VALUE &&
      size_y > 0 && size_y < PI_VALUE) {
    const double width = 2 * distance * std::tan(size_x / 2);
    const double length = 2 * distance * std::tan(size_y / 2);
    if (std::isfinite(width) && std::isfinite(length) && width <= 1000 && length <= 1000) {
      m_width = width;
      m_length = length;
      m_size_source = "received angular size (estimate)";
    }
  }
  std::array<double, 4> q{};
  bool quaternion =
      target("Q_W", q[0]) && target("Q_X", q[1]) && target("Q_Y", q[2]) && target("Q_Z", q[3]);
  double norm = 0;
  for (const auto value : q)
    norm += value * value;
  quaternion = quaternion && std::isfinite(norm) && norm > 1e-12;
  if (quaternion) {
    for (auto &value : q)
      value /= std::sqrt(norm);
  }
  m_orientation_source = quaternion ? "received quaternion" : "configured grid heading";
  const double corners[4][2] = {{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.5}, {-0.5, 0.5}};
  std::string spec = "pts={";
  std::array<std::array<double, 2>, 4> footprint{};
  for (int i = 0; i < 4; ++i) {
    double x = 0, y = 0;
    const double along = corners[i][0] * m_length;
    const double across = corners[i][1] * m_width;
    if (quaternion) {
      const auto body = rotate(q, along, across);
      const auto offset = world(body[0], body[1]);
      if (!project(lat, lon, center[0] + offset[0], center[1] + offset[1], x, y)) {
        m_status = "cannot project target footprint";
        return false;
      }
    } else {
      const double heading = m_fallback_heading * PI_VALUE / 180;
      x = m_x + along * std::sin(heading) + across * std::cos(heading);
      y = m_y + along * std::cos(heading) - across * std::sin(heading);
    }
    footprint[i] = {{x, y}};
    if (i)
      spec += ":";
    spec += doubleToStringX(x, 3) + "," + doubleToStringX(y, 3);
  }
  double area = 0;
  for (int i = 0; i < 4; ++i) {
    const auto &a = footprint[i];
    const auto &b = footprint[(i + 1) % 4];
    area += (a[0] - m_x) * (b[1] - m_y) - (a[1] - m_y) * (b[0] - m_x);
  }
  if (!std::isfinite(area) || std::fabs(area) < 1e-8) {
    m_status = "degenerate projected footprint";
    return false;
  }
  // Project a target-local forward unit vector to report its grid heading.
  if (quaternion) {
    const auto body = rotate(q, 1, 0);
    const auto offset = world(body[0], body[1]);
    double x = 0, y = 0;
    if (project(lat, lon, center[0] + offset[0], center[1] + offset[1], x, y))
      m_heading = std::fmod(std::atan2(x - m_x, y - m_y) * 180 / PI_VALUE + 360, 360);
  } else
    m_heading = std::fmod(std::fmod(m_fallback_heading, 360) + 360, 360);
  Notify("VIEW_POLYGON", spec + "},label=" + m_label + ",active=true,edge_color=" + m_color +
                             ",vertex_size=0,edge_size=2,label_color=invisible,fill_color=" +
                             m_color + ",fill_transparency=0.15");
  m_status = "displaying recent received target";
  return true;
}

bool LandingTargetViewer::Iterate()
{
  AppCastingMOOSApp::Iterate();
  m_active = drawTarget();
  if (!m_active)
    Notify("VIEW_POLYGON", "label=" + m_label + ",active=false");
  Notify("LANDING_TARGET_VIEW_ACTIVE", m_active ? 1.0 : 0.0);
  AppCastingMOOSApp::PostReport();
  return true;
}

bool LandingTargetViewer::buildReport()
{
  const auto label = [this](const std::string &name) {
    m_msgs << std::right << std::setw(22) << name << ": ";
  };
  m_msgs << std::fixed << std::setprecision(2);
  m_msgs << "Received target\n----------------------\n";
  label("Status");
  m_msgs << m_status << '\n';
  label("Target prefix");
  m_msgs << m_target_prefix << '\n';
  label("Vehicle prefix");
  m_msgs << m_vehicle_prefix << '\n';
  label("Observation age");
  if (m_age >= 0)
    m_msgs << m_age << " s (max " << m_observation_timeout << " s)\n";
  else
    m_msgs << "missing\n";
  label("Visible");
  m_msgs << (m_active ? "yes" : "no") << '\n';
  if (m_active) {
    m_msgs << "\nFootprint\n----------------------\n";
    label("Target x / y");
    m_msgs << m_x << " / " << m_y << " m\n";
    label("Width / length");
    m_msgs << m_width << " / " << m_length << " m\n";
    label("Size source");
    m_msgs << m_size_source << '\n';
    label("Grid heading");
    m_msgs << m_heading << " deg\n";
    label("Orientation source");
    m_msgs << m_orientation_source << '\n';
  }
  return true;
}
