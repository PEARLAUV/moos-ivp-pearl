/*
 * NMEAutcTime.h
 *
 *  Created on: May 21, 2014
 *      Author: Alon Yaari
 */

#ifndef NMEAUTCTIME_H_
#define NMEAUTCTIME_H_

#include <string>


class utcTime {

public:
                    utcTime();
                    ~utcTime() {};
    void            Clear();
    bool            IsValid() { return Validate(); };
    bool            IsBlank() { return bBlank; };
    void            Set_blank() { bBlank = true; bValid = true; };
    void            Set_Invalid() { bValid = false; };
    bool            Set_utcTime(utcTime uT);
    bool            Set_utcTime(unsigned short int hour, unsigned short int minute, float second);
    bool            Set_utcTime(unsigned short int hour, unsigned short int minute, unsigned short int second);
    bool            Set_utcTimeFromNMEA(std::string strUTC);
    bool            Set_utcTimeFromNMEAwithBackup(std::string strUTC, utcTime backup);
    bool            Set_utcTimeFromMOOSTime(double dMoosTime);
    bool            Get_utcTimeString(std::string& timeStr, unsigned short numDigitsPastDecPoint = 1);
    bool            Get_utcTimeIntOnlyString(std::string& timeStr);
    bool            Get_utcHour(unsigned short& hr);
    bool            Get_utcMinute(unsigned short& min);
    bool            Get_utcSecond(float& sec);

private:
    bool            Validate();
    void            Set_hour(unsigned short uiHour);
    void            Set_minute(unsigned short uiMin);
    void            Set_second(float fSec);
    void            Set_second(unsigned short uSec);

    unsigned short  hour;                       // Hour in 24-hour clock (valid is 0 to 23)
    unsigned short  minute;                     // Minute must be 0 to 59
    double          second;                     // Second must be > 0.0 and < 60.0
    bool            bBlank;                     // True when sentence should have blank space intentionally
    bool            bValid;
};
#endif
