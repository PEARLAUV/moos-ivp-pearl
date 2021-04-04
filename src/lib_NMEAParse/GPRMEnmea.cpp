/*
 * GPRMEnmea.cpp
 *
 *  Created on: July 19, 2014
 *      Author: Alon Yaari
 */


#include "GPRMEnmea.h"

using namespace std;


// A. Constructor
GPRMEnmea::GPRMEnmea()
{
    nmeaLen     = NUMELEM_GPRME;
    SetKey("GPRME");
    info.estHPE      = BAD_DOUBLE;
    info.estVPE      = BAD_DOUBLE;
    info.estPOSerr   = BAD_DOUBLE;
}

//     GPRME - Report of position error
//     $GPRME,<1>,<2>,<3>*hh<CR><LF>
//     <1>  Estimated horizontal position error, 0.0 to 999.99 meters
//     <2>  Horizontal error units, always M=meters
//     <3>  Estimated vertical position error, 0.0 to 999.99 meters
//     <4>  Vertical error units, always M=meters
//     <5>  Estimated position error, 0.0 to 999.99 meters
//     <6>  Position error units, always M=meters

bool GPRMEnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
    // Always call base class version of this function to validate basics
    //      and populate inSentence.elements
    if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
        return false;

    for (unsigned short i = 1; i < curSentence.numElements; i++) {
        string sVal = curSentence.elements.at(i);
        switch(i) {
            case 1: FromString_estHPE(sVal);                            break;
            case 3: FromString_estVPE(sVal);                            break;
            case 5: FromString_estPOSerr(sVal);                         break;
            default:                                                    break; } }
    return CriticalDataAreValid();
}

bool GPRMEnmea::CriticalDataAreValid()
{
    bool bValid = Validate_posErr(info.estHPE);
    bValid     &= Validate_posErr(info.estVPE);
    bValid     &= Validate_posErr(info.estPOSerr);
    return bValid;
}

bool GPRMEnmea::Validate_posErr(const double d)
{
    return IsValidBoundedDouble(d, 0.0, 1000.0, INCLUSIVE_YES, MAY_BE_BLANK);
}

bool GPRMEnmea::GetInfo(infoGPRME& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool GPRMEnmea::Get_estHPE(double& d)
{
    d = info.estHPE;
    return Validate_posErr(d);
}

bool GPRMEnmea::Get_estVPE(double& d)
{
    d = info.estVPE;
    return Validate_posErr(d);
}

bool GPRMEnmea::Get_estPOSerr(double& d)
{
    d = info.estPOSerr;
    return Validate_posErr(d);
}

bool GPRMEnmea::Set_estHPE(const double d)
{
    info.estHPE = d;
    return Validate_posErr(d);
}

bool GPRMEnmea::Set_estVPE(const double d)
{
    info.estVPE = d;
    return Validate_posErr(d);
}

bool GPRMEnmea::Set_estPOSerr(const double d)
{
    info.estPOSerr = d;
    return Validate_posErr(d);
}

bool GPRMEnmea::FromString_estHPE(string sVal)
{
	StoreDoubleFromNMEAstring(info.estHPE, sVal);
    return Validate_posErr(info.estHPE);
}

bool GPRMEnmea::FromString_estVPE(string sVal)
{
	StoreDoubleFromNMEAstring(info.estVPE, sVal);
    return Validate_posErr(info.estVPE);
}

bool GPRMEnmea::FromString_estPOSerr(string sVal)
{
	StoreDoubleFromNMEAstring(info.estPOSerr, sVal);
    return Validate_posErr(info.estPOSerr);
}

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool GPRMEnmea::ProduceNMEASentence(string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $GPRME and postpend *ch (ch = checksum)
    string dataBody = "";
    dataBody += ToString_PosErr(info.estHPE);
    dataBody += ",M";
    dataBody += ToString_PosErr(info.estVPE);
    dataBody += ",M";
    dataBody += ToString_PosErr(info.estPOSerr);
    dataBody += ",M";

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    newSentence = curSentence.nmeaSentence;
    return true;
}

string GPRMEnmea::ToString_PosErr(const double d)
{
	if (d == BLANK_DOUBLE || d == BAD_DOUBLE)
		return "";
	return doubleToString(d, 2);
}

















//
