/*
 * PEARL_Info.cpp
 * 
 * Created on: 1/21/2021
 * Author: Ethan Rolland
 * Adapted from: https://github.com/mandad/moos-ivp-manda/tree/master/src/iMOOSArduino
 *
 */

#include <cstdlib>
#include <iostream>
#include "PEARL_Info.h"
#include "ColorParse.h"

using namespace std;

void showSynopsis()
{
  blk("SYNOPSIS:                                                                   ");
  blk("------------------------------------                                        ");
  blk("  Connects with the an Arduino on PEARL via a serial port. Receives NMEA    ");
  blk("  sentences from the Arduino that contain data from the onboard IMU,        ");
  blk("  parses the sentences, then publishes relevant MOOS messages.              ");
  blk("  Sentences from Arduino currently parsed:                                  ");
  grn("   PLIMU: Manual control flag, Heading, Pitch, Roll                         ");
  grn("   PLRAW: Acc, Gyro, Mag measurements                                       ");
  grn("   PLMOT: Left/right motor thrust percentage (reported for double-checking) ");
  blk("  Subscribes to desired heading and thrust messages published by pPearlPID, ");
  blk("  converts to necessary left and right thrust commands, and sends over USB  ");
  blk("  serial port to Arduino front seat.                                        ");
  blk("                                                                            ");
}

void showHelpAndExit()
{
  blk("                                                                            ");
  blu("============================================================================");
  blu("Usage: iPEARL file.moos [OPTIONS]                                           ");
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
  blu("iPEARL Example MOOS Configuration (to be completed)                         ");
  blu("============================================================================");
  blk("                                                                            ");
  blk("ProcessConfig = iPEARL                                                      ");
  blk("{                                                                           ");
  blk("  AppTick    = 10              // Ticks should be set to a value equal to or");
  blk("  CommsTick  = 10              // greater than the GPS output frequency     ");
  blk("                                                                            ");
  blk("  Port            = /dev/ttyUSB0  // Fully-qualified path to the serial port");
  blk("  BaudRate        = 9600       // Serial port baud rate                     ");
  blk("  PREFIX          = IMU        // Prepends this to all GPS publications     ");
  blk("  HEADING_OFFSET  = 0.0        // If publishing heading, this offset value  ");
  blk("                               //   will be added to heading when published.");
  blk("  PUBLISH_RAW     = false      // If true, publish all nmea sentences to    ");
  blk("                               //   MOOS message PEARL_RAW_NMEA             ");
  blk("}                                                                           ");
  blk("                                                                            ");
  exit(0);
}

void showInterfaceAndExit()
{
  blk("                                                                            ");
  blu("============================================================================");
  blu("iPEARL INTERFACE   (to be completed)                                        ");
  blu("============================================================================");
  blk("                                                                            ");
  showSynopsis();
  blk("                                                                            ");
  blk("SUBSCRIPTIONS:                                                              ");
  blk("------------------------------------                                        ");
  blk("DESIRED_THRUST    double                                                    ");
  blk("DESIRED_RUDDER    double                                                    ");
  blk("DESIRED_THRUST_L  double                                                    ");
  blk("DESIRED_THRUST_R  double                                                    ");
  blk("IVPHELM_ALLSTOP   bool                                                      ");
  blk("CHG_MAX_THRUST    double                                                    ");
  blk("CHG_MAX_RUDDER    double                                                    ");
  blk("                                                                            ");
  blk("PUBLICATIONS:    (NOTE: publication conditional on incoming nmea sentences) ");
  blk("------------------------------------                                        ");
  blk("[prefix]_HEADING   double  Measured yaw + heading offset (degrees)          ");
  blk("[prefix]_PITCH     double  Measured pitch (degrees)                         ");
  blk("[prefix]_ROLL      double  Measured roll (degrees)                          ");
  blk("                                                                            ");
  exit(0);
}
