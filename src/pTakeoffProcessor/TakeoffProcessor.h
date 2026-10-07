#ifndef TAKEOFF_PROCESSOR_HEADER
#define TAKEOFF_PROCESSOR_HEADER

#include <map>
#include <string>
#include <vector>
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"

class TakeoffProcessor : public AppCastingMOOSApp {
 public:
  TakeoffProcessor();
 protected:
  bool OnStartUp() override;
  bool OnConnectToServer() override;
  bool OnNewMail(MOOSMSG_LIST& mail) override;
  bool Iterate() override;
  bool buildReport() override;
 private:
  struct CheckStatus {
    std::string result;
    std::string state_result;
    std::string session;
    bool succeeded = false;
  };
  struct Condition {
    std::string expected;
    bool matched = false;
  };
  struct TakeoffEvent {
    double elapsed;
    std::string status;
    std::string progress;
    std::string reason;
  };
  void registerVariables();
  bool addReleaseCondition(const std::string& expression);
  bool releaseMatched() const;
  void resetReleaseConditions();
  bool matches(const Condition& condition, const CMOOSMsg& msg) const;
  bool send(const std::string& id, const std::string& status);
  void setStatus(const std::string& status, const std::string& reason);
  void recordEvent(const std::string& reason);
  void finish(const std::string& status, const std::string& reason);
  void publishVisualization();
  void hideVisualization();
  std::string m_ownship, m_requester, m_receipt_var, m_status_var, m_progress_var;
  std::string m_view_label, m_view_x_var, m_view_y_var, m_view_mode;
  std::map<std::string, CheckStatus> m_checks;
  std::map<std::string, Condition> m_release_conditions;
  std::string m_active_id, m_status, m_progress_status, m_cancel_reason;
  std::string m_released_grant_id;
  std::string m_event_id;
  std::vector<TakeoffEvent> m_events;
  double m_timeout, m_received_at, m_released_at, m_progress_grace;
  double m_view_radius, m_view_post_interval, m_view_last_post, m_view_x, m_view_y;
  bool m_have_view_x, m_have_view_y;
  unsigned int m_progress_stage;
  bool m_valid, m_checks_succeeded;
};
#endif
