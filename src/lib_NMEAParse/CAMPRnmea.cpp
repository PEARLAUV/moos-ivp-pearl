/*
 * CAMPRnmea.cpp
 *
 *  Created on: May 22, 2015
 *      Author: Alon Yaari
 *
 *  Based on code by:
 *      Alon Yaari  May, 2014
 */

#include "CAMPRnmea.h"

using namespace std;


// A. Constructor
CAMPRnmea::CAMPRnmea()
{
    nmeaLen           = NUMELEM_CAMPR;
    SetKey("CAMPR");
    info.src          = BAD_UCHAR;
    info.dest         = BAD_UCHAR;
    info.travelTime   = BAD_DOUBLE;
}

/* Sentence Definition
        Reply to Ping has been received, modem to host
        $CAMPR,<1>,<2>,<3>*hh<CR><LF>
        <1>  SRC Source (data originator), Valid: 0 to 127
        <2>  DEST Destination (data receiver), Valid: 0 to 127
        <3>  TRAVEL TIME in seconds (double), Valid >= 0.0.
*/

// C. Parse incoming NMEA sentence

bool CAMPRnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
  if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
    return false;
  for (unsigned short i = 1; i < curSentence.numElements; i++) {
    string sVal = curSentence.elements.at(i);
    switch(i) {
      case 1: FromString_srcAddress(sVal);                        break;
      case 2: FromString_destAddress(sVal);                       break;
      case 3: FromString_travelTime(sVal);                        break;
      default:                                                    break; } }
  return CriticalDataAreValid();
}

// D. Data validation for info buffer
bool CAMPRnmea::CriticalDataAreValid()
{
  bool bValid = Validate_modemAddress(info.src);
  bValid &= Validate_modemAddress(info.dest);
  bValid &= Validate_travelTime();
  return bValid;
}

// E. Validation functions

bool CAMPRnmea::Validate_modemAddress(unsigned char uc)
{
  return IsValidBoundedUChar(uc, (unsigned char) 0, (unsigned char) 127, true, false);
}

bool CAMPRnmea::Validate_travelTime()
{
  return IsValidBoundedDouble(info.travelTime, 0.0, 999999.99, true, true);
}

// F. Getters
bool CAMPRnmea::GetInfo(infoCAMPR& curInfo)
{
  curInfo = info;
  return CriticalDataAreValid();
}

bool CAMPRnmea::Get_srcAddress(unsigned char& uc)
{
  uc = info.src;
  return Validate_modemAddress(uc);
}

bool CAMPRnmea::Get_destAddress(unsigned char& uc)
{
  uc = info.dest;
  return Validate_modemAddress(uc);
}

bool CAMPRnmea::Get_travelTime(double& d)
{
  d = info.travelTime;
  return Validate_travelTime();
}

// G. Setters
bool CAMPRnmea::Set_srcAddress(const unsigned char uc)
{
  info.src = uc;
  return Validate_modemAddress(uc);
}

bool CAMPRnmea::Set_destAddress(const unsigned char uc)
{
  info.dest = uc;
  return Validate_modemAddress(uc);
}

bool CAMPRnmea::Set_travelTime(const double d)
{
  info.travelTime = d;
  return Validate_travelTime();
}

// H. FromString_ functions
unsigned char CAMPRnmea::FromString_modemAddress(std::string sVal)
{
  sVal = removeWhite(sVal);
  int len = sVal.length();
  if (len == 1 || len == 2) {
    unsigned char uc = (unsigned char) (sVal.at(0) - '0');
    if (uc < 10) {
      if (len == 1)
        return uc;
      unsigned char uc1 = (unsigned char) (sVal.at(1) - '0');
      if (uc1 < 10)
        return 10u * uc + uc1; } }
  return BAD_UCHAR;
}

bool CAMPRnmea::FromString_srcAddress(string sVal)
{
  info.src = FromString_modemAddress(sVal);
  return Validate_modemAddress(info.src);
}

bool CAMPRnmea::FromString_destAddress(string sVal)
{
  info.dest = FromString_modemAddress(sVal);
  return Validate_modemAddress(info.dest);
}

bool CAMPRnmea::FromString_travelTime(std::string sVal)
{
  StoreDoubleFromNMEAstring(info.travelTime, sVal);
  return Validate_travelTime();
}

// I. Create an NMEA sentence from data stored in info buffer

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool CAMPRnmea::ProduceNMEASentence(string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $CAMPR and postpend *ch (ch = checksum)
    string dataBody = "";
    dataBody += ToString_srcAddress();
    dataBody += ",";
    dataBody += ToString_destAddress();
    dataBody += ",";
    dataBody += ToString_travelTime();

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    newSentence = curSentence.nmeaSentence;
    return true;
}

// J. Produce strings for NMEA sentences
string CAMPRnmea::ToString_srcAddress()
{
  if (!Validate_modemAddress(info.src))
      return "";
  return intToString((int) info.src);
}

string CAMPRnmea::ToString_destAddress()
{
  if (!Validate_modemAddress(info.dest))
      return "";
  return intToString((int) info.dest);
}

string CAMPRnmea::ToString_travelTime()
{
  if (info.travelTime == BLANK_DOUBLE ||
      info.travelTime == BAD_DOUBLE ||
      info.travelTime < 0.0)
    return "";
  return FormatPosDouble(info.travelTime, 1, 4);
}

















//
