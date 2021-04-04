/*
 * CPRBSnmea.cpp
 *
 *  Created on: Dec 31, 2013
 *      Author: yar
 */

#include "CPRBSnmea.h"

using namespace std;

CPRBSnmea::CPRBSnmea()
{
    nmeaLen     = NUMELEM_CPRBS;
    SetKey("CPRBS");

    // Invalidate info struct
    info.timestamp.Set_Invalid();
    info.battID             = BAD_USHORT;
    info.battStackVoltage   = BAD_DOUBLE;
    info.battMin            = BAD_DOUBLE;
    info.battMax            = BAD_DOUBLE;
    info.tempC              = BAD_DOUBLE;
}

//      $CPRBS,<1>,<2>,<3>,<4>,<5>,<6>*hh<CR><LF>
//      <1> <1>  [Timestamp]    Timestamp of the sentence
//      <2> < >  [ID_Battery]   Unique ID number of the battery being reported on
//      <3> <2>  [V_Batt_Stack] Voltage of the battery bank
//      <4> <3>  [V_Batt_Min]   Lowest voltage read from cells in the bank
//      <5> <4>  [V_Batt_Max]   Highest voltage read from the cells in the bank
//      <6> <5>  [TemperatureC] Temperature of the battery pack in C
//      NOTE
//          As of Aug 2013, this message is being published with no [ID_BATTERY]
//          and a 0 value published for [TEMPERATUREC]
//      example: $CPRBS,172909.322,15.121597,15.121597,15.121597,0*76

// CPRBSnmea::ParseSentenceIntoData()
//      Break apart incoming sentence into its parts
//      Returns TRUE  if valid NMEA sentence and checksum
//          - Still TRUE even if data is all blank or not enough items
//      Returns FALSE otherwise
bool CPRBSnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
    // Always call base class version of this function to validate basics
    //      and populate inSentence.elements
    if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
        return false;

    if (curSentence.numElements == 5) {
        for (unsigned short i = 1; i < curSentence.numElements; i++) {
            string sVal = curSentence.elements.at(i);
            switch(i) {
                case 1:         FromString_timestamp(sVal);                 break;
                case 2:         FromString_battStackVoltage(sVal);          break;
                case 3:         FromString_battMin(sVal);                   break;
                case 4:         FromString_battMax(sVal);                   break;
                case 5:         FromString_tempC(sVal);                     break;
                default:                                                    break; } } }
    else {
        for (unsigned short i = 1; i < curSentence.numElements; i++) {
            string sVal = curSentence.elements.at(i);
            switch(i) {
                case 1:         FromString_timestamp(sVal);                 break;
                case 2:         FromString_battID(sVal);                    break;
                case 3:         FromString_battStackVoltage(sVal);          break;
                case 4:         FromString_battMin(sVal);                   break;
                case 5:         FromString_battMax(sVal);                   break;
                case 6:         FromString_tempC(sVal);                     break;
                default:                                                    break; } } }
    return CriticalDataAreValid();
}

bool CPRBSnmea::CriticalDataAreValid()
{
    bool bValid = Validate_timestamp();
    bValid &= Validate_battStackVoltage();
    bValid &= Validate_battMin();
    bValid &= Validate_battMax();
    return bValid;
}

bool CPRBSnmea::Validate_timestamp()
{
    if(!info.timestamp.IsValid()) {
        AddError("Invalid timestamp.");
        return false; }
    return true;
}

bool CPRBSnmea::Validate_battID()
{
    if (info.battID == BAD_USHORT) {
        errorBuild.clear();
        errorBuild << "Bad battery ID " << (int) info.battID;
        AddError(errorBuild.str());
        return false; }
    return true;
}

bool CPRBSnmea::Validate_battStackVoltage()
{
    bool bGood = IsValidBoundedDouble(info.battStackVoltage, 0.0, 25.0, true, true);
    if (!bGood) {
        errorBuild.clear();
        errorBuild << "Bad battery stack voltage " << info.battStackVoltage;
        AddError(errorBuild.str()); }
    return bGood;
}

bool CPRBSnmea::Validate_battMin()
{
    return IsValidBoundedDouble(info.battMin, 0.0, 25.0, true, true);
}

bool CPRBSnmea::Validate_battMax()
{
    return IsValidBoundedDouble(info.battMax, 0.0, 25.0, true, true);
}

bool CPRBSnmea::Validate_tempC()
{
    return IsValidBoundedDouble(info.battMin, 0.0, 100.0, true, true);
}

bool CPRBSnmea::GetInfo(infoCPRBS& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool CPRBSnmea::Get_timestamp(utcTime& t)
{
    t = info.timestamp;
    return Validate_timestamp();
}

bool CPRBSnmea::Get_battID(unsigned short& d)
{
    d = info.battID;
    return Validate_battID();
}

bool CPRBSnmea::Get_battStackVoltage(double& d)
{
    d = info.battStackVoltage;
    return Validate_battStackVoltage();
}

bool CPRBSnmea::Get_battMin(double& d)
{
    d = info.battMin;
    return Validate_battMin();
}

bool CPRBSnmea::Get_battMax(double& d)
{
    d = info.battMax;
    return Validate_battMax();
}

bool CPRBSnmea::Get_tempC(double& d)
{
    d = info.tempC;
    return Validate_tempC();
}

bool CPRBSnmea::Set_timestamp(const utcTime t)
{
    info.timestamp = t;
    if (!Validate_timestamp()) {
        AddError("Could not set timestamp.");
        return false; }
    return true;
}

bool CPRBSnmea::Set_timestamp(const unsigned short int hour, const unsigned short int minute, const float second)
{
    if (!info.timestamp.Set_utcTime(hour, minute, second)) {
        errorBuild.clear();
        errorBuild << "Could not set timestamp (hour=" << (int) hour << ", minute=" << (int) minute << ", second=" << second << ")";
        AddError(errorBuild.str());
        return false; }
    return true;
}

bool CPRBSnmea::Set_battID(const unsigned short& d)
{
    info.battID = d;
    if (!Validate_battID()) {
        errorBuild.clear();
        errorBuild << "Could not set battery ID to " << (int) d;
        AddError(errorBuild.str());
        return false; }
    return true;
}

bool CPRBSnmea::Set_battStackVoltage(const double& d)
{
    info.battStackVoltage = d;
    if (!Validate_battStackVoltage()) {
        errorBuild.clear();
        errorBuild << "Could not set battery stack voltage to " << d;
        AddError(errorBuild.str());
        return false; }
    return true;
}

bool CPRBSnmea::Set_battMin(const double& d)
{
    info.battMin = d;
    if (!Validate_battMin()) {
        errorBuild.clear();
        errorBuild << "Could not set battery minimum voltage to " << d;
        AddError(errorBuild.str());
        return false; }
    return true;
}

bool CPRBSnmea::Set_battMax(const double& d)
{
    info.battMax = d;
    if (!Validate_battMax()) {
        errorBuild.clear();
        errorBuild << "Could not set battery maximum voltage to " << d;
        AddError(errorBuild.str());
        return false; }
    return true;
}

bool CPRBSnmea::Set_tempC(const double& d)
{
    info.tempC = d;
    if (!Validate_tempC()) {
        errorBuild.clear();
        errorBuild << "Could not set battery temperature to " << d;
        AddError(errorBuild.str());
        return false; }
    return true;
}

bool CPRBSnmea::FromString_timestamp(std::string sVal)
{
    info.timestamp.Set_utcTimeFromNMEA(sVal);
    return Validate_timestamp();
}

bool CPRBSnmea::FromString_battID(std::string sVal)
{
    if (sVal.empty())
        info.battID = BLANK_USHORT;
    ParseUShort(info.battID, sVal);
    return Validate_battID();
}

bool CPRBSnmea::FromString_battStackVoltage(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.battStackVoltage, sVal);
    return Validate_battStackVoltage();
}

bool CPRBSnmea::FromString_battMin(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.battMin, sVal);
    return Validate_battMin();
}

bool CPRBSnmea::FromString_battMax(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.battMax, sVal);
    return Validate_battMax();
}

bool CPRBSnmea::FromString_tempC(std::string sVal)
{
    StoreDoubleFromNMEAstring(info.tempC, sVal);
    return Validate_tempC();
}

bool CPRBSnmea::ProduceNMEASentence(std::string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;
    string dataBody = "";
    dataBody += ToString_timestamp();
    dataBody +=",";
    if (Validate_battID()) {
        dataBody += ToString_battID();
        dataBody += ","; }
    dataBody += ToString_battStackVoltage();
    dataBody +=",";
    dataBody += ToString_battMin();
    dataBody +=",";
    dataBody += ToString_battMax();
    dataBody +=",";
    dataBody += ToString_tempC();

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    return true;
}

std::string CPRBSnmea::ToString_timestamp()
{
    string sVal;
    if (!info.timestamp.Get_utcTimeString(sVal, 2))
        sVal = "";
    return sVal;
}

std::string CPRBSnmea::ToString_battID()
{
    return TwoDigitsToString((int) info.battID);
}

std::string CPRBSnmea::ToString_battStackVoltage()
{
    if (info.battStackVoltage == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.battStackVoltage, 3, 2);
}

std::string CPRBSnmea::ToString_battMin()
{
    if (info.battMin == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.battMin, 3, 2);
}

std::string CPRBSnmea::ToString_battMax()
{
    if (info.battMax == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.battMax, 3, 2);
}

std::string CPRBSnmea::ToString_tempC()
{
    if (info.tempC == BLANK_DOUBLE)
        return "";
    return FormatPosDouble(info.tempC, 3, 2);
}










//
