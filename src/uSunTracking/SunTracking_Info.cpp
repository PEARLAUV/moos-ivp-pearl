/*
 * SunTracking_Info.cpp
 * 
 * Created on: 2/7/2021
 * Author: Ethan Rolland
 *
 */

#include <cstdlib>
#include <iostream>
#include "SunTracking_Info.h"
#include "ColorParse.h"

using namespace std;

void showSynopsis()
{
  blk("SYNOPSIS:                                                                   ");
  blk("------------------------------------                                        ");
  blk("  Takes in UTC time and date and computes solar elevation and azimuth       ");
  blk("  angles. When PEARL is station-keeping, uSolarHeading updates the          ");
  blk("  DESIRED_HEADING variable instead of pHelmIvP in order to maximize solar   ");
  blk("  energy collection.                                                        ");
  blk("                                                                            ");
}

void showHelpAndExit()
{
  blk("                                                                            ");
  blu("============================================================================");
  blu("Usage: uSunTracking file.moos [OPTIONS]                                     ");
  blu("============================================================================");
  blk("                                                                            ");
  showSynopsis();
  blk("                                                                            ");
  blk("Options:                                                                    ");
  mag("  --example, -e                                                             ");
  blk("      Display example MOOS configuration block.                             ");
  mag("  --help, -h                                                                ");
  blk("      Display this help message.                                            ");
  mag("  --interface, -i                                                           ");
  blk("      Display MOOS publications and subscriptions.                          ");
  blk("                                                                            ");
  blk("Note: If argv[2] does not otherwise match a known option, then it will be   ");
  blk("      interpreted as a run alias. This is to support pAntler conventions.   ");
  blk("                                                                            ");
  exit(0);
}

void showExampleConfigAndExit()
{
  blk("                                                                            ");
  blu("============================================================================");
  blu("uSunTracking Example MOOS Configuration                                     ");
  blu("============================================================================");
  blk("                                                                            ");
  blk("ProcessConfig = uSunTracking                                                ");
  blk("{                                                                           ");
  blk("  AppTick    = 1                                                            ");
  blk("  CommsTick  = 1                                                            ");
  blk("                                                                            ");
  blk("  PREFIX         = SOLAR //keep as SOLAR for pPearlPID compatibility        ");
  blk("  HEADING_OFFSET = 0.0   //offset (in degrees) from sun azimuth to necessary");
  blk("                           PEARL heading angle for best solar panel angle   ");
  blk("  PUBLISH_ANGLES = false //if true, publishes sun elevation/azimuth angles  ");
  blk("}                                                                           ");
  blk("                                                                            ");
  exit(0);
}

void showInterfaceAndExit()
{
  blk("                                                                            ");
  blu("============================================================================");
  blu("uSunTracking INTERFACE                                                      ");
  blu("============================================================================");
  blk("                                                                            ");
  showSynopsis();
  blk("                                                                            ");
  blk("SUBSCRIPTIONS:                                                              ");
  blk("------------------------------------                                        ");
  blk("GPS_UTC_TIME       string  Current UTC time in hhmmss.s format              ");
  blk("GPS_UTC_DATE       string  Current UTC date in ddmmyy format                ");
  blk("GPS_LATITUDE       double  Latitude parsed from recent NMEA sentence        ");
  blk("GPS_LONGITUDE      double  Longitude parsed form recent NMEA sentence       ");
  blk("                                                                            ");
  blk("                                                                            ");
  blk("PUBLICATIONS:    (NOTE: publication conditional on incoming nmea sentences) ");
  blk("------------------------------------                                        ");
  blk("[prefix]_SUN_ELEVATION  double  Computed sun elevation angle in degrees     ");
  blk("[prefix]_SUN_AZIMUTH    double  Computed sun azimuth angle in degrees       ");
  blk("[prefix]_HEADING        double  Sun elevation angle plus heading offset     ");
  blk("                                                                            ");
  exit(0);
}
