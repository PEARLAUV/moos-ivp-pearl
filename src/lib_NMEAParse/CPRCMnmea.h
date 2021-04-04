/*
 * CPRCMnmea.h
 *
 *  Created on: Jan 9, 2015
 *      Author: Alon Yaari
 */

#ifndef CPRCMNMEA_H_
#define CPRCMNMEA_H_

#include "NMEAbase.h"
#include "NMEAutcTime.h"
#include "NMEAutcDate.h"

#define NUMELEM_CPRCM 6

//     CPRCM - Clearpath Raw Compass Data
//
//     $CPRCM,<1>,<2>,<3>,<4>,<5>,<6>*hh<CR><LF>
//     <1>  TIMESTAMP     Format hhmmss.s
//     <2>  ID_COMPASS    Unique ID number of the compass being reported on
//     <3>  HEADING       Raw reading from compass for degrees clockwise from true north
//     <4>  PITCH         Raw reading from compass for degrees of pitch
//     <5>  ROLL          Raw reading from compass for degrees of roll
//     <6>  NAV_TIMESTAMP Timestamp for time compass reported this data. If blank, use TIMESTAMP

class infoCPRCM {
public:
    infoCPRCM() {};
    ~infoCPRCM() {};
    utcTime         timeUTC;            // info.utcTime.IsValid() = false when blank in sentence
    int             idCompass;          // BLANK_DOUBLE when blank in sentence
    double          heading;            // BLANK_DOUBLE when blank in sentence
    double          pitch;              // BLANK_DOUBLE when blank in sentence
    double          roll;               // BLANK_DOUBLE when blank in sentence
    utcTime         navTimestamp;       // timeUTC.IsBlank()
};

class CPRCMnmea : public NMEAbase {
public:
                    CPRCMnmea();
                    ~CPRCMnmea() {};
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);
    bool            CriticalDataAreValid();

private:
    bool            Validate_timeUTC(utcTime& t);
    bool            Validate_id(int& iVal);
    bool            Validate_heading(double& dVal);
    bool            Validate_pitchRoll(double& dVal);

public:
    bool            GetInfo(infoCPRCM& curInfo);
    bool            Get_timeUTC(utcTime& timeUtc);
    bool            Get_id(int& iVal);
    bool            Get_heading(double& dVal);
    bool            Get_pitch(double& dVal);
    bool            Get_roll(double& dVal);
    bool            Get_navTimestamp(utcTime& navTime);

    bool            Set_timeUTC(const utcTime& t);
    bool            Set_id(const int& iVal);
    bool            Set_heading(const double& dVal);
    bool            Set_pitch(const double& dVal);
    bool            Set_roll(const double& dVal);
    bool            Set_navTimestamp(const utcTime& t);

    bool            FromString_timeUTC(std::string sVal);
    bool            FromString_id(std::string sVal);
    bool            FromString_heading(std::string sVal);
    bool            FromString_pitch(std::string sVal);
    bool            FromString_roll(std::string sVal);
    bool            FromString_navTimestamp(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);

private:
    std::string     ToString_utcTime();
    std::string     ToString_id(int& iVal);
    std::string     ToString_heading(double& dVal);
    std::string     ToString_pitchRoll(double& dVal);
    std::string     ToString_navTimestamp();

    infoCPRCM       info;

};
#endif














//
