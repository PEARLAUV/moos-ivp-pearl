#ifndef LANDING_COORDINATOR_HEADER
#define LANDING_COORDINATOR_HEADER

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
  };
  void registerVariables();
  void begin();
  void startRendezvous();
  void handleExchange(const std::string &value);
  void send();
  void state(const std::string &value, const std::string &reason);
  void abort(const std::string &reason);
  bool checksReady() const;
  bool fresh(const std::string &key) const;
  bool flag(const std::string &key) const;
  std::string value(const std::string &key) const;
  bool isTrue(const CMOOSMsg &msg) const;
  void publish();

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
  std::map<std::string, Input> m_inputs;
};
#endif
