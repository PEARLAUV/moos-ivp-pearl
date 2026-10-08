#include <cstdlib>
#include "Rendezvous_Info.h"
#include "ColorParse.h"
#include "ReleaseInfo.h"

using namespace std;

void showSynopsis()
{
  blk("SYNOPSIS:                                                       ");
  blk("  Each vehicle independently calculates and follows its own    ");
  blk("  rendezvous waypoint from ownship/peer motion history.        ");
  blk("  pRendezvous has no role, authority, request/response, or      ");
  blk("  landing behavior. Landing is owned by pLandingCoordinator.   ");
}

void showHelpAndExit()
{
  blu("Usage: pRendezvous file.moos [OPTIONS]");
  showSynopsis();
  mag("  --alias", "=<ProcessName>");
  mag("  --example, -e", "  Display an example configuration.");
  mag("  --help, -h", "     Display this help message.");
  mag("  --interface, -i", "Display subscriptions and publications.");
  mag("  --version, -v", "  Display release information.");
  exit(0);
}

void showExampleConfigAndExit()
{
  blu("pRendezvous Example MOOS Configuration");
  blk("ProcessConfig = pRendezvous");
  blk("{");
  blk("  AppTick   = 4");
  blk("  CommsTick = 4");
  blk("  ownship   = uav");
  blk("  peer_node = pearl");
  blk("  nav_stale_thresh    = 10");
  blk("  transit_timeout     = 300");
  blk("  point_freeze_radius = 3");
  blk("  rendezvous_distance = 1");
  blk("  arrival_dwell       = 0");
  blk("  point_shape         = disc");
  blk("  point_radius        = 1");
  blk("  point_color         = yellow");
  blk("  point_transparency  = 0.5");
  blk("  point_label         = false");
  blk("}");
  exit(0);
}

void showInterfaceAndExit()
{
  blu("pRendezvous INTERFACE");
  blk("SUBSCRIPTIONS:");
  blk("  RENDEZVOUS_START, RENDEZVOUS_ABORT, NAV_X, NAV_Y");
  blk("  NODE_REPORT, RENDEZVOUS_MOTION");
  blk("  <OWNSHIP>_RENDEZVOUS_WAYPOINT_REACHED");
  blk("PUBLICATIONS:");
  blk("  NODE_MESSAGE_LOCAL (peer motion history)");
  blk("  <OWNSHIP>_RENDEZVOUS_UPDATE/ACTIVE/STATE/PHASE/SESSION");
  blk("  <OWNSHIP>_RENDEZVOUS_SPEED/SPEED_VALID/SPEED_WINDOW");
  blk("  VIEW_POINT or VIEW_CIRCLE and RENDEZVOUS_POINT");
  exit(0);
}

void showReleaseInfoAndExit()
{
  showReleaseInfo("pRendezvous", "gpl");
  exit(0);
}
