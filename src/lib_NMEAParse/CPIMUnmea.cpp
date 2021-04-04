/*
 * CPIMUnmea.cpp
 *
 *  Created on: January 8 2015
 *      Author: Alon Yaari
 */

#include "CPIMUnmea.h"

using namespace std;

CPIMUnmea::CPIMUnmea()
{
    nmeaLen     = NUMELEM_CPIMU;
    SetKey("CPIMU");
    info.angRateX     = BLANK_DOUBLE;
    info.angRateY     = BLANK_DOUBLE;
    info.angRateZ     = BLANK_DOUBLE;
    info.accelX       = BLANK_DOUBLE;
    info.accelY       = BLANK_DOUBLE;
    info.accelZ       = BLANK_DOUBLE;
    info.timeUTC.Set_Invalid();
    info.navTimestamp.Set_Invalid();
}

//      CPIMU - Clearpath wire protocol raw IMU message
//
//      $CPIMU,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>*hh<CR><LF>
//      <1>  Timestamp      Timestamp of the sentence.
//      <2>  AngularRate_X  Angular rate about the X axis in deg/s, right-hand rule
//      <3>  AngularRate_Y  Angular rate about the Y axis in deg/s, right-hand rule
//      <4>  AngularRate_Z  Angular rate about the Z axis in deg/s, right-hand rule
//      <5>  Accel_X        Acceleration along the X axis in m/s^2, forward positive
//      <6>  Accel_Y        Acceleration along the Y axis in m/s^2, forward positive
//      <7>  Accel_Z        Acceleration along the Z axis in m/s^2, forward positive
//      <8>  NavTimestamp   Timestamp for time compass reported this data. If blank, use [Timestamp]

bool CPIMUnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
  // Always call base class version of this function to validate basics
  //      and populate inSentence.elements
  if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
      return false;

  string strLat       = BLANK_DOUBLE_STRING;
  string strLon       = BLANK_DOUBLE_STRING;
  string strHemiLat   = BLANK_STRING;
  string strHemiLon   = BLANK_STRING;
  for (unsigned short i = 1; i < curSentence.numElements; i++) {
      string sVal = curSentence.elements.at(i);
      switch(i) {
          case 1:     FromString_timeUTC(sVal);                       break;
          case 2:     FromString_angRateX(sVal);                      break;
          case 3:     FromString_angRateY(sVal);                      break;
          case 4:     FromString_angRateZ(sVal);                      break;
          case 5:     FromString_accelX(sVal);                        break;
          case 6:     FromString_accelY(sVal);                        break;
          case 7:     FromString_accelZ(sVal);                        break;
          case 8:     FromString_navTimestamp(sVal);                  break;
          default:                                                    break; } }
  return CriticalDataAreValid();
}

bool CPIMUnmea::CriticalDataAreValid()
{
  return true;
}

bool CPIMUnmea::Validate_timeUTC(utcTime& t)
{
    if (!t.IsValid()) {
        AddError("Invalid UTC time.");
        return false; }
    return true;
}

bool CPIMUnmea::Validate_angRate(double& dVal)
{
  return (dVal >=-1.0 && dVal <= 1.0);
}

bool CPIMUnmea::Validate_accel(double& dVal)
{
  return (dVal >=-1.0 && dVal <= 1.0);
}

bool CPIMUnmea::Validate_navTimestamp(utcTime& t)
{
  return Validate_timeUTC(t);
}

bool CPIMUnmea::GetInfo(infoCPIMU& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool CPIMUnmea::Get_timeUTC(utcTime& timeUtc)
{
    timeUtc = info.timeUTC;
    return Validate_timeUTC(timeUtc);
}

bool CPIMUnmea::Get_angRateX(double& dVal)
{
  dVal = info.angRateX;
  return Validate_angRate(dVal);
}

bool CPIMUnmea::Get_angRateY(double& dVal)
{
  dVal = info.angRateY;
  return Validate_angRate(dVal);
}

bool CPIMUnmea::Get_angRateZ(double& dVal)
{
  dVal = info.angRateZ;
  return Validate_angRate(dVal);
}

bool CPIMUnmea::Get_accelX(double& dVal)
{
  dVal = info.accelX;
  return Validate_accel(dVal);
}

bool CPIMUnmea::Get_accelY(double& dVal)
{
  dVal = info.accelY;
  return Validate_accel(dVal);
}

bool CPIMUnmea::Get_accelZ(double& dVal)
{
  dVal = info.accelZ;
  return Validate_accel(dVal);
}

bool CPIMUnmea::Get_navTimestamp(utcTime& timeUtc)
{
    timeUtc = info.navTimestamp;
    return Validate_timeUTC(timeUtc);
}

bool CPIMUnmea::Set_timeUTC(const utcTime& t)
{
    info.timeUTC = t;
    return Validate_timeUTC(info.timeUTC);
}

bool CPIMUnmea::Set_angRateX(const double& dVal)
{
  info.angRateX = dVal;
  return Validate_angRate(info.angRateX);
}

bool CPIMUnmea::Set_angRateY(const double& dVal)
{
  info.angRateY = dVal;
  return Validate_angRate(info.angRateY);
}

bool CPIMUnmea::Set_angRateZ(const double& dVal)
{
  info.angRateZ = dVal;
  return Validate_angRate(info.angRateZ);
}

bool CPIMUnmea::Set_accelX(const double& dVal)
{
  info.accelX = dVal;
  return Validate_angRate(info.accelX);
}

bool CPIMUnmea::Set_accelY(const double& dVal)
{
  info.accelY = dVal;
  return Validate_angRate(info.accelY);
}

bool CPIMUnmea::Set_accelZ(const double& dVal)
{
  info.accelZ = dVal;
  return Validate_angRate(info.accelZ);
}

bool CPIMUnmea::Set_navTimestamp(const utcTime& t)
{
  info.navTimestamp = t;
  return Validate_timeUTC(info.navTimestamp);
}

bool CPIMUnmea::FromString_timeUTC(std::string sVal)
{
  info.timeUTC.Set_utcTimeFromNMEA(sVal);     // Set_utcTimeFromNMEA() also handles blank strings
  return Validate_timeUTC(info.timeUTC);
}

bool CPIMUnmea::FromString_angRateX(string sVal)
{
  StoreDoubleFromNMEAstring(info.angRateX, sVal);
  return Validate_angRate(info.angRateX);
}

bool CPIMUnmea::FromString_angRateY(string sVal)
{
  StoreDoubleFromNMEAstring(info.angRateY, sVal);
  return Validate_angRate(info.angRateY);
}

bool CPIMUnmea::FromString_angRateZ(string sVal)
{
  StoreDoubleFromNMEAstring(info.angRateZ, sVal);
  return Validate_angRate(info.angRateZ);
}

bool CPIMUnmea::FromString_accelX(string sVal)
{
  StoreDoubleFromNMEAstring(info.accelX, sVal);
  return Validate_angRate(info.accelX);
}

bool CPIMUnmea::FromString_accelY(string sVal)
{
  StoreDoubleFromNMEAstring(info.accelY, sVal);
  return Validate_angRate(info.accelY);
}

bool CPIMUnmea::FromString_accelZ(string sVal)
{
  StoreDoubleFromNMEAstring(info.accelZ, sVal);
  return Validate_angRate(info.accelZ);
}

bool CPIMUnmea::FromString_navTimestamp(std::string sVal)
{
  info.timeUTC.Set_utcTimeFromNMEAwithBackup(sVal, info.timeUTC);     // If no navTimestamp, assign timeUTC
  return Validate_timeUTC(info.navTimestamp);
}

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool CPIMUnmea::ProduceNMEASentence(string& newSentence)
{
  // Call base class version to do basic checks
  //      - Includes call to CriticalDataAreValid()
  if (!NMEAbase::ProduceNMEASentence(newSentence))
    return false;

  string dataBody = "";
  dataBody += ToString_utcTime();                 // <1>
  dataBody += ",";
  dataBody += ToString_angRate(info.angRateX);    // <2>
  dataBody += ",";
  dataBody += ToString_angRate(info.angRateY);    // <3>
  dataBody += ",";
  dataBody += ToString_angRate(info.angRateZ);    // <4>
  dataBody += ",";
  dataBody += ToString_angRate(info.accelX);      // <5>
  dataBody += ",";
  dataBody += ToString_angRate(info.accelY);      // <6>
  dataBody += ",";
  dataBody += ToString_angRate(info.accelZ);      // <7>
  dataBody += ",";
  dataBody += ToString_navTimestamp();            // <8>

  // Pre and post-pend the mechanics of the NMEA sentence
  BuildFullSentence(curSentence.nmeaSentence, dataBody);
  newSentence = curSentence.nmeaSentence;
  return true;
}

string CPIMUnmea::ToString_utcTime()
{
    string sVal;
    if (!info.timeUTC.Get_utcTimeString(sVal, 2))
        sVal = "";
    return sVal;
}

string CPIMUnmea::ToString_angRate(double& dVal)
{
    if (dVal == BLANK_DOUBLE)
        return "";
    return doubleToString(dVal, 5);
}

string CPIMUnmea::ToString_accel(double& dVal)
{
    if (dVal == BLANK_DOUBLE)
        return "";
    return doubleToString(dVal, 6);
}

string CPIMUnmea::ToString_navTimestamp()
{
  string sVal;
  if (!info.navTimestamp.Get_utcTimeString(sVal, 2))
      sVal = "";
  return sVal;
}
























//
