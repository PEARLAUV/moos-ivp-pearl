/*
 * XAMPLnmea.cpp
 *
 *  Created on: ENTER DATE HERE
 *      Author: ENTER AUTHOR NAME HERE
 *
 *  Based on code by:
 *      Alon Yaari  May, 2014
 */

/* To create a class for a new NMEA sentence:
        1.  Complete these steps only after completing the steps in the corresponding.h file
        2.  In this .cpp file, replace every 'XAMPL' with the new key (your editor's search and replace is ok to use)
        3.  Populate date and author name at the top of this file
        4.  In Section A, rewrite the section to invalidate the info struct
        5.  In Section B, copy the sentence definition from the .h file for convenient reference
        6.  In Section C, rewrite ParseSentenceIntoData() to parse an NMEA sentence string into the info struct
        7.  In Section D, rewrite CriticalDataAreValid() to call validation on critical sentence elements
        8.  In Section E, populate validation functions to match the Validate_ functions defined in the header
        9.  In Section F, populate the Get_ functions defined in the header
        10. In Section G, populate the Set_ functions defined in the header
        11. In Section H, populate ToString_ functions defined in the header
        12. In Section I, rewrite ProduceNMEASentence() to produce an output NMEA sentence string based on the info struct
 */


#include "XAMPLnmea.h"

using namespace std;


// A. Constructor
XAMPLnmea::XAMPLnmea()
{
    nmeaLen     = NUMELEM_XAMPL;
    SetKey("XAMPL");

    // Invalidate the info struct
    //      - Objective is that a newly instantiated item of this class should not evaluate to valid
    //      - Populate each info item to some value that won't evaluate as true
    //      - Some struct items may not have any values that would fail the Validate() function
    //      - It will be validated by code that calls to store an NMEA sentence or to populate
    //        with local data
    //      - For NMEA keys with nonstandard variants, it is ok to use "BLANK" values for items
    //        that may not get populated in all instances, usually at the end of sentences.
    info.status        = 'X';
    info.timeUTC.Set_Invalid();
    info.latGeog.Set_GeogType(GEOG_LAT);
    info.latGeog.SetInvalid();
    info.lonGeog.Set_GeogType(GEOG_LON);
    info.lonGeog.SetInvalid();
}

/* B. Sentence Definition
        $XAMPL,<1>,<2>,<3>,<4>,<5>,<6>*hh<CR><LF>
        <1>  UTC time, format hhmmss.s
        <2>  Status, A=Valid, V=Receiver warning
        <3>  Lat, format ddmm.mmmmm (with leading 0s)
        <4>  Lat hemisphere, N(+) or S(-)
        <5>  Lon, format dddmm.mmmmm (with leading 0s)
        <6>  Lon hemisphere, E(+) or W(-)
*/

// C. Parse incoming NMEA sentence

// Parse an incoming NMEA sentence string into the info buffer
//      - First call base class which populates inSentence struct
//      - Parse the elements of the inSentence.elements vector
bool XAMPLnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
    // Always call base class version of this function to validate basics
    //      and populate inSentence.elements
    if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
        return false;

    // This section is different for each NMEA sentence
    //  - Switch statement used for simplicity
    //  - Result of each case line is to convert data from string into info buffer type
    //  - Critical conversion failures can set bValid flag to false if bad data
    //  - Base class and NMEAUtils contain convenience converters for common NMEA elements
    //  - Sometimes more than one vector element combine to produce one info data point (e.g. lat)
    string strLat       = BLANK_DOUBLE_STRING;
    string strLon       = BLANK_DOUBLE_STRING;
    string strHemiLat   = BLANK_STRING;
    string strHemiLon   = BLANK_STRING;
    for (unsigned short i = 1; i < curSentence.numElements; i++) {
        string sVal = curSentence.elements.at(i);
        switch(i) {
            case 1: FromString_utcTime(sVal);                           break;
            case 2: FromString_status(sVal);                            break;
            case 3: strLat = sVal;                                      break;
            case 4: strHemiLat = sVal;                                  break;
            case 5: strLon = sVal;                                      break;
            case 6: strHemiLon = sVal;                                  break;
            default:                                                    break; } }
    FromString_latlon(strLat, strHemiLat, strLon, strHemiLon);
    return CriticalDataAreValid();
}

// D. Data validation for info buffer
bool XAMPLnmea::CriticalDataAreValid()
{
    bool bValid = Validate_utcTime();
    bValid &= Validate_status();
    bValid &= Validate_latlon();
    return bValid;
}

// E. Validation functions
//      - Many common data types have validations that can be called from NMEAUtils

bool XAMPLnmea::Validate_utcTime()
{
    return info.timeUTC.IsValid();
}

bool XAMPLnmea::Validate_status()
{
    return ValidCharInString(info.status, "AV", MAY_BE_BLANK, CASE_INSENSITIVE);
}

bool XAMPLnmea::Validate_latlon()
{
    return (info.latGeog.Validate() && info.lonGeog.Validate());
}


// F. Getters

// GetInfo()
// Populate curInfo with whatever is currently stored in the info struct
// Returns results of checking contents for validity; false means curInfo has unreliable data
bool XAMPLnmea::GetInfo(infoXAMPL& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool XAMPLnmea::Get_timeUTC(utcTime& t)
{
    t = info.timeUTC;
    return Validate_utcTime();
}

bool XAMPLnmea::Get_status(char& c)
{
    c = info.status;
    return Validate_status();
}

bool XAMPLnmea::Get_latlon(NMEAgeog& gLat, NMEAgeog& gLon)
{
    gLat = info.latGeog;
    gLon = info.lonGeog;
    return Validate_latlon();
}

bool XAMPLnmea::Get_latlonValues(double& dLat, double& dLon)
{
    if (info.latGeog.Get_ValueAsDouble(dLat)
     && info.lonGeog.Get_ValueAsDouble(dLon))
        return Validate_latlon();
    return false;
}

// G. Setters

bool XAMPLnmea::Set_utcTime(const utcTime t)
{
    info.timeUTC = t;
    return Validate_utcTime();
}

bool XAMPLnmea::Set_utcTime(const unsigned short int hour, const unsigned short int minute, const float second)
{
    return info.timeUTC.Set_utcTime(hour, minute, second);
}

bool XAMPLnmea::Set_status(const char c)
{
    info.status = c;
    return Validate_status();
}

bool XAMPLnmea::Set_latlon(const NMEAgeog gLat, const NMEAgeog gLon)
{
    info.latGeog = gLat;
    info.lonGeog = gLon;
    return Validate_latlon();
}

bool XAMPLnmea::Set_latlonValues(const double dLat, const double dLon)
{
    info.latGeog.StoreDouble(dLat);
    info.lonGeog.StoreDouble(dLon);
    return Validate_latlon();
}

// H. FromString_ functions

bool XAMPLnmea::FromString_utcTime(string sVal)
{
    info.timeUTC.Set_utcTimeFromNMEA(sVal);     // Set_utcTimeFromNMEA() also handles blank strings
    return Validate_utcTime();
}

bool XAMPLnmea::FromString_status(string sVal)
{
    StoreCharFromNMEAstring(info.status, sVal);
    return Validate_status();
}

bool XAMPLnmea::FromString_latlon(string strLat, string strLatHemi, string strLon, string strLonHemi)
{
    // NMEAgeog::ParseFromNMEAstring() also handles blank strings
    info.latGeog.ParseFromNMEAstring(strLat, strLatHemi);
    info.lonGeog.ParseFromNMEAstring(strLon, strLonHemi);
    return Validate_latlon();
}

// I. Create an NMEA sentence from data stored in info buffer

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool XAMPLnmea::ProduceNMEASentence(string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $XAMPL and postpend *ch (ch = checksum)
    string dataBody = "";
    dataBody += ToString_utcTime();
    dataBody += ",";
    dataBody += ToString_status();
    dataBody += ",";
    dataBody += ToString_lat();
    dataBody += ",";
    dataBody += ToString_latHemi();
    dataBody += ",";
    dataBody += ToString_lon();
    dataBody += ",";
    dataBody += ToString_lonHemi();

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    newSentence = curSentence.nmeaSentence;
    return true;
}

// J. Produce strings for NMEA sentences
//      Note: Each part of an NMEA sentence has its own string producer.
//            Even coupled data such as latitude (which is stored as a value-hemisphere pair)
//            requires one producer for the value and one for the hemisphere character.
//            This is for convenient bookkeeping when assembling the full sentence.

string XAMPLnmea::ToString_utcTime()
{
    string sVal;
    if (!info.timeUTC.Get_utcTimeString(sVal, 2))
        sVal = "";
    return sVal;
}

string XAMPLnmea::ToString_status()
{
    if (info.status == BLANK_CHAR)
        return "";
    return CharToString(info.status);
}

string XAMPLnmea::ToString_lat()
{
    string sVal;
    if (!info.latGeog.Get_ValueString(sVal))
        return "";
    return sVal;
}

string XAMPLnmea::ToString_latHemi()
{
    string sVal;
    if (!info.latGeog.Get_HemisphereString(sVal))
        return "";
    return sVal;
}

string XAMPLnmea::ToString_lon()
{
    string sVal;
    if (!info.lonGeog.Get_ValueString(sVal))
        return "";
    return sVal;
}

string XAMPLnmea::ToString_lonHemi()
{
    string sVal;
    if (!info.lonGeog.Get_HemisphereString(sVal))
        return "";
    return sVal;
}

















//
