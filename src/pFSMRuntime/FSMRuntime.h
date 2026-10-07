/************************************************************/
/*    NAME: PEARL Team                                      */
/*    FILE: FSMRuntime.h                                    */
/*    DATE: September 2026                                  */
/************************************************************/

#ifndef FSM_RUNTIME_HEADER
#define FSM_RUNTIME_HEADER

#include <map>
#include <set>
#include <string>
#include <vector>

#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "LogicCondition.h"

struct FSMRuntimePredicate {
  std::string var;
  std::string op;
  std::string expected;
  std::string expression;
};

struct FSMRuntimeTransition {
  std::string id;
  std::string from;
  std::string to;
  std::string guard_text;
  LogicCondition guard;
  std::vector<std::string> guard_vars;
  // Scalar guard variable -> diagnostic uChecks snapshot variable.
  std::map<std::string, std::string> check_snapshots;
  std::vector<FSMRuntimePredicate> predicates;
};

struct FSMRuntimeSubstate {
  std::string parent;
  std::string section;
  std::string var;
};

struct FSMRuntimeEffect {
  std::string state;
  std::string phase;
  std::string var;
  std::string string_value;
  double double_value;
  bool is_double;
};

struct FSMRuntimeInitialValue {
  std::string var;
  std::string string_value;
  double double_value;
  bool is_double;
};

struct FSMTransitionOccurrence {
  unsigned long long occurrence_id = 0;
  double transition_time = 0;
  std::string machine;
  std::string id;
  std::string from;
  std::string to;
  std::map<std::string, std::string> string_context;
  std::map<std::string, double> double_context;
  // Full JSON objects published by uChecks, captured before exit effects.
  std::map<std::string, std::string> snapshot_context;
  std::set<std::string> missing_context;
};

struct FSMRuntimeMachine {
  // Configuration
  std::string id;
  std::string state_var;
  std::string initial_state;
  std::string initial_node;
  std::string initial_transition_id;
  std::string status_var;
  std::string transition_var;
  std::string active_state_var;
  std::string parent_state_var;
  std::string parent_state;
  std::string activation_condition_text;
  LogicCondition activation_condition;
  std::vector<std::string> activation_condition_vars;
  double post_interval;
  bool initially_active;
  bool reset_on_inactive;
  bool condition_history;
  std::vector<std::string> states;
  std::set<std::string> state_set;
  std::map<std::string, std::string> entry_epoch_vars;
  std::vector<FSMRuntimeTransition> transitions;
  std::vector<FSMRuntimeSubstate> substates;
  std::vector<FSMRuntimeEffect> effects;

  // Runtime
  std::string current_state;
  std::string last_status;
  std::string last_warning;
  std::string last_active_state;
  double last_state_post;
  double last_active_state_post;
  unsigned int transition_count;
  std::map<std::string, unsigned int> state_entry_counts;
  double state_active_elapsed;
  double state_active_since;
  bool active;
  bool initial_transition_pending;
  bool suspended_by_condition;

  FSMRuntimeMachine();
};

class FSMRuntime : public AppCastingMOOSApp {
 public:
  FSMRuntime();
  ~FSMRuntime() {}

 protected:
  bool OnNewMail(MOOSMSG_LIST& NewMail);
  bool Iterate();
  bool OnConnectToServer();
  bool OnStartUp();
  bool buildReport();

 private:
  void registerVariables();
  bool parseConfigLine(const std::string& original);
  bool addRuntime(const std::string& value);
  bool addInitialValue(const std::string& value);
  bool addMachine(const std::string& value);
  bool addState(const std::string& value);
  bool addEffect(const std::string& value);
  bool addTransition(const std::string& value);
  bool addSubstate(const std::string& value);
  bool validateConfiguration();
  void initializeRuntime();

  std::vector<std::string> hierarchyOrder(bool deepest_first) const;
  unsigned int machineDepth(const std::string& machine_id) const;
  std::map<std::string, bool> desiredActivation(
    const std::string& transition_machine="",
    const std::string& target_state="");
  bool deactivateUndesired(const std::map<std::string, bool>& desired);
  bool activateDesired(const std::map<std::string, bool>& desired);
  void deactivateMachine(FSMRuntimeMachine& machine, bool reset,
                         bool execute_exit_effects);
  void activateMachine(FSMRuntimeMachine& machine);
  bool activationConditionAllows(FSMRuntimeMachine& machine,
                                 bool& inputs_ready);
  bool evaluateGuard(FSMRuntimeTransition& transition,
                     bool& inputs_ready);
  double activeStateTime(const FSMRuntimeMachine& machine) const;
  double transitionStateTime(const FSMRuntimeMachine& machine,
                             const FSMRuntimeTransition& transition) const;
  bool minimumStateTimeElapsed(const FSMRuntimeMachine& machine) const;
  std::string transitionEvaluation(FSMRuntimeMachine& machine,
                                   FSMRuntimeTransition& transition);
  std::string observedValue(const std::string& var) const;
  std::string observedType(const std::string& var) const;
  std::vector<FSMRuntimePredicate> parsePredicates(
    const std::string& guard) const;
  std::string predicateEvaluation(const FSMRuntimePredicate& predicate) const;
  void takeTransition(FSMRuntimeMachine& machine,
                      const FSMRuntimeTransition& transition,
                      const FSMTransitionOccurrence& matched_context);
  void publishEffects(FSMRuntimeMachine& machine,
                      const std::string& state,
                      const std::string& phase);

  void publishState(FSMRuntimeMachine& machine, bool force);
  void publishActiveState(FSMRuntimeMachine& machine, bool force);
  void publishInitialTransition(FSMRuntimeMachine& machine);
  FSMTransitionOccurrence captureCandidateContext(
    const FSMRuntimeMachine& machine, const std::string& from) const;
  void recordTransition(const FSMRuntimeMachine& machine,
                        const std::string& id, const std::string& from,
                        const std::string& to,
                        const FSMTransitionOccurrence* matched_context=nullptr);
  void publishTransitionBatch();
  std::string jsonEscape(const std::string& value) const;
  void publishStateEntryCount(FSMRuntimeMachine& machine);
  void publishStatus(FSMRuntimeMachine& machine,
                     const std::string& status,
                     bool warning=false);

  void cacheString(const std::string& var, const std::string& value);
  void cacheDouble(const std::string& var, double value);
  void notifyString(const std::string& var, const std::string& value);
  void notifyDouble(const std::string& var, double value);
  bool valueString(const std::string& var, std::string& value) const;
  bool valueDouble(const std::string& var, double& value) const;

  std::string normalizeGuard(const std::string& value) const;
  bool validName(const std::string& value) const;
  std::map<std::string, std::string> parseFields(
    const std::string& value) const;

 private:
  std::vector<FSMRuntimeInitialValue> m_initial_values;
  std::vector<std::string> m_machine_order;
  std::map<std::string, FSMRuntimeMachine> m_machines;
  std::map<std::string, std::string> m_string_values;
  std::map<std::string, double> m_double_values;
  std::set<std::string> m_registered_vars;
  std::map<std::string, bool> m_condition_resets;
  std::string m_runtime_state_var;
  std::string m_runtime_heartbeat_var;
  std::string m_runtime_epoch_var;
  double m_heartbeat_interval;
  double m_last_heartbeat;
  double m_epoch;
  double m_min_state_time;
  bool m_config_valid;
  bool m_initialized;
  unsigned int m_iterations;
  unsigned int m_transitions_taken;
  unsigned long long m_transition_occurrence_id;
  std::vector<FSMTransitionOccurrence> m_cycle_transitions;
};

#endif
