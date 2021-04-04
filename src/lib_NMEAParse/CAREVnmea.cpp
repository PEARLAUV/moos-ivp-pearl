/*
 * CAREVnmea.cpp
 *
 *  Created on: May 22, 2015
 *      Author: Alon Yaari
 *
 *  Based on code by:
 *      Alon Yaari  May, 2014
 */

#include "WHOIuModemUtils.h"
#include "CAREVnmea.h"

using namespace std;


// A. Constructor
CAREVnmea::CAREVnmea()
{
    nmeaLen           = NUMELEM_CAREV;
    SetKey("CAREV");
    info.timeOn.Set_Invalid();
    info.ident        = BLANK_STRING;
    info.revision     = BLANK_STRING;
}

/* Sentence Definition
        A Ping has been received, modem to host
        $CAREV,<1>,<2>,<3>*hh<CR><LF>
        <1>  SRC Source (data originator), Valid: 0 to 127
        <2>  DEST Destination (data receiver), Valid: 0 to 127
*/

// C. Parse incoming NMEA sentence

bool CAREVnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
  if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
    return false;
  for (unsigned short i = 1; i < curSentence.numElements; i++) {
    string sVal = curSentence.elements.at(i);
    switch(i) {
      case 1: FromString_timeOn(sVal);                            break;
      case 2: FromString_ident(sVal);                             break;
      case 3: FromString_revision(sVal);                          break;
      default:                                                    break; } }
  return CriticalDataAreValid();
}

// D. Data validation for info buffer
bool CAREVnmea::CriticalDataAreValid()
{
  bool bValid = Validate_timeOn();
  bValid     &= Validate_ident();
  bValid     &= Validate_revision();
  return bValid;
}

// E. Validation functions

bool CAREVnmea::Validate_timeOn()
{
  if (!info.timeOn.IsValid()) {
      AddError("Invalid time stamp.");
      return false; }
  return true;
}

bool CAREVnmea::Validate_ident()
{
  return !info.ident.empty();
}

bool CAREVnmea::Validate_revision()
{
  return !info.revision.empty();
}

// F. Getters
bool CAREVnmea::GetInfo(infoCAREV& curInfo)
{
  curInfo = info;
  return CriticalDataAreValid();
}

bool CAREVnmea::Get_timeOn(utcTime& t)
{
    t = info.timeOn;
    return Validate_timeOn();
}

bool CAREVnmea::Get_ident(string& ident)
{
  ident = info.ident;
  return Validate_ident();
}

bool CAREVnmea::Get_revision(string& ident)
{
  ident = info.revision;
  return Validate_revision();
}

// G. Setters

bool CAREVnmea::Set_timeOn(const utcTime& t)
{
    info.timeOn = t;
    return Validate_timeOn();
}

bool CAREVnmea::Set_ident(string str)
{
  info.ident = str;
  return Validate_ident();
}

bool CAREVnmea::Set_revision(string str)
{
  info.revision = str;
  return Validate_revision();
}

// H. FromString_ functions

bool CAREVnmea::FromString_timeOn(std::string sVal)
{
    info.timeOn.Set_utcTimeFromNMEA(sVal);     // Set_utcTimeFromNMEA() also handles blank strings
    return Validate_timeOn();
}

bool CAREVnmea::FromString_ident(string sVal)
{
  info.ident = sVal;
  return Validate_ident();
}

bool CAREVnmea::FromString_revision(string sVal)
{
  info.revision = sVal;
  return Validate_ident();
}

// I. Create an NMEA sentence from data stored in info buffer

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool CAREVnmea::ProduceNMEASentence(string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $CAREV and postpend *ch (ch = checksum)
    string dataBody = "";
    dataBody += ToString_timeOn();
    dataBody += ",";
    dataBody += ToString_ident();
    dataBody += ",";
    dataBody += ToString_revision();

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    newSentence = curSentence.nmeaSentence;
    return true;
}

// J. Produce strings for NMEA sentences

string CAREVnmea::ToString_timeOn()
{
    string sVal;
    if (!info.timeOn.Get_utcTimeString(sVal, 0))
        sVal = "";
    return sVal;
}

string CAREVnmea::ToString_ident()
{
  if (!Validate_ident())
      return "";
  return info.ident;
}

string CAREVnmea::ToString_revision()
{
  if (!Validate_revision())
      return "";
  return info.revision;
}


















//
