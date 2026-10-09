#ifndef MAVLINK_SHIP_BRIDGE_HEADER
#define MAVLINK_SHIP_BRIDGE_HEADER

#include <atomic>
#include <string>
#include "MavlinkTcp.h"
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"

// Attachment intent and external deck pose only. No landing-target telemetry.
class MavlinkShipBridge : public AppCastingMOOSApp
{
public:
  ~MavlinkShipBridge() override;
protected:
  bool OnNewMail(MOOSMSG_LIST&) override;
  bool Iterate() override;
  bool OnConnectToServer() override;
  bool OnStartUp() override;
  bool buildReport() override;
private:
  void registerVariables();
  bool sendShipPosition();
  bool shipNavigationFresh() const;

  std::atomic<bool> m_configuration_loaded{false};
  std::string m_ship_nav_prefix = "NAV";
  std::string m_mavlink_host = "127.0.0.1";
  unsigned int m_mavlink_port = 5763;
  unsigned int m_ship_system_id = 17;
  unsigned int m_source_component_id = 191;
  double m_publish_rate_hz = 10;
  double m_input_timeout = 1;
  double m_ship_altitude_msl = 0;
  std::string m_ship_pose_enable_var = "SHIP_POSE_ENABLED";
  std::string m_ship_altitude_var;
  bool m_ship_pose_enabled = false;
  bool m_ship_pose_active = false;
  bool m_have_ship_altitude = false;
  double m_ship_altitude_time = 0;

  mavlink::TcpClient m_link;
  double m_last_connect_attempt = -1;
  double m_last_heartbeat = -1;
  double m_last_publish = -1;
  unsigned long m_ship_messages_sent = 0;

  double m_ship_latitude = 0;
  double m_ship_longitude = 0;
  double m_ship_heading = 0;
  double m_ship_speed = 0;
  double m_ship_latitude_time = 0;
  double m_ship_longitude_time = 0;
  double m_ship_heading_time = 0;
  double m_ship_speed_time = 0;
  bool m_have_ship_latitude = false;
  bool m_have_ship_longitude = false;
  bool m_have_ship_heading = false;
  bool m_have_ship_speed = false;
  double m_previous_pose_time = -1;
  double m_previous_latitude = 0;
  double m_previous_longitude = 0;
  double m_north_velocity = 0;
  double m_east_velocity = 0;
  bool m_have_ship_velocity = false;
};
#endif
