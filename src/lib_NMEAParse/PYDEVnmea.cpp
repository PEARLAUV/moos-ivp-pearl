/*
 * PYDEVnmea.cpp
 *
 *  Created on: Jun 26, 2014
 *      Author: Alon Yaari
 */

#include "PYDEVnmea.h"

using namespace std;

PYDEVnmea::PYDEVnmea()
{
    nmeaLen     = NUMELEM_PYDEV;
    SetKey("PYDEV");

    info.desHeading    = BAD_DOUBLE;
    info.desSpeed      = BAD_DOUBLE;
}

// Sentence Definition
//      $PYDEV,<1>,<2>*hh<CR><LF>
//      <1>  [DesHeading] Desired heading relative to true north, 0 to 359.
//      <2>  [DesSpeed] Desired speed over ground, 0.0 and positive real numbers.


bool PYDEVnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
    // Always call base class version of this function to validate basics
    //      and populate inSentence.elements
    if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
        return false;
    for (unsigned short i = 1; i < curSentence.numElements; i++) {
        string sVal = curSentence.elements.at(i);
        switch(i) {
            case 1: FromString_desHeading(sVal);                    break;
            case 2: FromString_desSpeed(sVal);                      break;
            default:                                                break; } }
    return CriticalDataAreValid();
}

bool PYDEVnmea::CriticalDataAreValid()
{
    bool bValid = Validate_desHeading();
    bValid &= Validate_desSpeed();
    return bValid;
}

// E. Validation functions
//      - Many common data types have validations that can be called from NMEAUtils

bool PYDEVnmea::Validate_desHeading()
{
    return IsValidBoundedDouble(info.desHeading, 0.0, 360.0, INCLUSIVE_YES, MAY_NOT_BE_BLANK);
}

bool PYDEVnmea::Validate_desSpeed()
{
    return IsValidBoundedDouble(info.desSpeed, 0.0, 100.0, INCLUSIVE_YES, MAY_NOT_BE_BLANK);
}

// GetInfo()
// Populate curInfo with whatever is currently stored in the info struct
// Returns results of checking contents for validity; false means curInfo has unreliable data
bool PYDEVnmea::GetInfo(infoPYDEV& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool PYDEVnmea::Get_desHeading(double& d)
{
    d = info.desHeading;
    return Validate_desHeading();
}

bool PYDEVnmea::Get_desSpeed(double& d)
{
    d = info.desSpeed;
    return Validate_desSpeed();
}

bool PYDEVnmea::Get_desHeadingAndSpeed(double& dHeading, double& dSpeed)
{
    return (Get_desHeading(dHeading) && Get_desSpeed(dSpeed));
}

bool PYDEVnmea::Set_desHeading(double d)
{
    info.desHeading = d;
    return Validate_desHeading();
}

bool PYDEVnmea::Set_desSpeed(double d)
{
    info.desSpeed = d;
    return Validate_desSpeed();
}

bool PYDEVnmea::Set_desHeadingAndSpeed(double dHeading, double dSpeed)
{
    return (Set_desHeading(dHeading) && Set_desSpeed(dSpeed));
}

bool PYDEVnmea::FromString_desHeading(string sVal)
{
    StoreDoubleFromNMEAstring(info.desHeading, sVal);
    return Validate_desHeading();
}

bool PYDEVnmea::FromString_desSpeed(string sVal)
{
    StoreDoubleFromNMEAstring(info.desSpeed, sVal);
    return Validate_desSpeed();
}

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool PYDEVnmea::ProduceNMEASentence(string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $PYDEV and postpend *ch (ch = checksum)
    string dataBody = "";
    dataBody += ToString_desHeading();
    dataBody += ",";
    dataBody += ToString_desSpeed();

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    newSentence = curSentence.nmeaSentence;
    return true;
}

string PYDEVnmea::ToString_desHeading()
{
    if (info.desHeading == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.desHeading, 3, 2);
}

string PYDEVnmea::ToString_desSpeed()
{
    if (info.desSpeed == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.desSpeed, 3, 1);
}

















//


