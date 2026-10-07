#ifndef TAKEOFF_COORDINATOR_HEADER
#define TAKEOFF_COORDINATOR_HEADER

#include <map>
#include <set>
#include <string>
#include <vector>

#include "LogicCondition.h"
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"

class TakeoffCoordinator : public AppCastingMOOSApp {
 public:
  TakeoffCoordinator();
 protected:
  bool OnStartUp() override;
  bool OnConnectToServer() override;
  bool OnNewMail(MOOSMSG_LIST& mail) override;
  bool Iterate() override;
  bool buildReport() override;
 private:
  enum Phase { IDLE, WAITING_CLEARANCE, ARMING, WAITING_TAKEOFF,
               TAKING_OFF, COMPLETE };
  struct CheckStatus {
    std::string result;
    std::string state_result;
    std::string session;
  };
  struct TakeoffEvent {
    double elapsed;
    std::string phase;
    std::string status;
    std::string reason;
  };
  void registerVariables();
  bool requestConditionsMet() const;
  bool checksMet() const;
  bool parseFlag(const std::string& spec, std::string& var, std::string& value);
  bool postFlag(const std::string& var, const std::string& value);
  bool send(const std::string& status, const std::string& reason = "");
  void updateState(const std::string& status, const std::string& reason);
  void recordEvent(const std::string& status, const std::string& reason);
  void finish(const std::string& status, const std::string& reason);
  std::string phaseName() const;

  std::vector<LogicCondition> m_request_conditions;
  std::set<std::string> m_vars, m_seen;
  std::map<std::string, CheckStatus> m_checks;
  std::string m_ownship, m_authority, m_result_var, m_state_var, m_reason_var;
  std::string m_receipt_var;
  std::string m_checks_session_var;
  std::string m_checks_session;
  std::string m_request_checks_session;
  std::string m_armed_var, m_landed_var, m_arming_var, m_arming_value;
  std::string m_takeoff_var, m_takeoff_value;
  std::string m_request_id, m_status, m_landed_state;
  std::vector<TakeoffEvent> m_events;
  double m_timeout, m_request_interval, m_completion_retry_time;
  double m_sent_at, m_last_request_at, m_last_progress_at, m_completed_at;
  unsigned long m_sequence, m_request_transmissions, m_complete_transmissions;
  Phase m_phase;
  bool m_pending, m_grant_received, m_was_met, m_valid;
  bool m_invalidated_pending, m_armed_seen, m_armed;
};
#endif
