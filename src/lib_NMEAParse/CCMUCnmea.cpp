/*
 * CCMUCnmea.cpp
 *
 *  Created on: May 22, 2015
 *      Author: Alon Yaari
 *
 *  Based on code by:
 *      Alon Yaari  May, 2014
 */

#include "WHOIuModemUtils.h"
#include "CCMUCnmea.h"

using namespace std;


// A. Constructor
CCMUCnmea::CCMUCnmea()
{
    nmeaLen     = NUMELEM_CCMUC;
    SetKey("CCMUC");
    info.src          = BAD_UCHAR;
    info.dest         = BAD_UCHAR;
    info.msg          = BAD_USHORT;
}

/* Sentence Definition
        User Mini-Packet command, host to modem
        $CCMUC,<1>,<2>,<3>*hh<CR><LF>
        <1>  SRC Source (data originator), Valid: 0 to 127
        <2>  DEST Destination (data receiver), Valid: 0 to 127
        <3>  ASCII-coded hex data (2 hex values). Values in the range 0 to 1FFF are legal
*/

// C. Parse incoming NMEA sentence

bool CCMUCnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
  if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
    return false;
  for (unsigned short i = 1; i < curSentence.numElements; i++) {
    string sVal = curSentence.elements.at(i);
    switch(i) {
      case 1: FromString_srcAddress(sVal);                        break;
      case 2: FromString_destAddress(sVal);                       break;
      case 3: FromString_msgBytes(sVal);                          break;
      default:                                                    break; } }
  return CriticalDataAreValid();
}

// D. Data validation for info buffer
bool CCMUCnmea::CriticalDataAreValid()
{
  bool bValid = Validate_modemAddress(info.src);
  bValid &= Validate_modemAddress(info.dest);
  bValid &= Validate_msg();
  return bValid;
}

// E. Validation functions

bool CCMUCnmea::Validate_modemAddress(unsigned char uc)
{
  return IsValidBoundedUChar(uc, (unsigned char) 0, (unsigned char) 127, true, false);
}

bool CCMUCnmea::Validate_msg()
{
  // Message can contain a maximum of 0x1FFF which as integer is 8191
  return info.msg <= (unsigned short) 0x1FFF;
}

// F. Getters
bool CCMUCnmea::GetInfo(infoCCMUC& curInfo)
{
  curInfo = info;
  return CriticalDataAreValid();
}

bool CCMUCnmea::Get_srcAddress(unsigned char& uc)
{
  uc = info.src;
  return Validate_modemAddress(uc);
}

bool CCMUCnmea::Get_destAddress(unsigned char& uc)
{
  uc = info.dest;
  return Validate_modemAddress(uc);
}

bool CCMUCnmea::Get_msgData(unsigned short& msg)
{
  msg = info.msg;
  return Validate_msg();
}

// G. Setters
bool CCMUCnmea::Set_srcAddress(const unsigned char uc)
{
  info.src = uc;
  return Validate_modemAddress(uc);
}

bool CCMUCnmea::Set_destAddress(const unsigned char uc)
{
  info.dest = uc;
  return Validate_modemAddress(uc);
}

bool CCMUCnmea::Set_msgData(const unsigned short msg)
{
  info.msg = msg;
  return Validate_msg();
}

// H. FromString_ functions
unsigned char CCMUCnmea::FromString_modemAddress(std::string sVal)
{
  return uModemAddressFromString(sVal);
}

bool CCMUCnmea::FromString_srcAddress(string sVal)
{
  info.src = FromString_modemAddress(sVal);
  return Validate_modemAddress(info.src);
}

bool CCMUCnmea::FromString_destAddress(string sVal)
{
  info.dest = FromString_modemAddress(sVal);
  return Validate_modemAddress(info.dest);
}

bool CCMUCnmea::FromString_msgBytes(std::string sVal)
{
  unsigned int ui;
  bool bGood = HexInAStringToUInt(sVal, ui);
  if (bGood)
    info.msg = ui;
  else
    info.msg = BAD_USHORT;
  return Validate_msg();
}

// I. Create an NMEA sentence from data stored in info buffer

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool CCMUCnmea::ProduceNMEASentence(string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $CCMUC and postpend *ch (ch = checksum)
    string dataBody = "";
    dataBody += ToString_srcAddress();
    dataBody += ",";
    dataBody += ToString_destAddress();
    dataBody += ",";
    dataBody += ToString_msg();

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    newSentence = curSentence.nmeaSentence;
    return true;
}

// J. Produce strings for NMEA sentences
string CCMUCnmea::ToString_srcAddress()
{
  if (!Validate_modemAddress(info.src))
      return "";
  return intToString((int) info.src);
}

string CCMUCnmea::ToString_destAddress()
{
  if (!Validate_modemAddress(info.dest))
      return "";
  return intToString((int) info.dest);
}

string CCMUCnmea::ToString_msg()
{
  string sVal = "";
  if (Validate_msg())
    UIntToHexInAString(info.msg, sVal, 4);
  return sVal;
}

















//
