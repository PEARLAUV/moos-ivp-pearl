/* 
 * PLIMUnmea.h
 * Created on: 1/23/2021
 * Author: Ethan Rolland
 * 
 */

#include "PLIMUnmea.h"

using namespace std;

PLIMUnmea::PLIMUnmea()
{
	  nmeaLen     = NUMELEM_PLIMU;
    SetKey("PLIMU");

    info.heading      = BLANK_DOUBLE;
    info.pitch        = BLANK_DOUBLE;
    info.roll         = BLANK_DOUBLE;
}

//     PLIMU - PEARL IMU Data
//
//     $PLIMU,<1>,<2>,<3>,*hh
//     <1>  HEADING       Raw reading from IMU for degrees clockwise from true north
//     <2>  PITCH         Raw reading from IMU for degrees of pitch
//     <3>  ROLL          Raw reading from IMU for degrees of roll

bool PLIMUnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
  // Always call base class version of this function to validate basics
  //      and populate inSentence.elements
  if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
    return false;

  for (unsigned short i = 1; i < curSentence.numElements; i++) {
    string sVal = curSentence.elements.at(i);
    switch(i) {
      case 1:     FromString_heading(sVal);                       break;
      case 2:     FromString_pitch(sVal);                         break;
      case 3:     FromString_roll(sVal);                          break; } }
  return CriticalDataAreValid();
}

bool PLIMUnmea::CriticalDataAreValid()
{
  return true;
}

//------------------------------------------------

bool PLIMUnmea::Validate_heading(double& dVal)
{
  return (dVal >= -360.0 && dVal <= 360.0);
}

bool PLIMUnmea::Validate_pitch(double& dVal)
{
  return (dVal >= -180.0 && dVal <= 180.0);
}

bool PLIMUnmea::Validate_roll(double& dVal)
{
  return (dVal >= -180.0 && dVal <= 180.0);
}

//------------------------------------------------

bool PLIMUnmea::GetInfo(infoPLIMU& curInfo)
{
  curInfo = info;
  return CriticalDataAreValid();
}

bool PLIMUnmea::Get_heading(double& dVal)
{
  dVal = info.heading;
  return Validate_heading(dVal);
}

bool PLIMUnmea::Get_pitch(double& dVal)
{
  dVal = info.pitch;
  return Validate_pitch(dVal);
}

bool PLIMUnmea::Get_roll(double& dVal)
{
  dVal = info.roll;
  return Validate_roll(dVal);
}

//------------------------------------------------

bool PLIMUnmea::Set_heading(const double& dVal)
{
  info.heading = dVal;
  return Validate_heading(info.heading);
}

bool PLIMUnmea::Set_pitch(const double& dVal)
{
  info.pitch = dVal;
  return Validate_pitch(info.pitch);
}

bool PLIMUnmea::Set_roll(const double& dVal)
{
  info.roll = dVal;
  return Validate_roll(info.roll);
}

//------------------------------------------------

bool PLIMUnmea::FromString_heading(string sVal)
{
  StoreDoubleFromNMEAstring(info.heading, sVal);
  return Validate_heading(info.heading);
}

bool PLIMUnmea::FromString_pitch(string sVal)
{
  StoreDoubleFromNMEAstring(info.pitch, sVal);
  return Validate_pitch(info.pitch);
}
bool PLIMUnmea::FromString_roll(string sVal)
{
  StoreDoubleFromNMEAstring(info.roll, sVal);
  return Validate_roll(info.roll);
}

//------------------------------------------------

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool PLIMUnmea::ProduceNMEASentence(string& newSentence)
{
  // Call base class version to do basic checks
  //      - Includes call to CriticalDataAreValid()
  if (!NMEAbase::ProduceNMEASentence(newSentence))
    return false;

  string dataBody = "";
//  dataBody += ToString_utcTime();                 // <1>
//  dataBody += ",";
  dataBody += ToString_heading(info.heading);     // <2>
  dataBody += ",";
  dataBody += ToString_pitch(info.pitch);     // <3>
  dataBody += ",";
  dataBody += ToString_roll(info.roll);      // <4>

  // Pre and post-pend the mechanics of the NMEA sentence
  BuildFullSentence(curSentence.nmeaSentence, dataBody);
  newSentence = curSentence.nmeaSentence;
  return true;
}

//------------------------------------------------

string PLIMUnmea::ToString_heading(double& dVal)
{
    if (dVal == BLANK_DOUBLE)
        return "";
    return doubleToString(dVal, 5);
}

string PLIMUnmea::ToString_pitch(double& dVal)
{
    if (dVal == BLANK_DOUBLE)
        return "";
    return doubleToString(dVal, 5);
}

string PLIMUnmea::ToString_roll(double& dVal)
{
    if (dVal == BLANK_DOUBLE)
        return "";
    return doubleToString(dVal, 5);
}
