/*
 * CPGGAnmea.cpp
 *
 *  Created on: Jun 16, 2014
 *      Author: Alon Yaari
 */

#include <iostream>
#include "GPGGAnmea.h"

using namespace std;

GPGGAnmea::GPGGAnmea()
{
    nmeaLen     = NUMELEM_GPGGA;
    SetKey("GPGGA");

    // Invalidate the info struct
    info.altGeoid       = BLANK_DOUBLE;     // OK if no geoid altitude
    info.altMSL         = BAD_DOUBLE;       // Must have MSL altitude
    info.diffAge        = BLANK_STRING;     // OK if no differential age
    info.diffStation    = BLANK_STRING;     // OK if no differential station
    info.gpsQual        = BAD_CHAR;
    info.hdop           = BAD_DOUBLE;
    info.satNum         = BAD_USHORT;
    info.latGeog.Set_GeogType(GEOG_LAT);
    info.latGeog.SetInvalid();
    info.lonGeog.Set_GeogType(GEOG_LON);
    info.lonGeog.SetInvalid();
    info.timeUTC.Set_Invalid();
}

//     $GPGGA,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>,<9>,<10>,<11>,<12>,<13>,<14>*hh<CR><LF>
//     <1>  UTC time, format hhmmss.s
//     <2>  Lat, format ddmm.mmmmm (with leading 0s)
//     <3>  Lat hemisphere, N(+) or S(-)
//     <4>  Lon, format dddmm.mmmmm (with leading 0s)
//     <5>  Lon hemisphere, E(+) or W(-)
//     <6>  GPS Quality, 0=No fix, 1=Non-diff, 2=Diff, 6=estimated
//     <7>  Number of Satellites
//     <8>  HDOP, 0.5 to 99.9
//     <9>  Alt above MSL -9999.9 to 999999.9 meters
//     <10> Alt MSL units, M=meters
//     <11> Geoid separation -999.9 to 9999.9 meters
//     <12> Geoid separation units, M=meters
//     <13> Age of differential correction, <blank> when not using differential
//     <14> Differential station ID, 0000 when not using differential

bool GPGGAnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
    // Always call base class version of this function to validate basics
    //      and populate inSentence.elements
    if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
        return false;

    string strLat       = BLANK_DOUBLE_STRING;
    string strLon       = BLANK_DOUBLE_STRING;
    string strHemiLat   = BLANK_STRING;
    string strHemiLon   = BLANK_STRING;
    for (unsigned short i = 1; i < curSentence.numElements; i++) {
        string sVal = curSentence.elements.at(i);
        switch(i) {
            case 1:     FromString_timeUTC(sVal);                       break;
            case 2:     strLat = sVal;                                  break;
            case 3:     strHemiLat = sVal;                              break;
            case 4:     strLon = sVal;                                  break;
            case 5:     strHemiLon = sVal;                              break;
            case 6:     FromString_gpsQual(sVal);                       break;
            case 7:     FromString_satNum(sVal);                        break;
            case 8:     FromString_hdop(sVal);                          break;
            case 9:     FromString_altMSL(sVal);                        break;
            case 10:                                                    break;
            case 11:    FromString_altGeoid(sVal);                      break;
            case 12:                                                    break;
            case 13:    FromString_diffAge(sVal);                       break;
            case 14:    FromString_diffStation(sVal);                   break; } }
    FromString_latlon(strLat, strHemiLat, strLon, strHemiLon);
    return CriticalDataAreValid();
}

bool GPGGAnmea::CriticalDataAreValid()
{
  return true;
    errors = "";
    bool bValid = Validate_utcTime();
    bValid &= Validate_latlon();
    bValid &= Validate_altMSL();
    bValid &= Validate_gpsQual();
    bValid &= Validate_hdop();
    bValid &= Validate_satNum();
    return bValid;
}

bool GPGGAnmea::Validate_utcTime()
{
    if (!info.timeUTC.IsValid()) {
        AddError("Invalid UTC time.");
        return false; }
    return true;
}

bool GPGGAnmea::Validate_latlon()
{
    bool bLat = info.latGeog.Validate();
    if (!bLat) {
        double d = 0.0;
        info.latGeog.Get_ValueAsDouble(d);
        errorBuild << "Invalid latitude: " << d;
        AddError(errorBuild.str()); }
    bool bLon = info.lonGeog.Validate();
    if (!bLon) {
        double d = 0.0;
        info.lonGeog.Get_ValueAsDouble(d);
        errorBuild << "Invalid longitude: " << d;
        AddError(errorBuild.str()); }
    return (bLat && bLon);
}

bool GPGGAnmea::Validate_gpsQual()
{
    bool bGood = ValidCharInString(info.gpsQual, "0126", MAY_NOT_BE_BLANK, CASE_INSENSITIVE);
    if (!bGood) {
        errorBuild << "Invalid GPS quality character '" << info.gpsQual << "'";
        AddError(errorBuild.str()); }
    return bGood; }

bool GPGGAnmea::Validate_satNum()
{
    bool bGood = IsValidBoundedUShort(info.satNum, 0, 30, INCLUSIVE_YES, MAY_NOT_BE_BLANK);
    if (!bGood) {
        errorBuild << "Invalid number of satellites: " << info.satNum;
        AddError(errorBuild.str()); }
    return bGood;
}

bool GPGGAnmea::Validate_hdop()
{
    bool bGood = IsValidBoundedDouble(info.hdop, 0.0, 100.0, INCLUSIVE_NO, MAY_BE_BLANK);
    if (!bGood) {
        errorBuild << "Invalid hdop: ";
        errorBuild << info.hdop;
        AddError(errorBuild.str()); }
    return bGood;
}

bool GPGGAnmea::Validate_altMSL()
{
    bool bGood = IsValidBoundedDouble(info.hdop, -9999.9, 999999.9, INCLUSIVE_YES, MAY_BE_BLANK);
    if (!bGood) {
        errorBuild << "Invalid altitude above mean sea level: " << info.altMSL;
        AddError(errorBuild.str()); }
    return bGood;
}

bool GPGGAnmea::Validate_altGeoid()
{
    bool bGood = IsValidBoundedDouble(info.hdop, -999.9, 9999.9, INCLUSIVE_YES, MAY_BE_BLANK);
    if (!bGood) {
        errorBuild << "Invalid geoid altitude: " << info.altGeoid;
        AddError(errorBuild.str()); }
    return bGood;
}

bool GPGGAnmea::GetInfo(infoGPGGA& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool GPGGAnmea::Get_timeUTC(utcTime& timeUtc)
{
    timeUtc = info.timeUTC;
    return Validate_utcTime();
}

bool GPGGAnmea::Get_latlon(NMEAgeog& gLat, NMEAgeog& gLon)
{
    gLat = info.latGeog;
    gLon = info.lonGeog;
    return Validate_latlon();
}

bool GPGGAnmea::Get_latlonValues(double& dLat, double& dLon)
{
    if (info.latGeog.Get_ValueAsDouble(dLat)
     && info.lonGeog.Get_ValueAsDouble(dLon))
        return Validate_latlon();
    return false;
}

bool GPGGAnmea::Get_gpsQual(char& cGpsQual)
{
    cGpsQual = info.gpsQual;
    return Validate_gpsQual();
}

bool GPGGAnmea::Get_satNum(unsigned short& uiSatNum)
{
    uiSatNum = info.satNum;
    return Validate_satNum();
}

bool GPGGAnmea::Get_hdop(double& hdop)
{
    hdop = info.hdop;
    return Validate_hdop();
}

bool GPGGAnmea::Get_altMSL(double &altMSL)
{
    altMSL = info.altMSL;
    return Validate_altMSL();
}

bool GPGGAnmea::Get_altGeoid(double &altGeoid)
{
    altGeoid = info.altGeoid;
    return Validate_altGeoid();
}

bool GPGGAnmea::Get_diffAge(std::string& diffAge)
{
    diffAge = info.diffAge;
    return true;
}

bool GPGGAnmea::Get_diffStation(std::string& diffStation)
{
    diffStation = info.diffStation;
    return true;
}

bool GPGGAnmea::Set_timeUTC(const utcTime& t)
{
    info.timeUTC = t;
    return Validate_utcTime();
}

bool GPGGAnmea::Set_latlon(const NMEAgeog& gLat, const NMEAgeog& gLon)
{
    info.latGeog = gLat;
    info.lonGeog = gLon;
    return Validate_latlon();
}

// Set_latlonValues()
//    Lat and lon arrive in decimal degrees
//    They need to be saved in the weird nmea ddmmm.mmmmm format
bool GPGGAnmea::Set_latlonValues(const double& dLat, const double& dLon)
{
    info.latGeog.StoreDouble(dLat);
    info.lonGeog.StoreDouble(dLon);
    return Validate_latlon();
}

bool GPGGAnmea::Set_gpsQual(const char& c)
{
    info.gpsQual = c;
    return Validate_gpsQual();
}

bool GPGGAnmea::Set_satNum(const unsigned short& u)
{
    info.satNum = u;
    return Validate_satNum();
}

bool GPGGAnmea::Set_hdop(const double& d)
{
    info.hdop = d;
    return Validate_hdop();
}

bool GPGGAnmea::Set_altMSL(const double& d)
{
    info.altMSL = d;
    return Validate_altMSL();
}

bool GPGGAnmea::Set_altGeoid(const double& d)
{
    info.altGeoid = d;
    return Validate_altGeoid();
}

bool GPGGAnmea::Set_diffAge(const std::string& s)
{
    info.diffAge = s;
    return true;
}

bool GPGGAnmea::Set_diffStation(const std::string& s)
{
    info.diffStation = s;
    return true;
}

bool GPGGAnmea::FromString_timeUTC(std::string sVal)
{
    info.timeUTC.Set_utcTimeFromNMEA(sVal);     // Set_utcTimeFromNMEA() also handles blank strings
    return Validate_utcTime();
}

bool GPGGAnmea::FromString_latlon(std::string strLat, std::string strLatHemi, std::string strLon, std::string strLonHemi)
{
    // NMEAgeog::ParseFromNMEAstring() also handles blank strings
    info.latGeog.ParseFromNMEAstring(strLat, strLatHemi);
    info.lonGeog.ParseFromNMEAstring(strLon, strLonHemi);
    return Validate_latlon();
}

bool GPGGAnmea::FromString_gpsQual(std::string sVal)
{
    StoreCharFromNMEAstring(info.gpsQual, sVal);
    return Validate_gpsQual();
}

bool GPGGAnmea::FromString_satNum(std::string sVal)
{
    StoreUShortFromNMEAstring(info.satNum, sVal);
    return Validate_satNum();
}

bool GPGGAnmea::FromString_hdop(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.hdop, sVal);
    return Validate_hdop();
}

bool GPGGAnmea::FromString_altMSL(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.altMSL, sVal);
    return Validate_altMSL();
}

bool GPGGAnmea::FromString_altGeoid(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.altGeoid, sVal);
    return Validate_altGeoid();
}

bool GPGGAnmea::FromString_diffAge(std::string sVal)
{
    info.diffAge = sVal;
    return true;
}

bool GPGGAnmea::FromString_diffStation(std::string sVal)
{
    info.diffStation = sVal;
    return true;
}

bool GPGGAnmea::ProduceNMEASentence(std::string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    string dataBody = "";
    dataBody += ToString_utcTime();     // <1>
    dataBody += ",";
    dataBody += ToString_lat();         // <2>
    dataBody += ",";
    dataBody += ToString_latHemi();     // <3>
    dataBody += ",";
    dataBody += ToString_lon();         // <4>
    dataBody += ",";
    dataBody += ToString_lonHemi();     // <5>
    dataBody += ",";
    dataBody += ToString_gpsQual();     // <6>
    dataBody += ",";
    dataBody += ToString_satNum();      // <7>
    dataBody += ",";
    dataBody += ToString_hdop();        // <8>
    dataBody += ",";
    dataBody += ToString_altMSL();      // <9>
    dataBody += ",";
    dataBody += "M";                    // <10>
    dataBody += ",";
    dataBody += ToString_altGeoid();    // <11>
    dataBody += ",";
    dataBody += "M";                    // <12>
    dataBody += ",";
    dataBody += ToString_diffAge();     // <13>
    dataBody += ",";
    dataBody += ToString_diffstation(); // <14>

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(newSentence, dataBody);
    return true;
}

string GPGGAnmea::ToString_utcTime()
{
    string sVal;
    if (!info.timeUTC.Get_utcTimeString(sVal, 2))
        sVal = "";
    return sVal;
}

string GPGGAnmea::ToString_lat()
{
    string sVal;
    if (!info.latGeog.Get_ValueString(sVal))
        return "";
    return sVal;
}

string GPGGAnmea::ToString_latHemi()
{
    string sVal;
    if (!info.latGeog.Get_HemisphereString(sVal))
        return "";
    return sVal;
}

string GPGGAnmea::ToString_lon()
{
    string sVal;
    if (!info.lonGeog.Get_ValueString(sVal))
        return "";
    return sVal;
}

string GPGGAnmea::ToString_lonHemi()
{
    string sVal;
    if (!info.lonGeog.Get_HemisphereString(sVal))
        return "";
    return sVal;
}

string GPGGAnmea::ToString_gpsQual()
{
    if (info.gpsQual == BLANK_CHAR)
        return "";
    return CharToString(info.gpsQual);
}

string GPGGAnmea::ToString_satNum()
{
    if (info.satNum == BLANK_USHORT)
        return "";
    return intToString(info.satNum);
}

string GPGGAnmea::ToString_hdop()
{
    if (info.hdop == BLANK_DOUBLE)
        return "";
    return doubleToString(info.hdop, 1);
}

string GPGGAnmea::ToString_altMSL()
{
    if (info.altMSL == BLANK_DOUBLE)
        return "";
    return doubleToString(info.altMSL, 1);
}

string GPGGAnmea::ToString_altGeoid()
{
    if (info.altGeoid == BLANK_DOUBLE)
        return "";
    return doubleToString(info.altGeoid, 1);
}

string GPGGAnmea::ToString_diffAge()
{
    return info.diffAge;
}

string GPGGAnmea::ToString_diffstation()
{
    return info.diffStation;
}




























//


