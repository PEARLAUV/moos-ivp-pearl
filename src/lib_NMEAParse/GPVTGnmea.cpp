/*
 * GPVTGnmea.cpp
 *
 *  Created on: Dec 31, 2014
 *      Author: Alon Yaari
 */

#include "GPVTGnmea.h"

using namespace std;


// A. Constructor
GPVTGnmea::GPVTGnmea()
{
    nmeaLen     = NUMELEM_GPVTG;
    SetKey("GPVTG");
    info.headingTrueN   = BAD_DOUBLE;
    info.headingMagN    = BAD_DOUBLE;
    info.speedKTS       = BAD_DOUBLE;
    info.speedKPH       = BAD_DOUBLE;
}

//      $GPVTG,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>,<9>*hh<CR><LF>
//      <1>  Track made good, degrees clockwise from true N
//      <2>  Always 'T' to indicate <1> is from true N
//      <3>  (optional) Track made good, degrees clockwise from magnetic N
//      <4>  Always 'M' to indicate <3> is from magnetic N
//      <5>  Speed over ground in knots
//      <6>  Always 'N' to indicate '5' is in knots
//      <7>  Speed over ground in kilometers per hour
//      <8>  Always 'K' to indicate <7> is in kph
//      <9>  (optional) Unknown, appears to contain 'A' in clearpath wire protocol
//
//      Example: $GPVTG,339.83,T,,M,0.011,N,0.020,K,A*3D

bool GPVTGnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
    // Always call base class version of this function to validate basics
    //      and populate inSentence.elements
    if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
        return false;

    for (unsigned short i = 1; i < curSentence.numElements; i++) {
        string sVal = curSentence.elements.at(i);
        switch(i) {
            case 1: FromString_headingTrueN(sVal);                      break;
            case 3: FromString_headingMagN(sVal);                       break;
            case 5: FromString_speedKTS(sVal);                          break;
            case 7: FromString_speedKPH(sVal);                          break;
            default:                                                    break; } }
    return CriticalDataAreValid();
}

bool GPVTGnmea::CriticalDataAreValid()
{
    bool bValid = Validate_headingTrueN(info.headingTrueN);
    bValid     &= Validate_speedKTS(info.speedKTS);
    return bValid;
}

bool GPVTGnmea::Validate_heading(const double d)
{
  return IsValidBoundedDouble(d, 0.0, 360.0, INCLUSIVE_YES, MAY_BE_BLANK);
}

bool GPVTGnmea::Validate_headingTrueN(const double d)
{
  bool bGood = Validate_heading(d);
  if (!bGood) {
          errorBuild << "Invalid headingTrueN:";
          errorBuild << info.headingTrueN;
          AddError(errorBuild.str()); }
  return bGood;
}

bool GPVTGnmea::Validate_headingMagN(const double d)
{
  bool bGood = Validate_heading(d);
  if (!bGood) {
          errorBuild << "Invalid headingMagN:";
          errorBuild << info.headingMagN;
          AddError(errorBuild.str()); }
  return bGood;
}

bool GPVTGnmea::Validate_speed(const double d)
{
  return IsValidBoundedDouble(d, 0.0, 1000.0, INCLUSIVE_YES, MAY_BE_BLANK);
}

bool GPVTGnmea::Validate_speedKTS(const double d)
{
  bool bGood = Validate_speed(d);
  if (!bGood) {
      errorBuild << "Invalid speedKTS:";
      errorBuild << info.speedKTS;
      AddError(errorBuild.str()); }
  return bGood;
}

bool GPVTGnmea::Validate_speedKPH(const double d)
{
  bool bGood = Validate_speed(d);
  if (!bGood) {
      errorBuild << "Invalid speedKPH:";
      errorBuild << info.speedKPH;
      AddError(errorBuild.str()); }
  return bGood;
}

bool GPVTGnmea::GetInfo(infoGPVTG& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool GPVTGnmea::Get_headingTrueN(double& dVal)
{
  dVal = info.headingTrueN;
  return Validate_headingTrueN(dVal);
}

bool GPVTGnmea::Get_headingMagN(double& dVal)
{
  dVal = info.headingMagN;
  return Validate_headingMagN(dVal);
}

bool GPVTGnmea::Get_speedKTS(double& dVal)
{
  dVal = info.speedKTS;
  return Validate_speedKTS(dVal);
}

bool GPVTGnmea::Get_speedMPS(double& dVal)
{
  dVal = info.speedKTS * KNOTS2METERSperSEC;
  return Validate_speedKTS(dVal);
}

bool GPVTGnmea::Get_speedKPH(double& dVal)
{
  dVal = info.speedKPH;
  return Validate_speedKPH(dVal);
}

bool GPVTGnmea::Set_headingTrueN(const double& d)
{
    info.headingTrueN = d;
    return Validate_headingTrueN(d);
}

bool GPVTGnmea::Set_headingMagN(const double& d)
{
    info.headingMagN = d;
    return Validate_headingMagN(d);
}

bool GPVTGnmea::Set_speedKTS(const double& d)
{
    info.speedKTS = d;
    return Validate_speedKTS(d);
}

bool GPVTGnmea::Set_speedKPH(const double& d)
{
    info.speedKPH = d;
    return Validate_speedKPH(d);
}

bool GPVTGnmea::FromString_headingTrueN(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.headingTrueN, sVal);
    return Validate_headingTrueN(info.headingTrueN);
}

bool GPVTGnmea::FromString_headingMagN(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.headingMagN, sVal);
    return Validate_headingMagN(info.headingMagN);
}

bool GPVTGnmea::FromString_speedKTS(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.speedKTS, sVal);
    return Validate_speedKTS(info.speedKTS);
}

bool GPVTGnmea::FromString_speedKPH(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.speedKPH, sVal);
    return Validate_speedKPH(info.speedKPH);
}

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool GPVTGnmea::ProduceNMEASentence(std::string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $GPVTG and postpend *ch (ch = checksum)
    string dataBody = "";
    dataBody += ToString_headingTrueN();
    dataBody +=",";
    dataBody += "T";
    dataBody +=",";
    dataBody += ToString_headingMagN();
    dataBody +=",";
    dataBody += "M";
    dataBody +=",";
    dataBody += ToString_speedKTS();
    dataBody +=",";
    dataBody += "N";
    dataBody +=",";
    dataBody += ToString_speedKPH();
    dataBody +=",";
    dataBody += "K";
    dataBody +=",";
    dataBody += "A";

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(newSentence, dataBody);
    return true;
}

string GPVTGnmea::ToString_headingTrueN()
{
    if (info.headingTrueN == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.headingTrueN, 3, 1);
}

string GPVTGnmea::ToString_headingMagN()
{
    if (info.headingMagN == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.headingMagN, 3, 1);
}

string GPVTGnmea::ToString_speedKTS()
{
    if (info.speedKTS == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.speedKTS, 3, 2);
}

string GPVTGnmea::ToString_speedKPH()
{
    if (info.speedKPH == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.speedKPH, 3, 2);
}



























//
