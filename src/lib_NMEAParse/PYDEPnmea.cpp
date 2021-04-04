/*
 * PYDEPnmea.cpp
 *
 *  Created on: Jun 26, 2014
 *      Author: Alon Yaari
 */

#include "PYDEPnmea.h"

using namespace std;

PYDEPnmea::PYDEPnmea()
{
    nmeaLen     = NUMELEM_PYDEP;
    SetKey("PYDEP");

    info.desYawRate        = BAD_DOUBLE;
    info.desThrustPct      = BAD_DOUBLE;
}

//      $PYDEP,<1>,<2>*hh<CR><LF>
//      <1>  [DesYawRate] Desired rate of yaw, in radians/sec.
//      <2>  [DesThrustPct] Desired percent of thrust, -100 to 100.
//           Stopped = 0, positive thrust = forward motion.

bool PYDEPnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
    // Always call base class version of this function to validate basics
    //      and populate inSentence.elements
    if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
        return false;
    for (unsigned short i = 1; i < curSentence.numElements; i++) {
        string sVal = curSentence.elements.at(i);
        switch(i) {
            case 1: FromString_desYawRate(sVal);                        break;
            case 2: FromString_desThrustPct(sVal);                      break;
            default:                                                    break; } }
    return CriticalDataAreValid();
}

bool PYDEPnmea::CriticalDataAreValid()
{
    bool bValid = Validate_desYawRate();
    bValid &= Validate_desThrustPct();
    return bValid;
}

// E. Validation functions
//      - Many common data types have validations that can be called from NMEAUtils

bool PYDEPnmea::Validate_desYawRate()
{
    return IsValidBoundedDouble(info.desYawRate, -10.0, 10.0, INCLUSIVE_YES, MAY_NOT_BE_BLANK);
}

bool PYDEPnmea::Validate_desThrustPct()
{
    return IsValidBoundedDouble(info.desThrustPct, -100.0, 100.0, INCLUSIVE_YES, MAY_NOT_BE_BLANK);
}

// GetInfo()
// Populate curInfo with whatever is currently stored in the info struct
// Returns results of checking contents for validity; false means curInfo has unreliable data
bool PYDEPnmea::GetInfo(infoPYDEP& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool PYDEPnmea::Get_desYawRate(double& d)
{
    d = info.desYawRate;
    return Validate_desYawRate();
}

bool PYDEPnmea::Get_desThrustPct(double& d)
{
    d = info.desThrustPct;
    return Validate_desThrustPct();
}

bool PYDEPnmea::Get_desYawAndThrust(double& dYaw, double& dThrust)
{
    return (Get_desYawRate(dYaw) && Get_desThrustPct(dThrust));
}

bool PYDEPnmea::Set_desYawRate(double d)
{
    info.desYawRate = d;
    return Validate_desYawRate();
}

bool PYDEPnmea::Set_desThrustPct(double d)
{
    info.desThrustPct = d;
    return Validate_desThrustPct();
}

bool PYDEPnmea::Set_desYawAndThrust(double dYaw, double dThrust)
{
    return (Set_desYawRate(dYaw) && Set_desThrustPct(dThrust));
}

bool PYDEPnmea::FromString_desYawRate(string sVal)
{
    StoreDoubleFromNMEAstring(info.desYawRate, sVal);
    return Validate_desYawRate();
}

bool PYDEPnmea::FromString_desThrustPct(string sVal)
{
    StoreDoubleFromNMEAstring(info.desThrustPct, sVal);
    return Validate_desThrustPct();
}

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool PYDEPnmea::ProduceNMEASentence(string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $PYDEP and postpend *ch (ch = checksum)
    string dataBody = "";
    dataBody += ToString_desYawRate();
    dataBody += ",";
    dataBody += ToString_desThrustPct();

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    newSentence = curSentence.nmeaSentence;
    return true;
}

string PYDEPnmea::ToString_desYawRate()
{
    if (info.desYawRate == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.desYawRate, 3, 2);
}

string PYDEPnmea::ToString_desThrustPct()
{
    if (info.desThrustPct == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.desThrustPct, 3, 1);
}

















//


