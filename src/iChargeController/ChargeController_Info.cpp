/*
 * ChargeController_Info.cpp
 * 
 * Created on: 2/6/2021
 * Author: Ethan Rolland
 *
 */

#include <cstdlib>
#include <iostream>
#include "ChargeController_Info.h"
#include "ColorParse.h"

using namespace std;

void showSynopsis()
{
  blk("SYNOPSIS:                                                                   ");
  blk("------------------------------------                                        ");
  blk("  Reads battery data from charge controller and adjusts maximum thrust and  ");
  blk("  rudder parameters to preserve battery life.                               ");
  blk("                                                                            ");
}

void showHelpAndExit()
{
  blk("                                                                            ");
  blu("============================================================================");
  blu("Usage: iChargeController file.moos [OPTIONS]                                ");
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
  blu("iChargeController Example MOOS Configuration                                ");
  blu("============================================================================");
  blk("                                                                            ");
  blk("ProcessConfig = iChargeController                                           ");
  blk("{                                                                           ");
  blk("  AppTick    = 1                                                            ");
  blk("  CommsTick  = 1                                                            ");
  blk("                                                                            ");
  blk("  PORT            = /dev/tty.. // Fully-qualified path to the serial port   ");
  blk("  BAUDRATE        = 9600       // Serial port baud rate                     ");
  blk("  PREFIX          = CHG        // Prepends this to all publications         ");
  blk("  READ_DATA       = false      // if true, publishes raw data               ");
  blk("  MAX_THRUST      = 0.0        // Manual specification of max thrust param  ");
  blk("  MAX_RUDDER      = 0.0        // Manual specification of max rudder param  ");
  blk("}                                                                           ");
  blk("                                                                            ");
  exit(0);
}

void showInterfaceAndExit()
{
  blk("                                                                            ");
  blu("============================================================================");
  blu("iChargeController INTERFACE                                                 ");
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
  blk("[prefix]_MAX_THRUST  double  Max thrust parameter to be used by other apps  ");
  blk("[prefix]_MAX_RUDDER  double  Max rudder parameter to be used by other apps  ");
  blk("                                                                            ");
  exit(0);
}
