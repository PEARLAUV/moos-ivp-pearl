/************************************************************/
/*    NAME: PEARL Team                                      */
/*    FILE: FSMRuntime_Info.cpp                             */
/*    DATE: September 2026                                  */
/************************************************************/

#include <cstdlib>

#include "ColorParse.h"
#include "FSMRuntime_Info.h"
#include "ReleaseInfo.h"

using namespace std;

void showSynopsis()
{
  blk("SYNOPSIS:                                                       ");
  blk("  pFSMRuntime executes generated hierarchical FSM definitions.  ");
  blk("  It publishes authoritative state, transition, status, aggregate");
  blk("  state, and sparse state-entry counter variables.               ");
}

void showHelpAndExit()
{
  blu("=============================================================== ");
  blu("Usage: pFSMRuntime file.moos [OPTIONS]                          ");
  blu("=============================================================== ");
  showSynopsis();
  blk("Options:                                                        ");
  mag("  --alias", "=<ProcessName>                                    ");
  mag("  --example, -e                                                 ");
  mag("  --help, -h                                                    ");
  mag("  --interface, -i                                               ");
  mag("  --version, -v                                                 ");
  exit(0);
}

void showExampleConfigAndExit()
{
  blu("=============================================================== ");
  blu("pFSMRuntime Example MOOS Configuration                          ");
  blu("=============================================================== ");
  blk("ProcessConfig = pFSMRuntime                                    ");
  blk("{                                                               ");
  blk("  AppTick   = 4                                                 ");
  blk("  CommsTick = 4                                                 ");
  blk("  min_state_time = 0  // seconds; optional, default zero         ");
  blk("  runtime = state_var=TEST_FSM_RUNTIME_STATE,                    ");
  blk("            heartbeat_var=TEST_FSM_RUNTIME_HEARTBEAT,            ");
  blk("            epoch_var=TEST_FSM_RUNTIME_EPOCH                     ");
  blk("  initialize = var=GO, type=string, value=false                 ");
  blk("  machine = id=operation, state_var=TEST_FSM_OPERATION_STATE,  ");
  blk("            initial_state=IDLE, initially_active=true,          ");
  blk("            activation_condition=\"MANUAL_OVERRIDE=false\",    ");
  blk("            condition_history=true,                             ");
  blk("            status_var=TEST_FSM_OPERATION_STATUS,               ");
  blk("            transition_var=TEST_FSM_OPERATION_TRANSITION        ");
  blk("  state = machine=operation, value=IDLE                        ");
  blk("  state = machine=operation, value=RUNNING                     ");
  blk("  effect = machine=operation, state=RUNNING, phase=entry,       ");
  blk("           var=WORK_STATE, type=string, value=active            ");
  blk("  transition = machine=operation, id=START, from=IDLE,          ");
  blk("               to=RUNNING, guard=\"GO=true\"                    ");
  blk("}                                                               ");
  exit(0);
}

void showInterfaceAndExit()
{
  blu("=============================================================== ");
  blu("pFSMRuntime INTERFACE                                           ");
  blu("=============================================================== ");
  showSynopsis();
  blk("SUBSCRIPTIONS:                                                  ");
  blk("  Variables referenced by generated guards, parent gates, and   ");
  blk("  child-state aggregation entries.                              ");
  blk("PUBLICATIONS:                                                   ");
  blk("  Generated *_FSM_*_STATE, *_STATUS, *_TRANSITION, aggregate,   ");
  blk("  *_COUNT, runtime health, initialized facts, and effect values. ");
  blk("APPCAST:                                                        ");
  blk("  Active machines, activation conditions, configured effects,     ");
  blk("  outgoing transitions,                                           ");
  blk("  each input's type, current value, operator, expected value, and ");
  blk("  individual predicate match result.                             ");
  exit(0);
}

void showReleaseInfoAndExit()
{
  showReleaseInfo("pFSMRuntime", "gpl");
  exit(0);
}
