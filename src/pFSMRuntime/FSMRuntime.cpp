/************************************************************/
/*    NAME: PEARL Team                                      */
/*    FILE: FSMRuntime.cpp                                  */
/*    DATE: September 2026                                  */
/************************************************************/

#include "FSMRuntime.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <queue>
#include <regex>
#include <sstream>

#include "ACTable.h"
#include "MBUtils.h"

using namespace std;

FSMRuntimeMachine::FSMRuntimeMachine()
{
  initial_node = "start";
  initial_transition_id = "INITIAL";
  post_interval = 1.0;
  initially_active = false;
  reset_on_inactive = false;
  condition_history = true;
  last_state_post = -1.0;
  last_active_state_post = -1.0;
  transition_count = 0;
  state_active_elapsed = 0;
  state_active_since = -1;
  active = false;
  initial_transition_pending = true;
  suspended_by_condition = false;
}

FSMRuntime::FSMRuntime()
{
  m_heartbeat_interval = 1.0;
  m_last_heartbeat = -1.0;
  m_epoch = 0;
  m_min_state_time = 0;
  m_config_valid = true;
  m_initialized = false;
  m_iterations = 0;
  m_transitions_taken = 0;
  m_transition_occurrence_id = 0;
}

bool FSMRuntime::OnNewMail(MOOSMSG_LIST& NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  for(MOOSMSG_LIST::iterator p=NewMail.begin(); p!=NewMail.end(); ++p) {
    const CMOOSMsg& msg = *p;
    if(msg.IsString())
      cacheString(msg.GetKey(), msg.GetString());
    else if(msg.IsDouble())
      cacheDouble(msg.GetKey(), msg.GetDouble());
  }
  return true;
}

bool FSMRuntime::Iterate()
{
  AppCastingMOOSApp::Iterate();
  ++m_iterations;
  m_cycle_transitions.clear();

  if(!m_config_valid || !m_initialized) {
    AppCastingMOOSApp::PostReport();
    return true;
  }

  const double now = MOOSTime();
  if((m_last_heartbeat < 0) ||
     ((now - m_last_heartbeat) >= m_heartbeat_interval)) {
    notifyDouble(m_runtime_heartbeat_var, now);
    m_last_heartbeat = now;
  }

  const map<string, bool> desired = desiredActivation();
  bool activation_changed = deactivateUndesired(desired);
  activation_changed = activateDesired(desired) || activation_changed;

  struct Candidate {
    string machine_id;
    unsigned int transition_index;
    FSMTransitionOccurrence matched_context;
  };
  vector<Candidate> candidates;
  bool ambiguous = false;

  if(!activation_changed) {
    for(unsigned int i=0; i<m_machine_order.size(); ++i) {
      FSMRuntimeMachine& machine = m_machines[m_machine_order[i]];
      if(!machine.active)
        continue;

      bool has_outgoing_transition = false;
      for(unsigned int j=0; j<machine.transitions.size(); ++j) {
        if(machine.transitions[j].from == machine.current_state) {
          has_outgoing_transition = true;
          break;
        }
      }
      if(has_outgoing_transition && !minimumStateTimeElapsed(machine)) {
        publishStatus(machine, "state=" + machine.current_state +
                               ",status=minimum_dwell");
        continue;
      }

      vector<unsigned int> matches;
      bool missing_inputs = false;
      bool waiting_for_child_dwell = false;
      for(unsigned int j=0; j<machine.transitions.size(); ++j) {
        FSMRuntimeTransition& transition = machine.transitions[j];
        if(transition.from != machine.current_state)
          continue;
        bool inputs_ready = false;
        if(evaluateGuard(transition, inputs_ready) && inputs_ready) {
          if(transitionStateTime(machine, transition) >= m_min_state_time)
            matches.push_back(j);
          else
            waiting_for_child_dwell = true;
        }
        if(!inputs_ready)
          missing_inputs = true;
      }

      if(matches.size() > 1) {
        string targets;
        for(unsigned int j=0; j<matches.size(); ++j) {
          if(!targets.empty())
            targets += ":";
          targets += machine.transitions[matches[j]].to;
        }
        publishStatus(machine,
                      "state=" + machine.current_state +
                      ",status=ambiguous,targets=" + targets,
                      true);
        ambiguous = true;
      }
      else if(matches.size() == 1)
        candidates.push_back(Candidate{machine.id, matches[0],
          captureCandidateContext(machine, machine.current_state)});
      else if(waiting_for_child_dwell)
        publishStatus(machine, "state=" + machine.current_state +
                               ",status=minimum_dwell");
      else if(missing_inputs)
        publishStatus(machine, "state=" + machine.current_state +
                               ",status=waiting_for_inputs");
      else
        publishStatus(machine, "state=" + machine.current_state +
                               ",status=holding");
    }
  }

  // A configuration ambiguity holds the complete hierarchy. Otherwise the
  // first machine in generated hierarchy order wins, ensuring at most one
  // guarded transition per application iteration.
  if(!ambiguous && !candidates.empty()) {
    const Candidate& candidate = candidates.front();
    FSMRuntimeMachine& machine = m_machines[candidate.machine_id];
    takeTransition(machine, machine.transitions[candidate.transition_index],
                   candidate.matched_context);
    for(unsigned int i=1; i<candidates.size(); ++i) {
      FSMRuntimeMachine& deferred = m_machines[candidates[i].machine_id];
      publishStatus(deferred, "state=" + deferred.current_state +
                              ",status=deferred");
    }
  }

  for(unsigned int i=0; i<m_machine_order.size(); ++i) {
    FSMRuntimeMachine& machine = m_machines[m_machine_order[i]];
    if(machine.active)
      publishState(machine, false);
  }
  // Aggregate parents after child state publications.
  for(unsigned int i=0; i<m_machine_order.size(); ++i) {
    FSMRuntimeMachine& machine = m_machines[m_machine_order[i]];
    if(machine.active)
      publishActiveState(machine, false);
  }

  publishTransitionBatch();
  AppCastingMOOSApp::PostReport();
  return true;
}

bool FSMRuntime::OnConnectToServer()
{
  registerVariables();
  return true;
}

bool FSMRuntime::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();

  STRING_LIST params;
  m_MissionReader.EnableVerbatimQuoting(false);
  if(!m_MissionReader.GetConfiguration(GetAppName(), params)) {
    reportConfigWarning("No config block found for " + GetAppName());
    m_config_valid = false;
  }

  // MOOS mission-reader configuration lists are returned in reverse file
  // order. Generated records declare each machine before its states and
  // transitions, so restore authored order before parsing.
  params.reverse();
  for(STRING_LIST::iterator p=params.begin(); p!=params.end(); ++p) {
    if(!parseConfigLine(*p)) {
      reportUnhandledConfigWarning(*p);
      m_config_valid = false;
    }
  }

  if(!validateConfiguration())
    m_config_valid = false;

  registerVariables();
  if(m_config_valid)
    initializeRuntime();
  return true;
}

void FSMRuntime::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  if(!m_config_valid)
    return;

  set<string> variables;
  for(unsigned int i=0; i<m_machine_order.size(); ++i) {
    const FSMRuntimeMachine& machine = m_machines[m_machine_order[i]];
    if(!machine.parent_state_var.empty())
      variables.insert(machine.parent_state_var);
    variables.insert(machine.activation_condition_vars.begin(),
                     machine.activation_condition_vars.end());
    for(unsigned int j=0; j<machine.substates.size(); ++j)
      variables.insert(machine.substates[j].var);
    for(unsigned int j=0; j<machine.transitions.size(); ++j) {
      const FSMRuntimeTransition& transition = machine.transitions[j];
      variables.insert(transition.guard_vars.begin(), transition.guard_vars.end());
      for(const auto& entry : transition.check_snapshots)
        variables.insert(entry.second);
    }
  }

  for(set<string>::const_iterator p=variables.begin(); p!=variables.end(); ++p) {
    if(m_registered_vars.insert(*p).second)
      Register(*p, 0);
  }
}

bool FSMRuntime::parseConfigLine(const string& original)
{
  string line = original;
  string param = tolower(biteStringX(line, '='));
  string value = stripBlankEnds(line);

  if((param == "apptick") || (param == "commstick"))
    return true;
  if(param == "min_state_time")
    return setNonNegDoubleOnString(m_min_state_time, value);
  if(param == "runtime")
    return addRuntime(value);
  if((param == "initialize") || (param == "fact"))
    return addInitialValue(value);
  if(param == "machine")
    return addMachine(value);
  if(param == "state")
    return addState(value);
  if(param == "effect")
    return addEffect(value);
  if(param == "transition")
    return addTransition(value);
  if(param == "substate")
    return addSubstate(value);
  return false;
}

map<string, string> FSMRuntime::parseFields(const string& value) const
{
  map<string, string> fields_out;
  vector<string> fields = parseStringQ(value, ',');
  for(unsigned int i=0; i<fields.size(); ++i) {
    string field = fields[i];
    string key = tolower(stripBlankEnds(biteStringX(field, '=')));
    string val = stripQuotes(stripBlankEnds(field));
    if(key.empty() || fields_out.count(key))
      return map<string, string>();
    fields_out[key] = val;
  }
  return fields_out;
}

bool FSMRuntime::addRuntime(const string& value)
{
  map<string, string> fields = parseFields(value);
  if(!fields.count("state_var") || !fields.count("heartbeat_var") ||
     !fields.count("epoch_var"))
    return false;
  if(!m_runtime_state_var.empty())
    return false;

  string checked;
  m_runtime_state_var = fields["state_var"];
  m_runtime_heartbeat_var = fields["heartbeat_var"];
  m_runtime_epoch_var = fields["epoch_var"];
  if(!setNonWhiteVarOnString(checked, m_runtime_state_var) ||
     !setNonWhiteVarOnString(checked, m_runtime_heartbeat_var) ||
     !setNonWhiteVarOnString(checked, m_runtime_epoch_var))
    return false;
  if(fields.count("heartbeat_interval") &&
     !setPosDoubleOnString(m_heartbeat_interval,
                           fields["heartbeat_interval"]))
    return false;
  return true;
}

bool FSMRuntime::addInitialValue(const string& value)
{
  map<string, string> fields = parseFields(value);
  if(!fields.count("var") || !fields.count("type") || !fields.count("value"))
    return false;

  FSMRuntimeInitialValue initial;
  initial.var = fields["var"];
  string type = tolower(fields["type"]);
  if(!setNonWhiteVarOnString(initial.var, initial.var))
    return false;
  if(type == "double") {
    initial.is_double = true;
    if(!setDoubleOnString(initial.double_value, fields["value"]))
      return false;
  }
  else if(type == "string") {
    initial.is_double = false;
    initial.string_value = fields["value"];
  }
  else
    return false;

  for(unsigned int i=0; i<m_initial_values.size(); ++i) {
    if(m_initial_values[i].var == initial.var)
      return false;
  }
  m_initial_values.push_back(initial);
  return true;
}

bool FSMRuntime::addMachine(const string& value)
{
  map<string, string> fields = parseFields(value);
  const char* required[] = {"id", "state_var", "initial_state",
                            "status_var", "transition_var"};
  for(unsigned int i=0; i<5; ++i) {
    if(!fields.count(required[i]))
      return false;
  }

  FSMRuntimeMachine machine;
  machine.id = fields["id"];
  machine.state_var = fields["state_var"];
  machine.initial_state = fields["initial_state"];
  machine.status_var = fields["status_var"];
  machine.transition_var = fields["transition_var"];
  if(fields.count("initial_node"))
    machine.initial_node = fields["initial_node"];
  if(fields.count("initial_transition_id"))
    machine.initial_transition_id = fields["initial_transition_id"];
  if(fields.count("active_state_var"))
    machine.active_state_var = fields["active_state_var"];
  if(fields.count("parent_state_var"))
    machine.parent_state_var = fields["parent_state_var"];
  if(fields.count("parent_state"))
    machine.parent_state = fields["parent_state"];
  if(fields.count("activation_condition")) {
    machine.activation_condition_text = fields["activation_condition"];
    if(machine.activation_condition_text.empty() ||
       !machine.activation_condition.setCondition(
         normalizeGuard(machine.activation_condition_text)))
      return false;
    machine.activation_condition_vars = removeDuplicates(
      machine.activation_condition.getVarNames());
  }
  if(fields.count("condition_history") &&
     !setBooleanOnString(machine.condition_history,
                         fields["condition_history"]))
    return false;
  if(fields.count("initially_active") &&
     !setBooleanOnString(machine.initially_active, fields["initially_active"]))
    return false;
  if(fields.count("reset_on_inactive") &&
     !setBooleanOnString(machine.reset_on_inactive,
                         fields["reset_on_inactive"]))
    return false;
  if(fields.count("post_interval") &&
     !setNonNegDoubleOnString(machine.post_interval, fields["post_interval"]))
    return false;

  string checked;
  if(!validName(machine.id) || !validName(machine.initial_state) ||
     !validName(machine.initial_node) ||
     !validName(machine.initial_transition_id) ||
     !setNonWhiteVarOnString(checked, machine.state_var) ||
     !setNonWhiteVarOnString(checked, machine.status_var) ||
     !setNonWhiteVarOnString(checked, machine.transition_var) ||
     (!machine.active_state_var.empty() &&
      !setNonWhiteVarOnString(checked, machine.active_state_var)) ||
     (!machine.parent_state_var.empty() &&
      !setNonWhiteVarOnString(checked, machine.parent_state_var)) ||
     (!machine.parent_state.empty() && !validName(machine.parent_state)) ||
     m_machines.count(machine.id))
    return false;

  m_machine_order.push_back(machine.id);
  m_machines[machine.id] = machine;
  return true;
}

bool FSMRuntime::addState(const string& value)
{
  map<string, string> fields = parseFields(value);
  if(!fields.count("machine") || !fields.count("value"))
    return false;
  map<string, FSMRuntimeMachine>::iterator p=m_machines.find(fields["machine"]);
  if((p == m_machines.end()) || !validName(fields["value"]) ||
     p->second.state_set.count(fields["value"]))
    return false;
  if(fields.count("entry_epoch_var")) {
    string checked;
    if(!setNonWhiteVarOnString(checked, fields["entry_epoch_var"]))
      return false;
    p->second.entry_epoch_vars[fields["value"]] = checked;
  }
  p->second.states.push_back(fields["value"]);
  p->second.state_set.insert(fields["value"]);
  return true;
}

bool FSMRuntime::addEffect(const string& value)
{
  map<string, string> fields = parseFields(value);
  const char* required[] = {"machine", "state", "phase", "var", "type",
                            "value"};
  for(unsigned int i=0; i<6; ++i) {
    if(!fields.count(required[i]))
      return false;
  }

  map<string, FSMRuntimeMachine>::iterator p=m_machines.find(fields["machine"]);
  if(p == m_machines.end())
    return false;

  FSMRuntimeEffect effect;
  effect.state = fields["state"];
  effect.phase = tolower(fields["phase"]);
  effect.var = fields["var"];
  const string type = tolower(fields["type"]);
  string checked;
  if(!validName(effect.state) ||
     ((effect.phase != "entry") && (effect.phase != "exit")) ||
     !setNonWhiteVarOnString(checked, effect.var))
    return false;

  if(type == "double") {
    effect.is_double = true;
    if(!setDoubleOnString(effect.double_value, fields["value"]))
      return false;
  }
  else if(type == "string") {
    effect.is_double = false;
    effect.string_value = fields["value"];
  }
  else
    return false;

  for(unsigned int i=0; i<p->second.effects.size(); ++i) {
    const FSMRuntimeEffect& existing = p->second.effects[i];
    if((existing.state == effect.state) &&
       (existing.phase == effect.phase) &&
       (existing.var == effect.var))
      return false;
  }
  p->second.effects.push_back(effect);
  return true;
}

bool FSMRuntime::addTransition(const string& value)
{
  map<string, string> fields = parseFields(value);
  const char* required[] = {"machine", "id", "from", "to", "guard"};
  for(unsigned int i=0; i<5; ++i) {
    if(!fields.count(required[i]))
      return false;
  }
  map<string, FSMRuntimeMachine>::iterator p=m_machines.find(fields["machine"]);
  if(p == m_machines.end())
    return false;

  FSMRuntimeTransition transition;
  transition.id = fields["id"];
  transition.from = fields["from"];
  transition.to = fields["to"];
  transition.guard_text = fields["guard"];
  if(!validName(transition.id) || !validName(transition.from) ||
     !validName(transition.to) || transition.guard_text.empty() ||
     !transition.guard.setCondition(normalizeGuard(transition.guard_text)))
    return false;
  transition.guard_vars = removeDuplicates(transition.guard.getVarNames());
  transition.predicates = parsePredicates(transition.guard_text);
  if(fields.count("check_snapshots")) {
    for(const string& pair : parseString(fields["check_snapshots"], ';')) {
      string remaining = pair;
      const string result = biteStringX(remaining, ':');
      if(!validName(result) || !validName(remaining) ||
         find(transition.guard_vars.begin(), transition.guard_vars.end(), result) ==
           transition.guard_vars.end() ||
         !transition.check_snapshots.insert({result, remaining}).second)
        return false;
    }
    if(transition.check_snapshots.empty())
      return false;
  }

  for(unsigned int i=0; i<p->second.transitions.size(); ++i) {
    if(p->second.transitions[i].id == transition.id)
      return false;
  }
  p->second.transitions.push_back(transition);
  return true;
}

bool FSMRuntime::addSubstate(const string& value)
{
  map<string, string> fields = parseFields(value);
  if(!fields.count("machine") || !fields.count("parent") ||
     !fields.count("var"))
    return false;
  map<string, FSMRuntimeMachine>::iterator p=m_machines.find(fields["machine"]);
  if(p == m_machines.end())
    return false;

  FSMRuntimeSubstate substate;
  substate.parent = fields["parent"];
  substate.var = fields["var"];
  if(fields.count("section"))
    substate.section = fields["section"];
  string checked;
  if(!validName(substate.parent) ||
     (!substate.section.empty() && !validName(substate.section)) ||
     !setNonWhiteVarOnString(checked, substate.var))
    return false;
  p->second.substates.push_back(substate);
  return true;
}

bool FSMRuntime::validateConfiguration()
{
  if(m_machines.empty()) {
    reportConfigWarning("pFSMRuntime requires at least one machine");
    return false;
  }

  bool valid = true;
  if(m_runtime_state_var.empty() || m_runtime_heartbeat_var.empty() ||
     m_runtime_epoch_var.empty()) {
    reportConfigWarning("pFSMRuntime requires one runtime health record");
    valid = false;
  }
  set<string> state_vars;
  for(unsigned int i=0; i<m_machine_order.size(); ++i) {
    FSMRuntimeMachine& machine = m_machines[m_machine_order[i]];
    if(machine.states.empty()) {
      reportConfigWarning("machine " + machine.id + " has no states");
      valid = false;
    }
    if(!machine.state_set.count(machine.initial_state)) {
      reportConfigWarning("machine " + machine.id +
                          " has an undeclared initial state");
      valid = false;
    }
    if(!state_vars.insert(machine.state_var).second) {
      reportConfigWarning("duplicate FSM state variable: " + machine.state_var);
      valid = false;
    }
    if(machine.parent_state_var.empty() != machine.parent_state.empty()) {
      reportConfigWarning("machine " + machine.id +
                          " requires both parent_state_var and parent_state");
      valid = false;
    }
    if(!machine.substates.empty() && machine.active_state_var.empty()) {
      reportConfigWarning("machine " + machine.id +
                          " has substates but no active_state_var");
      valid = false;
    }

    map<string, unsigned int> child_counts;
    for(unsigned int j=0; j<machine.substates.size(); ++j) {
      FSMRuntimeSubstate& substate = machine.substates[j];
      if(!machine.state_set.count(substate.parent)) {
        reportConfigWarning("machine " + machine.id +
                            " has an undeclared substate parent");
        valid = false;
      }
      ++child_counts[substate.parent];
    }
    for(unsigned int j=0; j<machine.substates.size(); ++j) {
      FSMRuntimeSubstate& substate = machine.substates[j];
      if((child_counts[substate.parent] > 1) && substate.section.empty()) {
        reportConfigWarning("parallel substate missing section in machine " +
                            machine.id);
        valid = false;
      }
    }

    for(unsigned int j=0; j<machine.effects.size(); ++j) {
      const FSMRuntimeEffect& effect = machine.effects[j];
      if(!machine.state_set.count(effect.state)) {
        reportConfigWarning("effect references undeclared state " +
                            effect.state + " in machine " + machine.id);
        valid = false;
      }
    }

    for(unsigned int j=0; j<machine.transitions.size(); ++j) {
      FSMRuntimeTransition& transition = machine.transitions[j];
      if((transition.id == machine.initial_transition_id) ||
         !machine.state_set.count(transition.from) ||
         !machine.state_set.count(transition.to)) {
        reportConfigWarning("invalid transition " + transition.id +
                            " in machine " + machine.id);
        valid = false;
      }
    }

    set<string> reachable;
    queue<string> pending;
    reachable.insert(machine.initial_state);
    pending.push(machine.initial_state);
    while(!pending.empty()) {
      string from = pending.front();
      pending.pop();
      for(unsigned int j=0; j<machine.transitions.size(); ++j) {
        if(machine.transitions[j].from != from)
          continue;
        const string& to = machine.transitions[j].to;
        if(reachable.insert(to).second)
          pending.push(to);
      }
    }
    for(unsigned int j=0; j<machine.states.size(); ++j) {
      if(!reachable.count(machine.states[j])) {
        reportConfigWarning("unreachable state " + machine.states[j] +
                            " in machine " + machine.id);
        valid = false;
      }
    }
  }
  return valid;
}

void FSMRuntime::initializeRuntime()
{
  m_epoch = MOOSTime();
  notifyString(m_runtime_state_var, "STARTING");
  notifyDouble(m_runtime_epoch_var, m_epoch);
  notifyDouble(m_runtime_heartbeat_var, m_epoch);
  m_last_heartbeat = m_epoch;

  for(unsigned int i=0; i<m_initial_values.size(); ++i) {
    const FSMRuntimeInitialValue& initial = m_initial_values[i];
    if(initial.is_double)
      notifyDouble(initial.var, initial.double_value);
    else
      notifyString(initial.var, initial.string_value);
  }

  // Establish every internal state before publishing parents so aggregate
  // state construction can read active children without a MOOS round trip.
  for(unsigned int i=0; i<m_machine_order.size(); ++i) {
    FSMRuntimeMachine& machine = m_machines[m_machine_order[i]];
    machine.current_state = machine.initial_state;
    machine.active = false;
    machine.initial_transition_pending = true;
    machine.state_active_elapsed = 0;
    machine.state_active_since = -1;
  }
  map<string, bool> initial_activation = desiredActivation();
  for(unsigned int i=0; i<m_machine_order.size(); ++i) {
    FSMRuntimeMachine& machine = m_machines[m_machine_order[i]];
    machine.active = initial_activation[machine.id];
    machine.state_active_since = machine.active ? m_epoch : -1;
    cacheString(machine.state_var,
                machine.active ? machine.current_state : "none");
  }

  // Establish the externally visible hierarchy in a deterministic order:
  // inactive descendants clear from the inside out, then active machines
  // publish from the root toward their leaves.
  const vector<string> inner_first = hierarchyOrder(true);
  for(unsigned int i=0; i<inner_first.size(); ++i) {
    FSMRuntimeMachine& machine = m_machines[inner_first[i]];
    if(!machine.active) {
      notifyString(machine.state_var, "none");
      publishStatus(machine, "state=none,status=inactive");
    }
  }
  const vector<string> outer_first = hierarchyOrder(false);
  for(unsigned int i=0; i<outer_first.size(); ++i) {
    FSMRuntimeMachine& machine = m_machines[outer_first[i]];
    if(machine.active) {
      publishState(machine, true);
      publishEffects(machine, machine.current_state, "entry");
      publishStateEntryCount(machine);
      publishInitialTransition(machine);
      publishStatus(machine, "state=" + machine.current_state +
                             ",status=initialized");
    }
  }
  // Aggregates depend on child state values, so build them from leaves back
  // to the outermost state after all individual active states are visible.
  for(unsigned int i=0; i<inner_first.size(); ++i) {
    FSMRuntimeMachine& machine = m_machines[inner_first[i]];
    if(machine.active)
      publishActiveState(machine, true);
  }
  notifyString(m_runtime_state_var, "READY");
  publishTransitionBatch();
  m_initialized = true;
}

unsigned int FSMRuntime::machineDepth(const string& machine_id) const
{
  map<string, FSMRuntimeMachine>::const_iterator current =
    m_machines.find(machine_id);
  if(current == m_machines.end())
    return 0;

  unsigned int depth = 0;
  set<string> visited;
  visited.insert(machine_id);
  while(!current->second.parent_state_var.empty()) {
    map<string, FSMRuntimeMachine>::const_iterator parent = m_machines.end();
    for(map<string, FSMRuntimeMachine>::const_iterator p=m_machines.begin();
        p!=m_machines.end(); ++p) {
      if(p->second.state_var == current->second.parent_state_var) {
        parent = p;
        break;
      }
    }
    if((parent == m_machines.end()) || !visited.insert(parent->first).second)
      break;
    ++depth;
    current = parent;
  }
  return depth;
}

vector<string> FSMRuntime::hierarchyOrder(bool deepest_first) const
{
  vector<string> ordered = m_machine_order;
  stable_sort(ordered.begin(), ordered.end(),
              [this, deepest_first](const string& lhs, const string& rhs) {
    const unsigned int lhs_depth = machineDepth(lhs);
    const unsigned int rhs_depth = machineDepth(rhs);
    return deepest_first ? (lhs_depth > rhs_depth) :
                           (lhs_depth < rhs_depth);
  });
  return ordered;
}

map<string, bool> FSMRuntime::desiredActivation(
  const string& transition_machine,
  const string& target_state)
{
  map<string, bool> desired;
  m_condition_resets.clear();
  const vector<string> outer_first = hierarchyOrder(false);
  for(unsigned int i=0; i<outer_first.size(); ++i) {
    const FSMRuntimeMachine& machine = m_machines.find(outer_first[i])->second;
    bool condition_inputs_ready = true;
    FSMRuntimeMachine& mutable_machine = m_machines[machine.id];
    const bool condition_allowed = activationConditionAllows(
      mutable_machine, condition_inputs_ready);
    if(!condition_allowed)
      m_condition_resets[machine.id] = !machine.condition_history;

    if(machine.parent_state_var.empty()) {
      desired[machine.id] = machine.initially_active && condition_allowed;
      continue;
    }

    const FSMRuntimeMachine* parent = 0;
    for(map<string, FSMRuntimeMachine>::const_iterator p=m_machines.begin();
        p!=m_machines.end(); ++p) {
      if(p->second.state_var == machine.parent_state_var) {
        parent = &p->second;
        break;
      }
    }
    if(parent) {
      const string& parent_state = (parent->id == transition_machine) ?
        target_state : parent->current_state;
      desired[machine.id] = desired[parent->id] && condition_allowed &&
                            (parent_state == machine.parent_state);
      if(m_condition_resets.count(parent->id))
        m_condition_resets[machine.id] = m_condition_resets[parent->id];
    }
    else {
      string parent_value;
      desired[machine.id] = condition_allowed &&
                            valueString(machine.parent_state_var, parent_value) &&
                            (parent_value == machine.parent_state);
    }
  }
  return desired;
}

void FSMRuntime::deactivateMachine(FSMRuntimeMachine& machine, bool reset,
                                   bool execute_exit_effects)
{
  const double now = MOOSTime();
  if(machine.state_active_since >= 0)
    machine.state_active_elapsed += now - machine.state_active_since;
  machine.state_active_since = -1;
  if(execute_exit_effects)
    publishEffects(machine, machine.current_state, "exit");
  machine.suspended_by_condition = !execute_exit_effects && !reset;
  machine.active = false;
  notifyString(machine.state_var, "none");
  machine.last_state_post = now;
  if(!machine.active_state_var.empty()) {
    notifyString(machine.active_state_var, "none");
    machine.last_active_state = "none";
    machine.last_active_state_post = now;
  }
  if(reset) {
    machine.current_state = machine.initial_state;
    machine.initial_transition_pending = true;
    machine.state_active_elapsed = 0;
  }
  publishStatus(machine, "state=none,status=parent_inactive");
}

void FSMRuntime::activateMachine(FSMRuntimeMachine& machine)
{
  const bool resumed = machine.suspended_by_condition;
  machine.suspended_by_condition = false;
  machine.active = true;
  machine.state_active_since = MOOSTime();
  publishState(machine, true);
  if(!resumed) {
    publishEffects(machine, machine.current_state, "entry");
    publishStateEntryCount(machine);
  }
  if(machine.initial_transition_pending)
    publishInitialTransition(machine);
  publishStatus(machine, "state=" + machine.current_state +
                         (resumed ? ",status=resumed" :
                                    ",status=initialized"));
}

bool FSMRuntime::deactivateUndesired(const map<string, bool>& desired)
{
  bool changed = false;
  const vector<string> inner_first = hierarchyOrder(true);
  for(unsigned int i=0; i<inner_first.size(); ++i) {
    FSMRuntimeMachine& machine = m_machines[inner_first[i]];
    map<string, bool>::const_iterator target = desired.find(machine.id);
    const bool should_be_active =
      (target != desired.end()) && target->second;
    if(machine.active && !should_be_active) {
      map<string, bool>::const_iterator condition_reset =
        m_condition_resets.find(machine.id);
      const bool condition_deactivation =
        condition_reset != m_condition_resets.end();
      const bool reset = condition_deactivation ?
        condition_reset->second : machine.reset_on_inactive;
      // Suspending a history-preserving condition is not a logical state exit.
      // Resetting conditions and ordinary parent-state exits remain true exits.
      const bool execute_exit_effects = !condition_deactivation || reset;
      deactivateMachine(machine, reset, execute_exit_effects);
      changed = true;
    }
    else if(!machine.active && !should_be_active) {
      const string reason = m_condition_resets.count(machine.id) ?
        "condition_inactive" : "parent_inactive";
      publishStatus(machine, "state=none,status=" + reason);
    }
  }
  return changed;
}

bool FSMRuntime::activateDesired(const map<string, bool>& desired)
{
  bool changed = false;
  const vector<string> outer_first = hierarchyOrder(false);
  for(unsigned int i=0; i<outer_first.size(); ++i) {
    FSMRuntimeMachine& machine = m_machines[outer_first[i]];
    map<string, bool>::const_iterator target = desired.find(machine.id);
    const bool should_be_active =
      (target != desired.end()) && target->second;
    if(!machine.active && should_be_active) {
      activateMachine(machine);
      changed = true;
    }
  }
  return changed;
}

bool FSMRuntime::activationConditionAllows(FSMRuntimeMachine& machine,
                                           bool& inputs_ready)
{
  if(machine.activation_condition_text.empty()) {
    inputs_ready = true;
    return true;
  }

  machine.activation_condition.clearVarVals();
  inputs_ready = true;
  for(unsigned int i=0; i<machine.activation_condition_vars.size(); ++i) {
    const string& var = machine.activation_condition_vars[i];
    string string_value;
    double double_value = 0;
    const bool string_ok = valueString(var, string_value);
    const bool double_ok = valueDouble(var, double_value);
    if(string_ok)
      machine.activation_condition.setVarVal(var, string_value);
    if(double_ok)
      machine.activation_condition.setVarVal(var, double_value);
    if(!string_ok && !double_ok)
      inputs_ready = false;
  }
  return inputs_ready && machine.activation_condition.eval();
}

bool FSMRuntime::evaluateGuard(FSMRuntimeTransition& transition,
                               bool& inputs_ready)
{
  transition.guard.clearVarVals();
  inputs_ready = true;
  for(unsigned int i=0; i<transition.guard_vars.size(); ++i) {
    const string& var = transition.guard_vars[i];
    string string_value;
    double double_value = 0;
    const bool string_ok = valueString(var, string_value);
    const bool double_ok = valueDouble(var, double_value);
    if(string_ok)
      transition.guard.setVarVal(var, string_value);
    if(double_ok)
      transition.guard.setVarVal(var, double_value);
    if(!string_ok && !double_ok)
      inputs_ready = false;
  }
  return inputs_ready && transition.guard.eval();
}

double FSMRuntime::activeStateTime(const FSMRuntimeMachine& machine) const
{
  double elapsed = machine.state_active_elapsed;
  if(machine.active && (machine.state_active_since >= 0))
    elapsed += MOOSTime() - machine.state_active_since;
  return elapsed;
}

double FSMRuntime::transitionStateTime(
  const FSMRuntimeMachine& machine,
  const FSMRuntimeTransition& transition) const
{
  double elapsed = activeStateTime(machine);
  // Generated exits from a composite state are owned by the parent machine,
  // but their guards name the terminal child state. Gate those exits on the
  // child's dwell too, not just the older parent state's dwell.
  for(map<string, FSMRuntimeMachine>::const_iterator p=m_machines.begin();
      p!=m_machines.end(); ++p) {
    const FSMRuntimeMachine& child = p->second;
    if(!child.active || (child.id == machine.id))
      continue;
    if(find(transition.guard_vars.begin(), transition.guard_vars.end(),
            child.state_var) == transition.guard_vars.end() &&
       find(transition.guard_vars.begin(), transition.guard_vars.end(),
            child.active_state_var) == transition.guard_vars.end())
      continue;

    string ancestor_var = child.parent_state_var;
    set<string> visited;
    while(!ancestor_var.empty() && visited.insert(ancestor_var).second) {
      if(ancestor_var == machine.state_var) {
        elapsed = min(elapsed, activeStateTime(child));
        break;
      }
      const FSMRuntimeMachine* parent = 0;
      for(map<string, FSMRuntimeMachine>::const_iterator q=m_machines.begin();
          q!=m_machines.end(); ++q) {
        if(q->second.state_var == ancestor_var) {
          parent = &q->second;
          break;
        }
      }
      ancestor_var = parent ? parent->parent_state_var : "";
    }
  }
  return elapsed;
}

bool FSMRuntime::minimumStateTimeElapsed(
  const FSMRuntimeMachine& machine) const
{
  return activeStateTime(machine) >= m_min_state_time;
}

string FSMRuntime::transitionEvaluation(
  FSMRuntimeMachine& machine,
  FSMRuntimeTransition& transition)
{
  const double elapsed = activeStateTime(machine);
  if(elapsed < m_min_state_time)
    return "DWELL " + doubleToString(m_min_state_time - elapsed, 2) + "s";

  bool inputs_ready = false;
  const bool result = evaluateGuard(transition, inputs_ready);
  if(!inputs_ready)
    return "WAITING INPUT";
  if(!result)
    return "BLOCKED";
  const double transition_elapsed = transitionStateTime(machine, transition);
  if(transition_elapsed < m_min_state_time)
    return "DWELL " +
           doubleToString(m_min_state_time - transition_elapsed, 2) + "s";
  return "ENABLED";
}

string FSMRuntime::observedValue(const string& var) const
{
  string string_value;
  if(valueString(var, string_value))
    return string_value;

  double double_value = 0;
  if(valueDouble(var, double_value))
    return doubleToString(double_value, 3);
  return "<unset>";
}

string FSMRuntime::observedType(const string& var) const
{
  if(m_string_values.count(var))
    return "string";
  if(m_double_values.count(var))
    return "double";
  return "missing";
}

vector<FSMRuntimePredicate> FSMRuntime::parsePredicates(
  const string& guard) const
{
  const regex comparison(
    "([A-Za-z_][A-Za-z0-9_]*)\\s*(==|!=|>=|<=|=|>|<)\\s*"
    "(\\\"[^\\\"]*\\\"|[A-Za-z0-9_.:+-]+)");
  vector<FSMRuntimePredicate> predicates;
  for(sregex_iterator p(guard.begin(), guard.end(), comparison), end;
      p != end; ++p) {
    FSMRuntimePredicate predicate;
    predicate.expression = (*p)[0].str();
    predicate.var = (*p)[1].str();
    predicate.op = (*p)[2].str();
    predicate.expected = (*p)[3].str();
    predicates.push_back(predicate);
  }
  return predicates;
}

string FSMRuntime::predicateEvaluation(
  const FSMRuntimePredicate& predicate) const
{
  LogicCondition condition;
  condition.setAllowDoubleEquals(true);
  if(!condition.setCondition(predicate.expression))
    return "ERROR";

  map<string, string>::const_iterator string_value =
    m_string_values.find(predicate.var);
  map<string, double>::const_iterator double_value =
    m_double_values.find(predicate.var);
  if(string_value != m_string_values.end())
    condition.setVarVal(predicate.var, string_value->second);
  else if(double_value != m_double_values.end())
    condition.setVarVal(predicate.var, double_value->second);
  else
    return "MISSING";
  return condition.eval() ? "true" : "false";
}

void FSMRuntime::takeTransition(FSMRuntimeMachine& machine,
                                const FSMRuntimeTransition& transition,
                                const FSMTransitionOccurrence& matched_context)
{
  const string previous = machine.current_state;
  // This snapshot was taken when the guard matched, before status or effects.
  recordTransition(machine, transition.id, previous, transition.to,
                   &matched_context);

  // Determine the target hierarchy without leaving the source state. Clear
  // exited descendants from the leaves inward, then run the source state's
  // exit effects while both its internal and published state are still current.
  const map<string, bool> desired = desiredActivation(machine.id, transition.to);
  deactivateUndesired(desired);
  publishEffects(machine, previous, "exit");

  machine.current_state = transition.to;
  machine.state_active_elapsed = 0;
  machine.state_active_since = MOOSTime();
  ++machine.transition_count;
  ++m_transitions_taken;

  publishState(machine, true);
  // Entry effects run only after the destination state is externally visible.
  publishEffects(machine, machine.current_state, "entry");
  publishStateEntryCount(machine);

  string report = "id=" + transition.id;
  report += ",from=" + previous;
  report += ",to=" + machine.current_state;
  report += ",count=" + uintToString(machine.transition_count);
  report += ",guard=" + transition.guard_text;
  notifyString(machine.transition_var, report);
  publishStatus(machine, "state=" + machine.current_state +
                         ",status=transitioned");

  // Enter the new hierarchy from the outermost machine toward its leaves.
  // Initial child publications are therefore never visible before the parent
  // state that authorizes them.
  activateDesired(desired);

  // Aggregate states depend on all active child values. Publish inner
  // aggregates first and the outermost aggregate last.
  const vector<string> inner_first = hierarchyOrder(true);
  for(unsigned int i=0; i<inner_first.size(); ++i) {
    FSMRuntimeMachine& active_machine = m_machines[inner_first[i]];
    if(active_machine.active)
      publishActiveState(active_machine, true);
  }
}

void FSMRuntime::publishEffects(FSMRuntimeMachine& machine,
                                const string& state,
                                const string& phase)
{
  for(unsigned int i=0; i<machine.effects.size(); ++i) {
    const FSMRuntimeEffect& effect = machine.effects[i];
    if((effect.state != state) || (effect.phase != phase))
      continue;
    if(effect.is_double)
      notifyDouble(effect.var, effect.double_value);
    else
      notifyString(effect.var, effect.string_value);
  }
}

void FSMRuntime::publishState(FSMRuntimeMachine& machine, bool force)
{
  const double now = MOOSTime();
  const bool interval_elapsed =
    (machine.last_state_post < 0) ||
    ((now - machine.last_state_post) >= machine.post_interval);
  if(force || interval_elapsed) {
    notifyString(machine.state_var, machine.current_state);
    machine.last_state_post = now;
  }
}

void FSMRuntime::publishActiveState(FSMRuntimeMachine& machine, bool force)
{
  if(machine.active_state_var.empty())
    return;

  vector<const FSMRuntimeSubstate*> active_substates;
  for(unsigned int i=0; i<machine.substates.size(); ++i) {
    if(machine.substates[i].parent == machine.current_state)
      active_substates.push_back(&machine.substates[i]);
  }

  string active_state = machine.current_state;
  vector<string> children;
  for(unsigned int i=0; i<active_substates.size(); ++i) {
    const FSMRuntimeSubstate& substate = *active_substates[i];
    string child;
    if(!valueString(substate.var, child) || child.empty() || (child == "none"))
      return;
    if(substate.section.empty())
      children.push_back(child);
    else
      children.push_back(substate.section + ":" + child);
  }

  if(children.size() == 1)
    active_state += ":" + children[0];
  else if(children.size() > 1) {
    active_state += ":{";
    for(unsigned int i=0; i<children.size(); ++i) {
      if(i > 0)
        active_state += ",";
      active_state += children[i];
    }
    active_state += "}";
  }

  const double now = MOOSTime();
  const bool changed = (active_state != machine.last_active_state);
  const bool interval_elapsed =
    (machine.last_active_state_post < 0) ||
    ((now - machine.last_active_state_post) >= machine.post_interval);
  if(force || changed || interval_elapsed) {
    notifyString(machine.active_state_var, active_state);
    machine.last_active_state = active_state;
    machine.last_active_state_post = now;
  }
}

void FSMRuntime::publishInitialTransition(FSMRuntimeMachine& machine)
{
  ++machine.transition_count;
  string report = "id=" + machine.initial_transition_id;
  report += ",from=" + machine.initial_node;
  report += ",to=" + machine.current_state;
  report += ",count=" + uintToString(machine.transition_count);
  report += ",guard=true";
  notifyString(machine.transition_var, report);
  recordTransition(machine, machine.initial_transition_id,
                   machine.initial_node, machine.current_state);
  machine.initial_transition_pending = false;
}

FSMTransitionOccurrence FSMRuntime::captureCandidateContext(
  const FSMRuntimeMachine& machine, const string& from) const
{
  FSMTransitionOccurrence context;
  context.transition_time = MOOSTime();
  set<string> inputs;
  map<string, string> snapshots;
  for(const FSMRuntimeTransition& candidate : machine.transitions) {
    if(candidate.from != from)
      continue;
    inputs.insert(candidate.guard_vars.begin(), candidate.guard_vars.end());
    snapshots.insert(candidate.check_snapshots.begin(),
                     candidate.check_snapshots.end());
  }
  for(const string& var : inputs) {
    // The scalar remains authoritative for the guard; publish its full
    // diagnostic snapshot instead of duplicating the scalar in telemetry.
    if(snapshots.count(var))
      continue;
    const auto text = m_string_values.find(var);
    const auto number = m_double_values.find(var);
    if(text != m_string_values.end())
      context.string_context[var] = text->second;
    else if(number != m_double_values.end())
      context.double_context[var] = number->second;
    else
      context.missing_context.insert(var);
  }
  for(const auto& entry : snapshots) {
    const string& snapshot_var = entry.second;
    const auto observed = m_string_values.find(snapshot_var);
    // uChecks owns this JSON. If its observation is missing or clearly not
    // an object, retain a null diagnostic rather than a stale scalar result.
    if(observed != m_string_values.end() && observed->second.size() >= 2 &&
       observed->second.front() == '{' && observed->second.back() == '}')
      context.snapshot_context[snapshot_var] = observed->second;
    else
      context.missing_context.insert(snapshot_var);
  }
  return context;
}

void FSMRuntime::recordTransition(
  const FSMRuntimeMachine& machine, const string& id, const string& from,
  const string& to, const FSMTransitionOccurrence* matched_context)
{
  FSMTransitionOccurrence occurrence;
  occurrence.occurrence_id = ++m_transition_occurrence_id;
  occurrence.transition_time = matched_context ?
    matched_context->transition_time : MOOSTime();
  occurrence.machine = machine.id;
  occurrence.id = id;
  occurrence.from = from;
  occurrence.to = to;
  if(matched_context) {
    occurrence.string_context = matched_context->string_context;
    occurrence.double_context = matched_context->double_context;
    occurrence.snapshot_context = matched_context->snapshot_context;
    occurrence.missing_context = matched_context->missing_context;
  }
  m_cycle_transitions.push_back(occurrence);
}

string FSMRuntime::jsonEscape(const string& value) const
{
  ostringstream result;
  const char* hex = "0123456789abcdef";
  for(unsigned char byte : value) {
    if(byte == '"' || byte == '\\') {
      result << '\\' << static_cast<char>(byte);
    }
    else if(byte < 0x20) {
      result << "\\u00" << hex[byte >> 4] << hex[byte & 0x0f];
    }
    else
      result << static_cast<char>(byte);
  }
  return result.str();
}

void FSMRuntime::publishTransitionBatch()
{
  if(m_cycle_transitions.empty()) return;
  ostringstream report;
  report << "{\"transitions\":[";
  for(size_t i=0; i<m_cycle_transitions.size(); ++i) {
    const FSMTransitionOccurrence& event = m_cycle_transitions[i];
    if(i) report << ',';
    report << "{\"occurrence_id\":" << event.occurrence_id
           << ",\"machine\":\"" << jsonEscape(event.machine)
           << "\",\"id\":\"" << jsonEscape(event.id)
           << "\",\"from\":\"" << jsonEscape(event.from)
           << "\",\"to\":\"" << jsonEscape(event.to) << "\"}";
  }
  // At most one guarded transition occurs per cycle, and it precedes any
  // child initial transitions. Initial-only batches have an empty snapshot.
  const FSMTransitionOccurrence& context = m_cycle_transitions.front();
  report << "],\"transition_time\":" << setprecision(17)
         << context.transition_time << ",\"context\":{";
  set<string> keys = context.missing_context;
  for(const auto& entry : context.string_context) keys.insert(entry.first);
  for(const auto& entry : context.double_context) keys.insert(entry.first);
  for(const auto& entry : context.snapshot_context) keys.insert(entry.first);
  bool first = true;
  for(const string& key : keys) {
    if(!first) report << ',';
    first = false;
    report << '"' << jsonEscape(key) << "\":";
    const auto text = context.string_context.find(key);
    const auto number = context.double_context.find(key);
    const auto snapshot = context.snapshot_context.find(key);
    if(snapshot != context.snapshot_context.end())
      report << snapshot->second;
    else if(text != context.string_context.end())
      report << '"' << jsonEscape(text->second) << '"';
    else if(number != context.double_context.end() &&
            std::isfinite(number->second))
      report << setprecision(17) << number->second;
    else
      report << "null";
  }
  report << "}}";
  notifyString("FSM_TRANSITION", report.str());
  m_cycle_transitions.clear();
}

void FSMRuntime::publishStateEntryCount(FSMRuntimeMachine& machine)
{
  unsigned int& count = machine.state_entry_counts[machine.current_state];
  ++count;
  notifyDouble(machine.state_var + "_" + machine.current_state + "_COUNT",
               static_cast<double>(count));
  const auto epoch_var = machine.entry_epoch_vars.find(machine.current_state);
  if(epoch_var != machine.entry_epoch_vars.end()) {
    ostringstream session;
    session.imbue(locale::classic());
    session << setprecision(17) << m_epoch << ':' << count;
    notifyString(epoch_var->second, session.str());
  }
}

void FSMRuntime::publishStatus(FSMRuntimeMachine& machine,
                               const string& status,
                               bool warning)
{
  if(status == machine.last_status)
    return;

  machine.last_status = status;
  notifyString(machine.status_var, status);
  if(!machine.last_warning.empty() && !warning) {
    retractRunWarning(machine.last_warning);
    machine.last_warning.clear();
  }
  if(warning) {
    const string message = "FSM " + machine.state_var + ": " + status;
    if(!machine.last_warning.empty() && (machine.last_warning != message))
      retractRunWarning(machine.last_warning);
    reportRunWarning(message);
    machine.last_warning = message;
  }
}

void FSMRuntime::cacheString(const string& var, const string& value)
{
  m_string_values[var] = value;
  m_double_values.erase(var);
}

void FSMRuntime::cacheDouble(const string& var, double value)
{
  m_double_values[var] = value;
  m_string_values.erase(var);
}

void FSMRuntime::notifyString(const string& var, const string& value)
{
  Notify(var, value);
  cacheString(var, value);
}

void FSMRuntime::notifyDouble(const string& var, double value)
{
  Notify(var, value);
  cacheDouble(var, value);
}

bool FSMRuntime::valueString(const string& var, string& value) const
{
  map<string, string>::const_iterator p=m_string_values.find(var);
  if(p == m_string_values.end())
    return false;
  value = p->second;
  return true;
}

bool FSMRuntime::valueDouble(const string& var, double& value) const
{
  map<string, double>::const_iterator p=m_double_values.find(var);
  if(p == m_double_values.end())
    return false;
  value = p->second;
  return true;
}

bool FSMRuntime::validName(const string& value) const
{
  return !value.empty() && !strContainsWhite(value) && isAlphaNum(value, "_-");
}

string FSMRuntime::normalizeGuard(const string& value) const
{
  vector<string> parts;
  vector<string> operators;
  unsigned int begin = 0;
  int depth = 0;
  bool quoted = false;

  for(unsigned int i=0; i<value.size(); ++i) {
    const char ch = value[i];
    if(ch == '"') {
      quoted = !quoted;
      continue;
    }
    if(quoted)
      continue;
    if(ch == '(') {
      ++depth;
      continue;
    }
    if(ch == ')') {
      --depth;
      continue;
    }
    if(depth != 0)
      continue;

    string tail = tolower(value.substr(i));
    string op;
    unsigned int width = 0;
    if(strBegins(tail, " and ")) {
      op = "and";
      width = 5;
    }
    else if(strBegins(tail, " or ")) {
      op = "or";
      width = 4;
    }
    if(op.empty())
      continue;

    parts.push_back(stripBlankEnds(value.substr(begin, i-begin)));
    operators.push_back(op);
    i += width - 1;
    begin = i + 1;
  }

  if(operators.size() < 2)
    return value;
  for(unsigned int i=1; i<operators.size(); ++i) {
    if(operators[i] != operators[0])
      return value;
  }

  parts.push_back(stripBlankEnds(value.substr(begin)));
  string result = parts[0];
  for(unsigned int i=0; i<operators.size(); ++i)
    result = "(" + result + " " + operators[i] + " " + parts[i+1] + ")";
  return result;
}

bool FSMRuntime::buildReport()
{
  ACTable summary(2);
  summary << "Runtime" << "Value";
  summary.addHeaderLines();
  summary << "Configuration" << (m_config_valid ? "valid" : "invalid");
  summary << "Runtime state" << (m_initialized ? "READY" : "NOT_READY");
  summary << "Epoch" << doubleToString(m_epoch, 3);
  summary << "Minimum state time" << doubleToString(m_min_state_time, 2) + " s";
  summary << "Machines" << static_cast<unsigned int>(m_machine_order.size());
  summary << "Iterations" << m_iterations;
  summary << "Guarded transitions" << m_transitions_taken;
  m_msgs << summary.getFormattedString() << endl << endl;

  ACTable states(5);
  states << "Machine" << "Active" << "State" << "Active time" << "Count";
  states.addHeaderLines();
  states.setColumnJustify(3, "right");
  states.setColumnJustify(4, "right");
  for(unsigned int i=0; i<m_machine_order.size(); ++i) {
    const FSMRuntimeMachine& machine = m_machines[m_machine_order[i]];
    states << machine.id << boolToString(machine.active)
           << (machine.active ? machine.current_state : "none")
           << (machine.active ? doubleToString(activeStateTime(machine), 2) + " s"
                              : "-")
           << machine.transition_count;
  }
  m_msgs << "Machine state:" << endl;
  m_msgs << states.getFormattedString() << endl;

  ACTable conditions(5);
  conditions << "Machine" << "Condition" << "Inputs" << "Result" << "History";
  conditions.addHeaderLines();
  unsigned int condition_count = 0;
  for(unsigned int i=0; i<m_machine_order.size(); ++i) {
    FSMRuntimeMachine& machine = m_machines[m_machine_order[i]];
    if(machine.activation_condition_text.empty())
      continue;
    bool inputs_ready = false;
    const bool result = activationConditionAllows(machine, inputs_ready);
    conditions << machine.id << machine.activation_condition_text
               << (inputs_ready ? "ready" : "missing")
               << boolToString(result)
               << (machine.condition_history ? "deep" : "reset");
    ++condition_count;
  }
  if(condition_count > 0) {
    m_msgs << endl << "Machine activation conditions:" << endl;
    m_msgs << conditions.getFormattedString() << endl;
  }

  ACTable effects(4);
  effects << "Machine" << "State" << "Phase" << "Assignment";
  effects.addHeaderLines();
  unsigned int effect_count = 0;
  for(unsigned int i=0; i<m_machine_order.size(); ++i) {
    const FSMRuntimeMachine& machine = m_machines[m_machine_order[i]];
    for(unsigned int j=0; j<machine.effects.size(); ++j) {
      const FSMRuntimeEffect& effect = machine.effects[j];
      const string value = effect.is_double ?
        doubleToString(effect.double_value, 3) : effect.string_value;
      effects << machine.id << effect.state << effect.phase
              << effect.var + "=" + value;
      ++effect_count;
    }
  }
  if(effect_count > 0) {
    m_msgs << endl << "State effects:" << endl;
    m_msgs << effects.getFormattedString() << endl;
  }

  struct ReportTransition {
    FSMRuntimeMachine* machine;
    FSMRuntimeTransition* transition;
  };
  vector<ReportTransition> outgoing;
  for(unsigned int i=0; i<m_machine_order.size(); ++i) {
    FSMRuntimeMachine& machine = m_machines[m_machine_order[i]];
    if(!machine.active)
      continue;
    for(unsigned int j=0; j<machine.transitions.size(); ++j) {
      FSMRuntimeTransition& transition = machine.transitions[j];
      if(transition.from == machine.current_state)
        outgoing.push_back(ReportTransition{&machine, &transition});
    }
  }

  m_msgs << endl << "Outgoing transitions from active states:" << endl;
  if(outgoing.empty()) {
    m_msgs << "  None (all active states are terminal)." << endl;
    return true;
  }

  ACTable next_states(5);
  next_states << "#" << "Machine" << "Current state" << "Next state"
              << "Evaluation";
  next_states.addHeaderLines();
  next_states.setColumnJustify(0, "right");
  for(unsigned int i=0; i<outgoing.size(); ++i) {
    FSMRuntimeMachine& machine = *outgoing[i].machine;
    FSMRuntimeTransition& transition = *outgoing[i].transition;
    next_states << (i + 1) << machine.id << machine.current_state
                << transition.to << transitionEvaluation(machine, transition);
  }
  m_msgs << next_states.getFormattedString() << endl;

  for(unsigned int i=0; i<outgoing.size(); ++i) {
    FSMRuntimeMachine& machine = *outgoing[i].machine;
    FSMRuntimeTransition& transition = *outgoing[i].transition;
    m_msgs << endl << "Transition [" << (i + 1) << "]:" << endl;

    ACTable details(2);
    details << "Field" << "Value";
    details.addHeaderLines();
    details << "Machine" << machine.id;
    details << "Transition" << transition.id;
    details << "Destination" << transition.to;
    details << "Evaluation" << transitionEvaluation(machine, transition);
    details << "Guard" << transition.guard_text;
    m_msgs << details.getFormattedString() << endl;

    ACTable inputs(6);
    inputs << "Input" << "Type" << "Current value" << "Operator"
           << "Expected value" << "Match";
    inputs.addHeaderLines();
    if(transition.predicates.empty()) {
      for(unsigned int j=0; j<transition.guard_vars.size(); ++j) {
        const string& var = transition.guard_vars[j];
        inputs << var << observedType(var) << observedValue(var)
               << "?" << "?" << "?";
      }
    }
    else {
      for(unsigned int j=0; j<transition.predicates.size(); ++j) {
        const FSMRuntimePredicate& predicate = transition.predicates[j];
        inputs << predicate.var << observedType(predicate.var)
               << observedValue(predicate.var) << predicate.op
               << stripQuotes(predicate.expected)
               << predicateEvaluation(predicate);
      }
    }
    m_msgs << inputs.getFormattedString() << endl;
  }
  return true;
}
