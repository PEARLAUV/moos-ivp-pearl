/************************************************************/
/*    NAME: Charles Benjamin                                */
/*    ORGN: MIT, Cambridge MA                               */
/*    FILE: TakeoffGate.h                                   */
/*    DATE: August 17th, 2026                               */
/************************************************************/

#ifndef TAKEOFF_GATE_HEADER
#define TAKEOFF_GATE_HEADER

#include <string>
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"

class TakeoffGate : public AppCastingMOOSApp
{
public:
  TakeoffGate();

protected:
  bool OnNewMail(MOOSMSG_LIST &NewMail) override;
  bool Iterate() override;
  bool OnConnectToServer() override;
  bool OnStartUp() override;
  bool buildReport() override;

private:
  struct TimedValue {
    double value = 0.0;
    double time = -1.0;
  };

  void registerVariables();
  void iterateUAV();
  void iteratePearl();
  bool handleTakeoffRequest(const CMOOSMsg &msg);
  bool handleTakeoffClearance(const CMOOSMsg &msg);
  bool sendMessage(const std::string &destination,
                   const std::string &variable,
                   const std::string &value);
  void publishPearlRequestPending();
  void rejectPearlRequest(const std::string &reason);
  void requestPearlCancellation(const std::string &reason);
  void completePearlCancellation();
  bool pearlFSMAvailable(std::string &reason) const;
  bool fsmValueFresh(double value_time) const;
  bool savePearlControlState();
  void restorePearlControlState();
  void resetPearlCoordination();
  bool readDouble(const CMOOSMsg &msg, TimedValue &target);
  bool readBool(const CMOOSMsg &msg, bool &value) const;
  bool readBoolValue(const CMOOSMsg &msg, TimedValue &target);
  bool setPercentConfig(const std::string &value, double &target);
  bool isFresh(const TimedValue &value) const;
  std::string gateReason() const;
  std::string pearlGateReason() const;
  std::string ageString(const TimedValue &value) const;
  std::string boolString(const TimedValue &value) const;

private:
  std::string m_role;
  std::string m_ownship;
  std::string m_peer_node;
  double m_request_timeout;
  double m_coordination_step_delay;

  double m_min_uav_soc;
  double m_min_pearl_soc;
  double m_max_wind_speed;
  double m_input_max_age;
  double m_fsm_state_max_age;

  TimedValue m_uav_soc;
  TimedValue m_uav_battery_valid;
  TimedValue m_pearl_soc;
  TimedValue m_pearl_battery_valid;
  TimedValue m_wind_speed;
  TimedValue m_wind_valid;

  bool m_request_pending;
  bool m_awaiting_clearance;
  std::string m_session_id;
  double m_request_time;
  std::string m_coordination_state;
  double m_coordination_state_time;
  std::string m_uav_operation_state;
  std::string m_pearl_operation_state;
  std::string m_pearl_support_activity_state;
  std::string m_pearl_takeoff_support_state;
  std::string m_pearl_fsm_runtime_state;
  double m_uav_operation_state_time;
  double m_pearl_operation_state_time;
  double m_pearl_support_activity_state_time;
  double m_pearl_takeoff_support_state_time;
  double m_pearl_fsm_runtime_state_time;
  double m_pearl_fsm_heartbeat_time;
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
  double m_last_status_post;
  bool m_last_ready;
  std::string m_last_reason;
  unsigned int m_requests_approved;
  unsigned int m_requests_rejected;
};

#endif
