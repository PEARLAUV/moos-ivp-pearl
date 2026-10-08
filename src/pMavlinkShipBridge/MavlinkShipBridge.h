/************************************************************/
/*    FILE: MavlinkShipBridge.h                             */
/************************************************************/

#ifndef MAVLINK_SHIP_BRIDGE_HEADER
#define MAVLINK_SHIP_BRIDGE_HEADER

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"

class MavlinkShipBridge : public AppCastingMOOSApp
{
public:
  MavlinkShipBridge();
  ~MavlinkShipBridge() override;

protected:
  bool OnNewMail(MOOSMSG_LIST &new_mail) override;
  bool Iterate() override;
  bool OnConnectToServer() override;
  bool OnStartUp() override;
  bool buildReport() override;

private:
  void registerVariables();
  bool connectMavlink();
  void closeMavlink();
  void drainMavlink();
  bool sendMessage(uint32_t message_id, const std::vector<uint8_t> &payload,
                   uint8_t system_id, uint8_t component_id);
  bool sendHeartbeat();
  bool sendShipPosition();
  bool sendLandingTarget();
  bool shipNavigationFresh() const;
  bool uavNavigationFresh() const;
  void updateTargetGeometry();

  static uint16_t mavlinkChecksum(const std::vector<uint8_t> &data);
  static void appendUint8(std::vector<uint8_t> &payload, uint8_t value);
  static void appendUint16(std::vector<uint8_t> &payload, uint16_t value);
  static void appendInt16(std::vector<uint8_t> &payload, int16_t value);
  static void appendUint32(std::vector<uint8_t> &payload, uint32_t value);
  static void appendInt32(std::vector<uint8_t> &payload, int32_t value);
  static void appendUint64(std::vector<uint8_t> &payload, uint64_t value);
  static void appendFloat(std::vector<uint8_t> &payload, float value);

private: // Configuration
  std::atomic<bool> m_configuration_loaded{false};
  std::string m_ship_nav_prefix = "NAV";
  std::string m_mavlink_host;
  unsigned int m_mavlink_port;
  unsigned int m_ship_system_id;
  unsigned int m_source_component_id;
  unsigned int m_target_system_id = 1;
  unsigned int m_target_number;
  double m_publish_rate_hz;
  double m_input_timeout;
  double m_detection_range;
  double m_ship_altitude_msl;
  std::string m_ship_pose_enable_var = "SHIP_POSE_ENABLED";
  std::string m_ship_altitude_var;
  bool m_ship_pose_enabled = false;
  bool m_ship_pose_active = false;
  bool m_have_ship_altitude = false;
  double m_ship_altitude_time = 0;

private: // Network state
  int m_socket_fd;
  uint8_t m_sequence;
  double m_last_connect_attempt;
  double m_last_heartbeat;
  double m_last_publish;
  unsigned long m_ship_messages_sent;
  unsigned long m_target_messages_sent;

private: // PEARL navigation state
  double m_ship_latitude;
  double m_ship_longitude;
  double m_ship_heading;
  double m_ship_speed;
  double m_ship_latitude_time;
  double m_ship_longitude_time;
  double m_ship_heading_time;
  double m_ship_speed_time;
  bool m_have_ship_latitude;
  bool m_have_ship_longitude;
  bool m_have_ship_heading;
  bool m_have_ship_speed;
  double m_previous_pose_time = -1;
  double m_previous_latitude = 0;
  double m_previous_longitude = 0;
  double m_north_velocity = 0;
  double m_east_velocity = 0;
  bool m_have_ship_velocity = false;

private: // UAV and target state
  double m_uav_latitude = 0;
  double m_uav_longitude = 0;
  double m_uav_latitude_time = 0;
  double m_uav_longitude_time = 0;
  bool m_have_uav_latitude = false;
  bool m_have_uav_longitude = false;
  double m_uav_heading;
  double m_uav_altitude;
  double m_uav_heading_time;
  double m_uav_altitude_time;
  bool m_have_uav_heading;
  bool m_have_uav_altitude;
  bool m_target_visible;
  double m_target_distance;
  double m_target_forward;
  double m_target_right;
  double m_target_down;
};

#endif
