#pragma once

#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include <map>
#include <set>
#include <string>

// Task transactions, not flight guidance. The bridge owns FC verification;
// the FSM owns flight lifecycle and the Helm owns secondary arrival evidence.
class RallyPoint : public AppCastingMOOSApp
{
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
  void task();
  void receive(const std::string &spec);
  bool idleGrounded(bool checking = false) const;
  void publish();
  void status(const std::string &state, const std::string &reason);
  std::string m_role = "shoreside", m_destination = "uav";
  std::string m_state = "STAGED", m_reason = "startup", m_id, m_pending;
  double m_x = 10, m_y = 15, m_altitude = 15, m_pending_since = -1;
  double m_started = 0;
  double m_active_x = 0, m_active_y = 0, m_active_altitude = 0;
  bool m_have_point = true, m_have_altitude = true;
  std::map<std::string, Input> m_inputs;
  std::set<std::string> m_seen_ids;
};
