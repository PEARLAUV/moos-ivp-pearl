/*
 * PYDIRnmea.cpp
 *
 *  Created on: Jun 26, 2014
 *      Author: Alon Yaari
 */

#include <iostream>
#include "PYDIRnmea.h"

using namespace std;

PYDIRnmea::PYDIRnmea()
{
    nmeaLen     = NUMELEM_PYDIR;
    SetKey("PYDIR");

    info.desThrustPctL      = BAD_DOUBLE;
    info.desThrustPctR      = BAD_DOUBLE;
}

// Sentence Definition
//      $PYDIR,<1>,<2>*hh<CR><LF>
//      <1>  [DesThrustPctL] Desired ThrustPctL relative to true north, 0 to 359.
//      <2>  [DesThrustPctR] Desired ThrustPctR over ground, 0.0 and positive real numbers.


bool PYDIRnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
    // Always call base class version of this function to validate basics
    //      and populate inSentence.elements
    if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
        return false;
    for (unsigned short i = 1; i < curSentence.numElements; i++) {
        string sVal = curSentence.elements.at(i);
        switch(i) {
            case 1: FromString_desThrustPctL(sVal);                 break;
            case 2: FromString_desThrustPctR(sVal);                 break;
            default:                                                break; } }
    return CriticalDataAreValid();
}

bool PYDIRnmea::CriticalDataAreValid()
{
    bool bValid = Validate_desThrustPctL();
    bValid &= Validate_desThrustPctR();
    return bValid;
}

// E. Validation functions
//      - Many common data types have validations that can be called from NMEAUtils

bool PYDIRnmea::Validate_desThrustPctL()
{
    bool bThrustL = IsValidBoundedDouble(info.desThrustPctL, -100.0, 100.0, INCLUSIVE_YES, MAY_NOT_BE_BLANK);
    if (!bThrustL) {
        errorBuild.clear();
        errorBuild << "Invalid thrustPctL: " << info.desThrustPctL;
        AddError(errorBuild.str()); }
    return bThrustL;
}

bool PYDIRnmea::Validate_desThrustPctR()
{
    bool bThrustR = IsValidBoundedDouble(info.desThrustPctR, -100.0, 100.0, INCLUSIVE_YES, MAY_NOT_BE_BLANK);
    if (!bThrustR) {
        errorBuild << "Invalid thrustPctR: " << info.desThrustPctR;
        AddError(errorBuild.str()); }
    return bThrustR;
}

// GetInfo()
// Populate curInfo with whatever is currently stored in the info struct
// Returns results of checking contents for validity; false means curInfo has unreliable data
bool PYDIRnmea::GetInfo(infoPYDIR& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool PYDIRnmea::Get_desThrustPctL(double& d)
{
    d = info.desThrustPctL;
    return Validate_desThrustPctL();
}

bool PYDIRnmea::Get_desThrustPctR(double& d)
{
    d = info.desThrustPctR;
    return Validate_desThrustPctR();
}

bool PYDIRnmea::Get_desThrustPctLandR(double& dL, double& dR)
{
    return (Get_desThrustPctL(dL) && Get_desThrustPctR(dR));
}

bool PYDIRnmea::Set_desThrustPctL(double d)
{
    info.desThrustPctL = d;
    return Validate_desThrustPctL();
}

bool PYDIRnmea::Set_desThrustPctR(double d)
{
    info.desThrustPctR = d;
    return Validate_desThrustPctR();
}

bool PYDIRnmea::Set_desThrustPctLandR(double dL, double dR)
{
    return (Set_desThrustPctL(dL) && Set_desThrustPctR(dR));
}

bool PYDIRnmea::FromString_desThrustPctL(string sVal)
{
    StoreDoubleFromNMEAstring(info.desThrustPctL, sVal);
    return Validate_desThrustPctL();
}

bool PYDIRnmea::FromString_desThrustPctR(string sVal)
{
    StoreDoubleFromNMEAstring(info.desThrustPctR, sVal);
    return Validate_desThrustPctR();
}

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool PYDIRnmea::ProduceNMEASentence(string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $PYDIR and postpend *ch (ch = checksum)
    string dataBody = "";
    dataBody += ToString_desThrustPctL();
    dataBody += ",";
    dataBody += ToString_desThrustPctR();

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    newSentence = curSentence.nmeaSentence;
    return true;
}

string PYDIRnmea::ToString_desThrustPctL()
{
    if (info.desThrustPctL == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.desThrustPctL, 3, 2);
}

string PYDIRnmea::ToString_desThrustPctR()
{
    if (info.desThrustPctR == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.desThrustPctR, 3, 1);
}

















//


