/*
 * GPSSimInfo.cpp
 *
 *  Created on: Nov 5, 2014
 *      Author: Alon Yaari
 */

#include "GPSSimInfo.h"

#include <cstdlib>
#include <iostream>
#include "ColorParse.h"
#include "ReleaseInfo.h"
#include "GPSSimInfo.h"

using namespace std;

void showSynopsis()
{
  blk("SYNOPSIS:                                                                   ");
  blk("------------------------------------                                        ");
  blk("    Simulates a serial GPS by publishing SIM_* locations as nmea sentences. ");
  blk("  Output from the GPS simulator is published onto a serial serial stream.   ");
  blk("  The stream can be an actual serial port or a simulated serial port created");
  blk("  by socat. With a simulated port, socat can pipe the serial stream such    ");
  blk("  that it can be connected to by iGPSDevice.                                ");
  blk("                                                                            ");
}

void showHelpAndExit()
{
  blk("                                                                            ");
  blu("============================================================================");
  blu("Usage: iGPSSim file.moos [OPTIONS]                                          ");
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
//     0         1         2         3         4         5         6         7
//     01234567890123456789012345678901234567890123456789012345678901234567890123456789
  blk("                                                                            ");
  blu("============================================================================");
  blu("iGPSSim Example MOOS Configuration                                          ");
  blu("============================================================================");
  blk("                                                                            ");
  blk("ProcessConfig = iGPSSim                                                     ");
  blk("{                                                                           ");
  blk("  AppTick    = 5               // Ticks should be set to the frequency of   ");
  blk("  CommsTick  = 5               //   the GPS being simulated                 ");
  blk("                                                                            ");
  blk("  Port            = /dev/ttyACM1  // Fully-qualified path to the serial port");
  blk("  BaudRate        = 19200      // Serial port baud rate                     ");
  blk("  INPUT_PREFIX    = SIM_       // Prefix for input _HEADING, _SPEED, etc.   ");
  blk("  GPGGA           = true       // Publish GPGGA? (default: true)            ");
  blk("  GPHDT           = false      // Publish GPHDT? (default: false)           ");
  blk("  GPRMC           = true       // Publish GPRMC? (default: true)            ");
  blk("  GPTXT           = false      // Publish GPTXT? (default: false)           ");
  blk("  PASHR           = false      // Publish PASHR? (default: false)           ");
  blk("}                                                                           ");
  blk("                                                                            ");
  exit(0);
}

void showInterfaceAndExit()
{
  blk("                                                                            ");
  blu("============================================================================");
  blu("iGPSSim INTERFACE                                                           ");
  blu("============================================================================");
  blk("                                                                            ");
  showSynopsis();
  blk("                                                                            ");
  blk("SUBSCRIPTIONS:                                                              ");
  blk("------------------------------------                                        ");
  blk("<prefix>_X         double  Vehicle position on x-axis of local grid         ");
  blk("<prefix>_Y         double  Vehicle position on y-axis of local grid         ");
  blk("<prefix>_SPEED     double  Vehicle speed in meters/second                   ");
  blk("<prefix>_HEADING   double  Heading (not yaw) in range [0.0, 360.0) degrees  ");
  blk("                                                                            ");
  blk("PUBLICATIONS:                                                               ");
  blk("------------------------------------                                        ");
  blk("none                                                                        ");
  blk("                                                                            ");
  exit(0);
}
























//
