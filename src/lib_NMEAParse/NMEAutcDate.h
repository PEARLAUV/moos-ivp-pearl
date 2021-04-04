/*
 * NMEAutcDate.h
 *
 *  Created on: May 21, 2014
 *      Author: Alon Yaari
 */

#ifndef NMEAUTCDATE_H_
#define NMEAUTCDATE_H_

#include <string>


class utcDate {

public:
                    utcDate();
                    ~utcDate() {};
    void            Clear();
    bool            IsValid()   { return Validate(); };
    bool            IsBlank()   { return bBlank; };
    void            Set_blank() { bBlank = true; bValid = true; };
    void            Set_Invalid() { bValid = false; };
    bool            Set_utcDate(unsigned short day, unsigned short month, unsigned short year);
    bool            Set_utcDateFromNMEA(std::string strUTC);
    void            Set_utcDateFromMOOSTime(double dTime);
    bool            Get_utcDateString(std::string& dateStr);
    bool            Get_utcDay(unsigned short& d);
    bool            Get_utcMonth(unsigned short& m);
    bool            Get_utcYear(unsigned short& y);

private:
    bool            Validate();
    void            Set_day(unsigned short uiDay);
    void            Set_month(unsigned short uiMonth);
    void            Set_year(unsigned short uiYear);

    unsigned short  day;
    unsigned short  month;
    unsigned short  year;
    bool            bBlank;
    bool            bValid;
};

#endif
