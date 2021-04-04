/*
 * GPGSAnmea.cpp
 *
 *  Created on: January 8 2015
 *      Author: Alon Yaari

#include "GPGSAnmea.h"

using namespace std;

GPGSAnmea::GPGSAnmea()
{
  nmeaLen     = NUMELEM_GPGSA;
  SetKey("GPGSAnmea");
  info.modeGPS      = GPS_MODE_BLANK;
  info.modeFix      = FIX_MODE_BLANK;
  info.pdop         = BLANK_DOUBLE;
  info.hdop         = BLANK_DOUBLE;
  info.vdop         = BLANK_DOUBLE;
  for (int i = 0; i < 12; i++)
    info.svID[i]    = NO_SV;
}

//     GPGSA - GPSA DOP and Active Satellites
//
//     $GPGSA<
//     $GPGSA,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>,<9>,<10>,<11>,<12>,<13>,<14>,<15>,<16>,<17>*hh<CR><LF>
//     <1>   Mode         M=Manual, forced to operate in 2D or 3D, A=Automatic, 3D/2D
//     <2>   Mode         1=Fix not available, 2=2D, 3=3D
//     <3-14> SV id       IDs of SVs used in position fix (null for unused fields)
//     <15>   PDOP
//     <16>   HDOP
//     <17>   VDOP

bool GPGSAnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
  // Always call base class version of this function to validate basics
  //      and populate inSentence.elements
  if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
    return false;

  for (unsigned short i = 1; i < curSentence.numElements; i++) {
    string sVal = curSentence.elements.at(i);
    if (i >= 3 && i <= 14)
      FromString_svID(i, sVal);
    else {
      switch(i) {
        case 1:     FromString_modeGPS(sVal);                       break;
        case 2:     FromString_modeFix(sVal);                       break;
        case 15:    FromString_pdop(sVal);                          break;
        case 16:    FromString_hdop(sVal);                          break;
        case 17:    FromString_vdop(sVal);                          break; } } }
    return CriticalDataAreValid();
}

bool GPGSAnmea::CriticalDataAreValid()
{
  return true;
}

bool GPGSAnmea::Validate_modeGPS(char& cVal)
{
  return (cVal == GPS_MODE_BLANK     ||
          cVal == GPS_MODE_AUTOMATIC ||
          cVal == GPS_MODE_MANUAL);
}

bool GPGSAnmea::Validate_modeFix(char &cVal)
{
  return (cVal == FIX_MODE_BLANK     ||
          cVal == FIX_MODE_NOT_AVAIL ||
          cVal == FIX_MODE_2D        ||
          cVal == FIX_MODE_3D);
}

bool GPGSAnmea::Validate_svID(unsigned short usVal)
{
  return (usVal >= 0 && usVal <= 128);
}

bool GPGSAnmea::Validate_DOP(double& dVal)
{
  return (dVal >= 0.0 && dVal <= 9999.9);
}

bool GPGSAnmea::GetInfo(infoGPGSA& curInfo)
{
  curInfo = info;
  return CriticalDataAreValid();
}

bool GPGSAnmea::Get_modeGPS(char& cVal)
{
  cVal = info.modeGPS;
  return Validate_modeGPS(cVal);
}

bool GPGSAnmea::Get_modeFix(char& cVal)
{
  cVal = info.modeFix;
  return Validate_modeFix(cVal);
}


bool GPGSAnmea::Get_satNum(unsigned short& usVal)
{
  usVal = info.svID;
  return Validate_svID(usVal);
}

bool GPGSAnmea::Get_heading(double& dVal)
{
  dVal = info.heading;
  return Validate_heading(dVal);
}

bool GPGSAnmea::Get_pitch(double& dVal)
{
  dVal = info.pitch;
  return Validate_pitchRoll(dVal);
}

bool GPGSAnmea::Get_roll(double& dVal)
{
  dVal = info.roll;
  return Validate_pitchRoll(dVal);
}

bool GPGSAnmea::Get_navTimestamp(utcTime& timeUtc)
{
    timeUtc = info.navTimestamp;
    return Validate_timeUTC(timeUtc);
}

bool GPGSAnmea::Set_timeUTC(const utcTime& t)
{
    info.timeUTC = t;
    return Validate_timeUTC(info.timeUTC);
}

bool GPGSAnmea::Set_id(const int& iVal)
{
  info.idCompass = iVal;
  return Validate_id(info.idCompass);
}

bool GPGSAnmea::Set_heading(const double& dVal)
{
  info.heading = dVal;
  return Validate_heading(info.heading);
}

bool GPGSAnmea::Set_roll(const double& dVal)
{
  info.roll = dVal;
  return Validate_pitchRoll(info.roll);
}

bool CPIMUnmea::Set_navTimestamp(const utcTime& t)
{
  info.navTimestamp = t;
  return Validate_timeUTC(info.navTimestamp);
}

bool GPGSAnmea::FromString_timeUTC(std::string sVal)
{
  info.timeUTC.Set_utcTimeFromNMEA(sVal);     // Set_utcTimeFromNMEA() also handles blank strings
  return Validate_timeUTC(info.timeUTC);
}

bool GPGSAnmea::FromString_id(string sVal)
{
  StoreIntFromNMEAstring(info.idCompass, sVal);
  return Validate_id(info.idCompass);
}

bool GPGSAnmea::FromString_heading(string sVal)
{
  StoreDoubleFromNMEAstring(info.heading, sVal);
  return Validate_heading(info.heading);
}

bool GPGSAnmea::FromString_pitch(string sVal)
{
  StoreDoubleFromNMEAstring(info.pitch, sVal);
  return Validate_pitchRoll(info.pitch);
}
bool GPGSAnmea::FromString_roll(string sVal)
{
  StoreDoubleFromNMEAstring(info.roll, sVal);
  return Validate_pitchRoll(info.roll);
}

bool CPIMUnmea::FromString_navTimestamp(std::string sVal)
{
  info.timeUTC.Set_utcTimeFromNMEAwithBackup(sVal, info.timeUTC);     // If no navTimestamp, assign timeUTC
  return Validate_timeUTC(info.navTimestamp);
}

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool GPGSAnmea::ProduceNMEASentence(string& newSentence)
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

string GPGSAnmea::ToString_utcTime()
{
    string sVal;
    if (!info.timeUTC.Get_utcTimeString(sVal, 2))
        sVal = "";
    return sVal;
}

string GPGSAnmea::ToString_id(int& iVal)
{
    if (iVal == BLANK_INT)
        return "";
    return intToString(iVal);
}

string GPGSAnmea::ToString_heading(double& dVal)
{
    if (dVal == BLANK_DOUBLE)
        return "";
    return doubleToString(dVal, 2);
}

string GPGSAnmea::ToString_pitchRoll(double& dVal)
{
    if (dVal == BLANK_DOUBLE)
        return "";
    return doubleToString(dVal, 5);
}

string GPGSAnmea::ToString_navTimestamp()
{
  string sVal;
  if (!info.navTimestamp.Get_utcTimeString(sVal, 2))
      sVal = "";
  return sVal;
}

*/
