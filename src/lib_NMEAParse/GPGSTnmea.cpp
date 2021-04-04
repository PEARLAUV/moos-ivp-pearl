/*
 * GPGSTnmea.cpp
 *
 *  Created on: July 19, 2014
 *      Author: Alon Yaari
 *
 *  Based on code by:
 *      Alon Yaari  May, 2014
 */


#include "GPGSTnmea.h"

using namespace std;


// A. Constructor
GPGSTnmea::GPGSTnmea()
{
    nmeaLen     = NUMELEM_GPGST;
    SetKey("GPGST");
    info.timeUTC.Set_Invalid();
    info.rmsStdDev   = BAD_DOUBLE;
    info.majorStdDev = BAD_DOUBLE;
    info.minorStdDev = BAD_DOUBLE;
    info.orienStdDev = BAD_DOUBLE;
    info.latStdDev   = BAD_DOUBLE;
    info.lonStdDev   = BAD_DOUBLE;
    info.altStdDev   = BAD_DOUBLE;
}

//     GPGST - Report of position error
//             Confirmed: Ublox GPS
//     $GPGST,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>*hh<CR><LF>
//     <1>  UTC time, format hhmmss.s
//     <2>  RMS value in meters of the std deviation of the ranges
//     <3>  StdDev of semimajor axis, not used
//     <4>  StdDev of semiminor axis, not used
//     <5>  Orientation of semimajor axis, not used
//     <6>  StdDev of latitude error in meters
//     <7>  StdDev of longitude error in meters
//     <8>  StdDev of altitude error in meters

bool GPGSTnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
    // Always call base class version of this function to validate basics
    //      and populate inSentence.elements
    if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
        return false;

    for (unsigned short i = 1; i < curSentence.numElements; i++) {
        string sVal = curSentence.elements.at(i);
        switch(i) {
            case 1: FromString_utcTime(sVal);                           break;
            case 2: FromString_rmsStdDev(sVal);                         break;
            case 3: FromString_majorStdDev(sVal);                       break;
            case 4: FromString_minorStdDev(sVal);                       break;
            case 5: FromString_orienStdDev(sVal);                       break;
            case 6: FromString_rmsStdDev(sVal);                         break;
            case 7: FromString_rmsStdDev(sVal);                         break;
            case 8: FromString_rmsStdDev(sVal);                         break;
            default:                                                    break; } }
    return CriticalDataAreValid();
}

bool GPGSTnmea::CriticalDataAreValid()
{
    bool bValid = Validate_utcTime();
    bValid &= Validate_stdDev(info.rmsStdDev);
    bValid &= Validate_stdDev(info.majorStdDev);
    bValid &= Validate_stdDev(info.minorStdDev);
    bValid &= Validate_stdDev(info.orienStdDev);
    bValid &= Validate_stdDev(info.latStdDev);
    bValid &= Validate_stdDev(info.lonStdDev);
    bValid &= Validate_stdDev(info.altStdDev);
    return bValid;
}

bool GPGSTnmea::Validate_utcTime()
{
    return info.timeUTC.IsValid();
}

bool GPGSTnmea::Validate_stdDev(const double d)
{
    return IsValidBoundedDouble(d, -1000.0, 1000.0, INCLUSIVE_YES, MAY_BE_BLANK);
}

bool GPGSTnmea::GetInfo(infoGPGST& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool GPGSTnmea::Get_timeUTC(utcTime& t)
{
    t = info.timeUTC;
    return Validate_utcTime();
}

bool GPGSTnmea::Get_rmsStdDev(double& d)
{
    d = info.rmsStdDev;
    return Validate_stdDev(d);
}

bool GPGSTnmea::Get_majorStdDev(double& d)
{
    d = info.majorStdDev;
    return Validate_stdDev(d);
}

bool GPGSTnmea::Get_minorStdDev(double& d)
{
    d = info.minorStdDev;
    return Validate_stdDev(d);
}

bool GPGSTnmea::Get_orienStdDev(double& d)
{
    d = info.orienStdDev;
    return Validate_stdDev(d);
}

bool GPGSTnmea::Get_latStdDev(double& d)
{
    d = info.latStdDev;
    return Validate_stdDev(d);
}

bool GPGSTnmea::Get_lonStdDev(double& d)
{
    d = info.lonStdDev;
    return Validate_stdDev(d);
}

bool GPGSTnmea::Get_altStdDev(double& d)
{
    d = info.altStdDev;
    return Validate_stdDev(d);
}

bool GPGSTnmea::Set_utcTime(const utcTime t)
{
    info.timeUTC = t;
    return Validate_utcTime();
}

bool GPGSTnmea::Set_utcTime(const unsigned short int hour, const unsigned short int minute, const float second)
{
    return info.timeUTC.Set_utcTime(hour, minute, second);
}

bool GPGSTnmea::Set_rmsStdDev(const double d)
{
    info.rmsStdDev = d;
    return Validate_stdDev(d);
}

bool GPGSTnmea::Set_majorStdDev(const double d)
{
    info.majorStdDev = d;
    return Validate_stdDev(d);
}

bool GPGSTnmea::Set_minorStdDev(const double d)
{
    info.minorStdDev = d;
    return Validate_stdDev(d);
}

bool GPGSTnmea::Set_orienStdDev(const double d)
{
    info.orienStdDev = d;
    return Validate_stdDev(d);
}

bool GPGSTnmea::Set_latStdDev(const double d)
{
    info.latStdDev = d;
    return Validate_stdDev(d);
}

bool GPGSTnmea::Set_lonStdDev(const double d)
{
    info.lonStdDev = d;
    return Validate_stdDev(d);
}

bool GPGSTnmea::Set_altStdDev(const double d)
{
    info.altStdDev = d;
    return Validate_stdDev(d);
}

bool GPGSTnmea::FromString_utcTime(string sVal)
{
    info.timeUTC.Set_utcTimeFromNMEA(sVal);     // Set_utcTimeFromNMEA() also handles blank strings
    return Validate_utcTime();
}

bool GPGSTnmea::FromString_rmsStdDev(string sVal)
{
	StoreDoubleFromNMEAstring(info.rmsStdDev, sVal);
    return Validate_stdDev(info.rmsStdDev);
}

bool GPGSTnmea::FromString_majorStdDev(string sVal)
{
	StoreDoubleFromNMEAstring(info.majorStdDev, sVal);
    return Validate_stdDev(info.majorStdDev);
}

bool GPGSTnmea::FromString_minorStdDev(string sVal)
{
	StoreDoubleFromNMEAstring(info.minorStdDev, sVal);
    return Validate_stdDev(info.minorStdDev);
}

bool GPGSTnmea::FromString_orienStdDev(string sVal)
{
	StoreDoubleFromNMEAstring(info.orienStdDev, sVal);
    return Validate_stdDev(info.orienStdDev);
}

bool GPGSTnmea::FromString_latStdDev(string sVal)
{
	StoreDoubleFromNMEAstring(info.latStdDev, sVal);
    return Validate_stdDev(info.latStdDev);
}

bool GPGSTnmea::FromString_lonStdDev(string sVal)
{
	StoreDoubleFromNMEAstring(info.lonStdDev, sVal);
    return Validate_stdDev(info.lonStdDev);
}

bool GPGSTnmea::FromString_altStdDev(string sVal)
{
	StoreDoubleFromNMEAstring(info.altStdDev, sVal);
    return Validate_stdDev(info.altStdDev);
}
// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool GPGSTnmea::ProduceNMEASentence(string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $GPGST and postpend *ch (ch = checksum)
    string dataBody = "";
    dataBody += ToString_utcTime();
    dataBody += ",";
    dataBody += ToString_StdDev(info.rmsStdDev);
    dataBody += ",";
    dataBody += ToString_StdDev(info.majorStdDev);
    dataBody += ",";
    dataBody += ToString_StdDev(info.minorStdDev);
    dataBody += ",";
    dataBody += ToString_StdDev(info.orienStdDev);
    dataBody += ",";
    dataBody += ToString_StdDev(info.latStdDev);
    dataBody += ",";
    dataBody += ToString_StdDev(info.lonStdDev);
    dataBody += ",";
    dataBody += ToString_StdDev(info.altStdDev);

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    newSentence = curSentence.nmeaSentence;
    return true;
}

string GPGSTnmea::ToString_utcTime()
{
    string sVal;
    if (!info.timeUTC.Get_utcTimeString(sVal, 2))
        sVal = "";
    return sVal;
}

string GPGSTnmea::ToString_StdDev(const double d)
{
	if (d == BLANK_DOUBLE || d == BAD_DOUBLE)
		return "";
	return doubleToString(d, 2);
}

















//
