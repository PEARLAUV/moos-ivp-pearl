/*
 * PASHRnmea.cpp
 *
 *  Created on: Aug 15, 2014
 *      Author: Alon Yaari
 */

#include "PASHRnmea.h"

using namespace std;


// A. Constructor
PASHRnmea::PASHRnmea()
{
    nmeaLen     = NUMELEM_PASHR;
    SetKey("PASHR");
    info.timeUTC.Set_Invalid();
    info.heading       = BAD_DOUBLE;
    info.headingRel    = BAD_CHAR;
    info.roll          = BAD_DOUBLE;
    info.pitch         = BAD_DOUBLE;
    info.heave         = BAD_DOUBLE;
    info.rollStdDev    = BAD_DOUBLE;
    info.pitchStdDev   = BAD_DOUBLE;
    info.headingStdDev = BAD_DOUBLE;
    info.qualityFlag   = BAD_CHAR;
}

//      PASHR - Clearpath Wire Protocol Battery Status Message
//
//      $PASHR,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>,<9>,<10>*hh<CR><LF>
//      <1>  [UTCtime]      UTC Timestamp of the sentence
//      <2>  [Heading]      Heading in degrees
//      <3>  [Heading_rel]  'T' disolayed if heading is relative to true north
//      <4>  [Roll]         Roll in decimal degrees
//      <5>  [Pitch]        Pitch in decimal degrees
//      <6>  [Heave]        Heave in meters
//      <7>  [Roll_stdDev]  Standard deviation of roll in decimal degrees
//      <8>  [Pitch_stdDev] Standard deviation of pitch in decimal degrees
//      <9>  [Heading_stdDev] Standard deviation of heading in decimal degrees
//      <10> [Quality]      Quality flag (0 = no position, 1 = non-RTK fixed integer pos, 2 = RTK fixed integer pos)
//      example: $PASHR,190841.80,69.38,T,5.35,-1.42,0.01,0.504,0.504,0.552,1*2E


bool PASHRnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
    // Always call base class version of this function to validate basics
    //      and populate inSentence.elements
    if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
        return false;

    for (unsigned short i = 1; i < curSentence.numElements; i++) {
        string sVal = curSentence.elements.at(i);
        switch(i) {
            case 1:  FromString_timestamp(sVal);                         break;
            case 2:  FromString_heading(sVal);                           break;
            case 3:  FromString_headingRel(sVal);                        break;
            case 4:  FromString_roll(sVal);                              break;
            case 5:  FromString_pitch(sVal);                             break;
            case 6:  FromString_heave(sVal);                             break;
            case 7:  FromString_rollStdDev(sVal);                        break;
            case 8:  FromString_pitchStdDev(sVal);                       break;
            case 9:  FromString_headingStdDev(sVal);                     break;
            case 10: FromString_qualityFlag(sVal);                       break;
            default:                                                     break; } }
    return CriticalDataAreValid();
}

bool PASHRnmea::CriticalDataAreValid()
{
    bool bValid = Validate_timestamp();
    bValid &= Validate_heading();
    bValid &= Validate_roll();
    bValid &= Validate_pitch();
    return bValid;
}

bool PASHRnmea::Validate_timestamp()
{
    return info.timeUTC.IsValid();
}

bool PASHRnmea::Validate_heading()
{
    bool bGood = IsValidBoundedDouble(info.heading, 0.0, 360.0, INCLUSIVE_YES, MAY_NOT_BE_BLANK);
    if (!bGood) {
        errorBuild << "Invalid heading '" << info.heading << "'";
        AddError(errorBuild.str()); }
    return bGood;
}

bool PASHRnmea::Validate_headingRel()
{
    bool bGood = ValidCharInString(info.headingRel, "T", MAY_BE_BLANK, CASE_INSENSITIVE);
    if (!bGood) {
        errorBuild << "Invalid relative heading designator '" << info.headingRel << "'";
        AddError(errorBuild.str()); }
    return bGood;
}

bool PASHRnmea::Validate_roll()
{
    bool bGood = IsValidBoundedDouble(info.roll, -180.0, 180.0, INCLUSIVE_YES, MAY_BE_BLANK);
    if (!bGood) {
        errorBuild << "Invalid roll '" << info.roll << "'";
        AddError(errorBuild.str()); }
    return bGood; }

bool PASHRnmea::Validate_pitch()
{
    bool bGood = IsValidBoundedDouble(info.pitch, -180.0, 180.0, INCLUSIVE_YES, MAY_BE_BLANK);
    if (!bGood) {
        errorBuild << "Invalid pitch '" << info.pitch << "'";
        AddError(errorBuild.str()); }
    return bGood; }

bool PASHRnmea::Validate_heave()
{
    bool bGood = IsValidBoundedDouble(info.heave, -180.0, 180.0, INCLUSIVE_YES, MAY_BE_BLANK);
    if (!bGood) {
        errorBuild << "Invalid heave '" << info.heave << "'";
        AddError(errorBuild.str()); }
    return bGood; }

bool PASHRnmea::Validate_rollStdDev()
{
    bool bGood = IsValidBoundedDouble(info.rollStdDev, -180.0, 180.0, INCLUSIVE_YES, MAY_BE_BLANK);
    if (!bGood) {
        errorBuild << "Invalid roll standard deviation '" << info.rollStdDev << "'";
        AddError(errorBuild.str()); }
    return bGood; }

bool PASHRnmea::Validate_pitchStdDev()
{
    bool bGood = IsValidBoundedDouble(info.pitchStdDev, -180.0, 180.0, INCLUSIVE_YES, MAY_BE_BLANK);
    if (!bGood) {
        errorBuild << "Invalid pitch standard deviation '" << info.pitchStdDev << "'";
        AddError(errorBuild.str()); }
    return bGood; }

bool PASHRnmea::Validate_headingStdDev()
{
    bool bGood = IsValidBoundedDouble(info.headingStdDev, -180.0, 180.0, INCLUSIVE_YES, MAY_BE_BLANK);
    if (!bGood) {
        errorBuild << "Invalid pitch standard deviation '" << info.headingStdDev << "'";
        AddError(errorBuild.str()); }
    return bGood; }

bool PASHRnmea::Validate_qualityFlag()
{
    bool bGood = ValidCharInString(info.qualityFlag, "012", MAY_BE_BLANK, CASE_INSENSITIVE);
    if (!bGood) {
        errorBuild << "Invalid quality flag '" << info.qualityFlag << "'";
        AddError(errorBuild.str()); }
    return bGood;
}

bool PASHRnmea::GetInfo(infoPASHR& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool PASHRnmea::Get_timestamp(utcTime& t)
{
    t = info.timeUTC;
    return Validate_timestamp();
}

bool PASHRnmea::Get_heading(double& d)
{
    d = info.heading;
    return Validate_heading();
}

bool PASHRnmea::Get_headingRel(char& c)
{
    c = info.headingRel;
    return Validate_headingRel();
}

bool PASHRnmea::Get_roll(double& d)
{
    d = info.roll;
    return Validate_roll();
}

bool PASHRnmea::Get_pitch(double& d)
{
    d = info.pitch;
    return Validate_pitch();
}

bool PASHRnmea::Get_heave(double& d)
{
    d = info.heave;
    return Validate_heave();
}

bool PASHRnmea::Get_rollStdDev(double& d)
{
    d = info.rollStdDev;
    return Validate_rollStdDev();
}

bool PASHRnmea::Get_pitchStdDev(double& d)
{
    d = info.pitchStdDev;
    return Validate_pitchStdDev();
}

bool PASHRnmea::Get_headingStdDev(double& d)
{
    d = info.headingStdDev;
    return Validate_headingStdDev();
}

bool PASHRnmea::Get_qualityFlag(char& c)
{
    c = info.qualityFlag;
    return Validate_qualityFlag();
}

bool PASHRnmea::Set_timestamp(const utcTime t)
{
    info.timeUTC = t;
    return Validate_timestamp();
}

bool PASHRnmea::Set_timestamp(const unsigned short int hour, const unsigned short int minute, const float second)
{
    return info.timeUTC.Set_utcTime(hour, minute, second);
}

bool PASHRnmea::Set_heading(const double& d)
{
    info.heading = d;
    return Validate_heading();
}

bool PASHRnmea::Set_headingRel(const char& c)
{
    info.headingRel = c;
    return Validate_headingRel();
}

bool PASHRnmea::Set_roll(const double& d)
{
    info.roll = d;
    return Validate_roll();
}

bool PASHRnmea::Set_pitch(const double& d)
{
    info.pitch = d;
    return Validate_pitch();
}

bool PASHRnmea::Set_heave(const double& d)
{
    info.heave = d;
    return Validate_heave();
}

bool PASHRnmea::Set_rollStdDev(const double& d)
{
    info.rollStdDev = d;
    return Validate_rollStdDev();
}

bool PASHRnmea::Set_pitchStdDev(const double& d)
{
    info.pitchStdDev = d;
    return Validate_pitchStdDev();
}

bool PASHRnmea::Set_headingStdDev(const double& d)
{
    info.headingStdDev = d;
    return Validate_headingStdDev();
}

bool PASHRnmea::Set_qualityFlag(const char& c)
{
    info.qualityFlag = c;
    return Validate_qualityFlag();
}

bool PASHRnmea::FromString_timestamp(string sVal)
{
    info.timeUTC.Set_utcTimeFromNMEA(sVal);     // Set_utcTimeFromNMEA() also handles blank strings
    return Validate_timestamp();
}

bool PASHRnmea::FromString_heading(string sVal)
{
    StoreDoubleFromNMEAstring(info.heading, sVal);
    return Validate_heading();
}

bool PASHRnmea::FromString_headingRel(string sVal)
{
    StoreCharFromNMEAstring(info.headingRel, sVal);
    return Validate_headingRel();
}

bool PASHRnmea::FromString_roll(string sVal)
{
    StoreDoubleFromNMEAstring(info.roll, sVal);
    return Validate_roll();
}

bool PASHRnmea::FromString_pitch(string sVal)
{
    StoreDoubleFromNMEAstring(info.pitch, sVal);
    return Validate_pitch();
}

bool PASHRnmea::FromString_heave(string sVal)
{
    StoreDoubleFromNMEAstring(info.heave, sVal);
    return Validate_heave();
}

bool PASHRnmea::FromString_rollStdDev(string sVal)
{
    StoreDoubleFromNMEAstring(info.rollStdDev, sVal);
    return Validate_rollStdDev();
}

bool PASHRnmea::FromString_pitchStdDev(string sVal)
{
    StoreDoubleFromNMEAstring(info.pitchStdDev, sVal);
    return Validate_pitchStdDev();
}

bool PASHRnmea::FromString_headingStdDev(string sVal)
{
    StoreDoubleFromNMEAstring(info.pitchStdDev, sVal);
    return Validate_pitchStdDev();
}

bool PASHRnmea::FromString_qualityFlag(string sVal)
{
    StoreCharFromNMEAstring(info.qualityFlag, sVal);
    return Validate_qualityFlag();
}

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool PASHRnmea::ProduceNMEASentence(string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $PASHR and postpend *ch (ch = checksum)
    string dataBody = "";
    dataBody += ToString_timestamp();
    dataBody += ",";
    dataBody += ToString_heading();
    dataBody += ",";
    dataBody += ToString_headingRel();
    dataBody += ",";
    dataBody += ToString_roll();
    dataBody += ",";
    dataBody += ToString_pitch();
    dataBody += ",";
    dataBody += ToString_heave();
    dataBody += ",";
    dataBody += ToString_rollStdDev();
    dataBody += ",";
    dataBody += ToString_pitchStdDev();
    dataBody += ",";
    dataBody += ToString_headingStdDev();
    dataBody += ",";
    dataBody += ToString_qualityFlag();

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    newSentence = curSentence.nmeaSentence;
    return true;
}

string PASHRnmea::ToString_double(const double& d)
{    if (d == BLANK_DOUBLE || d == BAD_DOUBLE)
        return "";
    return doubleToString(d, 2);
}

string PASHRnmea::ToString_timestamp()
{
    string sVal;
    if (!info.timeUTC.Get_utcTimeString(sVal, 2))
        sVal = "";
    return sVal;
}

string PASHRnmea::ToString_heading()
{
    return ToString_double(info.heading);
}

string PASHRnmea::ToString_headingRel()
{
    char c = info.headingRel;
    if (c == BLANK_CHAR || c == BAD_CHAR)
        return "";
    return CharToString(c);
}

string PASHRnmea::ToString_roll()
{
    return ToString_double(info.roll);
}

string PASHRnmea::ToString_pitch()
{
    return ToString_double(info.pitch);
}

string PASHRnmea::ToString_heave()
{
    return ToString_double(info.heave);
}

string PASHRnmea::ToString_rollStdDev()
{
    return ToString_double(info.rollStdDev);
}

string PASHRnmea::ToString_pitchStdDev()
{
    return ToString_double(info.pitchStdDev);
}

string PASHRnmea::ToString_headingStdDev()
{
    return ToString_double(info.headingStdDev);
}

string PASHRnmea::ToString_qualityFlag()
{
    char c = info.qualityFlag;
    if (c == BLANK_CHAR || c == BAD_CHAR)
        return "";
    return CharToString(c);
}

















//


