#pragma once

#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"

#include <cstdint>
#include <string>
#include <vector>

class Operator : public AppCastingMOOSApp {
public:
  Operator();
  ~Operator() override;

protected:
  bool OnNewMail(MOOSMSG_LIST &mail) override;
  bool Iterate() override;
  bool OnConnectToServer() override;
  bool OnStartUp() override;
  bool buildReport() override;
  void registerVariables();

private:
  using StringList = std::vector<std::string>;

  struct Transition {
    std::string actor;
    std::string from;
    std::string to;
  };

  struct Definition {
    std::string name;
    unsigned int version = 1;
    std::string initial;
    std::string request_var;
    std::string status_var;
    StringList pending;
    StringList success;
    StringList failure;
    std::vector<Transition> transitions;
    std::string when_unready = "defer";
    std::string when_busy = "reject";
    double max_defer_time = 30;
    double request_ttl = 60;
    double request_republish = 1;
    double status_republish = 1;
    double lease_timeout = 3;
    double max_clock_skew = 2;
    double terminal_republish = 120;
    double readiness_max_age = 3;
    bool require_correlated_commands = true;
    unsigned int replay_cache_size = 256;
    unsigned int event_queue_size = 32;
    uint64_t hash = 0;
  };

  struct Envelope {
    std::string protocol;
    unsigned int version = 0;
    uint64_t hash = 0;
    std::string operation_id;
    std::string requester;
    std::string requester_epoch;
    std::string authority;
    std::string authority_epoch;
    std::string action;
    std::string actor;
    std::string from;
    std::string to;
    std::string phase;
    std::string previous_phase;
    std::string reason;
    std::string payload;
    std::string event_result;
    unsigned int actor_sequence = 0;
    unsigned int accepted_event_sequence = 0;
    unsigned int base_revision = 0;
    unsigned int revision = 0;
    double created_at = 0;
    double deadline = 0;
    double sent_at = 0;
    double terminal_at = 0;
  };

  bool loadDefinition(const std::string &path);
  bool validateDefinition();
  bool parseEnvelope(const std::string &text, Envelope &env) const;
  std::string serializeEnvelope(const Envelope &env) const;
  std::string jsonEscape(const std::string &value) const;
  std::string upperName(const std::string &value) const;
  std::string phaseClass(const std::string &phase) const;
  bool isTerminal(const std::string &phase) const;
  bool transitionAllowed(const std::string &actor, const std::string &from,
                         const std::string &to) const;
  bool phaseDeclared(const std::string &phase) const;
  bool validateEnvelopeIdentity(const Envelope &env) const;
  bool graphReachable(const std::string &from, const std::string &to) const;
  bool acquireStateLock();
  bool loadState();
  bool saveState() const;
  bool persistOrFault(const std::string &context);
  void pruneTerminalCache();
  void rememberTerminal(const Envelope &commit);
  const Envelope *findTerminal(const std::string &operation_id) const;
  void settlePendingFromSnapshot(const Envelope &env);
  void startQueuedProposal();

  void handleLocalRequest(const std::string &value);
  void handleLocalCancel(const std::string &value);
  void handleLocalTransition(const std::string &value);
  void handleWireRequest(const std::string &value);
  void handleWireStatus(const std::string &value);
  void handleAuthorityUpdate(const std::string &value);

  void publishRequest(bool force = false);
  void publishProposal(bool force = false);
  void publishCommit(bool force = false);
  void publishSnapshot(bool force = false);
  void publishDiscovery(bool force = false);
  void publishRejection(const Envelope &proposal, const std::string &reason);
  bool commitTransition(const std::string &actor, const std::string &to,
                        const std::string &reason,
                        unsigned int accepted_event_sequence = 0);
  void publishView();

  Definition m_def;
  std::string m_definition_path;
  std::string m_identity;
  std::string m_authority;
  std::string m_epoch;
  std::string m_prefix;
  std::string m_state_file;
  std::string m_authority_epoch;
  std::string m_discovered_authority_epoch;
  double m_discovery_time = -1;
  int m_state_lock_fd = -1;
  bool m_is_authority = false;
  bool m_ready = false;
  std::string m_ready_reason = "NOT_READY";
  std::string m_ready_evidence_id;
  double m_ready_time = -1;
  uint64_t m_next_id = 1;

  Envelope m_request;
  Envelope m_commit;
  Envelope m_proposal;
  std::vector<Envelope> m_terminal_commits;
  StringList m_queued_transitions;
  StringList m_start_command_ids;
  bool m_cancel_requested = false;
  bool m_replay_cache_saturated = false;
  bool m_start_cache_saturated = false;
  bool m_storage_fault = false;
  bool m_event_queue_saturated = false;
  bool m_reconciliation_required = false;
  bool m_have_operation = false;
  bool m_have_commit = false;
  bool m_have_proposal = false;
  std::string m_phase;
  std::string m_local_state = "IDLE";
  unsigned int m_revision = 0;
  unsigned int m_next_actor_sequence = 1;
  unsigned int m_last_requester_sequence = 0;
  double m_operation_started = 0;
  double m_last_request_tx = -1;
  double m_last_status_tx = -1;
  double m_last_discovery_tx = -1;
  double m_last_status_rx = -1;
  double m_last_authority_sent_at = -1;
  double m_last_proposal_tx = -1;
  double m_terminal_at = -1;
};
