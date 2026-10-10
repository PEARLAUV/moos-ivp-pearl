/****************************************************************/
/*   NAME: Charles Benjamin                                     */
/*   ORGN: MIT, Cambridge MA                                    */
/*   FILE: RouteBuffer_Info.cpp                                 */
/*   DATE: July 25th, 2026                                      */
/****************************************************************/

#include <cstdlib>
#include "RouteBuffer_Info.h"
#include "ColorParse.h"
#include "ReleaseInfo.h"

using namespace std;

//----------------------------------------------------------------
// Procedure: showSynopsis

void showSynopsis()
{
  blk("SYNOPSIS:                                                       ");
  blk("------------------------------------                            ");
  blk("  pRouteBuffer collects operator-selected route points or a     ");
  blk("  fresh named contact position on the shoreside and submits one ");
  blk("  complete route command through pMediator. Its vehicle role    ");
  blk("  expands the command into route update, deploy, or clear posts.");
}

//----------------------------------------------------------------
// Procedure: showHelpAndExit

void showHelpAndExit()
{
  blk("                                                                ");
  blu("=============================================================== ");
  blu("Usage: pRouteBuffer file.moos [OPTIONS]                         ");
  blu("=============================================================== ");
  blk("                                                                ");
  showSynopsis();
  blk("                                                                ");
  blk("Options:                                                        ");
  mag("  --alias","=<ProcessName>                                      ");
  blk("      Launch pRouteBuffer with the given process name           ");
  blk("      rather than pRouteBuffer.                                 ");
  mag("  --example, -e                                                 ");
  blk("      Display example MOOS configuration block.                 ");
  mag("  --help, -h                                                    ");
  blk("      Display this help message.                                ");
  mag("  --interface, -i                                               ");
  blk("      Display MOOS publications and subscriptions.              ");
  mag("  --version,-v                                                  ");
  blk("      Display the release version of pRouteBuffer.              ");
  blk("                                                                ");
  blk("Note: If argv[2] does not otherwise match a known option,       ");
  blk("      then it will be interpreted as a run alias. This is       ");
  blk("      to support pAntler launching conventions.                 ");
  blk("                                                                ");
  exit(0);
}

//----------------------------------------------------------------
// Procedure: showExampleConfigAndExit

void showExampleConfigAndExit()
{
  blk("                                                                ");
  blu("=============================================================== ");
  blu("pRouteBuffer Example MOOS Configuration                         ");
  blu("=============================================================== ");
  blk("                                                                ");
  blk("ProcessConfig = pRouteBuffer                                    ");
  blk("{                                                               ");
  blk("  AppTick   = 4                                                 ");
  blk("  CommsTick = 4                                                 ");
  blk("                                                                ");
  blk("  role               = shoreside                                ");
  blk("  destination_node   = uav                                      ");
  blk("  point_var          = ROUTE_POINT                              ");
  blk("  deploy_request_var = ROUTE_BUFFER_DEPLOY                      ");
  blk("  clear_request_var  = ROUTE_BUFFER_CLEAR                       ");
  blk("  goto_request_var   = ROUTE_BUFFER_GOTO                        ");
  blk("  // Shared drawing: repeat task_destination for allowed nodes   ");
  blk("  task_destination   = pearl                                    ");
  blk("  task_destination   = uav                                      ");
  blk("  task_request_var   = ROUTE_BUFFER_TASK                         ");
  blk("  stage_clear_request_var = ROUTE_BUFFER_CLEAR_STAGED            ");
  blk("  // Optional: task_submit_flag = uav:RALLY_TASK_COMMIT=true     ");
  blk("  // Optional return staging (shoreside, disabled by default):  ");
  blk("  // return_point_var = PEARL_RETURN_POINT                     ");
  blk("  // task_return_destination = pearl                          ");
  blk("  // initial_return_point = x=10.6,y=4.8                       ");
  blk("  // Vehicle: return_update_var = PEARL_RETURN_UPDATE          ");
  blk("  node_report_var    = NODE_REPORT                              ");
  blk("  contact_max_age    = 3                                        ");
  blk("  deploy_delay       = 0     // Seconds; 0 uses route_ready_var   ");
  blk("  route_replay_delay = 0     // Replay after releasing the helm    ");
  blk("  route_identity_var =       // Optional: current-instance run guard ");
  blk("  helm_state_var     =       // Optional: defer snapshots until DRIVE ");
  blk("                            // e.g. IVPHELM_STATE; default disabled ");
  blk("  manual_override_var =        // Optional staged-deploy guard    ");
  blk("  command_var        = ROUTE_BUFFER_COMMAND                     ");
  blk("  max_points         = 50                                       ");
  blk("}                                                               ");
  blk("                                                                ");
  exit(0);
}

//----------------------------------------------------------------
// Procedure: showInterfaceAndExit

void showInterfaceAndExit()
{
  blk("                                                                ");
  blu("=============================================================== ");
  blu("pRouteBuffer INTERFACE                                          ");
  blu("=============================================================== ");
  blk("                                                                ");
  showSynopsis();
  blk("                                                                ");
  blk("SUBSCRIPTIONS:                                                  ");
  blk("------------------------------------                            ");
  blk("  Shoreside role:                                               ");
  blk("    ROUTE_POINT         = x=-100,y=-50                          ");
  blk("    ROUTE_BUFFER_DEPLOY = true                                  ");
  blk("    ROUTE_BUFFER_CLEAR  = true                                  ");
  blk("    ROUTE_BUFFER_GOTO   = pearl                                 ");
  blk("    ROUTE_BUFFER_TASK   = pearl|uav (configured destination)      ");
  blk("    ROUTE_BUFFER_CLEAR_STAGED = true (local drawing only)        ");
  blk("    Configured return_point_var = x=X,y=Y (future tasks only)   ");
  blk("    NODE_REPORT = NAME=pearl,X=-90,Y=-65,...                    ");
  blk("  Vehicle role:                                                 ");
  blk("    ROUTE_BUFFER_COMMAND = action=deploy # points={-100,-50}    ");
  blk("                           # speed=1 # capture_radius=0.5        ");
  blk("                           # slip_radius=0.5 # route_name=return ");
  blk("                           # purpose=operator|recovery           ");
  blk("                           // speed/radii/name are optional      ");
  blk("    Optional vehicle policy: route_purpose_var,               ");
  blk("    operation_state_var, recovery_source, repeated            ");
  blk("    operator_stage_state/recovery_stage_state.                ");
  blk("    Optional task policy: task_guard=VAR=VALUE|VALUE,            ");
  blk("    task_execute_var, task_started_state=VAR=VALUE,              ");
  blk("    route_ready_name_var and route_ready_name_prefix.           ");
  blk("    Tagged deploy: task_id=ID # task_time=MOOS_TIME; see README. ");
  blk("                                                                ");
  blk("PUBLICATIONS:                                                   ");
  blk("------------------------------------                            ");
  blk("  Shoreside role:                                               ");
  blk("    NODE_MESSAGE_LOCAL, VIEW_POINT, VIEW_SEGLIST                ");
  blk("    ROUTE_BUFFER_STATE, ROUTE_BUFFER_COUNT                      ");
  blk("    ROUTE_BUFFER_TASK_ID, ROUTE_BUFFER_TASK_DESTINATION           ");
  blk("  Vehicle role:                                                 ");
  blk("    ROUTE_UPDATE, route_deploy_var (configurable), ROUTE_CLEAR  ");
  blk("    route_purpose_var (if configured)                        ");
  blk("    ROUTE_BUFFER_VEHICLE_STATE                                  ");
  exit(0);
}

//----------------------------------------------------------------
// Procedure: showReleaseInfoAndExit

void showReleaseInfoAndExit()
{
  showReleaseInfo("pRouteBuffer", "gpl");
  exit(0);
}
