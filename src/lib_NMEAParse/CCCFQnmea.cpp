/*
 * CCCFQnmea.cpp
 *
 *  Created on: May 22, 2015
 *      Author: Alon Yaari
 *
 *  Based on code by:
 *      Alon Yaari  May, 2014
 */

#include "CCCFQnmea.h"

using namespace std;


// A. Constructor
CCCFQnmea::CCCFQnmea()
{
    nmeaLen     = NUMELEM_CCCFQ;
    SetKey("CCCFQ");
    info.src          = BAD_UCHAR;
    info.dest         = BAD_UCHAR;
}

/* Sentence Definition
        A Ping has been received, modem to host
        $CCCFQ,<1>,<2>,<3>*hh<CR><LF>
        <1>  SRC Source (data originator), Valid: 0 to 127
        <2>  DEST Destination (data receiver), Valid: 0 to 127
        <3>  ASCII-coded hex data (2 hex values). Values in the range 0 to 1FFF are legal
*/

// C. Parse incoming NMEA sentence

bool CCCFQnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
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
bool CCCFQnmea::CriticalDataAreValid()
{
  bool bValid = Validate_modemAddress(info.src);
  bValid &= Validate_modemAddress(info.dest);
  return bValid;
}

// E. Validation functions

bool CCCFQnmea::Validate_modemAddress(unsigned char uc)
{
  return IsValidBoundedUChar(uc, (unsigned char) 0, (unsigned char) 127, true, false);
}

// F. Getters
bool CCCFQnmea::GetInfo(infoCCCFQ& curInfo)
{
  curInfo = info;
  return CriticalDataAreValid();
}

bool CCCFQnmea::Get_srcAddress(unsigned char& uc)
{
  uc = info.src;
  return Validate_modemAddress(uc);
}

bool CCCFQnmea::Get_destAddress(unsigned char& uc)
{
  uc = info.dest;
  return Validate_modemAddress(uc);
}

// G. Setters
bool CCCFQnmea::Set_srcAddress(const unsigned char uc)
{
  info.src = uc;
  return Validate_modemAddress(uc);
}

bool CCCFQnmea::Set_destAddress(const unsigned char uc)
{
  info.dest = uc;
  return Validate_modemAddress(uc);
}

// H. FromString_ functions
unsigned char CCCFQnmea::FromString_modemAddress(std::string sVal)
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

bool CCCFQnmea::FromString_srcAddress(string sVal)
{
  info.src = FromString_modemAddress(sVal);
  return Validate_modemAddress(info.src);
}

bool CCCFQnmea::FromString_destAddress(string sVal)
{
  info.dest = FromString_modemAddress(sVal);
  return Validate_modemAddress(info.dest);
}

// I. Create an NMEA sentence from data stored in info buffer

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool CCCFQnmea::ProduceNMEASentence(string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $CCCFQ and postpend *ch (ch = checksum)
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
string CCCFQnmea::ToString_srcAddress()
{
  if (!Validate_modemAddress(info.src))
      return "";
  return intToString((int) info.src);
}

string CCCFQnmea::ToString_destAddress()
{
  if (!Validate_modemAddress(info.dest))
      return "";
  return intToString((int) info.dest);
}


















//
