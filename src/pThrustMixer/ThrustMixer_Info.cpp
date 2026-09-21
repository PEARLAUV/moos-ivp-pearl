/************************************************************/
/*    NAME: PEARL Project                                   */
/*    FILE: ThrustMixer_Info.cpp                            */
/************************************************************/

#include <cstdlib>
#include "ColorParse.h"
#include "ReleaseInfo.h"
#include "ThrustMixer_Info.h"

using namespace std;

void showSynopsis()
{
  blk("SYNOPSIS:");
  blk("  pThrustMixer converts conventional thrust/rudder commands into");
  blk("  left/right motor commands for a differential-thrust simulator.");
}

void showHelpAndExit()
{
  blu("===============================================================");
  blu("Usage: pThrustMixer file.moos [OPTIONS]");
  blu("===============================================================");
  showSynopsis();
  blk("  --alias=<ProcessName>");
  blk("  --example, -e");
  blk("  --help, -h");
  blk("  --interface, -i");
  blk("  --version, -v");
  exit(0);
}

void showExampleConfigAndExit()
{
  blu("===============================================================");
  blu("pThrustMixer Example MOOS Configuration");
  blu("===============================================================");
  blk("ProcessConfig = pThrustMixer");
  blk("{");
  blk("  AppTick   = 10");
  blk("  CommsTick = 10");
  blk("");
  blk("  input_thrust_var = DESIRED_THRUST");
  blk("  input_rudder_var = DESIRED_RUDDER");
  blk("  output_left_var  = DESIRED_THRUST_L");
  blk("  output_right_var = DESIRED_THRUST_R");
  blk("  state_var        = THRUST_MIXER_STATE");
  blk("  max_thrust       = 100");
  blk("  max_rudder       = 50");
  blk("  stale_timeout    = 1");
  blk("}");
  exit(0);
}

void showInterfaceAndExit()
{
  blu("===============================================================");
  blu("pThrustMixer INTERFACE");
  blu("===============================================================");
  blk("SUBSCRIPTIONS:");
  blk("  DESIRED_THRUST   double");
  blk("  DESIRED_RUDDER   double");
  blk("");
  blk("PUBLICATIONS:");
  blk("  DESIRED_THRUST_L double");
  blk("  DESIRED_THRUST_R double");
  blk("  THRUST_MIXER_STATE string");
  exit(0);
}

void showReleaseInfoAndExit()
{
  showReleaseInfo("pThrustMixer", "gpl");
  exit(0);
}
