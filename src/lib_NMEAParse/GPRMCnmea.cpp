/*
 * GPRMCnmea.cpp
 *
 *  Created on: Dec 27, 2013
 *      Author: Alon Yaari
 */

#include <iostream>
#include "GPRMCnmea.h"

using namespace std;

GPRMCnmea::GPRMCnmea()
{
    nmeaLen     = NUMELEM_GPRMC;
    SetKey("GPRMC");

    // Invalidate info struct
    info.status         = BAD_CHAR;
    info.speedKTS       = BAD_DOUBLE;
    info.headingTrueN   = BAD_DOUBLE;
    info.modeIndicator  = BLANK_CHAR;   // Since this is optional it defaults to blank instead of bad
    info.timeUTC.Set_Invalid();
    info.latGeog.Set_GeogType(GEOG_LAT);
    info.latGeog.SetInvalid();
    info.lonGeog.Set_GeogType(GEOG_LON);
    info.lonGeog.SetInvalid();
    info.dateUTC.Set_Invalid();
    info.magVar.Set_GeogType(GEOG_MAG);
    info.magVar.SetInvalid();
}

//     $GPRMC,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>,<9>,<10>,<11>,<12>*hh<CR><LF>
//
//     <2>  Status, A=Valid, V=Receiver warning
//     <3>  Lat, format ddmm.mmmmm (with leading 0s)
//     <4>  Lat hemisphere, N(+) or S(-)
//     <5>  Lon, format dddmm.mmmmm (with leading 0s)
//     <6>  Lon hemisphere, E(+) or W(-)
//     <7>  Speed over ground in KNOTS, format 000.00 (with leading 0s)
//     <8>  Course over ground in deg from true North, format ddd.d (with leading 0s)
//     <9>  UTC date, format ddmmyy
//     <10> Magnetic variation true North, format ddd.d (with leading 0s)
//     <11> Magnetic variation direction, E(-) or W(+)
//     <12> OPTIONAL Mode indicator, A=Autonomous, D=Differential, E=Estimated, N=bad

// GPRMCnmea::ParseSentenceIntoData()
//      Break apart incoming sentence into its parts
//      Returns TRUE  if valid NMEA sentence and checksum
//          - Still TRUE even if data is all blank or not enough items
//      Returns FALSE otherwise
bool GPRMCnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
    if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
        return false;

    // Data stored in multiple nmea elements must be retained and parsed after the for-loop
    string strLat       = BLANK_STRING;
    string strLon       = BLANK_STRING;
    string strMagVar    = BLANK_STRING;
    string strHemiLat   = BLANK_STRING;
    string strHemiLon   = BLANK_STRING;
    string strMagVarDir = BLANK_STRING;

    for (unsigned short i = 1; i < curSentence.numElements; i++) {
        string sVal = curSentence.elements.at(i);
        switch (i) {
            case 1:     FromString_timeUTC(sVal);                       break;
            case 2:     FromString_status(sVal);                        break;
            case 3:     strLat = sVal;                                  break;
            case 4:     strHemiLat = sVal;                              break;
            case 5:     strLon = sVal;                                  break;
            case 6:     strHemiLon = sVal;                              break;
            case 7:     FromString_speedKTS(sVal);                      break;
            case 8:     FromString_headingTrueN(sVal);                  break;
            case 9:     FromString_dateUTC(sVal);                       break;
            case 10:    strMagVar     = sVal;                           break;
            case 11:    strMagVarDir  = sVal;                           break;
            case 12:    FromString_modeIndicator(sVal);                 break;
            default:                                                    break; } }
    FromString_latlon(strLat, strHemiLat, strLon, strHemiLon);
    FromString_magVar(strMagVar, strMagVarDir);
    return CriticalDataAreValid();
}

bool GPRMCnmea::CriticalDataAreValid()
{
  return true;
    bool bValid = Validate_utcTime();
    bValid &= Validate_status();
    bValid &= Validate_latlon();
    bValid &= Validate_speedKTS();
    bValid &= Validate_headingTrueN();
    bValid &= Validate_utcDate();
    bValid &= Validate_magVar();
    bValid &= Validate_modeIndicator();
    return bValid;
}

bool GPRMCnmea::Validate_utcTime()
{
    if (!info.timeUTC.IsValid()) {
        AddError("Invalid UTC time.");
        return false; }
    return true;
}

// Validate_status()
//      True if status is one of these characters:
//          A V
bool GPRMCnmea::Validate_status()
{
    bool bGood = ValidCharInString(info.status, "AV", MAY_BE_BLANK, CASE_INSENSITIVE);
    if (!bGood) {
        errorBuild << "Invalid GPS status character '" << info.status << "'";
        AddError(errorBuild.str()); }
    return bGood;
}

bool GPRMCnmea::Validate_latlon()
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

// Validate_speedKTS()
//      True if speed is within:
//          0.0 to 1000.0, inclusive
bool GPRMCnmea::Validate_speedKTS()
{
    bool bGood = IsValidBoundedDouble(info.speedKTS, 0.0, 1000.0, INCLUSIVE_YES, MAY_BE_BLANK);
    if (!bGood) {
        errorBuild << "Invalid speedKTS:";
        errorBuild << info.speedKTS;
        AddError(errorBuild.str()); }
    return bGood;
}

// Validate_headingTrueN()
//      True if heading is within:
//          0.0 to 360.0, inclusive
bool GPRMCnmea::Validate_headingTrueN()
{
    bool bGood = IsValidBoundedDouble(info.headingTrueN, 0.0, 360.0, INCLUSIVE_YES, MAY_BE_BLANK);
    if (!bGood) {
            errorBuild << "Invalid headingTrueN:";
            errorBuild << info.headingTrueN;
            AddError(errorBuild.str()); }
    return bGood;
}

bool GPRMCnmea::Validate_utcDate()
{
    if (!info.dateUTC.IsValid()) {
        AddError("Invalid UTC date.");
        return false; }
    return true;
}

bool GPRMCnmea::Validate_magVar()
{
    bool mMagVar = info.magVar.Validate();
    if (!mMagVar) {
        double d = 0.0;
        info.magVar.Get_ValueAsDouble(d);
        errorBuild << "Invalid magnetic variation: " << d;
        AddError(errorBuild.str()); }
    return (mMagVar);
}

// Validate_status()
//      True if status is one of these characters:
//          A D E N <blank>
bool GPRMCnmea::Validate_modeIndicator()
{
    bool bGood = ValidCharInString(info.modeIndicator, "ADEN", MAY_BE_BLANK, CASE_INSENSITIVE);
    if (!bGood) {
        errorBuild << "Invalid GPS mode indicator character '" << info.modeIndicator << "'";
        AddError(errorBuild.str()); }
    return bGood;
}

bool GPRMCnmea::GetInfo(infoGPRMC& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool GPRMCnmea::Get_timeUTC(utcTime& timeUtc)
{
    timeUtc = info.timeUTC;
    return Validate_utcTime();
}

bool GPRMCnmea::Get_status(char& cStatus)
{
    cStatus = info.status;
    return Validate_status();
}

bool GPRMCnmea::Get_latlon(NMEAgeog& gLat, NMEAgeog& gLon)
{
    gLat = info.latGeog;
    gLon = info.lonGeog;
    return Validate_latlon();
}

bool GPRMCnmea::Get_latlonValues(double& dLat, double& dLon)
{
    if (info.latGeog.Get_ValueAsDouble(dLat)
     && info.lonGeog.Get_ValueAsDouble(dLon))
        return Validate_latlon();
    return false;
}

bool GPRMCnmea::Get_speedKTS(double& dSpeedKts)
{
    dSpeedKts = info.speedKTS;
    return Validate_speedKTS();
}

bool GPRMCnmea::Get_speedMPS(double& dSpeedMpS)
{
    dSpeedMpS = info.speedKTS * KNOTS2METERSperSEC;
    return Validate_speedKTS();
}

bool GPRMCnmea::Get_headingTrueN(double& dHeadingTrue)
{
    dHeadingTrue = info.headingTrueN;
    return Validate_headingTrueN();
}

bool GPRMCnmea::Get_dateUTC(utcDate& utcDate)
{
    utcDate = info.dateUTC;
    return utcDate.IsValid();
}

bool GPRMCnmea::Get_magVar(double& dMagVar)
{
    if (info.magVar.Get_ValueAsDouble(dMagVar))
        return Validate_magVar();
    return false;
}

bool GPRMCnmea::Get_modeIndicator(char& cModeIndicator)
{
    cModeIndicator = info.modeIndicator;
    return Validate_modeIndicator();
}

bool GPRMCnmea::Set_timeUTC(const utcTime& t)
{
    info.timeUTC = t;
    return Validate_utcTime();
}

bool GPRMCnmea::Set_status(const char& c)
{
    info.status = c;
    return Validate_status();
}

bool GPRMCnmea::Set_latlon(const NMEAgeog& gLat, const NMEAgeog& gLon)
{
    info.latGeog = gLat;
    info.lonGeog = gLon;
    return Validate_latlon();
}

bool GPRMCnmea::Set_latlonValues(const double& dLat, const double& dLon)
{
    info.latGeog.StoreDouble(dLat);
    info.lonGeog.StoreDouble(dLon);
    return Validate_latlon();
}

bool GPRMCnmea::Set_speedKTS(const double& d)
{
    info.speedKTS = d;
    return Validate_speedKTS();
}

bool GPRMCnmea::Set_speedMPS(const double& d)
{
    info.speedKTS = d * METERSperSEC2KNOTS;
    return Validate_speedKTS();
}

bool GPRMCnmea::Set_headingTrueN(const double& d)
{
    info.headingTrueN = d;
    return Validate_headingTrueN();
}

bool GPRMCnmea::Set_dateUTC(const utcDate& date)
{
    info.dateUTC = date;
    return Validate_utcDate();
}

bool GPRMCnmea::Set_magVar(const NMEAgeog& gMagVar)
{
    info.magVar = gMagVar;
    return Validate_magVar();
}

bool GPRMCnmea::Set_magVarValue(const double& d)
{
    info.magVar.StoreDouble(d);
    return Validate_magVar();
}

bool GPRMCnmea::Set_modeIndicator(const char& c)
{
    info.modeIndicator = c;
    return Validate_modeIndicator();
}

bool GPRMCnmea::FromString_timeUTC(std::string sVal)
{
    info.timeUTC.Set_utcTimeFromNMEA(sVal);     // Set_utcTimeFromNMEA() also handles blank strings
    return Validate_utcTime();
}

bool GPRMCnmea::FromString_status(std::string sVal)
{
    StoreCharFromNMEAstring(info.status, sVal);
    return Validate_status();
}

bool GPRMCnmea::FromString_latlon(std::string strLat, std::string strLatHemi, std::string strLon, std::string strLonHemi)
{
    info.latGeog.ParseFromNMEAstring(strLat, strLatHemi);
    info.lonGeog.ParseFromNMEAstring(strLon, strLonHemi);
    return Validate_latlon();
}

bool GPRMCnmea::FromString_speedKTS(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.speedKTS, sVal);
    return Validate_speedKTS();
}

bool GPRMCnmea::FromString_headingTrueN(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.headingTrueN, sVal);
    return Validate_headingTrueN();
}

bool GPRMCnmea::FromString_dateUTC(std::string sVal)
{
    info.dateUTC.Set_utcDateFromNMEA(sVal);
    return Validate_utcDate();
}

bool GPRMCnmea::FromString_magVar(std::string strVar, std::string strVarHemi)
{
    info.magVar.ParseFromNMEAstring(strVar,strVarHemi);
    return Validate_magVar();
}

bool GPRMCnmea::FromString_modeIndicator(std::string sVal)
{
    StoreCharFromNMEAstring(info.modeIndicator, sVal);
    return Validate_modeIndicator();
}

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool GPRMCnmea::ProduceNMEASentence(std::string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $GPRMC and postpend *ch (ch = checksum)
    string dataBody = "";
    dataBody += ToString_utcTime();
    dataBody +=",";
    dataBody += ToString_status();
    dataBody +=",";
    dataBody += ToString_lat();
    dataBody +=",";
    dataBody += ToString_latHemi();
    dataBody +=",";
    dataBody += ToString_lon();
    dataBody +=",";
    dataBody += ToString_lonHemi();
    dataBody +=",";
    dataBody += ToString_speedKnots();
    dataBody +=",";
    dataBody += ToString_headingTrueN();
    dataBody +=",";
    dataBody += ToString_dateUTC();
    dataBody +=",";
    dataBody += ToString_magVar();
    dataBody +=",";
    dataBody += ToString_magVarDirection();
    dataBody +=",";
    dataBody += ToString_modeIndicator();

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(newSentence, dataBody);
    //newSentence = curSentence.nmeaSentence;
    return true;
}

string GPRMCnmea::ToString_utcTime()
{
    string sVal;
    if (!info.timeUTC.Get_utcTimeString(sVal, 2))
        sVal = "";
    return sVal;
}

string GPRMCnmea::ToString_status()
{
    return CharToString(info.status);
}

string GPRMCnmea::ToString_lat()
{
    string sVal;
    if (!info.latGeog.Get_ValueString(sVal))
        return "";
    return sVal;
}

string GPRMCnmea::ToString_latHemi()
{
    string sVal;
    if (!info.latGeog.Get_HemisphereString(sVal))
        return "";
    return sVal;
}

string GPRMCnmea::ToString_lon()
{
    string sVal;
    if (!info.lonGeog.Get_ValueString(sVal))
        return "";
    return sVal;
}

string GPRMCnmea::ToString_lonHemi()
{
    string sVal;
    if (!info.lonGeog.Get_HemisphereString(sVal))
        return "";
    return sVal;
}

string GPRMCnmea::ToString_speedKnots()
{
    if (info.speedKTS == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.speedKTS, 3, 2);
}

string GPRMCnmea::ToString_headingTrueN()
{
    if (info.headingTrueN == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.headingTrueN, 3, 1);
}

string GPRMCnmea::ToString_dateUTC()
{
    string uDate = "";
    if (!info.dateUTC.IsBlank())
        info.dateUTC.Get_utcDateString(uDate);
    return uDate;
}

string GPRMCnmea::ToString_magVar()
{
    if (info.magVar.IsBlank())
        return "";
    string sVal;
    if (info.magVar.Get_ValueString(sVal))
        return sVal;
    return "";
}

string GPRMCnmea::ToString_magVarDirection()
{
    if (info.magVar.IsBlank())
        return "";
    string sVal;
    if (info.magVar.Get_HemisphereString(sVal))
        return sVal;
    return "";
}

string GPRMCnmea::ToString_modeIndicator()
{
    if (info.modeIndicator == BLANK_CHAR)
        return "";
    return CharToString(info.modeIndicator);
}




















//
