#ifndef MAVLINK_LANDING_TARGET_HEADER
#define MAVLINK_LANDING_TARGET_HEADER

#include "LogicBuffer.h"
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "MOOS/libMOOSGeodesy/MOOSGeodesy.h"
#include "MavlinkTcp.h"
#include <atomic>
#include <string>

// Condition/range-gated observations only. No ship pose, attachment or flight commands.
class MavlinkLandingTarget : public AppCastingMOOSApp
{
public:
  ~MavlinkLandingTarget() override;

protected:
  bool OnNewMail(MOOSMSG_LIST &) override;
  bool Iterate() override;
  bool OnStartUp() override;
  bool OnConnectToServer() override;
  bool buildReport() override;

private:
  struct Sample
  {
    double value = 0;
    double time = 0;
    bool known = false;
    void receive(const CMOOSMsg &);
    bool fresh(double now, double timeout) const;
  };
  void registerVariables();
  bool geometryFresh() const;
  void updateTargetGeometry();
  bool sendLandingTarget();
  void publishVisuals();

  std::atomic<bool> m_configuration_loaded{false};
  std::string m_target_nav_prefix = "TARGET_NAV";
  std::string m_vehicle_nav_prefix = "UAV_NAV";
  std::string m_target_altitude_var;
  std::string m_mavlink_host = "127.0.0.1";
  unsigned int m_mavlink_port = 5763;
  unsigned int m_target_system_id = 1;
  unsigned int m_source_component_id = 191;
  unsigned int m_target_number = 0;
  double m_publish_rate_hz = 10;
  double m_input_timeout = 1;
  double m_detection_range = 12;
  double m_target_altitude_msl = 0;
  Sample m_target_lat, m_target_lon, m_target_alt;
  Sample m_target_x, m_target_y, m_target_heading;
  CMOOSGeodesy m_geodesy;
  bool m_origin_valid = false;
  bool m_orientation_valid = false;
  double m_relative_heading = 0;
  bool m_show_visuals = false;
  Sample m_vehicle_lat, m_vehicle_lon, m_vehicle_yaw, m_vehicle_alt;
  InfoBuffer m_condition_info;
  LogicBuffer m_conditions;
  std::set<std::string> m_condition_vars;
  bool m_conditions_valid = true;
  bool m_conditions_met = true;

  mavlink::TcpClient m_link;
  double m_last_connect_attempt = -1;
  double m_last_heartbeat = -1;
  double m_last_publish = -1;
  unsigned long m_target_messages_sent = 0;
  bool m_target_visible = false;
  bool m_target_active = false;
  double m_target_distance = 0;
  double m_target_forward = 0;
  double m_target_right = 0;
  double m_target_down = 0;
};
#endif
