/*
 * NMEAparsing.cpp
 *
 *  Created on: Feb 3, 2014
 *      Author: Alon Yaari
 */


#include "iostream"
#include "NMEAparsing.h"
#include "stdlib.h"      //strtol

using namespace std;

// ParseInt()
//      - General function to convert string to integer
//      - Parses [strI] and stores value by reference in [i]
//      - Returns FALSE when string is empty
//      - Returns TRUE when some integer value was found
//      - NOTE: bad data (e.g., letters) will parse to 0 and return TRUE
bool ParseInt(int& i, string strI)
{
    bool bGood = (!strI.empty());
    if (bGood) {
        if (isNumber(strI)) {
            i = (int) strtol(strI.c_str(), NULL, 10); } }
    return bGood;
}

// ParseUInt()
//      - General function to convert string to unsigned integer
//      - Parses [strUI] and stores value by reference in [uI]
//      - Returns FALSE when string is empty or when value is negative
//      - Returns TRUE when a positive integer value was found
//      - NOTE: bad data (e.g., letters) will parse to 0 and return TRUE
bool ParseUInt(unsigned int& uI, string strUI)
{
    bool bGood = false;
    int i;
    if (ParseInt(i, strUI)) {
        if (i >= 0) {
            uI = (unsigned int) i;
            bGood = true; } }
    return bGood;
}

// ParseUShort()
//      - General function to convert string to unsigned short integer
//      - Parses [strUI] and stores value by reference in [uI]
//      - Returns FALSE when string is empty or when value is negative
//      - Returns TRUE when a positive integer value was found
//      - NOTE: bad data (e.g., letters) will parse to 0 and return TRUE
bool ParseUShort(unsigned short& uI, string strUI)
{
    bool bGood = false;
    int i;
    if (ParseInt(i, strUI)) {
        if (i >= 0) {
            uI = (unsigned int) i;
            bGood = true; } }
    return bGood;
}

// ParseIntBounded()
//      - General function to convert string to integer, within bounds
//      - Parses [strI] and stores value by reference in [i]
//      - Calls ParseInt()
//      - Also FALSE if value is outside [min]/[max] bounds
bool ParseIntBounded(int& i, string strI, int min, int max)
{
    bool bGood = ParseInt(i, strI);
    if (bGood)
        bGood = (i >= min && i <= max);
    return bGood;
}

// ParseIntBounded()
//      - General function to convert string to integer, within bounds
//      - Parses [strUI] and stores value by reference in [uI]
//      - Calls ParseUIntBounded()
//      - Also FALSE if value is outside [min]/[max] bounds
bool ParseUIntBounded(unsigned int& uI, string strUI, unsigned int min, unsigned int max)
{
    bool bGood = ParseUInt(uI, strUI);
    if (bGood)
        bGood = (uI >= min && uI <= max);
    return bGood;
}

// ParseDouble()
//      - General function to convert string to double
//      - Parses [strDbl] and stores value by reference in [dbl]
//      - Returns FALSE when string is empty
//      - Returns TRUE when a positive number (integer or double) value was found
//      - NOTE: bad data (e.g., letters) will parse to 0.0 and return TRUE
bool ParseDouble(double& dbl, string strDbl)
{
    bool bGood = (!strDbl.empty());
    if (bGood) {
        if (isNumber(strDbl)) {
            dbl = strtod(strDbl.c_str(), NULL); } }
    return bGood;
}

// ParseDoubleBounded()
//      - General function to convert string to double, within bounds
//      - Parses [strDbl] and stores value by reference in [dbl]
//      - Calls ParseDoubleBounded()
//      - Also FALSE if value is outside [min]/[max] bounds
bool ParseDoubleBounded(double& dbl, string strDbl, double min, double max)
{
    bool bGood = ParseDouble(dbl, strDbl);
    if (bGood)
        bGood = (dbl >= min && dbl <= max);
    return bGood;
}

bool ParseHeading(double& heading, string headingStr)
{
    bool bGood = ParseDoubleBounded(heading, headingStr, 0.0, 360.0);
    return bGood;
}

bool ParseSatNum(unsigned short& satNum, string satNumStr)
{
    unsigned int uI;
    bool bGood = ParseUIntBounded(uI, satNumStr, 0, 30);
    satNum = (bGood ? (unsigned short) uI : 0);
    return bGood;
}

bool ParseHDOP(double& hdop, string hdopStr)
{
    double d;
    bool bGood = ParseDoubleBounded(d, hdopStr, 0.5, 99.9);
    hdop = (bGood ? d : BLANK_DOUBLE);
    return bGood;
}

// ParseAlt()
//      Currently ignores the unitsStr, which can't be anything besides 'M'
//      Generic- does not check for maxes and mins (e.g., alt above msl -9999.99 to 999999.9)
bool ParseAlt(double& alt, string altStr, string unitsStr)
{
    if (unitsStr == "M")
        unitsStr = "M";
    double d;
    bool bGood = ParseDouble(d, altStr);
    alt = (bGood ? d : BLANK_DOUBLE);
    return bGood;
}

void ParseChar(char& c, string cStr)
{
    if (cStr.empty())
        c = BLANK_CHAR;
    else
        c = cStr.at(0);
}

bool ParsePlusMinus100(double& dVal, string dValString)
{
    bool bGood = ParseDoubleBounded(dVal, dValString, -100.0, 100.0);
    return bGood;
}

// GPSnmeaDDDMMtoDecDeg()
//      GPS stores lat as  ddmm.mmmmmmmm
//      GPS stores lon as dddmm.mmmmmmmmm
//      Real world uses decimal degrees, dd.dddddddddddd
//      Input:  double dddmmm   absolute value double where two digits to the left of the dec point and
//                              and digits to the right are minutes, everything to the left of
//                              minutes are degrees
//      Output: Number converted to DD.DDDDDDDDDDD
double GPSnmeaDDDMMtoDecDeg(const double dddmmm)
{                                                                    // double   dddmmm = 12345.67
    int iVal     = (int) dddmmm;                                    // int      iVal   = 12345
    int iMins    = iVal % 100;                                      // int      iMins  =    45
    double dMins = (double) iMins;                                  // double   dMins  =    45.00
    dMins        += ((double) dddmmm - (double) iVal);              //          dMins  =    45.67
    double dDegF = dMins / 60.0;                                    // double   dDegF  =     0.761167
    double dDegs = dddmmm - dMins;                                  // double   dDegs  = 12300.00
    dDegs        /= 100.0;                                          //          dDegs  =   123.00
    return (dDegs + dDegF);                                         // return              123.761167
}

// GPSnmeaDecDegToDDDMM()
//    Opposite of GPSnmeaDDDMMtoDecDeg
double GPSnmeaDecDegToDDDMM(const double ddd_mmm)
{
  int iVal     = (int) ddd_mmm * 100;       // Get just degrees, move it out a 2 places to make room for minutes
  double dMins = ddd_mmm - (double) iVal;   // Get the fraction of one degree representing the minutes
  dMins        *= 60;                       // Convert it to minutes
  return ((double) iVal + dMins);           // Add it to the minutes to form dddmm.mmmmmm
}
























//
