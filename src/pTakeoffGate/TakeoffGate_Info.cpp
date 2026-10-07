/****************************************************************/
/*   NAME: Charles Benjamin                                     */
/*   ORGN: MIT, Cambridge MA                                    */
/*   FILE: TakeoffGate_Info.cpp                                 */
/*   DATE: August 17th, 2026                                    */
/****************************************************************/

#include <cstdlib>
#include "TakeoffGate_Info.h"
#include "ColorParse.h"
#include "ReleaseInfo.h"

using namespace std;

void showSynopsis()
{
  blk("SYNOPSIS:                                                       ");
  blk("  pTakeoffGate coordinates each UAV takeoff request with PEARL.  ");
  blk("  The UAV role checks combined readiness and requests clearance; ");
  blk("  the PEARL role publishes support-state facts and replies.       ");
}

void showHelpAndExit()
{
  blu("=============================================================== ");
  blu("Usage: pTakeoffGate file.moos [OPTIONS]                         ");
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
  blu("pTakeoffGate Example MOOS Configuration                         ");
  blu("=============================================================== ");
  blk("ProcessConfig = pTakeoffGate                                    ");
  blk("{                                                               ");
  blk("  AppTick   = 4                                                 ");
  blk("  CommsTick = 4                                                 ");
  blk("                                                                ");
  blk("  role              = uav  // or pearl                         ");
  blk("  ownship           = uav                                     ");
  blk("  peer_node         = pearl                                   ");
  blk("  request_timeout   = 15                                      ");
  blk("  coordination_step_delay = 1                                 ");
  blk("  fsm_state_max_age = 2.5                                     ");
  blk("  min_uav_soc      = 30                                        ");
  blk("  min_pearl_soc    = 15                                        ");
  blk("  max_wind_speed   = 4                                         ");
  blk("  input_max_age    = 3                                         ");
  blk("}                                                               ");
  exit(0);
}

void showInterfaceAndExit()
{
  blu("=============================================================== ");
  blu("pTakeoffGate INTERFACE                                          ");
  blu("=============================================================== ");
  showSynopsis();
  blk("SUBSCRIPTIONS:                                                  ");
  blk("  UAV role: UAV_TAKEOFF_REQUEST, TAKEOFF_CLEARANCE              ");
  blk("  PEARL role: TAKEOFF_REQUEST, UAV_FSM_OPERATION_STATE,         ");
  blk("              PEARL_FSM_OPERATION_STATE,                        ");
  blk("              PEARL_FSM_SUPPORT_ACTIVITY_STATE,                  ");
  blk("              PEARL_FSM_TAKEOFF_SUPPORT_STATE,                   ");
  blk("              PEARL_FSM_RUNTIME_STATE/HEARTBEAT                  ");
  blk("  UAV_BATTERY_SOC, UAV_BATTERY_DATA_VALID                       ");
  blk("  PEARL_BATTERY_SOC, PEARL_BATTERY_DATA_VALID                   ");
  blk("  PEARL_WIND_SPEED, PEARL_WIND_DATA_VALID                       ");
  blk("PUBLICATIONS:                                                   ");
  blk("  UAV_TAKEOFF_REQUEST       Consumed by posting false           ");
  blk("  UAV_TAKEOFF_APPROVED      One-shot approval for local action  ");
  blk("  UAV_TAKEOFF_RESULT        PENDING, APPROVED, or REJECTED      ");
  blk("  UAV_TAKEOFF_GATE_READY    Current readiness                   ");
  blk("  UAV_TAKEOFF_GATE_REASON   Current reason token                ");
  blk("  NODE_MESSAGE_LOCAL        Mediated request/clearance          ");
  blk("  PEARL role: TAKEOFF_* support coordination facts              ");
  exit(0);
}

void showReleaseInfoAndExit()
{
  showReleaseInfo("pTakeoffGate", "gpl");
  exit(0);
}
