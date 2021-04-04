/*
 * CPNVGnmea.cpp
 *
 *  Created on: Dec 31, 2013
 *      Author: yar
 */


#include "CPNVGnmea.h"

using namespace std;

CPNVGnmea::CPNVGnmea()
{
    nmeaLen     = NUMELEM_CPNVG;
    SetKey("CPNVG");

    info.altBottom      = BAD_DOUBLE;
    info.depthTop       = BAD_DOUBLE;
    info.headingTrueN   = BAD_DOUBLE;
    info.pitch          = BAD_DOUBLE;
    info.posQual        = BAD_CHAR;
    info.roll           = BAD_DOUBLE;
    info.timeUTC.Set_Invalid();
    info.latGeog.Set_GeogType(GEOG_LAT);
    info.latGeog.SetInvalid();
    info.lonGeog.Set_GeogType(GEOG_LON);
    info.lonGeog.SetInvalid();
    info.navTimestamp.Set_Invalid();
}

// GPRMCnmea::ParseSentenceIntoData()
//      Break apart incoming sentence into its parts
//      Returns TRUE  if valid NMEA sentence and checksum
//          - Still TRUE even if data is all blank or not enough items
//      Returns FALSE otherwise
bool CPNVGnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
    if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
        return false;

    // Data stored in multiple nmea elements must be retained and parsed after the for-loop
    string strLat       = BLANK_STRING;
    string strLon       = BLANK_STRING;
    string strHemiLat   = BLANK_STRING;
    string strHemiLon   = BLANK_STRING;

    for (unsigned short i = 1; i < curSentence.numElements; i++) {
        string sVal = curSentence.elements.at(i);
        switch (i) {
            case 1:     FromString_utcTime(sVal);                       break;
            case 2:     strLat = sVal;                                  break;
            case 3:     strHemiLat = sVal;                              break;
            case 4:     strLon = sVal;                                  break;
            case 5:     strHemiLon = sVal;                              break;
            case 6:     FromString_posQual(sVal);                       break;
            case 7:     FromString_altBottom(sVal);                     break;
            case 8:     FromString_depthTop(sVal);                      break;
            case 9:     FromString_headingTrueN(sVal);                  break;
            case 10:    FromString_roll(sVal);                          break;
            case 11:    FromString_pitch(sVal);                         break;
            case 12:    FromString_navTimestamp(sVal);                  break;
            default:                                                    break; } }
    FromString_latlon(strLat, strHemiLat, strLon, strHemiLon);
    return CriticalDataAreValid();
}

bool CPNVGnmea::CriticalDataAreValid()
{
  return true;
    bool bValid = Validate_utcTime();
    bValid &= Validate_altBottom();
    bValid &= Validate_depthTop();
    bValid &= Validate_headingTrueN();
    bValid &= Validate_latlon();
    bValid &= Validate_navTimestamp();
    bValid &= Validate_pitch();
    bValid &= Validate_posQual();
    bValid &= Validate_roll();
    bValid &= Validate_utcTime();
    return bValid;
}

bool CPNVGnmea::Validate_utcTime()
{
    return info.timeUTC.IsValid();
}

bool CPNVGnmea::Validate_latlon()
{
    return (info.latGeog.Validate() && info.lonGeog.Validate());
}

bool CPNVGnmea::Validate_posQual()
{
    return ValidCharInString(info.posQual, "01", true, false);
}

bool CPNVGnmea::Validate_altBottom()
{
    return IsValidBoundedDouble(info.altBottom, 0.0, 1000.0, true, true);
}

bool CPNVGnmea::Validate_depthTop()
{
    return IsValidBoundedDouble(info.depthTop, 0.0, 1000.0, true, true);
}

bool CPNVGnmea::Validate_headingTrueN()
{
    return IsValidBoundedDouble(info.headingTrueN, 0.0, 360.0, INCLUSIVE_YES, MAY_BE_BLANK);
}

bool CPNVGnmea::Validate_roll()
{
    return IsValidBoundedDouble(info.roll, -90.0, 90.0, true, true);
}

bool CPNVGnmea::Validate_pitch()
{
    return IsValidBoundedDouble(info.pitch, -90.0, 90.0, true, true);
}

bool CPNVGnmea::Validate_navTimestamp()
{
    return info.navTimestamp.IsValid();
}

bool CPNVGnmea::GetInfo(infoCPNVG& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool CPNVGnmea::Get_timeUTC(utcTime& timeUtc)
{
    timeUtc = info.timeUTC;
    return Validate_utcTime();
}

bool CPNVGnmea::Get_latlon(NMEAgeog& gLat, NMEAgeog& gLon)
{
    gLat = info.latGeog;
    gLon = info.lonGeog;
    return Validate_latlon();
}

bool CPNVGnmea::Get_latlonValues(double& dLat, double& dLon)
{
    if (info.latGeog.Get_ValueAsDouble(dLat)
     && info.lonGeog.Get_ValueAsDouble(dLon))
        return Validate_latlon();
    return false;
}

bool CPNVGnmea::Get_posQual(char& c)
{
    c = info.posQual;
    return Validate_posQual();
}

bool CPNVGnmea::Get_altBottom(double& dVal)
{
    dVal = info.altBottom;
    return Validate_altBottom();
}

bool CPNVGnmea::Get_depthTop(double& dVal)
{
    dVal = info.depthTop;
    return Validate_depthTop();
}

bool CPNVGnmea::Get_headingTrueN(double& dVal)
{
    dVal = info.headingTrueN;
    return Validate_headingTrueN();
}

bool CPNVGnmea::Get_roll(double& dVal)
{
    dVal = info.roll;
    return Validate_roll();
}

bool CPNVGnmea::Get_pitch(double& dVal)
{
    dVal = info.pitch;
    return Validate_pitch();
}

bool CPNVGnmea::Get_navTimestamp(utcTime& navTime)
{
    navTime = info.navTimestamp;
    return Validate_navTimestamp();
}

bool CPNVGnmea::Set_utcTime(const utcTime& t)
{
    // [NavTimestamp] Timestamp for time this pose/position was calculated. If blank, use [Timestamp].
    if (info.navTimestamp.IsBlank() || !info.navTimestamp.IsValid())
        Set_navTimestamp(t);
    info.timeUTC = t;
    return Validate_utcTime();
}

bool CPNVGnmea::Set_latlon(const NMEAgeog& gLat, const NMEAgeog& gLon)
{
    info.latGeog = gLat;
    info.lonGeog = gLon;
    return Validate_latlon();
}

bool CPNVGnmea::Set_latlonValues(const double& dLat, const double& dLon)
{
    info.latGeog.StoreDouble(dLat);
    info.lonGeog.StoreDouble(dLon);
    return Validate_latlon();
}

bool CPNVGnmea::Set_posQual(const char& c)
{
    info.posQual = c;
    return Validate_posQual();
}

bool CPNVGnmea::Set_altBottom(const double& d)
{
    info.altBottom = d;
    return Validate_altBottom();
}

bool CPNVGnmea::Set_depthTop(const double& d)
{
    info.depthTop = d;
    return Validate_depthTop();
}

bool CPNVGnmea::Set_headingTrueN(const double& d)
{
    info.headingTrueN = d;
    return Validate_headingTrueN();
}

bool CPNVGnmea::Set_roll(const double& d)
{
    info.roll = d;
    return Validate_roll();
}

bool CPNVGnmea::Set_pitch(const double& d)
{
    info.pitch = d;
    return Validate_pitch();
}

bool CPNVGnmea::Set_navTimestamp(const utcTime& t)
{
    info.timeUTC = t;

    // [NavTimestamp] Timestamp for time this pose/position was calculated. If blank, use [Timestamp].
    if (info.timeUTC.IsBlank() || !Validate_navTimestamp())
        info.navTimestamp = info.timeUTC;
    return Validate_navTimestamp();
}

bool CPNVGnmea::FromString_utcTime(std::string sVal)
{
    info.timeUTC.Set_utcTimeFromNMEA(sVal);
    return Validate_utcTime();

}

bool CPNVGnmea::FromString_latlon(std::string strLat, std::string strLatHemi, std::string strLon, std::string strLonHemi)
{
    info.latGeog.ParseFromNMEAstring(strLat, strLatHemi);
    info.lonGeog.ParseFromNMEAstring(strLon, strLonHemi);
    return Validate_latlon();
}

bool CPNVGnmea::FromString_posQual(std::string sVal)
{
    StoreCharFromNMEAstring(info.posQual, sVal);
    return Validate_posQual();
}

bool CPNVGnmea::FromString_altBottom(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.altBottom, sVal);
    return Validate_altBottom();
}

bool CPNVGnmea::FromString_depthTop(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.depthTop, sVal);
    return Validate_depthTop();
}

bool CPNVGnmea::FromString_headingTrueN(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.headingTrueN, sVal);
    return Validate_headingTrueN();
}

bool CPNVGnmea::FromString_roll(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.roll, sVal);
    return Validate_roll();
}

bool CPNVGnmea::FromString_pitch(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.pitch, sVal);
    return Validate_pitch();
}

bool CPNVGnmea::FromString_navTimestamp(std::string sVal)
{
    info.navTimestamp.Set_utcTimeFromNMEA(sVal);
    if (info.navTimestamp.IsBlank() || !info.navTimestamp.IsValid())
        info.navTimestamp = info.timeUTC;
    return Validate_navTimestamp();
}

bool CPNVGnmea::ProduceNMEASentence(std::string& newSentence)
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
    dataBody += ToString_lat();
    dataBody +=",";
    dataBody += ToString_latHemi();
    dataBody +=",";
    dataBody += ToString_lon();
    dataBody +=",";
    dataBody += ToString_lonHemi();
    dataBody +=",";
    dataBody += ToString_posQual();
    dataBody +=",";
    dataBody += ToString_altBottom();
    dataBody +=",";
    dataBody += ToString_depthTop();
    dataBody +=",";
    dataBody += ToString_headingTrueN();
    dataBody +=",";
    dataBody += ToString_roll();
    dataBody +=",";
    dataBody += ToString_pitch();
    dataBody +=",";
    dataBody += ToString_navTimestamp();
    dataBody +=",";

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(newSentence, dataBody);
    return true;
}

string CPNVGnmea::ToString_utcTime()
{
    string sVal;
    if (!info.timeUTC.Get_utcTimeString(sVal, 2))
        sVal = "";
    return sVal;
}

string CPNVGnmea::ToString_lat()
{
    string sVal;
    if (!info.latGeog.Get_ValueString(sVal))
        return "";
    return sVal;
}

string CPNVGnmea::ToString_latHemi()
{
    string sVal;
    if (!info.latGeog.Get_HemisphereString(sVal))
        return "";
    return sVal;
}

string CPNVGnmea::ToString_lon()
{
    string sVal;
    if (!info.lonGeog.Get_ValueString(sVal))
        return "";
    return sVal;
}

string CPNVGnmea::ToString_lonHemi()
{
    string sVal;
    if (!info.lonGeog.Get_HemisphereString(sVal))
        return "";
    return sVal;
}

string CPNVGnmea::ToString_posQual()
{
    if (info.posQual == BLANK_CHAR)
        return "";
    return CharToString(info.posQual);
}

string CPNVGnmea::ToString_altBottom()
{
    if (info.altBottom == BLANK_DOUBLE)
        return "";
    return doubleToString(info.altBottom, 2);
}

string CPNVGnmea::ToString_depthTop()
{
    if (info.depthTop == BLANK_DOUBLE)
        return "";
    return doubleToString(info.depthTop, 2);
}

string CPNVGnmea::ToString_headingTrueN()
{
    if (info.headingTrueN == BLANK_DOUBLE)
        return "";
    return doubleToString(info.headingTrueN, 2);
}

string CPNVGnmea::ToString_roll()
{
    if (info.roll == BLANK_DOUBLE)
        return "";
    return doubleToString(info.roll, 1);
}

string CPNVGnmea::ToString_pitch()
{
    if (info.pitch == BLANK_DOUBLE)
        return "";
    return doubleToString(info.pitch, 1);
}

string CPNVGnmea::ToString_navTimestamp()
{
    string sVal;
    if (!info.navTimestamp.Get_utcTimeString(sVal, 2))
        sVal = "";
    return sVal;
}





// ATTIC


/*

// CPNVGnmea::ParseSentenceIntoData()
//      Break apart incoming sentence into its parts
//      Returns TRUE  if valid NMEA sentence and checksum
//          - Still TRUE even if data is all blank or not enough items
//      Returns FALSE otherwise
bool CPNVGnmea::ParseSentenceIntoData(string sentence)
{
    // Break apart sentence into data map
    //      - Store each item in the info struct
    bValid = IsValidChecksum(sentence);
    if (!bValid) return false;
    bValid = MOOSStrCmp(nmeaKey, GetKeyFromSentence(sentence));
    if (!bValid) return false;
    string sVal = MOOSChomp(sentence, ",");     // Strips off $XXXXX, from the start of the sentence
    string toParse = MOOSChomp(sentence, "*");  // Grabs all data not including the '*' and checksum

    // Data stored in two nmea elements must be retained and parsed after the for-loop
    string strLat, strLatHemi, strLon, strLonHemi;

    for (unsigned short i = 1; i <= nmeaLen; i++) {
        sVal = MOOSChomp(toParse, ",");         // Grabs just the data until the next comma (or end)
        bValid = StoreDataElement(i, sVal);     // Store in the data map
        switch (i) {                            // Store items in the info struct
            //case 1:     ParseTimeUTC(info.timestamp, sVal);             break;
            case 2:     strLat = sVal;                                  break;
            case 3:     strLatHemi = sVal;                              break;
            case 4:     strLon = sVal;                                  break;
            case 5:     strLonHemi = sVal;                              break;
            case 6:     ParseChar(info.posQual, sVal);                  break;
            case 7:     ParseDouble(info.altBottom, sVal);              break;
            case 8:     ParseDouble(info.depthTop, sVal);               break;
            case 9:     ParseDouble(info.heading, sVal);                break;
            case 10:    ParseDouble(info.pitch, sVal);                  break;
            case 11:    ParseDouble(info.roll, sVal);                   break;
            //case 12:    ParseTimeUTC(info.navTimestamp, sVal);          break;
            default:                                                    break; } }

    ParseLat(info.latDD, strLat, strLatHemi);
    ParseLon(info.lonDD, strLon, strLonHemi);
    return bValid;
}
bool CPNVGnmea::GetInfo(infoCPNVG& curInfo)
{
    if (bValid)
        curInfo = info;
    return bValid;
}

bool CPNVGnmea::Get_timestamp(utcTime& t)
{
    if (info.timestamp.IsValid())
        t = info.timestamp;
    return info.timestamp.IsValid();
}

bool CPNVGnmea::Get_lat(double& d)
{
    bool bGood = (info.latDD != BLANK_DOUBLE);
    if (bGood)
        d = info.latDD;
    return bGood;
}

bool CPNVGnmea::Get_lon(double& d)
{
    bool bGood = (info.latDD != BLANK_DOUBLE);
    if (bGood)
        d = info.lonDD;
    return bGood;
}

bool CPNVGnmea::Get_depthTop(double& d)
{
    bool bGood = (info.depthTop != BLANK_DOUBLE);
    if (bGood)
        d = info.depthTop;
    return bGood;
}

bool CPNVGnmea::Get_altBottom(double& d)
{
    bool bGood = (info.altBottom != BLANK_DOUBLE);
    if (bGood)
        d = info.altBottom;
    return bGood;
}

bool CPNVGnmea::Get_heading(double& d)
{
    bool bGood = (info.heading != BLANK_DOUBLE);
    if (bGood)
        d = info.heading;
    return bGood;
}

bool CPNVGnmea::Get_roll(double& d)
{
    bool bGood = (info.roll != BLANK_DOUBLE);
    if (bGood)
        d = info.roll;
    return bGood;
}

bool CPNVGnmea::Get_pitch(double& d)
{
    bool bGood = (info.pitch != BLANK_DOUBLE);
    if (bGood)
        d = info.pitch;
    return bGood;
}

bool CPNVGnmea::Get_navTimestamp(utcTime& t)
{
    if (info.navTimestamp.IsValid())
        t = info.navTimestamp;
    return info.navTimestamp.IsValid();
}



*/







//
