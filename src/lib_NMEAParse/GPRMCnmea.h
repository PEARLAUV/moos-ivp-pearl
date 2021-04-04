/*
 * GPRMCnmea.h
 *
 *  Created on: Dec 27, 2013
 *      Author: Alon Yaari
 */

#ifndef GPRMCNMEA_H_
#define GPRMCNMEA_H_

//#include "MBUtils.h"
#include "NMEAbase.h"
#include "NMEAutcTime.h"
#include "NMEAutcDate.h"

#define NUMELEM_GPRMC 11

//     $GPRMC,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>,<9>,<10>,<11>,<12>*hh<CR><LF>
//     <1>  UTC time, format hhmmss.s
//     <2>  Status, A=Valid, V=Receiver warning
//     <3>  Lat, format ddmm.mmmmm (with leading 0s)
//     <4>  Lat hemisphere, N(+) or S(-)
//     <5>  Lon, format dddmm.mmmmm (with leading 0s)
//     <6>  Lon hemisphere, E(+) or W(-)
//     <7>  Speed over ground in KNOTS, format 000.00 (with leading 0s)
//     <8>  Course over ground in deg from true North, format ddd.d (with leading 0s)
//     <9>  UTC date, format ddmmyy
//     <10> Magnetic variation true North, format ddd.d (with leading 0s)
//     <11> Magnetic variation direction, E(-) or W(+)
//     <12> OPTIONAL Mode indicator, A=Autonomous, D=Differential, E=Estimated, N=bad

class infoGPRMC {
public:
    infoGPRMC() {};
    ~infoGPRMC() {};
    utcTime         timeUTC;          // info.utcTime.IsValid() = false when blank in sentence
    char            status;           // BLANK_CHAR when blank in sentence
    NMEAgeog        latGeog;          // info.latGeog.IsBlank() = true when blank in sentence
    NMEAgeog        lonGeog;          // info.lonGeog.IsBlank() = true when blank in sentence
    double          speedKTS;         // BLANK_DOUBLE when blank in sentence
    double          headingTrueN;     // BLANK_DOUBLE when blank in sentence
    utcDate         dateUTC;          // info.utcDate.IsValid() = false when blank in sentence
    NMEAgeog        magVar;           // info.magVar.IsBlank() = true when blank in sentence
    char            modeIndicator;    // BLANK_CHAR when blank in sentence
};

class GPRMCnmea: public NMEAbase {
public:
                    GPRMCnmea();
    virtual         ~GPRMCnmea() {};
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);
    bool            CriticalDataAreValid();

private:
    bool            Validate_utcTime();
    bool            Validate_status();
    bool            Validate_latlon();
    bool            Validate_speedKTS();
    bool            Validate_headingTrueN();
    bool            Validate_utcDate();
    bool            Validate_magVar();
    bool            Validate_modeIndicator();

public:
    bool            GetInfo(infoGPRMC& curInfo);
    bool            Get_timeUTC(utcTime& timeUtc);
    bool            Get_status(char& cStatus);
    bool            Get_latlon(NMEAgeog& gLat, NMEAgeog& gLon);
    bool            Get_latlonValues(double& dLat, double& dLon);
    bool            Get_speedKTS(double& dSpeedKts);
    bool            Get_speedMPS(double& dSpeedMpS);
    bool            Get_headingTrueN(double& dHeadingTrue);
    bool            Get_dateUTC(utcDate& utcDate);
    bool            Get_magVar(NMEAgeog& gMagVar);
    bool            Get_magVar(double& dMagVar);
    bool            Get_modeIndicator(char& cModeIndicator);

    bool            Set_timeUTC(const utcTime& t);
    bool            Set_status(const char& c);
    bool            Set_latlon(const NMEAgeog& gLat, const NMEAgeog& gLon);
    bool            Set_latlonValues(const double& dLat, const double& dLon);
    bool            Set_speedKTS(const double& d);
    bool            Set_speedMPS(const double& d);
    bool            Set_headingTrueN(const double& d);
    bool            Set_dateUTC(const utcDate& date);
    bool            Set_magVar(const NMEAgeog& gMagVar);
    bool            Set_magVarValue(const double& d);
    bool            Set_modeIndicator(const char& c);

    bool            FromString_timeUTC(std::string sVal);
    bool            FromString_status(std::string sVal);
    bool            FromString_latlon(std::string strLat, std::string strLatHemi, std::string strLon, std::string strLonHemi);
    bool            FromString_speedKTS(std::string sVal);
    bool            FromString_headingTrueN(std::string sVal);
    bool            FromString_dateUTC(std::string sVal);
    bool            FromString_magVar(std::string strVar, std::string strVarHemi);
    bool            FromString_modeIndicator(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);

private:
    std::string     ToString_utcTime();
    std::string     ToString_status();
    std::string     ToString_lat();
    std::string     ToString_latHemi();
    std::string     ToString_lon();
    std::string     ToString_lonHemi();
    std::string     ToString_speedKnots();
    std::string     ToString_headingTrueN();
    std::string     ToString_dateUTC();
    std::string     ToString_magVar();
    std::string     ToString_magVarDirection();
    std::string     ToString_modeIndicator();

    infoGPRMC       info;

};
#endif
