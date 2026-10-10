#ifndef LANDING_COORDINATOR_HEADER
#define LANDING_COORDINATOR_HEADER

#include "LogicCondition.h"
#include "AttitudeHistory.h"
#include "DirectPose.h"
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include <map>
#include <string>

// UAV requests recovery; PEARL accepts the session and owns clearance.
// UAV checks telemetry
// before handing landing control to ArduPilot. Target streaming is unrelated.
class LandingCoordinator : public AppCastingMOOSApp
{
public:
  LandingCoordinator() = default;
  ~LandingCoordinator() override;

protected:
  bool OnStartUp() override;
  bool OnConnectToServer() override;
  bool OnNewMail(MOOSMSG_LIST &mail) override;
  bool Iterate() override;
  bool buildReport() override;

private:
  struct Input
  {
    std::string value;
    double time = -1;
    bool numeric = false;
  };
  void registerVariables();
  void begin();
  void startRendezvous();
  void handleExchange(const std::string &value);
  void send();
  void state(const std::string &value, const std::string &reason);
  void abort(const std::string &reason);
  bool checksReady() const;
  bool landedAndDisarmed() const;
  bool targetFresh() const;
  bool permits(const std::string &stage) const;
  bool fresh(const std::string &key) const;
  bool flag(const std::string &key) const;
  std::string value(const std::string &key) const;
  bool isTrue(const CMOOSMsg &msg) const;
  void publish();
  bool number(const std::string &key, double &data) const;
  bool targetHeading(double &heading, double &yaw, double &sample_time);
  bool yawReady();

  std::string m_ownship;
  std::string m_peer;
  std::string m_role;
  std::string m_state = "IDLE";
  std::string m_reason = "startup";
  std::string m_session;
  std::string m_peer_phase;
  std::string m_old_rv_session;
  std::string m_rv_session;
  std::string m_rv_phase = "IDLE";
  std::string m_peer_rv_phase;
  double m_session_time = 0;
  double m_state_time = 0;
  double m_peer_time = -1;
  double m_last_send = -1;
  double m_command_time = 0;
  double m_timeout = 300;
  double m_input_timeout = 3;
  bool m_rv_started = false;
  bool m_command_accepted = false;
  bool m_valid = true;
  bool m_align_yaw = false;
  bool m_yaw_accepted = false;
  double m_yaw_tolerance = 5, m_yaw_rate = 30, m_yaw_timeout = 60;
  double m_yaw_started = -1, m_yaw_request_time = -1, m_yaw_ack_time = -1;
  double m_yaw_goal = 0, m_yaw_error = 0;
  std::string m_yaw_state = "DISABLED";
  std::string m_yaw_input_reason = "not_evaluated";
  std::string m_yaw_request_id;
  bodyframe::AttitudeHistory m_attitude_history;
  std::string m_pose_samples_var;
  std::map<uint64_t, mavlink::NativePose> m_observation_poses;
  bool m_native_samples_seen = false;
  std::map<std::string, Input> m_inputs;
  std::map<std::string, LogicCondition> m_conditions;
  std::map<std::string, std::string> m_condition_specs;
};
#endif
