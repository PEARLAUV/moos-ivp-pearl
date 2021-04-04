/*
 * PASHRnmea.h
 *
 *  Created on: Aug 15, 2014
 *      Author: Alon Yaari
 */

#ifndef CREATEPASHR_H_
#define CREATEPASHR_H_

//      PASHR - Hemisphere Time, true heading, roll, pitch, and heave data in one message
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

#define NUMELEM_PASHR 10

//#include "MOOS/libMOOS/MOOSLib.h"
//#include "MBUtils.h"
#include "NMEAbase.h"
#include "NMEAutcTime.h"

class infoPASHR {
public:
    infoPASHR() {};
    ~infoPASHR() {};
    utcTime         timeUTC;      // utcTime.valid = false when blank in sentence
    double          heading;
    char            headingRel;
    double          roll;
    double          pitch;
    double          heave;
    double          rollStdDev;
    double          pitchStdDev;
    double          headingStdDev;
    char            qualityFlag;
};

class PASHRnmea: public NMEAbase {
public:
                    PASHRnmea();
    virtual         ~PASHRnmea() {};
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);
    bool            CriticalDataAreValid();

private:
    bool            Validate_timestamp();
    bool            Validate_heading();
    bool            Validate_headingRel();
    bool            Validate_roll();
    bool            Validate_pitch();
    bool            Validate_heave();
    bool            Validate_rollStdDev();
    bool            Validate_pitchStdDev();
    bool            Validate_headingStdDev();
    bool            Validate_qualityFlag();

public:
    bool            GetInfo(infoPASHR& curInfo);
    bool            Get_timestamp(utcTime& t);
    bool            Get_heading(double& d);
    bool            Get_headingRel(char &c);
    bool            Get_roll(double& d);
    bool            Get_pitch(double& d);
    bool            Get_heave(double& d);
    bool            Get_rollStdDev(double& d);
    bool            Get_pitchStdDev(double& d);
    bool            Get_headingStdDev(double& d);
    bool            Get_qualityFlag(char &c);

    bool            Set_timestamp(const utcTime t);
    bool            Set_timestamp(const unsigned short int hour, const unsigned short int minute, const float second);
    bool            Set_heading(const double& d);
    bool            Set_headingRel(const char& c);
    bool            Set_roll(const double& d);
    bool            Set_pitch(const double& d);
    bool            Set_heave(const double& d);
    bool            Set_rollStdDev(const double& d);
    bool            Set_pitchStdDev(const double& d);
    bool            Set_headingStdDev(const double& d);
    bool            Set_qualityFlag(const char& c);

    bool            FromString_timestamp(std::string sVal);
    bool            FromString_heading(std::string sVal);
    bool            FromString_headingRel(std::string sVal);
    bool            FromString_roll(std::string sVal);
    bool            FromString_pitch(std::string sVal);
    bool            FromString_heave(std::string sVal);
    bool            FromString_rollStdDev(std::string sVal);
    bool            FromString_pitchStdDev(std::string sVal);
    bool            FromString_headingStdDev(std::string sVal);
    bool            FromString_qualityFlag(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:
    std::string     ToString_double(const double& d);
    std::string     ToString_timestamp();
    std::string     ToString_heading();
    std::string     ToString_headingRel();
    std::string     ToString_roll();
    std::string     ToString_pitch();
    std::string     ToString_heave();
    std::string     ToString_rollStdDev();
    std::string     ToString_pitchStdDev();
    std::string     ToString_headingStdDev();
    std::string     ToString_qualityFlag();

private:
    infoPASHR       info;
};
#endif
