/*
 * GPSInfo.cpp
 * 
 * Created on: 1/12/2021
 * Author: Ethan Rolland
 * 
 */

#include "GPSInfo.h"

#include <cstdlib>
#include <iostream>
#include "ColorParse.h"
#include "ReleaseInfo.h"
#include "GPSInfo.h"

using namespace std;

void showSynopsis()
{
  blk("SYNOPSIS:                                                                   ");
  blk("------------------------------------                                        ");
  blk("    Connects with a GPS device via a serial port. Receives NMEA sentences   ");
  blk("  from the GPS, parses the sentences, then publishes relevant MOOS messages.");
  blk("    This application assumes that a GPS unit connects via the serial port,  ");
  blk("  that the GPS was configured beforehand, and that the GPS retains the      ");
  blk("  configuration data when not powered.                                      ");
  blk("  Sentences currently parsed:                                               ");
  grn("    GPGGA  Time, position and fix related data                              ");
  grn("    GPRMC  Recommended minimum navigation info                              ");
  blk("                                                                            ");
}

void showHelpAndExit()
{
  blk("                                                                            ");
  blu("============================================================================");
  blu("Usage: iGPS file.moos [OPTIONS]                                             ");
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
  blu("iGPS Example MOOS Configuration                                             ");
  blu("============================================================================");
  blk("                                                                            ");
  blk("ProcessConfig = iGPS                                                        ");
  blk("{                                                                           ");
  blk("  AppTick    = 10              // Ticks should be set to a value equal to or");
  blk("  CommsTick  = 10              // greater than the GPS output frequency     ");
  blk("                                                                            ");
  blk("  Port            = /dev/ttyUSB0  // Fully-qualified path to the serial port");
  blk("  BaudRate        = 9600       // Serial port baud rate                     ");
  blk("  PREFIX          = GPS_       // Prepends this to all GPS publications     ");
  blk("  HEADING_SOURCE  = none       // none or blank: no published GPS heading   ");
  blk("                               // gprmc: heading is published from $GPRMC   ");
  blk("                               // compass: heading is published, source is  ");
  blk("                               //            from COMPASS_HEADING message   ");
  blk("  HEADING_OFFSET  = 0.0        // If publishing heading, this offset value  ");
  blk("                               //   will be added to heading when published.");
  blk("  PUBLISH_HDOP    = false      // If true, publishes HDOP                   ");
  blk("  PUBLISH_RAW     = false      // If true, publish all nmea sentences to    ");
  blk("                               //   MOOS message NMEA_FROM_GPS              ");
  blk("  TRIGGER_MSG     = gpgga      // Accumulates data from all incoming        ");
  blk("                               //   NMEA_MSGs but only publishes when the   ");
  blk("                               //   trigger is received.                    ");
  blk("                               //   No trigger when not defined, blank, or  ");
  blk("                               //   set to 'NONE'                           ");
  blk("}                                                                           ");
  blk("                                                                            ");
  exit(0);
}

void showInterfaceAndExit()
{
  blk("                                                                            ");
  blu("============================================================================");
  blu("iGPS INTERFACE                                                              ");
  blu("============================================================================");
  blk("                                                                            ");
  showSynopsis();
  blk("                                                                            ");
  blk("SUBSCRIPTIONS:                                                              ");
  blk("------------------------------------                                        ");
  blk("none                                                                        ");
  //blk("COMPASS_HEADING    double  [0.0, 360.0) heading reported by the compass     ");
  blk("                                                                            ");
  blk("PUBLICATIONS:    (NOTE: publication conditional on incoming nmea sentences) ");
  blk("------------------------------------                                        ");
  blk("[prefix]_LATITUDE  double  Latitude parsed from recent NMEA sentence        ");
  blk("[prefix]_LAT       double  Copy of _LATITUDE                                ");
  blk("[prefix]_LONGITUDE double  Longitude parsed from recent NMEA sentence       ");
  blk("[prefix]_LONG      double  Copy of _LONGITUDE                               ");
  blk("[prefix]_X         double  X position in meters relative to the local origin");
  blk("[prefix]_Y         double  Y position in meters relative to the local origin");
  blk("[prefix]_SPEED     double  Speed in meters per second, provided by GPS      ");
  blk("[prefix]_HEADING   double  Heading in deg CW from true N, see mission params");
  blk("[prefix]_SAT       double  Number of satellites GPS can make use of         ");
  blk("[prefix]_HDOP      double  Horizontal dilution of precision value from GPS  ");
  blk("[prefix]_QUALITY   string  DIFF Differential fix (provides best position)   ");
  blk("                           NO_D Fix, but without differential input         ");
  blk("                           EST  Position is estimated                       ");
  blk("                           BAD  No position can be calculated               ");
  blk("[prefix]_MAGVAR    double  If available, local compass magnetic variation   ");
  blk("[prefix]_UTC_TIME  string  Current UTC time in hhmmss.s format              ");
  blk("[prefix]_UTC_DATE  string  Current UTC date in ddmmyy format                ");
  blk("NMEA_FROM_GPS      string  Received NMEA sentences                          ");
  blk("                                                                            ");
  exit(0);
}
