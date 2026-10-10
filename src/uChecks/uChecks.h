#ifndef UCHECKS_HEADER
#define UCHECKS_HEADER

#include <map>
#include <set>
#include <string>
#include <vector>

#include "LogicCondition.h"
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"

struct CheckRule {
  std::string var;
  bool numeric = false;
  bool immediate = false;
  bool has_equal = false;
  bool has_min = false;
  bool has_max = false;
  std::string string_equal;
  std::vector<std::string> string_options;
  double equal = 0;
  double minimum = 0;
  double maximum = 0;
};

struct CheckInput {
  bool received = false;
  bool numeric = false;
  double number = 0;
  std::string text;
  double time = -1;
};

struct NamedCheck {
  std::string name;
  double timeout = 0;
  double max_age = 0; // Omitted means no input-age limit.
  double success_hold = 0;
  std::vector<CheckRule> rules;
  double active_since = -1;
  double pass_since = -1;
  double deactivated_at = -1;
  double inactive_deadline = -1;
  bool failed = false;
  bool has_run = false;
  std::string status;
  std::string info;
};

class uChecks : public AppCastingMOOSApp {
 public:
  uChecks();

 protected:
  bool OnStartUp() override;
  bool OnConnectToServer() override;
  bool OnNewMail(MOOSMSG_LIST& mail) override;
  bool Iterate() override;
  bool buildReport() override;

 private:
  bool parseCheck(const std::string& config);
  bool parseRule(const std::string& config);
  void registerVariables();
  bool active() const;
  double expiryDeadline(const NamedCheck& check, double now) const;
  std::string evaluate(const NamedCheck& check, std::string& info,
                       double now) const;
  void publish(const std::string& var, std::string& status_cache,
               std::string& info_cache, const std::string& status,
               const std::string& info, bool refresh = false);
  void updateCheck(NamedCheck& check, bool enabled, double now, bool refresh);
  void publishSnapshot(double now, bool enabled);
  std::string snapshotSignature() const;

  std::string m_report_var;
  std::string m_state_var;
  std::string m_result_prefix;
  std::string m_session_var;
  std::string m_session;
  std::string m_snapshot_var;
  std::string m_snapshot_epoch;
  std::string m_snapshot_signature;
  std::string m_snapshot_session;
  double m_result_interval = 0; // Opt-in active authorization lease; zero is change-only.
  double m_last_result_time = -1;
  double m_snapshot_interval = 1;
  double m_last_snapshot_time = -1;
  unsigned long long m_snapshot_sequence = 0;
  bool m_snapshot_active = false;
  bool m_session_changed = false;
  LogicCondition m_condition;
  std::vector<std::string> m_condition_vars;
  std::map<std::string, NamedCheck> m_checks;
  std::map<std::string, CheckInput> m_inputs;
  std::map<std::string, CheckInput> m_condition_inputs;
  std::set<std::string> m_condition_seen;
  std::string m_status;
  std::string m_info;
  bool m_condition_configured;
  bool m_config_valid;
};

#endif
