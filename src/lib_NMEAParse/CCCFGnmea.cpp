/*
 * CCCFGnmea.cpp
 *
 *  Created on: May 22, 2015
 *      Author: Alon Yaari
 *
 *  Based on code by:
 *      Alon Yaari  May, 2014
 */

#include "CCCFGnmea.h"

using namespace std;


// A. Constructor
CCCFGnmea::CCCFGnmea()
{
    nmeaLen     = NUMELEM_CCCFG;
    SetKey("CCCFG");
    info.paramName    = "";
    info.newValue     = BAD_INT;

    validParams["AGC"] = validRange(0, 1);
    validParams["AGN"] = validRange(0, 255);
    validParams["AGX"] = validRange(0, 1);
    validParams["ASD"] = validRange(0, 1);
    validParams["BBD"] = validRange(0, 1);
    validParams["BND"] = validRange(0, 3);
    validParams["BR1"] = validRange(0, 7);  // 0=2400, 1=4800, 2=9600, 3=19200, 4=38400, 5=57600, 6=115200, 7=230400
    validParams["BR2"] = validRange(0, 7);
    validParams["BRN"] = validRange(0, 1);
    validParams["BSP"] = validRange(1, 2);
    validParams["BW0"] = validRange(1000, 5000);  // 1000, 1250, 2000, 2500, 4000, 5000
    validParams["CPR"] = validRange(0, 1);
    validParams["CRL"] = validRange(0, MAX_INT);
    validParams["CST"] = validRange(0, 1);
    validParams["CTO"] = validRange(0, 255);
    validParams["DBG"] = validRange(0, 1);
    validParams["DGM"] = validRange(0, 1);
    validParams["DOP"] = validRange(0, 1);
    validParams["DQF"] = validRange(0, 1);
    validParams["DTH"] = validRange(0, MAX_INT);
    validParams["DTO"] = validRange(1, 30);
    validParams["DTP"] = validRange(0, MAX_INT);
    validParams["ECD"] = validRange(0, MAX_INT);
    validParams["EDR"] = validRange(0, 1);
    validParams["EFF"] = validRange(0, MAX_INT);
    validParams["EFB"] = validRange(0, MAX_INT);
    validParams["FMD"] = validRange(0, MAX_INT);
    validParams["FML"] = validRange(0, MAX_INT);
    validParams["FC0"] = validRange(0, MAX_INT);
    validParams["GPS"] = validRange(0, 1);
    validParams["HFC"] = validRange(0, 1);
    validParams["IRE"] = validRange(0, 1);
    validParams["MCM"] = validRange(0, 1);
    validParams["MFD"] = validRange(0, 1);
    validParams["MOD"] = validRange(0, 1);
    validParams["MPR"] = validRange(0, 1);
    validParams["MSE"] = validRange(0, 1);
    validParams["MVM"] = validRange(0, 1);
    validParams["NDT"] = validRange(0, MAX_INT);
    validParams["NPT"] = validRange(0, MAX_INT);
    validParams["NRL"] = validRange(0, MAX_INT);
    validParams["NRV"] = validRange(0, MAX_INT);
    validParams["PAD"] = validRange(0, MAX_INT);
    validParams["PCM"] = validRange(0, 255);
    validParams["POW"] = validRange(0, MAX_INT);
    validParams["PRL"] = validRange(0, MAX_INT);
    validParams["PTH"] = validRange(0, MAX_INT);
    validParams["PTO"] = validRange(1, 30);
    validParams["REV"] = validRange(0, 1);
    validParams["RSP"] = validRange(-255, 0);
    validParams["RXA"] = validRange(0, 1);
    validParams["RXD"] = validRange(0, 1);
    validParams["RXP"] = validRange(0, 1);
    validParams["SCG"] = validRange(0, 1);
    validParams["SGP"] = validRange(0, 1);
    validParams["SHF"] = validRange(0, 1);
    validParams["SNR"] = validRange(0, 1);
    validParams["SNV"] = validRange(0, 1);
    validParams["SRC"] = validRange(0, 127);
    validParams["TAT"] = validRange(0, 255);
    validParams["TOA"] = validRange(0, 1);
    validParams["TXD"] = validRange(0, MAX_INT);
    validParams["TXP"] = validRange(0, 1);
    validParams["TXF"] = validRange(0, 1);
    validParams["XST"] = validRange(0, 1);
}

/* Sentence Definition
        Set NVRAM configuration parameter, host to modem
        $CCCFG,<1>,<2>*hh<CR><LF>
        <1>  Name of NVRAM parameter to set
        <2>  New value
*/

// C. Parse incoming NMEA sentence

bool CCCFGnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
  if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
    return false;
  for (unsigned short i = 1; i < curSentence.numElements; i++) {
    string sVal = curSentence.elements.at(i);
    switch(i) {
      case 1: FromString_paramName(sVal);                         break;
      case 2: FromString_newValue(sVal);                          break;
      default:                                                    break; } }
  return CriticalDataAreValid();
}

// D. Data validation for info buffer
bool CCCFGnmea::CriticalDataAreValid()
{
  bool bValid = Validate_paramName();
  bValid &= Validate_newValue();
  return bValid;
}

// E. Validation functions

bool CCCFGnmea::Validate_paramName()
{
  if (info.paramName.length() == 3) {
    map<string, validRange>::iterator it = validParams.find(info.paramName);
    return !(it == validParams.end()); }
  return false;
}

bool CCCFGnmea::Validate_newValue()
{
  if (info.paramName == "BW0)")
    return (info.newValue == 1000 ||
            info.newValue == 1250 ||
            info.newValue == 1250 ||
            info.newValue == 2000 ||
            info.newValue == 2500 ||
            info.newValue == 4000 ||
            info.newValue == 5000);
  map<string, validRange>::iterator it = validParams.find(info.paramName);
  return IsValidBoundedInt(info.newValue, it->second.lo, it->second.hi);
}

// F. Getters
bool CCCFGnmea::GetInfo(infoCCCFG& curInfo)
{
  curInfo = info;
  return CriticalDataAreValid();
}

bool CCCFGnmea::Get_paramName(string& str)
{
  str = info.paramName;
  return Validate_paramName();
}

bool CCCFGnmea::Get_newValue(int& i)
{
  i = info.newValue;
  return Validate_newValue();
}

// G. Setters
bool CCCFGnmea::Set_paramName(const string str)
{
  info.paramName = str;
  return Validate_paramName();
}

bool CCCFGnmea::Set_newValue(const int i)
{
  info.newValue = i;
  return Validate_newValue();
}

// H. FromString_ functions
bool CCCFGnmea::FromString_paramName(std::string sVal)
{
  sVal = removeWhite(sVal);
  if (sVal.empty())
    info.paramName = BLANK_STRING;
  else
    info.paramName = sVal;
  return Validate_paramName();
}

bool CCCFGnmea::FromString_newValue(string sVal)
{
  StoreIntFromNMEAstring(info.newValue, sVal);
  return Validate_newValue();
}

// I. Create an NMEA sentence from data stored in info buffer

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool CCCFGnmea::ProduceNMEASentence(string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $CCCFG and postpend *ch (ch = checksum)
    string dataBody = "";
    dataBody += ToString_paramName();
    dataBody += ",";
    dataBody += ToString_newValue();

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    newSentence = curSentence.nmeaSentence;
    return true;
}

// J. Produce strings for NMEA sentences
string CCCFGnmea::ToString_paramName()
{
  if (!Validate_paramName())
      return "";
  return info.paramName;
}

string CCCFGnmea::ToString_newValue()
{
  if (!Validate_newValue())
      return "";
  return intToString((int) info.newValue);
}

















//
