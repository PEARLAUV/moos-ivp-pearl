/*
 * CPNVRnmea.cpp
 *
 *  Created on: January 8 2015
 *      Author: Alon Yaari
 */

#include "CPNVRnmea.h"

using namespace std;

CPNVRnmea::CPNVRnmea()
{
    nmeaLen     = NUMELEM_CPNVR;
    SetKey("CPNVRnmea");
    info.velEast      = BLANK_DOUBLE;
    info.velNorth     = BLANK_DOUBLE;
    info.velDown      = BLANK_DOUBLE;
    info.ratePitch    = BLANK_DOUBLE;
    info.rateRoll     = BLANK_DOUBLE;
    info.rateYaw      = BLANK_DOUBLE;
    info.timeUTC.Set_Invalid();
}

//     CPNVR - Velocity and rate updated as determined by the front-seat estimation filter
//
//     $CPNVR,<1>,<2>,<3>,<4>,<5>,<6>,<7>*hh<CR><LF>
//     <1>  UTC time,  format hhmmss.s
//     <2>  vel_east   East component of vehicle transit velocity
//     <3>  vel_north  North component of vehicle transit velocity
//     <4>  vel_down   Vertical component of vehicle transit velocity
//     <5>  rate_pitch Degree per second of pitch rate
//     <6>  rate_roll  Degree per second of roll rate
//     <7>  rate_yaw   Degree per second of yaw rate

bool CPNVRnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
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
      case 2:     FromString_velEast(sVal);                       break;
      case 3:     FromString_velNorth(sVal);                      break;
      case 4:     FromString_velDown(sVal);                       break;
      case 5:     FromString_ratePitch(sVal);                     break;
      case 7:     FromString_rateYaw(sVal);                       break;
      case 8:     FromString_rateRoll(sVal);                      break; } }
    return CriticalDataAreValid();
}

bool CPNVRnmea::CriticalDataAreValid()
{
  return true;
}

bool CPNVRnmea::Validate_timeUTC(utcTime& t)
{
    if (!t.IsValid()) {
        AddError("Invalid UTC time.");
        return false; }
    return true;
}

bool CPNVRnmea::Validate_velocity(double& dVal)
{
  return (dVal >=-1.0 && dVal <= 1.0);
}

bool CPNVRnmea::Validate_rate(double& dVal)
{
  return (dVal >=-1.0 && dVal <= 1.0);
}

bool CPNVRnmea::GetInfo(infoCPNVR& curInfo)
{
  curInfo = info;
  return CriticalDataAreValid();
}

bool CPNVRnmea::Get_timeUTC(utcTime& timeUtc)
{
  timeUtc = info.timeUTC;
  return Validate_timeUTC(timeUtc);
}

bool CPNVRnmea::Get_velEast(double& dVal)
{
  dVal = info.velEast;
  return Validate_velocity(dVal);
}

bool CPNVRnmea::Get_velNorth(double& dVal)
{
  dVal = info.velNorth;
  return Validate_velocity(dVal);
}

bool CPNVRnmea::Get_velDown(double& dVal)
{
  dVal = info.velDown;
  return Validate_velocity(dVal);
}

bool CPNVRnmea::Get_ratePitch(double& dVal)
{
  dVal = info.ratePitch;
  return Validate_rate(dVal);
}

bool CPNVRnmea::Get_rateRoll(double& dVal)
{
  dVal = info.rateRoll;
  return Validate_rate(dVal);
}

bool CPNVRnmea::Get_rateYaw(double& dVal)
{
  dVal = info.rateYaw;
  return Validate_rate(dVal);
}

bool CPNVRnmea::Set_timeUTC(const utcTime& t)
{
    info.timeUTC = t;
    return Validate_timeUTC(info.timeUTC);
}

bool CPNVRnmea::Set_velEast(const double& dVal)
{
  info.velEast = dVal;
  return Validate_velocity(info.velEast);
}

bool CPNVRnmea::Set_velNorth(const double& dVal)
{
  info.velNorth = dVal;
  return Validate_velocity(info.velNorth);
}

bool CPNVRnmea::Set_velDown(const double& dVal)
{
  info.velDown = dVal;
  return Validate_velocity(info.velDown);
}

bool CPNVRnmea::Set_ratePitch(const double& dVal)
{
  info.ratePitch = dVal;
  return Validate_velocity(info.ratePitch);
}

bool CPNVRnmea::Set_rateRoll(const double& dVal)
{
  info.rateRoll = dVal;
  return Validate_velocity(info.rateRoll);
}

bool CPNVRnmea::Set_rateYaw(const double& dVal)
{
  info.rateYaw = dVal;
  return Validate_velocity(info.rateYaw);
}

bool CPNVRnmea::FromString_timeUTC(std::string sVal)
{
  info.timeUTC.Set_utcTimeFromNMEA(sVal);     // Set_utcTimeFromNMEA() also handles blank strings
  return Validate_timeUTC(info.timeUTC);
}

bool CPNVRnmea::FromString_velEast(string sVal)
{
  StoreDoubleFromNMEAstring(info.velEast, sVal);
  return Validate_velocity(info.velEast);
}

bool CPNVRnmea::FromString_velNorth(string sVal)
{
  StoreDoubleFromNMEAstring(info.velNorth, sVal);
  return Validate_velocity(info.velNorth);
}

bool CPNVRnmea::FromString_velDown(string sVal)
{
  StoreDoubleFromNMEAstring(info.velDown, sVal);
  return Validate_velocity(info.velDown);
}

bool CPNVRnmea::FromString_ratePitch(string sVal)
{
  StoreDoubleFromNMEAstring(info.ratePitch, sVal);
  return Validate_velocity(info.ratePitch);
}

bool CPNVRnmea::FromString_rateRoll(string sVal)
{
  StoreDoubleFromNMEAstring(info.rateRoll, sVal);
  return Validate_velocity(info.rateRoll);
}

bool CPNVRnmea::FromString_rateYaw(string sVal)
{
  StoreDoubleFromNMEAstring(info.rateYaw, sVal);
  return Validate_velocity(info.rateYaw);
}

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool CPNVRnmea::ProduceNMEASentence(string& newSentence)
{
  // Call base class version to do basic checks
  //      - Includes call to CriticalDataAreValid()
  if (!NMEAbase::ProduceNMEASentence(newSentence))
    return false;

  string dataBody = "";
  dataBody += ToString_utcTime();                 // <1>
  dataBody += ",";
  dataBody += ToString_velocity(info.velEast);    // <2>
  dataBody += ",";
  dataBody += ToString_velocity(info.velNorth);   // <3>
  dataBody += ",";
  dataBody += ToString_velocity(info.velDown);    // <4>
  dataBody += ",";
  dataBody += ToString_rate(info.ratePitch);      // <5>
  dataBody += ",";
  dataBody += ToString_rate(info.rateRoll);       // <6>
  dataBody += ",";
  dataBody += ToString_rate(info.rateYaw);        // <7>

  // Pre and post-pend the mechanics of the NMEA sentence
  BuildFullSentence(curSentence.nmeaSentence, dataBody);
  newSentence = curSentence.nmeaSentence;
  return true;
}

string CPNVRnmea::ToString_utcTime()
{
    string sVal;
    if (!info.timeUTC.Get_utcTimeString(sVal, 2))
        sVal = "";
    return sVal;
}

string CPNVRnmea::ToString_velocity(double& dVal)
{
    if (dVal == BLANK_DOUBLE)
        return "";
    return doubleToString(dVal, 6);
}

string CPNVRnmea::ToString_rate(double& dVal)
{
    if (dVal == BLANK_DOUBLE)
        return "";
    return doubleToString(dVal, 5);
}
























//
