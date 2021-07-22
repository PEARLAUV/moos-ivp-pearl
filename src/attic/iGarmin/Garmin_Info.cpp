/*
 * Garmin_Info.cpp
 * 
 * Created on: 6/30/2021
 * Author: Ethan Rolland
 * 
 */

#include <cstdlib>
#include <iostream>
#include "Garmin_Info.h"
#include "ColorParse.h"

using namespace std;

void showSynopsis()
{
  blk("SYNOPSIS:                                                                   ");
  blk("------------------------------------                                        ");
  blk("  Reads data from Garmin compass and/or heading sensor                      ");
  blk("                                                                            ");
}

void showHelpAndExit()
{
  blk("                                                                            ");
  blu("============================================================================");
  blu("Usage: iGarmin file.moos [OPTIONS]                                          ");
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
  blu("iGarmin Example MOOS Configuration                                          ");
  blu("============================================================================");
  blk("                                                                            ");
  blk("ProcessConfig = iGarmin                                                     ");
  blk("{                                                                           ");
  blk("  AppTick    = 10                                                           ");
  blk("  CommsTick  = 10                                                           ");
  blk("                                                                            ");
  blk("}                                                                           ");
  blk("                                                                            ");
  exit(0);
}

void showInterfaceAndExit()
{
  blk("                                                                            ");
  blu("============================================================================");
  blu("iGarmin INTERFACE                                                           ");
  blu("============================================================================");
  blk("                                                                            ");
  showSynopsis();
  blk("                                                                            ");
  blk("SUBSCRIPTIONS:                                                              ");
  blk("------------------------------------                                        ");
  blk("                                                                            ");
  blk("                                                                            ");
  blk("PUBLICATIONS:    (NOTE: publication conditional on incoming nmea sentences) ");
  blk("------------------------------------                                        ");
  blk("[prefix]_HEADING   double   Heading value reported from device              ");
  blk("                                                                            ");
  exit(0);
}
