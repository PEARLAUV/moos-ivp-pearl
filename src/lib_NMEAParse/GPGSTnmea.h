/*
 * GPGSTnmea.h
 *
 *  Created on: July 19, 2014
 *      Author: Alon Yaari
 */

#ifndef GPGSTNMEA_H_
#define GPGSTNMEA_H_



//     GPGST - Report of position error
//     $GPGST,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>*hh<CR><LF>
//     <1>  UTC time, format hhmmss.s
//     <2>  RMS value in meters of the std deviation of the ranges
//     <3>  StdDev of semimajor axis, not used
//     <4>  StdDev of semiminor axis, not used
//     <5>  Orientation of semimajor axis, not used
//     <6>  StdDev of latitude error in meters
//     <7>  StdDev of longitude error in meters
//     <8>  StdDev of altitude error in meters

// B. Number of Elements
#define NUMELEM_GPGST   8

// C. Relevant includes
#include "NMEAbase.h"                   // Always include the base class
#include "NMEAutcTime.h"                // Uncomment if using utcTime class with your sentence

// D. info Struct
class infoGPGST {
public:
    infoGPGST() {};
    ~infoGPGST() {};
    utcTime         timeUTC;            // timeUTC.IsBlank()
    double          rmsStdDev;          // BLANK_DOUBLE when blank in sentence
    double          majorStdDev;        // BLANK_DOUBLE when blank in sentence
    double          minorStdDev;        // BLANK_DOUBLE when blank in sentence
    double          orienStdDev;        // BLANK_DOUBLE when blank in sentence
    double          latStdDev;          // BLANK_DOUBLE when blank in sentence
    double          lonStdDev;          // BLANK_DOUBLE when blank in sentence
    double          altStdDev;          // BLANK_DOUBLE when blank in sentence
};


// Class Definition
class GPGSTnmea: public NMEAbase {
public:
                    GPGSTnmea();                                    // Standard constructor
    virtual         ~GPGSTnmea() {};                                // Standard destructor
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

private:
    bool            Validate_utcTime();
    bool            Validate_stdDev(const double d);

public:
    bool            GetInfo(infoGPGST& curInfo);                    // Always include a GetInfo function that returns the info struct
    bool            Get_timeUTC(utcTime& t);
    bool            Get_rmsStdDev(double& d);
    bool            Get_majorStdDev(double& d);
    bool            Get_minorStdDev(double& d);
    bool            Get_orienStdDev(double& d);
    bool            Get_latStdDev(double& d);
    bool            Get_lonStdDev(double& d);
    bool            Get_altStdDev(double& d);

    bool            Set_utcTime(const utcTime t);
    bool            Set_utcTime(const unsigned short int hour, const unsigned short int minute, const float second);
    bool            Set_rmsStdDev(const double d);
    bool            Set_majorStdDev(const double d);
    bool            Set_minorStdDev(const double d);
    bool            Set_orienStdDev(const double d);
    bool            Set_latStdDev(const double d);
    bool            Set_lonStdDev(const double d);
    bool            Set_altStdDev(const double d);

    bool            FromString_utcTime(std::string sVal);
    bool            FromString_rmsStdDev(std::string sVal);
    bool            FromString_majorStdDev(std::string sVal);
    bool            FromString_minorStdDev(std::string sVal);
    bool            FromString_orienStdDev(std::string sVal);
    bool            FromString_latStdDev(std::string sVal);
    bool            FromString_lonStdDev(std::string sVal);
    bool            FromString_altStdDev(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:
    std::string     ToString_utcTime();
    std::string		ToString_StdDev(const double d);

    infoGPGST       info;
};

#endif
