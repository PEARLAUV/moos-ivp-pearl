/*
 * CAMPAnmea.cpp
 *
 *  Created on: May 22, 2015
 *      Author: Alon Yaari
 *
 *  Based on code by:
 *      Alon Yaari  May, 2014
 */

#include "WHOIuModemUtils.h"
#include "CAMPAnmea.h"

using namespace std;


// A. Constructor
CAMPAnmea::CAMPAnmea()
{
    nmeaLen     = NUMELEM_CAMPA;
    SetKey("CAMPA");
    info.src          = BAD_UCHAR;
    info.dest         = BAD_UCHAR;
}

/* Sentence Definition
        A Ping has been received, modem to host
        $CAMPA,<1>,<2>,<3>*hh<CR><LF>
        <1>  SRC Source (data originator), Valid: 0 to 127
        <2>  DEST Destination (data receiver), Valid: 0 to 127
*/

// C. Parse incoming NMEA sentence

bool CAMPAnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
  if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
    return false;
  for (unsigned short i = 1; i < curSentence.numElements; i++) {
    string sVal = curSentence.elements.at(i);
    switch(i) {
      case 1: FromString_srcAddress(sVal);                        break;
      case 2: FromString_destAddress(sVal);                       break;
      default:                                                    break; } }
  return CriticalDataAreValid();
}

// D. Data validation for info buffer
bool CAMPAnmea::CriticalDataAreValid()
{
  bool bValid = Validate_modemAddress(info.src);
  bValid &= Validate_modemAddress(info.dest);
  return bValid;
}

// E. Validation functions

bool CAMPAnmea::Validate_modemAddress(unsigned char uc)
{
  return IsValidBoundedUChar(uc, (unsigned char) 0, (unsigned char) 127, true, false);
}

// F. Getters
bool CAMPAnmea::GetInfo(infoCAMPA& curInfo)
{
  curInfo = info;
  return CriticalDataAreValid();
}

bool CAMPAnmea::Get_srcAddress(unsigned char& uc)
{
  uc = info.src;
  return Validate_modemAddress(uc);
}

bool CAMPAnmea::Get_destAddress(unsigned char& uc)
{
  uc = info.dest;
  return Validate_modemAddress(uc);
}

// G. Setters
bool CAMPAnmea::Set_srcAddress(const unsigned char uc)
{
  info.src = uc;
  return Validate_modemAddress(uc);
}

bool CAMPAnmea::Set_destAddress(const unsigned char uc)
{
  info.dest = uc;
  return Validate_modemAddress(uc);
}

// H. FromString_ functions
unsigned char CAMPAnmea::FromString_modemAddress(std::string sVal)
{
  return uModemAddressFromString(sVal);
}

bool CAMPAnmea::FromString_srcAddress(string sVal)
{
  info.src = FromString_modemAddress(sVal);
  return Validate_modemAddress(info.src);
}

bool CAMPAnmea::FromString_destAddress(string sVal)
{
  info.dest = FromString_modemAddress(sVal);
  return Validate_modemAddress(info.dest);
}

// I. Create an NMEA sentence from data stored in info buffer

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool CAMPAnmea::ProduceNMEASentence(string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $CAMPA and postpend *ch (ch = checksum)
    string dataBody = "";
    dataBody += ToString_srcAddress();
    dataBody += ",";
    dataBody += ToString_destAddress();

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    newSentence = curSentence.nmeaSentence;
    return true;
}

// J. Produce strings for NMEA sentences
string CAMPAnmea::ToString_srcAddress()
{
  if (!Validate_modemAddress(info.src))
      return "";
  return intToString((int) info.src);
}

string CAMPAnmea::ToString_destAddress()
{
  if (!Validate_modemAddress(info.dest))
      return "";
  return intToString((int) info.dest);
}


















//
