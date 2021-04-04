/*
 * NMEAutcTime.cpp
 *
 *  Created on: May 21, 2014
 *      Author: Alon Yaari
 */

#include "MBUtils.h"
#include "NMEAUtils.h"
#include "NMEAutcTime.h"
#include "stdlib.h"      //strtod

using namespace std;

utcTime::utcTime()
{
    Clear();
}

void utcTime::Clear()
{
    hour     = 0;
    minute   = 0;
    second   = 0.0;
    bBlank = false;
    bValid   = false;
}

bool utcTime::Set_utcTime(unsigned short int hour, unsigned short int minute, float second)
{
    bBlank = false;
    Set_hour(hour);
    Set_minute(minute);
    Set_second(second);
    return Validate();
}

bool utcTime::Set_utcTime(utcTime uT)
{
  unsigned short h, m;
  float s;
  uT.Get_utcHour(h);
  uT.Get_utcMinute(m);
  uT.Get_utcSecond(s);
  return Set_utcTime(h, m ,s);
}

bool utcTime::Set_utcTime(unsigned short int hour, unsigned short int minute, unsigned short int second)
{
    bBlank = false;
    Set_hour(hour);
    Set_minute(minute);
    Set_second(second);
    return Validate();
}

// Parses NMEA utc string into hour, minute, second
//      Minimum string is hhmmss
//      Other examples: hhmmss.   hhmmss.s   hhmmss.sssssssss
bool utcTime::Set_utcTimeFromNMEA(string strUTC)
{
    bBlank = strUTC.empty();
    if (bBlank)
        Set_blank();
    else {
        int strLen = strUTC.length();
        if (strLen < 6)
            Clear();
        else {
            CharsToUShort(hour, strUTC.at(1), strUTC.at(0));
            CharsToUShort(minute, strUTC.at(3), strUTC.at(2));
            string secs = strUTC.substr(4);
            second = strtod(secs.c_str(), 0);
            Validate(); } }
    return bValid;
}


bool utcTime::Set_utcTimeFromNMEAwithBackup(std::string strUTC, utcTime backup)
{
  if (!Set_utcTimeFromNMEA(strUTC)) {
    unsigned short h, m;
    backup.Get_utcHour(h);
    backup.Get_utcMinute(m);
    float s;
    backup.Get_utcSecond(s);
    Set_utcTime(h, m ,s);
    if (backup.IsBlank())
      backup.Set_blank();
    if (!backup.IsValid())
      backup.Set_Invalid(); }
  return true;
}

bool utcTime::Set_utcTimeFromMOOSTime(double dTime)
{
  if (dTime < 0.0)
    return false;
  struct tm *theTime;
  time_t aclock = dTime;
  struct tm *Now;
  Now = localtime(&aclock);
  Set_hour((unsigned int) Now->tm_hour);
  Set_minute((unsigned short int) Now->tm_min);
  double dSec = dTime - (double) ((int) dTime);
  float fSec  = (float) Now->tm_sec + (float) dSec;
  Set_second(fSec);
  return true;
}

// Get_utcTimeString()
// Create a string suitable for an NMEA sentence in the format hhmmss or hhmmss.s
//      INPUT:  timeStr                 string to put assembled UTC time into
//              numDigitsPastDecPoint   Number of digits past the decimal point in seconds
//      OUTPUT: "112233.44" where 11 is hour, 22 is minute, 33 is second, 44 is fraction of second
//              Outputs "" if bBlank = true
//      If numdigits is less than 1, no decimal point or fractions of seconds is added,
//      just seconds rounded to the nearest whole second.
//      ex:     hhmmss  hhmmss.s  hhmmss.sssssss
bool utcTime::Get_utcTimeString(string& timeStr, unsigned short numDigitsPastDecPoint)
{
    timeStr = "";
    if (!bBlank) {
        timeStr = UnsignedShortToStringWithTwoChars(hour);
        timeStr += UnsignedShortToStringWithTwoChars(minute);
        if (numDigitsPastDecPoint < 1)
            timeStr += UnsignedShortToStringWithTwoChars((unsigned short) (0.5 + second));
        else {
            if (second < 10.0)
                timeStr += "0";
            timeStr += floatToString(second, numDigitsPastDecPoint); } }
    return IsValid();
}

// Get_utcTimeIntOnlyString()
// Create a string suitable for an NMEA sentence in the format hhmmss
//      INPUT:  timeStr                 string to put assembled UTC time into
//      OUTPUT: "112233" where 11 is hour, 22 is minute, and 33 is second
//              Outputs "" if bBlank = true
bool utcTime::Get_utcTimeIntOnlyString(string& timeStr)
{
    return Get_utcTimeString(timeStr, 0);
}

bool utcTime::Get_utcHour(unsigned short& hr)
{
    if (bBlank)
        return false;
    hr = hour;
    return bValid;
}

bool utcTime::Get_utcMinute(unsigned short& min)
{
    if (bBlank)
        return false;
    min = minute;
    return bValid;
}

bool utcTime::Get_utcSecond(float& sec)
{
    if (bBlank)
        return false;
    sec = second;
    return sec;
}

bool utcTime::Validate()
{
    if (bBlank)
        bValid = true;
    else {
        bValid = false;
        if (hour < 24)
            if (minute < 60)
                if (second >= 0.0 && second < 60.0)
                    bValid = true; }
    return bValid;
}

void utcTime::Set_hour(unsigned short uiHour)
{
    bBlank = false;
    hour = uiHour;
}

void utcTime::Set_minute(unsigned short uiMin)
{
    bBlank = false;
    minute = uiMin;
}

void utcTime::Set_second(float fSec)
{
    bBlank = false;
    second = fSec;
}

void utcTime::Set_second(unsigned short uSec)
{
    bBlank = false;
    second = (double) uSec;
}















//
