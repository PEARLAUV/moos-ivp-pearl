/*
 * CPRCMnmea.cpp
 *
 *  Created on: January 8 2015
 *      Author: Alon Yaari
 */

#include "CPRCMnmea.h"

using namespace std;

CPRCMnmea::CPRCMnmea()
{
    nmeaLen     = NUMELEM_CPRCM;
    SetKey("CPRCMnmea");
    info.idCompass    = BLANK_INT;
    info.heading      = BLANK_DOUBLE;
    info.pitch        = BLANK_DOUBLE;
    info.roll         = BLANK_DOUBLE;
    info.timeUTC.Set_Invalid();
    info.navTimestamp.Set_Invalid();
}

//     CPRCM - Clearpath Raw Compass Data
//
//     $CPRCM,<1>,<2>,<3>,<4>,<5>,<6>*hh<CR><LF>
//     <1>  TIMESTAMP     Format hhmmss.s
//     <2>  ID_COMPASS    Unique ID number of the compass being reported on
//     <3>  HEADING       Raw reading from compass for degrees clockwise from true north
//     <4>  PITCH         Raw reading from compass for degrees of pitch
//     <5>  ROLL          Raw reading from compass for degrees of roll
//     <6>  NAV_TIMESTAMP Timestamp for time compass reported this data. If blank, use TIMESTAMP

bool CPRCMnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
  // Always call base class version of this function to validate basics
  //      and populate inSentence.elements
  if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
    return false;

  for (unsigned short i = 1; i < curSentence.numElements; i++) {
    string sVal = curSentence.elements.at(i);
    switch(i) {
      case 1:     FromString_timeUTC(sVal);                       break;
      case 2:     FromString_id(sVal);                            break;
      case 3:     FromString_heading(sVal);                       break;
      case 4:     FromString_pitch(sVal);                         break;
      case 5:     FromString_roll(sVal);                          break;
      case 8:     FromString_navTimestamp(sVal);                  break; } }
    return CriticalDataAreValid();
}

bool CPRCMnmea::CriticalDataAreValid()
{
  return true;
}

bool CPRCMnmea::Validate_timeUTC(utcTime& t)
{
    if (!t.IsValid()) {
        AddError("Invalid UTC time.");
        return false; }
    return true;
}

bool CPRCMnmea::Validate_id(int &iVal)
{
  return (iVal >=0 && iVal <= 999);
}

bool CPRCMnmea::Validate_heading(double& dVal)
{
  return (dVal >= 0.0 && dVal <= 360.0);
}

bool CPRCMnmea::Validate_pitchRoll(double& dVal)
{
  return (dVal >= -90.0 && dVal <= 90.0);
}

bool CPRCMnmea::GetInfo(infoCPRCM& curInfo)
{
  curInfo = info;
  return CriticalDataAreValid();
}

bool CPRCMnmea::Get_timeUTC(utcTime& timeUtc)
{
  timeUtc = info.timeUTC;
  return Validate_timeUTC(timeUtc);
}

bool CPRCMnmea::Get_id(int& iVal)
{
  iVal = info.idCompass;
  return Validate_id(iVal);
}

bool CPRCMnmea::Get_heading(double& dVal)
{
  dVal = info.heading;
  return Validate_heading(dVal);
}

bool CPRCMnmea::Get_pitch(double& dVal)
{
  dVal = info.pitch;
  return Validate_pitchRoll(dVal);
}

bool CPRCMnmea::Get_roll(double& dVal)
{
  dVal = info.roll;
  return Validate_pitchRoll(dVal);
}

bool CPRCMnmea::Get_navTimestamp(utcTime& timeUtc)
{
    timeUtc = info.navTimestamp;
    return Validate_timeUTC(timeUtc);
}

bool CPRCMnmea::Set_timeUTC(const utcTime& t)
{
    info.timeUTC = t;
    return Validate_timeUTC(info.timeUTC);
}

bool CPRCMnmea::Set_id(const int& iVal)
{
  info.idCompass = iVal;
  return Validate_id(info.idCompass);
}

bool CPRCMnmea::Set_heading(const double& dVal)
{
  info.heading = dVal;
  return Validate_heading(info.heading);
}

bool CPRCMnmea::Set_roll(const double& dVal)
{
  info.roll = dVal;
  return Validate_pitchRoll(info.roll);
}

bool CPRCMnmea::Set_navTimestamp(const utcTime& t)
{
  info.navTimestamp = t;
  return Validate_timeUTC(info.navTimestamp);
}

bool CPRCMnmea::FromString_timeUTC(std::string sVal)
{
  info.timeUTC.Set_utcTimeFromNMEA(sVal);     // Set_utcTimeFromNMEA() also handles blank strings
  return Validate_timeUTC(info.timeUTC);
}

bool CPRCMnmea::FromString_id(string sVal)
{
  StoreIntFromNMEAstring(info.idCompass, sVal);
  return Validate_id(info.idCompass);
}

bool CPRCMnmea::FromString_heading(string sVal)
{
  StoreDoubleFromNMEAstring(info.heading, sVal);
  return Validate_heading(info.heading);
}

bool CPRCMnmea::FromString_pitch(string sVal)
{
  StoreDoubleFromNMEAstring(info.pitch, sVal);
  return Validate_pitchRoll(info.pitch);
}
bool CPRCMnmea::FromString_roll(string sVal)
{
  StoreDoubleFromNMEAstring(info.roll, sVal);
  return Validate_pitchRoll(info.roll);
}

bool CPRCMnmea::FromString_navTimestamp(std::string sVal)
{
  info.timeUTC.Set_utcTimeFromNMEAwithBackup(sVal, info.timeUTC);     // If no navTimestamp, assign timeUTC
  return Validate_timeUTC(info.navTimestamp);
}

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool CPRCMnmea::ProduceNMEASentence(string& newSentence)
{
  // Call base class version to do basic checks
  //      - Includes call to CriticalDataAreValid()
  if (!NMEAbase::ProduceNMEASentence(newSentence))
    return false;

  string dataBody = "";
  dataBody += ToString_utcTime();                 // <1>
  dataBody += ",";
  dataBody += ToString_id(info.idCompass);        // <2>
  dataBody += ",";
  dataBody += ToString_heading(info.heading);     // <3>
  dataBody += ",";
  dataBody += ToString_pitchRoll(info.pitch);     // <4>
  dataBody += ",";
  dataBody += ToString_pitchRoll(info.roll);      // <5>
  dataBody += ",";
  dataBody += ToString_navTimestamp();            // <6>

  // Pre and post-pend the mechanics of the NMEA sentence
  BuildFullSentence(curSentence.nmeaSentence, dataBody);
  newSentence = curSentence.nmeaSentence;
  return true;
}

string CPRCMnmea::ToString_utcTime()
{
    string sVal;
    if (!info.timeUTC.Get_utcTimeString(sVal, 2))
        sVal = "";
    return sVal;
}

string CPRCMnmea::ToString_id(int& iVal)
{
    if (iVal == BLANK_INT)
        return "";
    return intToString(iVal);
}

string CPRCMnmea::ToString_heading(double& dVal)
{
    if (dVal == BLANK_DOUBLE)
        return "";
    return doubleToString(dVal, 2);
}

string CPRCMnmea::ToString_pitchRoll(double& dVal)
{
    if (dVal == BLANK_DOUBLE)
        return "";
    return doubleToString(dVal, 5);
}

string CPRCMnmea::ToString_navTimestamp()
{
  string sVal;
  if (!info.navTimestamp.Get_utcTimeString(sVal, 2))
      sVal = "";
  return sVal;
}
























//
