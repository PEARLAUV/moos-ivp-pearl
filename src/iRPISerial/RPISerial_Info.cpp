/*
 * RPISerial_Info.cpp
 * 
 * Created on: 7/20/2021
 * Author: Henry Crawford-Eng
 * 
 */

#include <cstdlib>
#include <iostream>
#include "RPISerial_Info.h"
#include "ColorParse.h"

using namespace std;

void showSynopsis()
{
  blk("SYNOPSIS:                                                                   ");
  blk("------------------------------------                                        ");
  blk("  Reads formatted data from device over serial port. Recieves Sentences     ");
  blk("  describing data for Solar Charge Controller (SCC), Light Sensor, and Wind ");
  blk("  speed sensors. Sentences are curently formatted Like so:                  ");
  blk("  Solar Charge Controller: values sent through string are listed below:     ");
  blk("  [S1] PV Voltage (volts),[S2] PV Current (amps),[S3] PV Power L (watts),   ");
  blk("  [S4] PV Power H (watts),[S5] Battery Voltage (volts),                     ");
  blk("  [S6] Battery Current (amps),[S7] Battery Net Current (amps)               ");
  blk("  [S8] Battery Power L (watts),[S9] Battery Power H (watts)                 ");
  blk("  [S10] Load Voltage (volts),[S11] Load Current (amps)                      ");
  blk("  [S12] Load Power L (watts),[S13] Load Power H (watts),                    ");
  blk("  [S14] Battery SOC (%), [S15] Battery Temperature (degrees Celsius),       ");
  blk("  [S16] Device Temperature (degrees Celsius)                                ");
  blk("  This data is ordered from 1-16 in the Serial Sentence shown below:        ");
  blk("  $PLSCC,[S1], [S2], [S3],..., [S11], [S12], [S13], [S14], [S15], [S16]*    ");
  blk("  The Serial Sentence for the light Sensor is shown below:                  ");
  blk("  $PLLUX,[L1] Lux, [L2] White, [L3] Raw ALS*                                ");
  blk("  The Serial Sentence for the wind Sensor is shown below:                   ");    
  blk("  $PLWND,[W1] Wind Reading (volts),[W2] Wind Speed (m/s)*                   ");
  blk("  An empty piece of data for every string is denoted with a -               ");        
  blk("                                                                            ");
}

void showHelpAndExit()
{
  blk("                                                                            ");
  blu("============================================================================");
  blu("Usage: iRPISERIAL file.moos [OPTIONS]                                       ");
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
  blu("iRPISerial Example MOOS Configuration                                       ");
  blu("============================================================================");
  blk("                                                                            ");
  blk("ProcessConfig = iRPISerial                                                  ");
  blk("{                                                                           ");
  blk("  AppTick    = 1                                                            ");
  blk("  CommsTick  = 1                                                            ");
  blk("                                                                            ");
  blk("  PORT       = /dev/ttyUSB0                                                 ");
  blk("  BAUDRATE   = 115200                                                       ");
  blk("  PREFIX     = RPI                                                          ");
  blk("                                                                            ");
  blk("}                                                                           ");
  blk("                                                                            ");
  exit(0);
}

void showInterfaceAndExit()
{
  blk("                                                                            ");
  blu("============================================================================");
  blu("iRPISerial INTERFACE                                                        ");
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
  blk("PV_VOLTAGE           double                                                 ");
  blk("PV_CURRENT           double                                                 ");
  blk("PV_POWER_L           double                                                 ");
  blk("PV_POWER_H           double                                                 ");
  blk("BATTERY_VOLTAGE      double                                                 ");
  blk("BATTERY_CURRENT      double                                                 ");
  blk("BATTERY_NET_CURRENT  double                                                 ");
  blk("BATTERY_POWER_L      double                                                 ");
  blk("BATTERY_POWER_H      double                                                 ");
  blk("LOAD_VOLTAGE         double                                                 ");
  blk("LOAD_CURRENT         double                                                 ");
  blk("LOAD_POWER_L         double                                                 ");
  blk("LOAD_POWER_H         double                                                 ");
  blk("BATTERY_SOC          double                                                 ");
  blk("BATTERY_TEMP         double                                                 ");
  blk("DEVICE_TEMP          double                                                 ");
  blk("                                                                            ");
  blk("LUX                  double                                                 ");
  blk("WHITE                double                                                 ");
  blk("RAW_ALS              double                                                 ");
  blk("                                                                            ");
  blk("WIND_READING         double                                                 ");
  blk("WIND_SPEED           double                                                 ");
  blk("                                                                            ");
  exit(0);
}
