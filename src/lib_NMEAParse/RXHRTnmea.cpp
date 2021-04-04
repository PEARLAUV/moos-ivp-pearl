/*
 * RXHRTnmea.cpp
 *
 *  Created on: Jun 17, 2014
 *      Author: yar
 */

#include "RXHRTnmea.h"

using namespace std;

RXHRTnmea::RXHRTnmea()
{
    nmeaLen     = NUMELEM_RXHRT;
    SetKey("RXHRT");

    // Invalidate the info struct
    info.timeUTC.Set_Invalid();
    info.latGeog.Set_GeogType(GEOG_LATDD);
    info.latGeog.SetInvalid();
    info.lonGeog.Set_GeogType(GEOG_LONDD);
    info.lonGeog.SetInvalid();
    info.teamID = BLANK_STRING;
    info.curMode = 'X';
    info.curTask = 'X';
}

bool RXHRTnmea::ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum)
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
            case 1: FromString_utcTime(sVal);                           break;
            case 2: strLat = sVal;                                      break;
            case 3: strHemiLat = sVal;                                  break;
            case 4: strLon = sVal;                                      break;
            case 5: strHemiLon = sVal;                                  break;
            case 6: FromString_teamID(sVal);                            break;
            case 7: FromString_curMode(sVal);                           break;
            case 8: FromString_curTask(sVal);                           break;
            default:                                                    break; } }
    FromString_latlon(strLat, strHemiLat, strLon, strHemiLon);
    return CriticalDataAreValid();
}

bool RXHRTnmea::CriticalDataAreValid()
{
    bool bValid = Validate_utcTime();
    bValid &= Validate_latlon();
    bValid &= Validate_teamID();
    bValid &= Validate_curMode();
    bValid &= Validate_curTask();
    return bValid;
}

bool RXHRTnmea::Validate_utcTime()
{
    return info.timeUTC.IsValid();
}

bool RXHRTnmea::Validate_latlon()
{
    return (info.latGeog.Validate() && info.lonGeog.Validate());
}

bool RXHRTnmea::Validate_teamID()
{
    return true;
}

bool RXHRTnmea::Validate_curMode()
{
    return ValidCharInString(info.curMode, "12", MAY_BE_BLANK, CASE_INSENSITIVE);
}

bool RXHRTnmea::Validate_curTask()
{
    return ValidCharInString(info.curTask, "12345", MAY_BE_BLANK, CASE_INSENSITIVE);
}

bool RXHRTnmea::GetInfo(infoRXHRT& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool RXHRTnmea::Get_timeUTC(utcTime& t)
{
    t = info.timeUTC;
    return Validate_utcTime();
}

bool RXHRTnmea::Get_latlon(NMEAgeog& dLat, NMEAgeog& dLon)
{
    dLat = info.latGeog;
    dLon = info.lonGeog;
    return Validate_latlon();
}

bool RXHRTnmea::Get_latlonValues(double& dLat, double& dLon)
{
    if (info.latGeog.Get_ValueAsDouble(dLat)
     && info.lonGeog.Get_ValueAsDouble(dLon))
        return Validate_latlon();
    return false;
}

bool RXHRTnmea::Get_teamID(std::string& teamID)
{
    teamID = info.teamID;
    return Validate_teamID();
}

bool RXHRTnmea::Get_curMode(char& mode)
{
    mode = info.curMode;
    return Validate_curMode();
}

bool RXHRTnmea::Get_curTask(char& task)
{
    task = info.curTask;
    return Validate_curTask();
}

bool RXHRTnmea::Set_utcTime(const utcTime t)
{
    info.timeUTC = t;
    return Validate_utcTime();
}

bool RXHRTnmea::Set_utcTime(const unsigned short int hour, const unsigned short int minute, const unsigned short int second)
{
    return info.timeUTC.Set_utcTime(hour, minute, second);
}

bool RXHRTnmea::Set_latlon(const NMEAgeog dLat, const NMEAgeog dLon)
{
    info.latGeog = dLat;
    info.lonGeog = dLon;
    return Validate_latlon();
}

bool RXHRTnmea::Set_latlonValues(const double dLat, const double dLon)
{
    info.latGeog.StoreDouble(dLat);
    info.lonGeog.StoreDouble(dLon);
    return Validate_latlon();
}

bool RXHRTnmea::Set_teamID(const std::string s)
{
    info.teamID = s;
    return Validate_teamID();
}

bool RXHRTnmea::Set_curMode(const char c)
{
    info.curMode = c;
    return Validate_curMode();
}

bool RXHRTnmea::Set_curTask(const char c)
{
    info.curTask = c;
    return Validate_curTask();
}

bool RXHRTnmea::FromString_utcTime(std::string sVal)
{
    info.timeUTC.Set_utcTimeFromNMEA(sVal);     // Set_utcTimeFromNMEA() also handles blank strings
    return Validate_utcTime();
}

bool RXHRTnmea::FromString_latlon(std::string strLat, std::string strLatHemi, std::string strLon, std::string strLonHemi)
{
    // NMEAgeog::ParseFromNMEAstring() also handles blank strings
    info.latGeog.ParseFromNMEAstring(strLat, strLatHemi);
    info.lonGeog.ParseFromNMEAstring(strLon, strLonHemi);
    return Validate_latlon();
}

bool RXHRTnmea::FromString_teamID(std::string sVal)
{
    Set_teamID(sVal);
    return Validate_teamID();
}

bool RXHRTnmea::FromString_curMode(std::string sVal)
{
    StoreCharFromNMEAstring(info.curMode, sVal);
    return Validate_curMode();
}

bool RXHRTnmea::FromString_curTask(std::string sVal)
{
    StoreCharFromNMEAstring(info.curTask, sVal);
    return Validate_curTask();
}

bool RXHRTnmea::ProduceNMEASentence(std::string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    string dataBody = "";
    dataBody += ToString_utcTime();
    dataBody += ",";
    dataBody += ToString_lat();
    dataBody += ",";
    dataBody += ToString_latHemi();
    dataBody += ",";
    dataBody += ToString_lon();
    dataBody += ",";
    dataBody += ToString_lonHemi();
    dataBody += ",";
    dataBody += ToString_teamID();
    dataBody += ",";
    dataBody += ToString_curMode();
    dataBody += ",";
    dataBody += ToString_curTask();

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    return true;
}

string RXHRTnmea::ToString_utcTime()
{
    string sVal;
    if (!info.timeUTC.Get_utcTimeString(sVal, 2))
        sVal = "";
    return sVal;
}

string RXHRTnmea::ToString_lat()
{
    string sVal;
    if (!info.latGeog.Get_ValueString(sVal))
        return "";
    return sVal;
}

string RXHRTnmea::ToString_latHemi()
{
    string sVal;
    if (!info.latGeog.Get_HemisphereString(sVal))
        return "";
    return sVal;
}

string RXHRTnmea::ToString_lon()
{
    string sVal;
    if (!info.lonGeog.Get_ValueString(sVal))
        return "";
    return sVal;
}

string RXHRTnmea::ToString_lonHemi()
{
    string sVal;
    if (!info.lonGeog.Get_HemisphereString(sVal))
        return "";
    return sVal;
}

string RXHRTnmea::ToString_teamID()
{
    return info.teamID;
}

string RXHRTnmea::ToString_curMode()
{
    if (info.curMode == BLANK_CHAR)
        return "";
    return CharToString(info.curMode);
}

string RXHRTnmea::ToString_curTask()
{
    if (info.curMode == BLANK_CHAR)
        return "";
    return CharToString(info.curMode);
}














//

