/*
 * NMEAgeog.cpp
 *
 *  Created on: May 30, 2014
 *      Author: yar
 */


#include <sstream>
#include <iostream>
#include <iomanip>
#include "MBUtils.h"
#include "NMEAgeog.h"
#include "NMEAparsing.h"
#include "stdlib.h"      //strtod

using namespace std;

NMEAgeog::NMEAgeog()
{
    Clear();
}

void NMEAgeog::Clear()
{
    geogType      = GEOG_UNK;
    doubleDD      = BAD_DOUBLE;
    strValue      = BAD_DOUBLE_STRING;
    strHemisphere = BAD_CHAR;
    bIsBlank      = false;
    bValid        = false;
}

// Validate()
//      Validates that the doubleDD value is within spec for the geogType
bool NMEAgeog::Validate()
{
    double minVal = BLANK_DOUBLE;
    double maxVal = BLANK_DOUBLE;

    if (!bValid)
        return false;

    if (bIsBlank)
        return true;        // Blank value is valid

    switch (geogType) {
        case GEOG_LAT:
        case GEOG_LATDD:
            minVal = -90.0;
            maxVal =  90.0;
            break;
        case GEOG_LON:
        case GEOG_LONDD:
        case GEOG_MAG:
            minVal = -180.0;
            maxVal =  180.0;
            break;
        default:
            bValid = false;
            break; }

    bValid = WithinBounds(doubleDD, maxVal, minVal);
    return bValid;
}

bool NMEAgeog::ParseFromNMEAstring(const string strNumber, const string strHemi)
{
    if (!Valid_GeogType())
        return false;
    if (strNumber.empty() || strHemi.empty()) {
        doubleDD      = BLANK_DOUBLE;
        strHemisphere = BLANK_CHAR;
        strValue      = BLANK_DOUBLE_STRING;
        bIsBlank      = true;
        bValid        = true;
        return true; }

    ParseChar(strHemisphere, strHemi);      // Store the hemisphere as a char
    strValue = strNumber;                   // Store the input value string
    double sign = (ValidCharInString(strHemisphere, "WS", false, true) ? -1.0 : 1.0);   // W and S are negative, otherwise positive

    // Store the numerical value as a signed double
    double dVal;
    switch (geogType) {
        case GEOG_MAG:
            doubleDD = strtod(strValue.c_str(), 0) * sign;
            break;
        case GEOG_LAT:
        case GEOG_LON:
            doubleDD = GPSnmeaDDDMMtoDecDeg(strtod(strValue.c_str(), 0)) * sign;
            break;
        case GEOG_LATDD:
        case GEOG_LONDD:
            dVal = strtod(strValue.c_str(), 0);
            if (dVal < 0.0)
                dVal *= -1.0;
            doubleDD = dVal * sign;
            break;
        default:
            doubleDD = BAD_DOUBLE;
            strValue = BAD_DOUBLE_STRING;
            strHemisphere = BAD_CHAR;
            break; }
    return Validate();
}

bool NMEAgeog::ParseFromNMEAstring(unsigned short typeOfGeog, const string strNumber, const string strHemi)
{
    if (!Set_GeogType(typeOfGeog))
        return false;
    return ParseFromNMEAstring(strNumber, strHemi);
}

// StoreDouble()
//      Input is a double value to store
//      Stores the double
//      Also determines and stores the hemisphere and doubleDD value (which is absolute value)
bool NMEAgeog::StoreDouble(double d)
{
    if (!Valid_GeogType())
        return false;
    bool bPos = d >= 0.0;
    doubleDD = (bPos ? d : -1.0 * d);
    switch (geogType) {
        case GEOG_LAT:
            strHemisphere = (bPos ? 'N' : 'S');
            DoubleDDtoStrValue();
            break;
        case GEOG_LON:
        case GEOG_MAG:
            strHemisphere = (bPos ? 'E' : 'W');
            DoubleDDtoStrValue();
            break;
        case GEOG_LATDD:
            strHemisphere = (bPos ? 'N' : 'S');
            strValue = doubleToString(d, 6);
            break;
        case GEOG_LONDD:
            strHemisphere = (bPos ? 'E' : 'W');
            strValue = doubleToString(d, 6);
            break;
        case GEOG_UNK:
        default:
            bValid = false;
            return false;
            break; }
    return Validate();
}

bool NMEAgeog::StoreDouble(unsigned short typeOfGeog, double d)
{
    if (!Set_GeogType(typeOfGeog))
        return false;
    return StoreDouble(d);
}

bool NMEAgeog::Get_ValueAsDouble(double& gVal)
{
    if (bValid)
        gVal = doubleDD;
    return bValid;
}

bool NMEAgeog::Get_ValueString(string& str)
{
    if (bValid) {
        if (bIsBlank)
            str = "";
        else
            str = strValue; }
    return bValid;
}

bool NMEAgeog::Get_HemisphereString(string& str)
{
    if (bValid) {
        if (bIsBlank)
            str = "";
        else
            str = strHemisphere; }
    return bValid;
}

bool NMEAgeog::Valid_GeogType()
{
    bValid = geogType > GEOG_UNK && geogType < GEOG_BAD;
    return bValid;
}

bool NMEAgeog::Set_GeogType(unsigned short typeOfGeog)
{
    geogType = typeOfGeog;
    return Valid_GeogType();
}

// DoubleDDtoStrValue()
//      Format the strValue string
//      - Always positive, no negative or positive signs
//      - Format: ddmm.mmmmmm or ddmm.mmmmmm where d=degrees and m=minutes
//      - Must have leading zeros
//      - Decimal Degrees has the minutes and seconds rolled up into a fraction of a degree
//      - Need to convert the fraction of the degree into minutes only
//          - Isolate the fractional part (right side of the decimal point)
//          - Multiply by 60 to get the number of degrees

void NMEAgeog::DoubleDDtoStrValue()
{
                                                      // double doubleDD =   123.456
    int intVal     = (int) doubleDD;                  // int    intVal   =   123
    double degFrac = doubleDD - (double) intVal;      // double degFrac  =     0.456
    double mins    = degFrac * 60.0;                  // double mins     =    27.36
    double ddd00   = (double) intVal * 100.0;         // double ddd00    = 12300
    double dddmm   = ddd00 + mins;                    // double dddmm    = 12327.36
    int beforeDecPoint;
    bool sign = (doubleDD > 0.0);
    switch (geogType) {
            case GEOG_LAT:
                beforeDecPoint = 4;
                break;
            case GEOG_LON:
                beforeDecPoint = 5;
                break;
            case GEOG_MAG:
                beforeDecPoint = 2;
                break;
            default:
                bValid = false;
                break; }
    strValue = FormatDouble(dddmm, beforeDecPoint, 6);
}




























//
