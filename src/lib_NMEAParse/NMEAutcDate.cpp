/*
 * UTCdate.cpp
 *
 *  Created on: Feb 3, 2014
 *      Author: Alon Yaari
 */

#include <iostream>
#include "MBUtils.h"
#include "NMEAUtils.h"
#include "NMEAutcDate.h"

using namespace std;

utcDate::utcDate()
{
    Clear();
}

void utcDate::Clear()
{
    month    = 0;
    day      = 0;
    year     = 0;
    bBlank = false;
    bValid   = false;
}

bool utcDate::Set_utcDate(unsigned short day, unsigned short month, unsigned short year)
{
    bBlank = false;
    Set_day(day);
    Set_month(month);
    Set_year(year);
    return Validate();
}

// Set_utcDateFromNMEA()
//      INPUT: UTC Date in format ddmmyy
bool utcDate::Set_utcDateFromNMEA(std::string strUTC)
{
    // Blank string is allowed
    bBlank = strUTC.empty();
    if (bBlank) {
        Set_blank();
        return true; }

    // Validate that all characters are digits
    if (!isNumber(strUTC, false)) {
        bValid = false;
        return false; }

    // Must be 6 digits, 5 digits is ok, it means leading 0 got truncated
    int len = strUTC.length();
    switch (len) {
        case 5:
            strUTC = "0" + strUTC;
            // intentionally missing break statement
        case 6:
            unsigned short ui;
            CharsToUShort(ui, strUTC.at(1), strUTC.at(0));
            Set_day(ui);
            CharsToUShort(ui, strUTC.at(3), strUTC.at(2));
            Set_month(ui);
            CharsToUShort(ui, strUTC.at(5), strUTC.at(4));
            Set_year(ui);
            Validate();
            break;
        default:
            bValid = false; break; }
    return bValid;
}

// Get_utcDateString()
// Create a string suitable for an NMEA sentence in the format ddmmyy
//      INPUT:  dateStr                 string to put assembled UTC time into
// ex: ddmmyy  052714
bool utcDate::Get_utcDateString(string& dateStr)
{
  if (bBlank)
    dateStr = "";
  else {
    dateStr =  UnsignedShortToStringWithTwoChars(day);
    dateStr += UnsignedShortToStringWithTwoChars(month);
    dateStr += UnsignedShortToStringWithTwoChars(year); }
  return bValid;
}

bool utcDate::Validate()
{
    if (bBlank)
        bValid = true;
    else {
        bValid = false;
        if (day > 0 && day < 32)
            if (month > 0 && month < 13)
                if (year < 100)
                    bValid = true; }
    return bValid;
}

void utcDate::Set_day(unsigned short uiDay)
{
    bBlank = false;
    day = uiDay;
}

void utcDate::Set_month(unsigned short uiMonth)
{
    bBlank = false;
    month = uiMonth;
}

// StoreUTCDateYear()
//      - Can be 4 or 2 digit year
//      - Years between 100 and 1999 inclusive are considered errors
//      - Stores year as 4 digit
void utcDate::Set_year(unsigned short uiYear)
{
    bBlank = false;
    year = (uiYear % (unsigned short) 100);

}


void utcDate::Set_utcDateFromMOOSTime(double dTime)
{
  if (dTime < 0.0)
    return;
  struct tm *theTime;
  time_t aclock = dTime;
  struct tm *Now;
  Now = localtime(&aclock);
  Set_day((unsigned int) Now->tm_mday);
  Set_month((unsigned int) Now->tm_mon);
  Set_year((unsigned int) Now->tm_year);
  return;
}














//
