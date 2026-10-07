#include "Operator.h"

#include "MBUtils.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <fcntl.h>
#include <iomanip>
#include <limits>
#include <memory>
#include <jsoncpp/json/json.h>
#include <sstream>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

namespace {
string stripTomlComment(const string &value) {
  bool quoted = false;
  bool escaped = false;
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '"' && !escaped) quoted = !quoted;
    if (value[i] == '#' && !quoted) return value.substr(0, i);
    escaped = value[i] == '\\' && !escaped;
    if (value[i] != '\\') escaped = false;
  }
  return value;
}

string trimCopy(string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == string::npos) return "";
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

bool parseDoubleExact(const string &text, double &result) {
  try {
    const string trimmed = trimCopy(text);
    size_t consumed = 0;
    result = stod(trimmed, &consumed);
    return consumed == trimmed.size() && isfinite(result);
  } catch (...) { return false; }
}

string unquote(string value) {
  value = trimCopy(value);
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    return value.substr(1, value.size() - 2);
  return value;
}

vector<string> parseArray(string value) {
  vector<string> result;
  value = trimCopy(value);
  if (value.size() < 2 || value.front() != '[' || value.back() != ']') return result;
  value = value.substr(1, value.size() - 2);
  for (const string &item : parseStringQ(value, ',')) {
    const string parsed = toupper(unquote(item));
    if (!parsed.empty()) result.push_back(parsed);
  }
  return result;
}

bool contains(const vector<string> &values, const string &value) {
  return find(values.begin(), values.end(), value) != values.end();
}

uint64_t fnv1a(const string &text) {
  uint64_t hash = 1469598103934665603ULL;
  for (unsigned char c : text) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool parseUnsigned(const string &text, unsigned int &value) {
  if (text.empty() || !all_of(text.begin(), text.end(), [](char c) {
        return isdigit(static_cast<unsigned char>(c));
      })) return false;
  try {
    const unsigned long parsed = stoul(text);
    if (parsed > numeric_limits<unsigned int>::max()) return false;
    value = static_cast<unsigned int>(parsed);
    return true;
  } catch (...) { return false; }
}

} // namespace

Operator::Operator() = default;

Operator::~Operator() {
  if (m_state_lock_fd >= 0) close(m_state_lock_fd);
}

bool Operator::OnStartUp() {
  AppCastingMOOSApp::OnStartUp();
  STRING_LIST params;
  m_MissionReader.EnableVerbatimQuoting(false);
  if (!m_MissionReader.GetConfiguration(GetAppName(), params)) {
    reportConfigWarning("No configuration block found");
    return false;
  }

  for (const string &original : params) {
    string line = original;
    const string key = tolower(biteStringX(line, '='));
    const string value = trimCopy(line);
    bool handled = true;
    if (key == "identity") m_identity = tolower(value);
    else if (key == "authority") m_authority = tolower(value);
    else if (key == "definition") m_definition_path = value;
    else if (key == "state_file") m_state_file = value;
    else handled = false;
    if (!handled) reportUnhandledConfigWarning(original);
  }

  if (m_identity.empty() || m_authority.empty() || m_definition_path.empty()) {
    reportConfigWarning("identity, authority, and definition are required");
    return false;
  }
  if (!loadDefinition(m_definition_path)) return false;

  m_is_authority = m_identity == m_authority;
  m_prefix = upperName(m_def.name);
  ostringstream epoch;
  epoch << m_identity << '-' << static_cast<unsigned long>(getpid()) << '-'
        << fixed << setprecision(0) << MOOSTime() * 1000;
  m_epoch = epoch.str();
  if (m_state_file.empty())
    m_state_file = ".pOperator-" + m_identity + "-" + m_def.name + ".state";
  m_authority_epoch = m_is_authority ? m_epoch : "";
  if (!acquireStateLock() || !loadState()) return false;

  registerVariables();
  Notify(m_prefix + "_OPERATOR_ROLE", m_is_authority ? "AUTHORITY" : "REQUESTER");
  Notify(m_prefix + "_OPERATOR_EPOCH", m_epoch);
  Notify(m_prefix + "_PROTOCOL_HASH", to_string(m_def.hash));
  publishView();
  return true;
}

bool Operator::OnConnectToServer() { registerVariables(); return true; }

void Operator::registerVariables() {
  AppCastingMOOSApp::RegisterVariables();
  if (m_prefix.empty()) return;
  Register(m_def.request_var, 0);
  Register(m_def.status_var, 0);
  if (m_is_authority) {
    Register(m_prefix + "_AUTHORITY_READY", 0);
    Register(m_prefix + "_AUTHORITY_REASON", 0);
    Register(m_prefix + "_AUTHORITY_TRANSITION", 0);
    // Compatibility with the first pOperator interface.
    Register(m_prefix + "_AUTHORITY_STATUS_UPDATE", 0);
  } else {
    Register(m_prefix + "_REQUEST", 0);
    Register(m_prefix + "_TRANSITION", 0);
    Register(m_prefix + "_PROGRESS", 0);
    Register(m_prefix + "_CANCEL", 0);
  }
}

bool Operator::OnNewMail(MOOSMSG_LIST &mail) {
  AppCastingMOOSApp::OnNewMail(mail);
  if (m_storage_fault) return true;
  for (CMOOSMsg &msg : mail) {
    const string key = msg.GetKey();
    const string value = msg.GetAsString();
    if (!m_is_authority && key == m_prefix + "_REQUEST") handleLocalRequest(value);
    else if (!m_is_authority && (key == m_prefix + "_TRANSITION" || key == m_prefix + "_PROGRESS"))
      handleLocalTransition(value);
    else if (!m_is_authority && key == m_prefix + "_CANCEL")
      handleLocalCancel(value);
    else if (m_is_authority && key == m_prefix + "_AUTHORITY_READY") {
      bool ready = false;
      double observed_at = MOOSTime();
      if (m_def.require_correlated_commands) {
        const string ready_value = tokStringParse(value, "ready", '#', '=');
        const string evidence_id = tokStringParse(value, "evidence_id", '#', '=');
        const string observed = tokStringParse(value, "observed_at", '#', '=');
        if (evidence_id.empty() || !setBooleanOnString(ready, ready_value) ||
            !parseDoubleExact(observed, observed_at) ||
            observed_at > MOOSTime() + m_def.max_clock_skew ||
            MOOSTime() - observed_at > m_def.readiness_max_age ||
            observed_at < m_ready_time ||
            (observed_at == m_ready_time && evidence_id != m_ready_evidence_id)) {
          reportRunWarning("Stale or uncorrelated authority readiness rejected");
          continue;
        }
      } else if (!setBooleanOnString(ready, value)) continue;
      m_ready = ready;
      m_ready_time = observed_at;
      m_ready_evidence_id = m_def.require_correlated_commands ?
        tokStringParse(value, "evidence_id", '#', '=') : "legacy";
    } else if (m_is_authority && key == m_prefix + "_AUTHORITY_REASON") m_ready_reason = value;
    else if (m_is_authority &&
             (key == m_prefix + "_AUTHORITY_TRANSITION" ||
              key == m_prefix + "_AUTHORITY_STATUS_UPDATE")) handleAuthorityUpdate(value);
    else if (m_is_authority && key == m_def.request_var) handleWireRequest(value);
    else if (!m_is_authority && key == m_def.status_var) handleWireStatus(value);
  }
  return true;
}

bool Operator::Iterate() {
  AppCastingMOOSApp::Iterate();
  const double now = MOOSTime();

  if (m_storage_fault || m_reconciliation_required) {
    if (!m_is_authority) {
      Notify(m_prefix + "_RESPONSE_FRESH", 0.0);
      Notify(m_prefix + "_LOCAL_STATE", m_local_state);
    }
    publishView();
    AppCastingMOOSApp::PostReport();
    return true;
  }

  if (m_is_authority && (!m_have_operation || isTerminal(m_phase)))
    publishDiscovery();

  if (m_is_authority && m_have_operation && !isTerminal(m_phase)) {
    const bool ready_fresh = m_ready && m_ready_time >= 0 &&
      now - m_ready_time <= m_def.readiness_max_age;
    if ((m_phase == m_def.initial || m_phase == "DEFERRED") &&
        (now >= m_request.deadline + m_def.max_clock_skew ||
         (m_phase == "DEFERRED" && now - m_operation_started >= m_def.max_defer_time)))
      commitTransition("protocol", "EXPIRED", "ADMISSION_DEADLINE_EXPIRED");
    else if (m_phase == m_def.initial) {
      const string target = ready_fresh ? "ACCEPTED" :
        (m_def.when_unready == "reject" ? "REJECTED" : "DEFERRED");
      commitTransition("authority", target, ready_fresh ? "READY" : m_ready_reason);
    } else if (m_phase == "DEFERRED" && ready_fresh)
      commitTransition("authority", "ACCEPTED", "READY");
    publishSnapshot();
  } else if (m_is_authority && m_have_operation && isTerminal(m_phase) &&
             now - m_terminal_at <= m_def.terminal_republish) {
    publishSnapshot();
  } else if (!m_is_authority && m_have_operation && !isTerminal(m_phase)) {
    publishRequest();
    if (m_have_proposal) publishProposal();
    const double reference = m_last_status_rx >= 0 ? m_last_status_rx : m_operation_started;
    if (now - reference > m_def.lease_timeout &&
        m_local_state != "AUTHORITY_RESTARTED" &&
        m_local_state != "STORAGE_FAULT" &&
        m_local_state != "RECOVERY_REQUIRED")
      m_local_state = "AUTHORITY_TIMEOUT";
  }

  if (!m_is_authority) {
    const bool fresh = m_last_status_rx >= 0 && now - m_last_status_rx <= m_def.lease_timeout;
    Notify(m_prefix + "_RESPONSE_FRESH", fresh ? 1.0 : 0.0);
    if (m_last_status_rx >= 0) Notify(m_prefix + "_STATUS_AGE", max(0.0, now - m_last_status_rx));
    Notify(m_prefix + "_LOCAL_STATE", m_local_state);
  }
  publishView();
  AppCastingMOOSApp::PostReport();
  return true;
}

void Operator::handleLocalRequest(const string &value) {
  if (m_storage_fault || (m_have_operation && !isTerminal(m_phase))) return;
  string command_id = tokStringParse(value, "command_id", '#', '=');
  string payload = tokStringParse(value, "payload", '#', '=');
  const string start_value = tokStringParse(value, "start", '#', '=');
  if (m_def.require_correlated_commands) {
    if (command_id.empty() || command_id.size() > 128 ||
        command_id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-") != string::npos ||
        tolower(start_value) != "true") {
      reportRunWarning("Start command requires command_id and start=true");
      return;
    }
    if (contains(m_start_command_ids, command_id)) return;
    if (m_discovered_authority_epoch.empty() ||
        MOOSTime() - m_discovery_time > m_def.lease_timeout) {
      reportRunWarning("Start command withheld without fresh authority discovery");
      return;
    }
  } else {
    bool start = true;
    if (isBoolean(value) && !setBooleanOnString(start, value)) return;
    if (isBoolean(value) && !start) return;
    if (command_id.empty()) command_id = to_string(m_next_id++);
    if (payload.empty() && !isBoolean(value)) payload = value;
  }
  if (m_start_cache_saturated || m_start_command_ids.size() >= m_def.replay_cache_size) {
    m_start_cache_saturated = true;
    m_local_state = "START_CACHE_SATURATED";
    persistOrFault("start command replay cache saturation");
    return;
  }
  const double now = MOOSTime();
  m_request = Envelope{};
  m_request.protocol = m_def.name;
  m_request.version = m_def.version;
  m_request.hash = m_def.hash;
  m_request.operation_id = m_epoch + ':' + m_def.name + ':' + command_id;
  m_request.requester = m_identity;
  m_request.requester_epoch = m_epoch;
  m_request.authority = m_authority;
  m_request.authority_epoch = m_discovered_authority_epoch;
  m_request.action = "REQUEST";
  m_request.payload = payload;
  m_request.created_at = now;
  m_request.deadline = now + m_def.request_ttl;

  m_have_operation = true;
  m_have_commit = false;
  m_have_proposal = false;
  m_queued_transitions.clear();
  m_cancel_requested = false;
  m_authority_epoch = m_discovered_authority_epoch;
  m_phase = m_def.initial;
  m_revision = 0;
  m_next_actor_sequence = 1;
  m_operation_started = now;
  m_last_status_rx = -1;
  m_local_state = "WAITING_FOR_AUTHORITY";
  m_start_command_ids.push_back(command_id);
  if (!persistOrFault("start request")) return;
  publishRequest(true);
  publishView();
}

void Operator::handleLocalCancel(const string &value) {
  if (!m_have_operation || isTerminal(m_phase)) return;
  const string id = tokStringParse(value, "id", '#', '=');
  const string revision = tokStringParse(value, "revision", '#', '=');
  const string cancel_value = tokStringParse(value, "cancel", '#', '=');
  if (m_def.require_correlated_commands) {
    unsigned int parsed_revision = 0;
    if (id != m_request.operation_id || !parseUnsigned(revision, parsed_revision) ||
        parsed_revision != m_revision || tolower(cancel_value) != "true") {
      reportRunWarning("Uncorrelated cancellation rejected");
      return;
    }
  } else {
    bool cancel = false;
    if (!setBooleanOnString(cancel, value) || !cancel) return;
  }
  const string target = transitionAllowed("requester", m_phase, "ABORTING") ?
    "ABORTING" : "CANCELLED";
  m_cancel_requested = true;
  if (!persistOrFault("cancellation intent") || m_have_proposal) return;
  handleLocalTransition("id=" + m_request.operation_id + "#revision=" +
                        to_string(m_revision) + "#to=" + target +
                        "#reason=REQUESTER_CANCELLED");
}

void Operator::handleLocalTransition(const string &value) {
  if (!m_have_operation || isTerminal(m_phase)) return;
  const string command_id = tokStringParse(value, "id", '#', '=');
  const string command_revision = tokStringParse(value, "revision", '#', '=');
  string target = tokStringParse(value, "to", '#', '=');
  if (m_def.require_correlated_commands) {
    unsigned int parsed_revision = 0;
    if (command_id != m_request.operation_id ||
        !parseUnsigned(command_revision, parsed_revision) ||
        parsed_revision != m_revision || target.empty()) {
      reportRunWarning("Uncorrelated requester transition rejected");
      return;
    }
  } else if (target.empty()) target = value;
  string reason = tokStringParse(value, "reason", '#', '=');
  if (reason.empty()) reason = "REQUESTER_TRANSITION";
  target = toupper(target);
  const size_t comma = target.find(',');
  if (comma != string::npos) {
    const string comma_reason = target.substr(comma + 1);
    target = target.substr(0, comma);
    if (!comma_reason.empty()) reason = comma_reason;
  }
  target = trimCopy(target);
  if (!phaseDeclared(target)) {
    reportRunWarning("Requester named an undeclared phase: " + target);
    return;
  }
  if (m_have_proposal || m_local_state == "AUTHORITY_TIMEOUT" ||
      m_local_state == "AUTHORITY_RESTARTED" ||
      m_local_state == "RECOVERY_REQUIRED" || m_authority_epoch.empty()) {
    if (m_queued_transitions.size() >= m_def.event_queue_size) {
      m_event_queue_saturated = true;
      m_local_state = "EVENT_QUEUE_SATURATED";
      persistOrFault("event queue saturation");
      return;
    }
    if (m_queued_transitions.empty() || m_queued_transitions.back() != target)
      m_queued_transitions.push_back(target);
    persistOrFault("queued requester observation");
    return;
  }
  if (!transitionAllowed("requester", m_phase, target)) {
    reportRunWarning("Requester transition not allowed: " + m_phase + " -> " + target);
    Notify(m_prefix + "_TRANSITION_RESULT", "REJECTED_LOCAL");
    Notify(m_prefix + "_TRANSITION_REASON", "TRANSITION_NOT_ALLOWED");
    return;
  }

  m_proposal = m_request;
  m_proposal.action = "TRANSITION_PROPOSAL";
  m_proposal.actor = "requester";
  m_proposal.from = m_phase;
  m_proposal.to = target;
  m_proposal.reason = reason;
  m_proposal.actor_sequence = m_next_actor_sequence++;
  m_proposal.base_revision = m_revision;
  m_proposal.authority_epoch = m_authority_epoch;
  m_have_proposal = true;
  m_local_state = "WAITING_FOR_COMMIT";
  if (!persistOrFault("requester proposal")) return;
  publishProposal(true);
}

void Operator::handleWireRequest(const string &value) {
  Envelope incoming;
  if (!parseEnvelope(value, incoming)) return;
  if (!validateEnvelopeIdentity(incoming) || incoming.authority != m_identity ||
      m_storage_fault) return;

  if (incoming.action == "REQUEST") {
    if (incoming.requester.empty() || incoming.requester_epoch.empty()) return;
    pruneTerminalCache();
    if (const Envelope *terminal = findTerminal(incoming.operation_id)) {
      if (terminal->requester != incoming.requester ||
          terminal->requester_epoch != incoming.requester_epoch) return;
      Envelope snapshot = *terminal;
      snapshot.action = "STATE_SNAPSHOT";
      snapshot.sent_at = MOOSTime();
      Notify(m_def.status_var, serializeEnvelope(snapshot));
      return;
    }
    if (m_have_operation && incoming.operation_id == m_request.operation_id) {
      if (incoming.authority_epoch != m_authority_epoch) return;
      publishSnapshot(true);
      return;
    }
    if (incoming.authority_epoch.empty() || incoming.authority_epoch != m_epoch) {
      Envelope mismatch = incoming;
      mismatch.action = "GENERATION_MISMATCH";
      mismatch.actor = "protocol";
      mismatch.authority_epoch = m_epoch;
      mismatch.phase = m_def.initial;
      mismatch.reason = "AUTHORITY_GENERATION_CHANGED";
      mismatch.sent_at = MOOSTime();
      Notify(m_def.status_var, serializeEnvelope(mismatch));
      return;
    }
    if (m_replay_cache_saturated) {
      reportRunWarning("Replay cache saturated; refusing new operations fail-safe");
      return;
    }

    const double now = MOOSTime();
    string rejection_reason;
    string rejection_phase = "REJECTED";
    string rejection_actor = "authority";
    if (incoming.deadline <= now - m_def.max_clock_skew ||
        incoming.created_at > now + m_def.max_clock_skew ||
        incoming.deadline <= incoming.created_at ||
        incoming.deadline - incoming.created_at > m_def.request_ttl + m_def.max_clock_skew) {
      rejection_reason = "REQUEST_EXPIRED_OR_CLOCK_INVALID";
      rejection_phase = "EXPIRED";
      rejection_actor = "protocol";
    } else if (m_have_operation && !isTerminal(m_phase)) {
      rejection_reason = "BUSY";
    }
    if (!rejection_reason.empty()) {
      Envelope rejected = incoming;
      rejected.action = "TRANSITION_COMMIT";
      rejected.actor = rejection_actor;
      rejected.authority_epoch = m_epoch;
      rejected.previous_phase = m_def.initial;
      rejected.phase = rejection_phase;
      rejected.reason = rejection_reason;
      rejected.event_result = "COMMITTED";
      rejected.revision = 1;
      rejected.terminal_at = MOOSTime();
      rememberTerminal(rejected);
      if (!persistOrFault("terminal request rejection")) return;
      Notify(m_prefix + "_LAST_REQUEST_DECISION", rejection_phase);
      rejected.sent_at = MOOSTime();
      Notify(m_def.status_var, serializeEnvelope(rejected));
      return;
    }

    m_request = incoming;
    m_have_operation = true;
    m_have_proposal = false;
    m_phase = m_def.initial;
    m_revision = 1;
    m_authority_epoch = m_epoch;
    m_last_requester_sequence = 0;
    m_operation_started = MOOSTime();
    m_local_state = "ACTIVE";

    m_commit = m_request;
    m_commit.action = "TRANSITION_COMMIT";
    m_commit.actor = "protocol";
    m_commit.authority_epoch = m_authority_epoch;
    m_commit.previous_phase.clear();
    m_commit.phase = m_phase;
    m_commit.reason = "REQUEST_RECEIVED";
    m_commit.event_result = "COMMITTED";
    m_commit.revision = m_revision;
    m_have_commit = true;
    if (!persistOrFault("operation admission")) return;
    publishCommit(true);

    const bool ready_fresh = m_ready && m_ready_time >= 0 &&
      MOOSTime() - m_ready_time <= m_def.readiness_max_age;
    const string target = ready_fresh ? "ACCEPTED" :
      (m_def.when_unready == "reject" ? "REJECTED" : "DEFERRED");
    commitTransition("authority", target, ready_fresh ? "READY" : m_ready_reason);
    return;
  }

  if (incoming.action != "TRANSITION_PROPOSAL" || !m_have_operation ||
      incoming.operation_id != m_request.operation_id ||
      incoming.requester != m_request.requester ||
      incoming.requester_epoch != m_request.requester_epoch ||
      incoming.authority_epoch != m_authority_epoch ||
      incoming.actor != "requester") return;

  if (incoming.actor_sequence <= m_last_requester_sequence) {
    publishSnapshot(true);
    return;
  }
  if (incoming.base_revision != m_revision || incoming.from != m_phase) {
    publishRejection(incoming, "STALE_BASE_REVISION");
    return;
  }
  if (!transitionAllowed("requester", m_phase, incoming.to)) {
    publishRejection(incoming, "TRANSITION_NOT_ALLOWED");
    return;
  }

  commitTransition("requester", incoming.to, incoming.reason, incoming.actor_sequence);
}

void Operator::handleWireStatus(const string &value) {
  Envelope incoming;
  if (!parseEnvelope(value, incoming)) return;
  if (incoming.protocol != m_def.name || incoming.version != m_def.version ||
      incoming.hash != m_def.hash || incoming.authority != m_authority ||
      incoming.authority_epoch.empty()) return;
  if (incoming.action == "AUTHORITY_DISCOVERY") {
    if (incoming.sent_at <= m_last_authority_sent_at) return;
    m_last_authority_sent_at = incoming.sent_at;
    m_discovered_authority_epoch = incoming.authority_epoch;
    m_discovery_time = MOOSTime();
    return;
  }
  if (m_reconciliation_required || incoming.sent_at <= m_last_authority_sent_at) return;
  if (!m_have_operation || incoming.operation_id != m_request.operation_id ||
      incoming.requester != m_request.requester ||
      incoming.requester_epoch != m_request.requester_epoch ||
      !phaseDeclared(incoming.phase)) return;

  if (incoming.action != "GENERATION_MISMATCH" &&
      incoming.action != "TRANSITION_REJECTION" &&
      incoming.action != "TRANSITION_COMMIT" &&
      incoming.action != "STATE_SNAPSHOT") return;
  if (incoming.action == "GENERATION_MISMATCH") {
    m_last_authority_sent_at = incoming.sent_at;
    m_reconciliation_required = true;
    m_local_state = "AUTHORITY_RESTARTED";
    Notify(m_prefix + "_TRANSITION_REASON", incoming.reason);
    persistOrFault("authority generation mismatch");
    return;
  }
  if (m_authority_epoch.empty()) {
    m_authority_epoch = incoming.authority_epoch;
    m_request.authority_epoch = m_authority_epoch;
  } else if (incoming.authority_epoch != m_authority_epoch) {
    m_last_authority_sent_at = incoming.sent_at;
    m_reconciliation_required = true;
    m_local_state = "AUTHORITY_RESTARTED";
    Notify(m_prefix + "_TRANSITION_REASON", "AUTHORITY_GENERATION_CHANGED");
    persistOrFault("authority generation change");
    return;
  }

  const unsigned int issued_sequence = m_next_actor_sequence - 1;
  if (incoming.accepted_event_sequence > issued_sequence) return;
  if (incoming.action == "TRANSITION_REJECTION") {
    if (!m_have_proposal || incoming.actor_sequence != m_proposal.actor_sequence ||
        incoming.revision < m_revision) return;
    m_last_authority_sent_at = incoming.sent_at;
    m_have_proposal = false;
    m_local_state = "TRANSITION_REJECTED";
    m_last_status_rx = MOOSTime();
    Notify(m_prefix + "_TRANSITION_RESULT", "REJECTED");
    Notify(m_prefix + "_TRANSITION_REASON", incoming.reason);
    persistOrFault("transition rejection");
    return;
  }

  const bool commit = incoming.action == "TRANSITION_COMMIT";
  const bool snapshot = incoming.action == "STATE_SNAPSHOT";
  const bool had_proposal = m_have_proposal;
  const bool had_cancel = m_cancel_requested;
  const size_t queued_before = m_queued_transitions.size();
  const string old_phase = m_phase;
  const unsigned int old_revision = m_revision;
  const Envelope old_commit = m_commit;
  const bool old_have_commit = m_have_commit;
  const bool old_have_proposal = m_have_proposal;
  const StringList old_queue = m_queued_transitions;
  const bool old_cancel = m_cancel_requested;
  const double old_last_status_rx = m_last_status_rx;
  const double old_last_authority_sent_at = m_last_authority_sent_at;
  if ((!commit && !snapshot) || incoming.revision < m_revision) return;
  if (incoming.revision == m_revision) {
    if (incoming.phase != m_phase ||
        (m_have_commit &&
         (incoming.accepted_event_sequence != m_commit.accepted_event_sequence ||
          incoming.previous_phase != m_commit.previous_phase ||
          incoming.actor != m_commit.actor))) {
      reportRunWarning("Conflicting authority states use the same revision");
      return;
    }
  } else if (snapshot) {
    if (isTerminal(m_phase) || !graphReachable(m_phase, incoming.phase) ||
        (m_have_commit && incoming.accepted_event_sequence <
                          m_commit.accepted_event_sequence)) {
      reportRunWarning("Regressive or inconsistent authority snapshot rejected");
      return;
    }
  } else if (commit) {
    const bool initial_commit = m_revision == 0 && incoming.revision == 1 &&
      incoming.previous_phase.empty() && incoming.phase == m_def.initial &&
      incoming.actor == "protocol";
    if (!initial_commit &&
        (incoming.revision != m_revision + 1 || incoming.previous_phase != m_phase ||
         !transitionAllowed(incoming.actor, incoming.previous_phase, incoming.phase))) {
      reportRunWarning("Non-consecutive or invalid authority commit rejected");
      return;
    }
  }

  const bool newer_revision = incoming.revision > m_revision;
  if (newer_revision || !m_have_commit) {
    m_phase = incoming.phase;
    m_revision = incoming.revision;
    m_commit = incoming;
    m_have_commit = true;
  }
  m_last_authority_sent_at = incoming.sent_at;
  m_last_status_rx = MOOSTime();
  settlePendingFromSnapshot(incoming);
  if (isTerminal(m_phase)) {
    m_have_proposal = false;
    m_queued_transitions.clear();
    m_cancel_requested = false;
    m_local_state = "TERMINAL";
  } else if (m_event_queue_saturated) {
    m_local_state = "RECOVERY_REQUIRED";
  } else {
    m_local_state = "SYNCHRONIZED";
  }
  const bool state_changed = newer_revision || had_proposal != m_have_proposal ||
    had_cancel != m_cancel_requested || queued_before != m_queued_transitions.size();
  if (state_changed && !saveState()) {
    m_phase = old_phase;
    m_revision = old_revision;
    m_commit = old_commit;
    m_have_commit = old_have_commit;
    m_have_proposal = old_have_proposal;
    m_queued_transitions = old_queue;
    m_cancel_requested = old_cancel;
    m_last_status_rx = old_last_status_rx;
    m_last_authority_sent_at = old_last_authority_sent_at;
    m_storage_fault = true;
    m_local_state = "STORAGE_FAULT";
    reportRunWarning("Persistent state write failed while receiving authority state");
    publishView();
    return;
  }
  if (!isTerminal(m_phase) && !m_event_queue_saturated) startQueuedProposal();
  publishView();
}

void Operator::handleAuthorityUpdate(const string &value) {
  if (!m_have_operation || isTerminal(m_phase)) return;
  const string command_id = tokStringParse(value, "id", '#', '=');
  const string command_revision = tokStringParse(value, "revision", '#', '=');
  string target = tokStringParse(value, "to", '#', '=');
  string reason = tokStringParse(value, "reason", '#', '=');
  if (m_def.require_correlated_commands) {
    unsigned int parsed_revision = 0;
    if (command_id != m_request.operation_id ||
        !parseUnsigned(command_revision, parsed_revision) ||
        parsed_revision != m_revision || target.empty()) {
      reportRunWarning("Uncorrelated authority transition rejected");
      return;
    }
  } else if (target.empty()) target = value;
  target = toupper(target);
  if (reason.empty()) reason = "AUTHORITY_TRANSITION";
  const size_t comma = target.find(',');
  if (comma != string::npos) {
    reason = target.substr(comma + 1);
    target = target.substr(0, comma);
  }
  target = trimCopy(target);
  if (!commitTransition("authority", target, reason))
    reportRunWarning("Authority transition not allowed: " + m_phase + " -> " + target);
}

void Operator::publishRequest(bool force) {
  const double now = MOOSTime();
  if (!force && now - m_last_request_tx < m_def.request_republish) return;
  Envelope outgoing = m_request;
  outgoing.sent_at = now;
  Notify(m_def.request_var, serializeEnvelope(outgoing));
  m_last_request_tx = now;
}

void Operator::publishProposal(bool force) {
  const double now = MOOSTime();
  if (!m_have_proposal || (!force && now - m_last_proposal_tx < m_def.request_republish)) return;
  Envelope outgoing = m_proposal;
  outgoing.sent_at = now;
  Notify(m_def.request_var, serializeEnvelope(outgoing));
  m_last_proposal_tx = now;
}

void Operator::publishCommit(bool force) {
  const double now = MOOSTime();
  if (!m_have_commit || (!force && now - m_last_status_tx < m_def.status_republish)) return;
  Envelope outgoing = m_commit;
  outgoing.sent_at = now;
  Notify(m_def.status_var, serializeEnvelope(outgoing));
  m_last_status_tx = now;
}

void Operator::publishSnapshot(bool force) {
  const double now = MOOSTime();
  if (!m_have_commit || (!force && now - m_last_status_tx < m_def.status_republish)) return;
  Envelope snapshot = m_commit;
  snapshot.action = "STATE_SNAPSHOT";
  snapshot.sent_at = now;
  Notify(m_def.status_var, serializeEnvelope(snapshot));
  m_last_status_tx = now;
}

void Operator::publishDiscovery(bool force) {
  const double now = MOOSTime();
  if (!force && now - m_last_discovery_tx < m_def.status_republish) return;
  Envelope discovery;
  discovery.protocol = m_def.name;
  discovery.version = m_def.version;
  discovery.hash = m_def.hash;
  discovery.operation_id = "__discovery__";
  discovery.requester = "__discovery__";
  discovery.requester_epoch = "__discovery__";
  discovery.authority = m_identity;
  discovery.authority_epoch = m_epoch;
  discovery.action = "AUTHORITY_DISCOVERY";
  discovery.phase = m_def.initial;
  discovery.sent_at = now;
  Notify(m_def.status_var, serializeEnvelope(discovery));
  m_last_discovery_tx = now;
}

void Operator::publishRejection(const Envelope &proposal, const string &reason) {
  Envelope rejected = m_commit;
  rejected.action = "TRANSITION_REJECTION";
  rejected.actor_sequence = proposal.actor_sequence;
  rejected.reason = reason;
  rejected.event_result = "REJECTED";
  rejected.sent_at = MOOSTime();
  Notify(m_def.status_var, serializeEnvelope(rejected));
  publishSnapshot(true);
}

bool Operator::commitTransition(const string &actor, const string &to,
                                const string &reason,
                                unsigned int accepted_event_sequence) {
  if (m_storage_fault || !transitionAllowed(actor, m_phase, to)) return false;
  if (isTerminal(to)) {
    pruneTerminalCache();
    if (!findTerminal(m_request.operation_id) &&
        m_terminal_commits.size() >= m_def.replay_cache_size) {
      m_replay_cache_saturated = true;
      reportRunWarning("Replay cache cannot retain terminal decision; transition withheld");
      return false;
    }
  }
  const string old_phase = m_phase;
  const unsigned int old_revision = m_revision;
  const unsigned int old_sequence = m_last_requester_sequence;
  const Envelope old_commit = m_commit;
  const bool old_have_commit = m_have_commit;
  const double old_terminal_at = m_terminal_at;
  const vector<Envelope> old_terminals = m_terminal_commits;
  const bool old_saturated = m_replay_cache_saturated;
  const string previous = m_phase;
  m_phase = to;
  ++m_revision;

  m_commit = m_request;
  m_commit.action = "TRANSITION_COMMIT";
  m_commit.actor = actor;
  m_commit.authority_epoch = m_authority_epoch;
  m_commit.previous_phase = previous;
  m_commit.phase = m_phase;
  m_commit.reason = reason;
  m_commit.event_result = "COMMITTED";
  if (accepted_event_sequence > m_last_requester_sequence)
    m_last_requester_sequence = accepted_event_sequence;
  m_commit.accepted_event_sequence = m_last_requester_sequence;
  m_commit.revision = m_revision;
  m_have_commit = true;
  if (isTerminal(m_phase)) {
    m_terminal_at = MOOSTime();
    m_commit.terminal_at = m_terminal_at;
    rememberTerminal(m_commit);
  }
  m_local_state = isTerminal(m_phase) ? "TERMINAL" : "ACTIVE";
  if (!saveState()) {
    m_phase = old_phase;
    m_revision = old_revision;
    m_last_requester_sequence = old_sequence;
    m_commit = old_commit;
    m_have_commit = old_have_commit;
    m_terminal_at = old_terminal_at;
    m_terminal_commits = old_terminals;
    m_replay_cache_saturated = old_saturated;
    m_storage_fault = true;
    m_local_state = "STORAGE_FAULT";
    reportRunWarning("Persistent state write failed while committing transition");
    publishView();
    return false;
  }
  publishCommit(true);
  publishView();
  return true;
}

void Operator::publishView() {
  if (m_prefix.empty()) return;
  Notify(m_prefix + "_PHASE", m_phase.empty() ? "IDLE" : m_phase);
  Notify(m_prefix + "_PHASE_CLASS", phaseClass(m_phase));
  Notify(m_prefix + "_REVISION", static_cast<double>(m_revision));
  Notify(m_prefix + "_OPERATION_ID", m_request.operation_id);
  // Compatibility names: status is now the authoritative shared phase.
  Notify(m_prefix + "_STATUS", m_phase.empty() ? "IDLE" : m_phase);
  Notify(m_prefix + "_STATUS_CLASS", phaseClass(m_phase));
  if (m_have_commit) {
    Notify(m_prefix + "_REASON", m_commit.reason);
    Notify(m_prefix + "_OPERATION_SNAPSHOT", serializeEnvelope(m_commit));
  }
  Notify(m_prefix + "_STORAGE_FAULT", m_storage_fault ? 1.0 : 0.0);
  Notify(m_prefix + "_EVENT_QUEUE_SATURATED", m_event_queue_saturated ? 1.0 : 0.0);
  Notify(m_prefix + "_START_CACHE_SATURATED", m_start_cache_saturated ? 1.0 : 0.0);
  Notify(m_prefix + "_RECONCILIATION_REQUIRED", m_reconciliation_required ? 1.0 : 0.0);
  if (m_is_authority) {
    Notify(m_prefix + "_REPLAY_CACHE_SATURATED", m_replay_cache_saturated ? 1.0 : 0.0);
    Notify(m_prefix + "_REQUEST_PENDING", m_have_operation && !isTerminal(m_phase) ? 1.0 : 0.0);
    Notify(m_prefix + "_REQUEST_ID", m_request.operation_id);
    Notify(m_prefix + "_REQUESTER", m_request.requester);
    Notify(m_prefix + "_REQUEST_PAYLOAD", m_request.payload);
  }
}

bool Operator::phaseDeclared(const string &phase) const {
  return contains(m_def.pending, phase) || contains(m_def.success, phase) ||
         contains(m_def.failure, phase);
}

bool Operator::validateEnvelopeIdentity(const Envelope &env) const {
  return env.protocol == m_def.name && env.version == m_def.version &&
         env.hash == m_def.hash && !env.operation_id.empty() &&
         !env.requester.empty() && !env.requester_epoch.empty() &&
         env.authority == m_authority;
}

bool Operator::graphReachable(const string &from, const string &to) const {
  if (from == to) return true;
  StringList frontier(1, from);
  StringList visited(1, from);
  while (!frontier.empty()) {
    const string current = frontier.back();
    frontier.pop_back();
    for (const Transition &transition : m_def.transitions) {
      if (transition.from != current) continue;
      if (transition.to == to) return true;
      if (!contains(visited, transition.to)) {
        visited.push_back(transition.to);
        frontier.push_back(transition.to);
      }
    }
  }
  return false;
}

void Operator::pruneTerminalCache() {
  const double now = MOOSTime();
  m_terminal_commits.erase(
    remove_if(m_terminal_commits.begin(), m_terminal_commits.end(),
      [&](const Envelope &known) {
        return known.terminal_at > 0 &&
               known.terminal_at + m_def.terminal_republish + m_def.max_clock_skew < now;
      }), m_terminal_commits.end());
  m_replay_cache_saturated = m_terminal_commits.size() >= m_def.replay_cache_size;
}

void Operator::rememberTerminal(const Envelope &commit) {
  pruneTerminalCache();
  for (Envelope &known : m_terminal_commits) {
    if (known.operation_id == commit.operation_id) {
      known = commit;
      return;
    }
  }
  if (m_terminal_commits.size() >= m_def.replay_cache_size) {
    m_replay_cache_saturated = true;
    return;
  }
  m_terminal_commits.push_back(commit);
}

const Operator::Envelope *Operator::findTerminal(const string &operation_id) const {
  for (auto it = m_terminal_commits.rbegin(); it != m_terminal_commits.rend(); ++it)
    if (it->operation_id == operation_id) return &*it;
  return nullptr;
}

void Operator::settlePendingFromSnapshot(const Envelope &env) {
  if (!m_have_proposal || env.accepted_event_sequence < m_proposal.actor_sequence) return;
  if (m_proposal.to == "ABORTING") m_cancel_requested = false;
  m_have_proposal = false;
  Notify(m_prefix + "_TRANSITION_RESULT", "COMMITTED");
  Notify(m_prefix + "_TRANSITION_REASON", env.reason);
}

void Operator::startQueuedProposal() {
  if (m_is_authority || m_have_proposal || !m_have_operation || isTerminal(m_phase)) return;
  string target;
  if (m_cancel_requested) {
    if (transitionAllowed("requester", m_phase, "ABORTING")) target = "ABORTING";
    else if (transitionAllowed("requester", m_phase, "CANCELLED")) target = "CANCELLED";
    else {
      Notify(m_prefix + "_TRANSITION_RESULT", "DEFERRED_LOCAL");
      Notify(m_prefix + "_TRANSITION_REASON", "CANCELLATION_PENDING_PHASE_ADVANCE");
    }
  }
  if (target.empty() && !m_queued_transitions.empty() &&
      transitionAllowed("requester", m_phase, m_queued_transitions.front())) {
    target = m_queued_transitions.front();
    m_queued_transitions.erase(m_queued_transitions.begin());
  }
  if (target.empty()) return;
  const string command = "id=" + m_request.operation_id + "#revision=" +
    to_string(m_revision) + "#to=" + target;
  handleLocalTransition(command);
}

bool Operator::acquireStateLock() {
  if (m_state_file.empty()) return true;
  const size_t slash = m_state_file.find_last_of('/');
  const string directory = slash == string::npos ? "." : m_state_file.substr(0, slash);
  struct stat directory_stat {};
  if (stat(directory.c_str(), &directory_stat) != 0 || !S_ISDIR(directory_stat.st_mode)) {
    reportConfigWarning("State directory does not exist: " + directory);
    return false;
  }
  m_state_lock_fd = open((m_state_file + ".lock").c_str(), O_CREAT | O_RDWR, 0600);
  if (m_state_lock_fd < 0 || flock(m_state_lock_fd, LOCK_EX | LOCK_NB) != 0) {
    reportConfigWarning("State file is already owned or cannot be locked: " + m_state_file);
    if (m_state_lock_fd >= 0) close(m_state_lock_fd);
    m_state_lock_fd = -1;
    return false;
  }
  struct stat temporary_stat {};
  if (lstat((m_state_file + ".tmp").c_str(), &temporary_stat) == 0) {
    reportConfigWarning("Unresolved state transaction requires recovery: " +
                        m_state_file + ".tmp");
    return false;
  }
  return true;
}

bool Operator::persistOrFault(const string &context) {
  if (saveState()) return true;
  m_storage_fault = true;
  m_local_state = "STORAGE_FAULT";
  reportRunWarning("Persistent state write failed: " + context);
  publishView();
  return false;
}

bool Operator::saveState() const {
  if (m_state_file.empty()) return true;
  ostringstream body;
  if (m_have_operation) {
    body << "REQUEST " << serializeEnvelope(m_request) << '\n';
    body << setprecision(17) << "STARTED " << m_operation_started << '\n';
  }
  if (m_have_commit) body << "CURRENT " << serializeEnvelope(m_commit) << '\n';
  if (m_have_proposal) body << "PROPOSAL " << serializeEnvelope(m_proposal) << '\n';
  body << "NEXT_ACTOR_SEQUENCE " << m_next_actor_sequence << '\n';
  for (const string &target : m_queued_transitions) body << "QUEUE " << target << '\n';
  for (const string &command_id : m_start_command_ids)
    body << "START_COMMAND " << command_id << '\n';
  if (m_cancel_requested) body << "CANCEL 1\n";
  if (m_event_queue_saturated) body << "EVENT_QUEUE_SATURATED 1\n";
  if (m_start_cache_saturated) body << "START_CACHE_SATURATED 1\n";
  if (m_reconciliation_required) body << "RECONCILIATION AUTHORITY_RESTARTED\n";
  for (const Envelope &terminal : m_terminal_commits)
    body << "TERMINAL " << serializeEnvelope(terminal) << '\n';
  const string body_text = body.str();
  const string temporary = m_state_file + ".tmp";
  ofstream output(temporary, ios::trunc);
  if (!output) return false;
  output << "P_OPERATOR_STATE 2\n" << body_text << "END " << fnv1a(body_text) << '\n';
  output.flush();
  if (!output.good()) return false;
  output.close();
  const int state_fd = open(temporary.c_str(), O_RDONLY);
  if (state_fd < 0 || fsync(state_fd) != 0) {
    if (state_fd >= 0) close(state_fd);
    return false;
  }
  close(state_fd);
  if (rename(temporary.c_str(), m_state_file.c_str()) != 0) return false;
  const size_t slash = m_state_file.find_last_of('/');
  const string directory = slash == string::npos ? "." : m_state_file.substr(0, slash);
  const int directory_fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
  if (directory_fd < 0) return false;
  const bool synced = fsync(directory_fd) == 0;
  close(directory_fd);
  return synced;
}

bool Operator::loadState() {
  struct stat state_stat {};
  if (stat(m_state_file.c_str(), &state_stat) != 0) {
    if (errno == ENOENT) return true;
    reportConfigWarning("Cannot inspect pOperator state file: " + m_state_file);
    return false;
  }
  ifstream input(m_state_file);
  if (!input) {
    reportConfigWarning("Cannot read pOperator state file: " + m_state_file);
    return false;
  }
  string line;
  if (!getline(input, line) || line != "P_OPERATOR_STATE 2") {
    reportConfigWarning("Invalid pOperator state file: " + m_state_file);
    return false;
  }
  vector<string> records;
  while (getline(input, line)) records.push_back(line);
  if (!input.eof() || records.empty() || records.back().compare(0, 4, "END ") != 0)
    return false;
  uint64_t expected_hash = 0;
  try {
    size_t consumed = 0;
    const string value = records.back().substr(4);
    expected_hash = stoull(value, &consumed);
    if (consumed != value.size()) return false;
  } catch (...) { return false; }
  records.pop_back();
  string body_text;
  for (const string &record : records) body_text += record + '\n';
  if (fnv1a(body_text) != expected_hash) return false;

  Envelope current;
  Envelope request;
  bool have_current = false;
  bool have_request = false;
  bool have_started = false;
  bool have_next_sequence = false;
  bool have_cancel = false;
  bool have_event_saturation = false;
  bool have_start_saturation = false;
  bool have_reconciliation = false;
  for (const string &record : records) {
    line = record;
    if (line.empty()) return false;
    const size_t space = line.find(' ');
    if (space == string::npos) return false;
    const string kind = line.substr(0, space);
    const string data = line.substr(space + 1);
    if (kind == "STARTED") {
      if (have_started || !parseDoubleExact(data, m_operation_started) ||
          m_operation_started <= 0) return false;
      have_started = true;
      continue;
    }
    if (kind == "NEXT_ACTOR_SEQUENCE") {
      if (have_next_sequence || !parseUnsigned(data, m_next_actor_sequence) ||
          m_next_actor_sequence == 0) return false;
      have_next_sequence = true;
      continue;
    }
    if (kind == "QUEUE") {
      if (!phaseDeclared(data) || m_queued_transitions.size() >= m_def.event_queue_size)
        return false;
      m_queued_transitions.push_back(data);
      continue;
    }
    if (kind == "START_COMMAND") {
      if (data.empty() || data.size() > 128 ||
          data.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-") != string::npos ||
          m_start_command_ids.size() >= m_def.replay_cache_size ||
          contains(m_start_command_ids, data)) return false;
      m_start_command_ids.push_back(data);
      continue;
    }
    if (kind == "CANCEL") {
      if (have_cancel || data != "1") return false;
      have_cancel = true;
      m_cancel_requested = true;
      continue;
    }
    if (kind == "EVENT_QUEUE_SATURATED") {
      if (have_event_saturation || data != "1") return false;
      have_event_saturation = true;
      m_event_queue_saturated = true;
      continue;
    }
    if (kind == "START_CACHE_SATURATED") {
      if (have_start_saturation || data != "1") return false;
      have_start_saturation = true;
      m_start_cache_saturated = true;
      continue;
    }
    if (kind == "RECONCILIATION") {
      if (have_reconciliation || data != "AUTHORITY_RESTARTED") return false;
      have_reconciliation = true;
      m_reconciliation_required = true;
      continue;
    }
    Envelope env;
    if (!parseEnvelope(data, env) || env.protocol != m_def.name ||
        env.version != m_def.version || env.hash != m_def.hash) {
      reportConfigWarning("Incompatible record in pOperator state file");
      return false;
    }
    if (kind == "REQUEST") {
      if (have_request) return false;
      request = env; have_request = true;
    } else if (kind == "CURRENT") {
      if (have_current) return false;
      current = env; have_current = true;
    } else if (kind == "PROPOSAL") {
      if (m_have_proposal) return false;
      m_proposal = env; m_have_proposal = true;
    }
    else if (kind == "TERMINAL") {
      if (!phaseDeclared(env.phase) || !isTerminal(env.phase) ||
          env.operation_id.empty() || env.authority_epoch.empty() ||
          env.terminal_at <= 0 || findTerminal(env.operation_id) ||
          m_terminal_commits.size() >= m_def.replay_cache_size) return false;
      rememberTerminal(env);
    }
    else return false;
  }
  if (!have_next_sequence) return false;
  if (m_start_command_ids.size() >= m_def.replay_cache_size)
    m_start_cache_saturated = true;
  if (m_terminal_commits.size() >= m_def.replay_cache_size)
    m_replay_cache_saturated = true;
  if (!have_current && !have_request) {
    if (have_started || m_have_proposal || !m_queued_transitions.empty() ||
        m_cancel_requested || m_event_queue_saturated || m_reconciliation_required)
      return false;
    return true;
  }
  if (have_current && !have_request) {
    reportConfigWarning("Current operation lacks its request in pOperator state file");
    return false;
  }
  if (have_request && !have_current) {
    if (m_is_authority || !have_started || request.action != "REQUEST" ||
        request.requester != m_identity || request.authority != m_authority ||
        request.authority_epoch.empty() || m_have_proposal) return false;
    m_request = request;
    m_have_operation = true;
    m_phase = m_def.initial;
    m_revision = 0;
    m_authority_epoch = request.authority_epoch;
    m_local_state = m_reconciliation_required ? "AUTHORITY_RESTARTED" :
      "WAITING_FOR_AUTHORITY";
    return true;
  }
  if (!have_started) return false;
  if (!phaseDeclared(current.phase) || current.revision == 0 ||
      (current.action != "TRANSITION_COMMIT" && current.action != "STATE_SNAPSHOT") ||
      current.operation_id != request.operation_id ||
      current.requester != request.requester ||
      current.requester_epoch != request.requester_epoch ||
      current.authority != request.authority ||
      current.authority_epoch.empty() ||
      (m_is_authority ? current.authority != m_identity : current.requester != m_identity) ||
      (m_have_proposal &&
       (m_proposal.operation_id != current.operation_id ||
        m_proposal.requester_epoch != current.requester_epoch ||
        m_proposal.authority_epoch != current.authority_epoch ||
        m_proposal.action != "TRANSITION_PROPOSAL" ||
        m_proposal.actor_sequence >= m_next_actor_sequence ||
        m_proposal.actor_sequence <= current.accepted_event_sequence))) {
    reportConfigWarning("Invalid current operation in pOperator state file");
    return false;
  }
  m_commit = current;
  m_have_commit = true;
  m_request = have_request ? request : current;
  m_request.action = "REQUEST";
  m_phase = current.phase;
  m_revision = current.revision;
  m_have_operation = true;
  m_authority_epoch = current.authority_epoch;
  m_last_requester_sequence = current.accepted_event_sequence;
  if (m_next_actor_sequence <= current.accepted_event_sequence) return false;
  m_terminal_at = isTerminal(m_phase) ? current.terminal_at : -1;
  if (isTerminal(m_phase) && m_terminal_at <= 0) return false;
  m_local_state = m_reconciliation_required ? "AUTHORITY_RESTARTED" :
    (isTerminal(m_phase) ? "TERMINAL" : "RECOVERY_REQUIRED");
  return true;
}

string Operator::phaseClass(const string &phase) const {
  if (contains(m_def.success, phase)) return "SUCCESS";
  if (contains(m_def.failure, phase)) return "FAILURE";
  if (contains(m_def.pending, phase)) return "PENDING";
  return phase.empty() ? "IDLE" : "UNKNOWN";
}

bool Operator::isTerminal(const string &phase) const {
  return contains(m_def.success, phase) || contains(m_def.failure, phase);
}

bool Operator::transitionAllowed(const string &actor, const string &from,
                                 const string &to) const {
  for (const Transition &transition : m_def.transitions) {
    if (transition.actor == actor && transition.from == from && transition.to == to)
      return true;
  }
  return false;
}

string Operator::upperName(const string &value) const {
  string result;
  for (char c : value)
    result += isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(toupper(c)) : '_';
  return result;
}

bool Operator::loadDefinition(const string &path) {
  ifstream input(path);
  if (!input) {
    reportConfigWarning("Unable to open operation definition: " + path);
    return false;
  }
  string content((istreambuf_iterator<char>(input)), istreambuf_iterator<char>());
  istringstream stream(content);
  string section, line;
  while (getline(stream, line)) {
    line = trimCopy(stripTomlComment(line));
    if (line.empty()) continue;
    if (line.front() == '[' && line.back() == ']') {
      section = line.substr(1, line.size() - 2);
      const bool transition_section = section.compare(0, 12, "transitions.") == 0 &&
        (section.substr(12) == "authority" || section.substr(12) == "requester" ||
         section.substr(12) == "protocol");
      if (!transition_section && section != "operation" && section != "wire" &&
          section != "states" && section != "admission" && section != "timing" &&
          section != "protocol") {
        reportConfigWarning("Unknown operation definition section: [" + section + "]");
        return false;
      }
      continue;
    }
    const size_t equal = line.find('=');
    if (equal == string::npos) {
      reportConfigWarning("Malformed operation definition line: " + line);
      return false;
    }
    const string key = trimCopy(line.substr(0, equal));
    string value = trimCopy(line.substr(equal + 1));
    while (!value.empty() && value.front() == '[' && value.find(']') == string::npos) {
      string continuation;
      if (!getline(stream, continuation)) break;
      value += trimCopy(stripTomlComment(continuation));
    }
    if (!value.empty() && value.front() == '[' && value.find(']') == string::npos) {
      reportConfigWarning("Unterminated operation definition array: " + key);
      return false;
    }
    try {
      if (section == "operation" && key == "name") m_def.name = unquote(value);
      else if (section == "operation" && key == "version") {
        unsigned int parsed = 0;
        if (!parseUnsigned(value, parsed)) throw invalid_argument("unsigned integer");
        m_def.version = parsed;
      }
      else if (section == "operation" && key == "initial") m_def.initial = toupper(unquote(value));
      else if (section == "wire" && key == "request_variable") m_def.request_var = unquote(value);
      else if (section == "wire" && key == "status_variable") m_def.status_var = unquote(value);
      else if (section == "states" && key == "pending") m_def.pending = parseArray(value);
      else if (section == "states" && key == "success") m_def.success = parseArray(value);
      else if (section == "states" && key == "failure") m_def.failure = parseArray(value);
      else if (section.compare(0, 12, "transitions.") == 0) {
        const string actor = tolower(section.substr(12));
        for (const string &target : parseArray(value))
          m_def.transitions.push_back({actor, toupper(unquote(key)), target});
      } else if (section == "admission" && key == "when_unready") m_def.when_unready = tolower(unquote(value));
      else if (section == "admission" && key == "when_busy") m_def.when_busy = tolower(unquote(value));
      else if (section == "admission" && key == "max_defer_time") {
        if (!parseDoubleExact(value, m_def.max_defer_time)) throw invalid_argument("number");
      } else if (section == "timing" && key == "request_ttl") {
        if (!parseDoubleExact(value, m_def.request_ttl)) throw invalid_argument("number");
      } else if (section == "timing" && key == "request_republish_interval") {
        if (!parseDoubleExact(value, m_def.request_republish)) throw invalid_argument("number");
      } else if (section == "timing" && key == "status_republish_interval") {
        if (!parseDoubleExact(value, m_def.status_republish)) throw invalid_argument("number");
      } else if (section == "timing" && key == "lease_timeout") {
        if (!parseDoubleExact(value, m_def.lease_timeout)) throw invalid_argument("number");
      } else if (section == "timing" && key == "max_clock_skew") {
        if (!parseDoubleExact(value, m_def.max_clock_skew)) throw invalid_argument("number");
      } else if (section == "timing" && key == "terminal_republish_time") {
        if (!parseDoubleExact(value, m_def.terminal_republish)) throw invalid_argument("number");
      } else if (section == "timing" && key == "readiness_max_age") {
        if (!parseDoubleExact(value, m_def.readiness_max_age)) throw invalid_argument("number");
      } else if (section == "protocol" && key == "replay_cache_size") {
        unsigned int parsed = 0;
        if (!parseUnsigned(value, parsed)) throw invalid_argument("unsigned integer");
        m_def.replay_cache_size = parsed;
      } else if (section == "protocol" && key == "event_queue_size") {
        unsigned int parsed = 0;
        if (!parseUnsigned(value, parsed)) throw invalid_argument("unsigned integer");
        m_def.event_queue_size = parsed;
      } else if (section == "protocol" && key == "require_correlated_commands") {
        if (!setBooleanOnString(m_def.require_correlated_commands, value))
          throw invalid_argument("boolean");
      } else {
        reportConfigWarning("Unknown operation definition field: [" + section + "]." + key);
        return false;
      }
    } catch (const exception &) {
      reportConfigWarning("Invalid operation definition value: " + line);
      return false;
    }
  }
  return validateDefinition();
}

bool Operator::validateDefinition() {
  if (m_def.name.empty() || m_def.initial.empty() || m_def.request_var.empty() ||
      m_def.status_var.empty() || m_def.success.empty() || m_def.failure.empty()) {
    reportConfigWarning("Operation definition is missing required fields");
    return false;
  }
  StringList states = m_def.pending;
  states.insert(states.end(), m_def.success.begin(), m_def.success.end());
  states.insert(states.end(), m_def.failure.begin(), m_def.failure.end());
  if (!contains(states, m_def.initial) || isTerminal(m_def.initial)) {
    reportConfigWarning("Initial state must be declared and nonterminal");
    return false;
  }
  for (size_t i = 0; i < states.size(); ++i) {
    if (states[i].empty() ||
        states[i].find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != string::npos) {
      reportConfigWarning("State names must use uppercase letters, digits, or underscore");
      return false;
    }
    if (count(states.begin(), states.end(), states[i]) != 1) {
      reportConfigWarning("State classes overlap or contain duplicates: " + states[i]);
      return false;
    }
  }
  if (m_def.transitions.empty() || !isfinite(m_def.max_defer_time) ||
      !isfinite(m_def.request_ttl) || !isfinite(m_def.request_republish) ||
      !isfinite(m_def.status_republish) || !isfinite(m_def.lease_timeout) ||
      !isfinite(m_def.max_clock_skew) || !isfinite(m_def.terminal_republish) ||
      !isfinite(m_def.readiness_max_age) || m_def.max_defer_time <= 0 ||
      m_def.request_ttl <= 0 || m_def.request_republish <= 0 ||
      m_def.status_republish <= 0 || m_def.lease_timeout <= 0 ||
      m_def.max_clock_skew < 0 || m_def.terminal_republish < 0 ||
      m_def.readiness_max_age <= 0 || m_def.replay_cache_size == 0 ||
      m_def.event_queue_size == 0) {
    reportConfigWarning("Invalid transition graph or protocol timing/cache value");
    return false;
  }
  if (m_def.when_unready != "defer" && m_def.when_unready != "reject") {
    reportConfigWarning("when_unready must be defer or reject");
    return false;
  }
  if (m_def.when_busy != "reject") {
    reportConfigWarning("Only when_busy=reject is currently supported");
    return false;
  }
  for (size_t i = 0; i < m_def.transitions.size(); ++i) {
    const Transition &transition = m_def.transitions[i];
    if (transition.actor != "requester" && transition.actor != "authority" &&
        transition.actor != "protocol") {
      reportConfigWarning("Unknown transition actor: " + transition.actor);
      return false;
    }
    if (!contains(states, transition.from) || !contains(states, transition.to)) {
      reportConfigWarning("Transition references an undeclared state: " +
                          transition.from + " -> " + transition.to);
      return false;
    }
    if (isTerminal(transition.from)) {
      reportConfigWarning("Terminal state has an outgoing transition: " + transition.from);
      return false;
    }
    for (size_t j = i + 1; j < m_def.transitions.size(); ++j) {
      const Transition &other = m_def.transitions[j];
      if (transition.actor == other.actor && transition.from == other.from &&
          transition.to == other.to) {
        reportConfigWarning("Duplicate transition: " + transition.from + " -> " + transition.to);
        return false;
      }
    }
  }
  const string admission = m_def.when_unready == "defer" ? "DEFERRED" : "REJECTED";
  if (!transitionAllowed("authority", m_def.initial, "ACCEPTED") ||
      !transitionAllowed("authority", m_def.initial, admission) ||
      !transitionAllowed("protocol", m_def.initial, "EXPIRED")) {
    reportConfigWarning("Definition lacks transitions required by admission policy");
    return false;
  }
  StringList pending = m_def.pending, success = m_def.success, failure = m_def.failure;
  sort(pending.begin(), pending.end());
  sort(success.begin(), success.end());
  sort(failure.begin(), failure.end());
  vector<string> transitions;
  for (const Transition &transition : m_def.transitions)
    transitions.push_back(transition.actor + ':' + transition.from + '>' + transition.to);
  sort(transitions.begin(), transitions.end());
  ostringstream semantic;
  semantic << setprecision(17);
  const auto append = [&](const string &value) {
    semantic << value.size() << ':' << value;
  };
  append("pOperator-wire-schema-3");
  append(m_def.name);
  append(to_string(m_def.version));
  append(m_def.initial);
  append(m_def.request_var);
  append(m_def.status_var);
  append("pending");
  for (const string &state : pending) append(state);
  append("success");
  for (const string &state : success) append(state);
  append("failure");
  for (const string &state : failure) append(state);
  append("transitions");
  for (const string &transition : transitions) append(transition);
  append(m_def.when_unready);
  append(m_def.when_busy);
  ostringstream numeric;
  numeric << setprecision(17) << m_def.max_defer_time << ' ' << m_def.request_ttl << ' '
          << m_def.request_republish << ' ' << m_def.status_republish << ' '
          << m_def.lease_timeout << ' ' << m_def.max_clock_skew << ' '
          << m_def.terminal_republish << ' ' << m_def.readiness_max_age << ' '
          << m_def.replay_cache_size << ' ' << m_def.event_queue_size << ' '
          << m_def.require_correlated_commands;
  append(numeric.str());
  m_def.hash = fnv1a(semantic.str());
  return true;
}

string Operator::jsonEscape(const string &value) const {
  string result;
  for (char c : value) {
    if (c == '"' || c == '\\') { result += '\\'; result += c; }
    else if (c == '\n') result += "\\n";
    else if (c == '\r') result += "\\r";
    else if (c == '\t') result += "\\t";
    else result += c;
  }
  return result;
}

string Operator::serializeEnvelope(const Envelope &env) const {
  ostringstream out;
  out << setprecision(17) << '{'
      << "\"protocol\":\"" << jsonEscape(env.protocol) << "\","
      << "\"version\":" << env.version << ','
      << "\"hash\":\"" << env.hash << "\","
      << "\"operation_id\":\"" << jsonEscape(env.operation_id) << "\","
      << "\"requester\":\"" << jsonEscape(env.requester) << "\","
      << "\"requester_epoch\":\"" << jsonEscape(env.requester_epoch) << "\","
      << "\"authority\":\"" << jsonEscape(env.authority) << "\","
      << "\"authority_epoch\":\"" << jsonEscape(env.authority_epoch) << "\","
      << "\"action\":\"" << jsonEscape(env.action) << "\","
      << "\"actor\":\"" << jsonEscape(env.actor) << "\","
      << "\"from\":\"" << jsonEscape(env.from) << "\","
      << "\"to\":\"" << jsonEscape(env.to) << "\","
      << "\"phase\":\"" << jsonEscape(env.phase) << "\","
      << "\"previous_phase\":\"" << jsonEscape(env.previous_phase) << "\","
      << "\"reason\":\"" << jsonEscape(env.reason) << "\","
      << "\"payload\":\"" << jsonEscape(env.payload) << "\","
      << "\"event_result\":\"" << jsonEscape(env.event_result) << "\","
      << "\"actor_sequence\":" << env.actor_sequence << ','
      << "\"accepted_event_sequence\":" << env.accepted_event_sequence << ','
      << "\"base_revision\":" << env.base_revision << ','
      << "\"revision\":" << env.revision << ','
      << "\"created_at\":" << env.created_at << ','
      << "\"deadline\":" << env.deadline << ','
      << "\"sent_at\":" << env.sent_at << ','
      << "\"terminal_at\":" << env.terminal_at << '}';
  return out.str();
}

bool Operator::parseEnvelope(const string &text, Envelope &env) const {
  if (text.empty() || text.size() > 16384) return false;
  Json::CharReaderBuilder builder;
  builder["allowComments"] = false;
  builder["allowTrailingCommas"] = false;
  builder["strictRoot"] = true;
  builder["failIfExtra"] = true;
  builder["rejectDupKeys"] = true;
  unique_ptr<Json::CharReader> reader(builder.newCharReader());
  Json::Value root;
  string errors;
  if (!reader->parse(text.data(), text.data() + text.size(), &root, &errors) ||
      !root.isObject()) return false;
  const vector<string> names = {
    "protocol", "version", "hash", "operation_id", "requester", "requester_epoch",
    "authority", "authority_epoch", "action", "actor", "actor_sequence",
    "accepted_event_sequence", "base_revision", "revision", "from", "to", "phase",
    "previous_phase", "reason", "payload", "event_result", "created_at", "deadline",
    "sent_at", "terminal_at"
  };
  if (root.getMemberNames().size() != names.size()) return false;
  for (const string &name : names) if (!root.isMember(name)) return false;
  const vector<string> string_names = {
    "protocol", "hash", "operation_id", "requester", "requester_epoch", "authority",
    "authority_epoch", "action", "actor", "from", "to", "phase", "previous_phase",
    "reason", "payload", "event_result"
  };
  for (const string &name : string_names) if (!root[name].isString()) return false;
  const vector<string> unsigned_names = {
    "version", "actor_sequence", "accepted_event_sequence", "base_revision", "revision"
  };
  for (const string &name : unsigned_names) {
    const Json::ValueType type = root[name].type();
    if (type != Json::uintValue &&
        (type != Json::intValue || root[name].asInt64() < 0)) return false;
  }
  if (!root["created_at"].isNumeric() || !root["deadline"].isNumeric() ||
      !root["sent_at"].isNumeric() || !root["terminal_at"].isNumeric()) return false;
  env.protocol = root["protocol"].asString();
  env.version = root["version"].asUInt();
  try {
    size_t consumed = 0;
    env.hash = stoull(root["hash"].asString(), &consumed);
    if (consumed != root["hash"].asString().size()) return false;
  } catch (...) { return false; }
  env.operation_id = root["operation_id"].asString();
  env.requester = root["requester"].asString();
  env.requester_epoch = root["requester_epoch"].asString();
  env.authority = root["authority"].asString();
  env.authority_epoch = root["authority_epoch"].asString();
  env.action = root["action"].asString();
  env.actor = root["actor"].asString();
  env.actor_sequence = root["actor_sequence"].asUInt();
  env.accepted_event_sequence = root["accepted_event_sequence"].asUInt();
  env.base_revision = root["base_revision"].asUInt();
  env.revision = root["revision"].asUInt();
  env.from = root["from"].asString();
  env.to = root["to"].asString();
  env.phase = root["phase"].asString();
  env.previous_phase = root["previous_phase"].asString();
  env.reason = root["reason"].asString();
  env.payload = root["payload"].asString();
  env.event_result = root["event_result"].asString();
  env.created_at = root["created_at"].asDouble();
  env.deadline = root["deadline"].asDouble();
  env.sent_at = root["sent_at"].asDouble();
  env.terminal_at = root["terminal_at"].asDouble();
  return isfinite(env.created_at) && isfinite(env.deadline) &&
         isfinite(env.sent_at) && isfinite(env.terminal_at) &&
         !env.protocol.empty() && !env.operation_id.empty() &&
         !env.requester.empty() && !env.requester_epoch.empty() &&
         !env.authority.empty() && !env.action.empty();
}

bool Operator::buildReport() {
  m_msgs << "Definition: " << m_definition_path << '\n';
  m_msgs << "Operation: " << m_def.name << " v" << m_def.version << '\n';
  m_msgs << "Identity: " << m_identity << '\n';
  m_msgs << "Authority: " << m_authority << '\n';
  m_msgs << "Role: " << (m_is_authority ? "AUTHORITY" : "REQUESTER") << '\n';
  m_msgs << "Epoch: " << m_epoch << '\n';
  m_msgs << "Operation ID: " << m_request.operation_id << '\n';
  m_msgs << "Phase: " << (m_phase.empty() ? "IDLE" : m_phase)
         << " (revision " << m_revision << ")\n";
  m_msgs << "Class: " << phaseClass(m_phase) << '\n';
  m_msgs << "Local state: " << m_local_state << '\n';
  m_msgs << "Pending proposal: " << boolToString(m_have_proposal) << '\n';
  m_msgs << "Ready: " << boolToString(m_ready) << " / " << m_ready_reason << '\n';
  return true;
}
