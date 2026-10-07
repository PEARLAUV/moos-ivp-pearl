/************************************************************/
/*    NAME: Charles Benjamin                               */
/*    ORGN: MIT, Cambridge MA                              */
/*    FILE: Rendezvous.h                                   */
/*    DATE: August 2026                                    */
/************************************************************/

#ifndef Rendezvous_HEADER
#define Rendezvous_HEADER

#include <map>
#include <string>
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"

class Rendezvous : public AppCastingMOOSApp
{
 public:
  Rendezvous();
  ~Rendezvous();

 protected: // Standard MOOSApp functions to overload
  bool OnNewMail(MOOSMSG_LIST &NewMail);
  bool Iterate();
  bool OnConnectToServer();
  bool OnStartUp();

 protected: // Standard AppCastingMOOSApp function to overload
  bool buildReport();

 protected:
  void registerVariables();

  bool handleMailStart(const CMOOSMsg& msg);
  bool handleMailAbort(const CMOOSMsg& msg);
  bool handleMailRequest(const CMOOSMsg& msg);
  bool handleMailProposal(const CMOOSMsg& msg);
  bool handleMailResponse(const CMOOSMsg& msg);
  bool handleMailClearance(const CMOOSMsg& msg);
  bool handleMailNodeReport(const CMOOSMsg& msg);
  bool handleMailPrecisionLandRequest(const CMOOSMsg& msg);
  bool handleMailDouble(const CMOOSMsg& msg, double& value,
                        double& value_time);
  bool handleMailBool(const CMOOSMsg& msg, bool& value,
                      bool& value_set);
  bool handleMailTimedBool(const CMOOSMsg& msg, bool& value,
                           bool& value_set, double& value_time);
  int requiredChecksStatus(std::string& reason) const;
  void beginRequiredChecks(const std::string& request,
                           const std::string& id);

  void iterateUAV();
  void iteratePearl();
  bool beginRequest();
  bool proposeRendezvous(const std::string& request);
  bool acceptProposal(const std::string& proposal);
  void activatePearlTransit();
  void checkPearlArrival();
  void checkUAVArrival();
  void grantLandingClearance();
  void beginTargetAcquisition(double pearl_x, double pearl_y);
  void updateTargetAcquisition();
  void routeToPearl(bool force=false);
  void beginPrecisionLanding();
  void savePearlControlState();
  void restorePearlControlState();
  void reconcilePearlWithUAV();
  void beginSecondaryRecovery();
  void updateSecondaryRecovery();
  void publishSecondaryRecoveryState(const std::string& state);
  void abortMission(const std::string& reason, bool notify_peer=true);
  void transitionTo(const std::string& state, const std::string& reason);
  void publishState(bool force=false);
  void publishPoint(bool active);

  bool sendMessage(const std::string& destination,
                   const std::string& variable,
                   const std::string& value);
  bool navIsFresh() const;
  bool peerReportIsFresh() const;
  bool landingTargetIsReady(std::string& reason) const;
  bool landingPlatformIsReady(std::string& reason) const;
  bool uavIsReady(std::string& reason) const;
  std::string recoveryPriority() const;
  bool mailIsTrue(const CMOOSMsg& msg) const;
  std::string makeSessionID() const;
  std::string pointString(double x, double y) const;

 private:
  struct RequiredCheck {
    std::string result;
    std::string state_result;
    std::string session;
  };

 private: // Configuration variables
  std::string m_role;
  std::string m_ownship;
  std::string m_peer_node;
  double m_uav_speed;
  double m_pearl_speed;
  double m_min_battery;
  double m_critical_battery;
  double m_battery_max_age;
  double m_min_pearl_battery;
  double m_max_landing_wind_speed;
  double m_platform_data_max_age;
  double m_nav_stale_thresh;
  double m_request_timeout;
  double m_route_timeout;
  double m_transit_timeout;
  double m_arrival_radius;
  double m_arrival_dwell;
  double m_max_peer_separation;
  double m_landing_target_max_age;
  double m_landing_target_lock_dwell;
  double m_landing_target_acquire_timeout;
  double m_landing_target_max_offset;
  double m_landing_target_max_angle;
  double m_reacquire_update_interval;
  double m_reacquire_update_distance;
  double m_recovery_route_speed;
  double m_recovery_capture_radius;
  double m_secondary_landing_x;
  double m_secondary_landing_y;
  double m_secondary_arrival_radius;
  double m_secondary_arrival_dwell;
  double m_secondary_landing_delay;
  unsigned int m_expected_target_num;
  double m_state_post_interval;
  bool m_require_health;
  bool m_require_battery;
  bool m_require_flight_state;
  bool m_require_landing_target;
  bool m_require_platform_ready;
  bool m_require_platform_health;
  bool m_secondary_landing_enabled;
  std::map<std::string, RequiredCheck> m_required_checks;

 private: // State variables
  std::string m_state;
  std::string m_reason;
  std::string m_session_id;
  std::string m_route_state;
  std::string m_remote_uav_phase;
  std::string m_remote_uav_session;
  std::string m_pending_request;
  double m_state_enter_time;
  double m_last_state_post_time;
  double m_route_command_time;
  double m_route_state_time;
  double m_nav_x;
  double m_nav_y;
  double m_nav_x_time;
  double m_nav_y_time;
  double m_battery;
  double m_battery_time;
  double m_battery_valid_time;
  double m_pearl_battery;
  double m_pearl_battery_time;
  double m_pearl_battery_valid_time;
  double m_pearl_wind_speed;
  double m_pearl_wind_speed_time;
  double m_pearl_wind_valid_time;
  double m_platform_health_time;
  double m_target_x;
  double m_target_y;
  double m_peer_report_x;
  double m_peer_report_y;
  double m_peer_report_time;
  double m_acquisition_start_time;
  double m_target_lock_start_time;
  double m_last_reacquire_route_time;
  double m_last_reacquire_x;
  double m_last_reacquire_y;
  double m_landing_target_available_time;
  double m_landing_target_age;
  double m_landing_target_age_time;
  double m_landing_target_num;
  double m_landing_target_num_time;
  double m_landing_target_position_valid;
  double m_landing_target_position_valid_time;
  double m_landing_target_x;
  double m_landing_target_y;
  double m_landing_target_position_time;
  double m_landing_target_angle_x;
  double m_landing_target_angle_y;
  double m_landing_target_angle_time;
  double m_pearl_activation_time;
  double m_arrival_start_time;
  double m_secondary_state_enter_time;
  double m_secondary_arrival_start_time;
  std::string m_secondary_recovery_state;
  bool m_nav_x_set;
  bool m_nav_y_set;
  bool m_battery_set;
  bool m_battery_valid;
  bool m_battery_valid_set;
  bool m_pearl_battery_set;
  bool m_pearl_battery_valid;
  bool m_pearl_battery_valid_set;
  bool m_pearl_wind_speed_set;
  bool m_pearl_wind_valid;
  bool m_pearl_wind_valid_set;
  bool m_platform_health_ok;
  bool m_platform_health_set;
  bool m_health_ok;
  bool m_health_set;
  bool m_uav_armed;
  bool m_uav_armed_set;
  bool m_uav_in_air;
  bool m_uav_landed_set;
  bool m_target_set;
  bool m_pearl_activation_pending;
  bool m_pearl_arrived;
  bool m_uav_arrived;
  bool m_pearl_deploy;
  bool m_pearl_return;
  bool m_pearl_station_keep;
  bool m_pearl_manual_override;
  bool m_pearl_deploy_set;
  bool m_pearl_return_set;
  bool m_pearl_station_keep_set;
  bool m_pearl_manual_override_set;
  bool m_pearl_control_saved;
  bool m_saved_pearl_deploy;
  bool m_saved_pearl_return;
  bool m_saved_pearl_station_keep;
  bool m_saved_pearl_manual_override;
  bool m_clearance_received;
  bool m_landing_target_available;
  bool m_landing_target_gate_ready;
  bool m_completion_sent;
  bool m_config_valid;
  unsigned int m_requests_sent;
  unsigned int m_proposals_sent;
  unsigned int m_acceptances_sent;
  unsigned int m_clearances_sent;
  unsigned int m_aborts;
};

#endif
