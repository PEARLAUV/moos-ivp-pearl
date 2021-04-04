/*
 * CPGGAnmea.h
 *
 *  Created on: Jun 16, 2014
 *      Author: Alon Yaari
 */

#ifndef GPGGANMEA_H_
#define GPGGANMEA_H_

#include "NMEAbase.h"
#include "NMEAutcTime.h"
#include "NMEAutcDate.h"

// By definition there are 14 items in GPGGA
//      but some GPS units only output the first 12
#define NUMELEM_GPGGA 12

//     GPGGA - Common NMEA message for position
//
//     $GPGGA,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>,<9>,<10>,<11>,<12>,<13>,<14>*hh<CR><LF>
//     <1>  UTC time, format hhmmss.s
//     <2>  Lat, format ddmm.mmmmm (with leading 0s)
//     <3>  Lat hemisphere, N(+) or S(-)
//     <4>  Lon, format dddmm.mmmmm (with leading 0s)
//     <5>  Lon hemisphere, E(+) or W(-)
//     <6>  GPS Quality, 0=No fix, 1=Non-diff, 2=Diff, 6=estimated
//     <7>  Number of Satellites
//     <8>  HDOP, 0.5 to 99.9
//     <9>  Alt above MSL -9999.9 to 999999.9 meters
//     <10> Alt MSL units, M=meters
//     <11> Geoid separation -999.9 to 9999.9 meters
//     <12> Geoid separation units, M=meters
//     <13> Age of differential correction, <blank> when not using differential
//     <14> Differential station ID, 0000 when not using differential

class infoGPGGA {
public:
    infoGPGGA() {};
    ~infoGPGGA() {};
    utcTime         timeUTC;            // info.utcTime.IsValid() = false when blank in sentence
    NMEAgeog        latGeog;            // info.latGeog.IsBlank() = true when blank in sentence
    NMEAgeog        lonGeog;            // info.lonGeog.IsBlank() = true when blank in sentence
    char            gpsQual;            // BLANK_CHAR when blank in sentence
    unsigned short  satNum;             // BLANK_POS_SHORT when blank in sentence
    double          hdop;               // BLANK_DOUBLE when blank in sentence
    double          altMSL;             // BLANK_DOUBLE when blank in sentence
    double          altGeoid;           // BLANK_DOUBLE when blank in sentence
    std::string     diffAge;            // BLANK_STRING when blank in sentence
    std::string     diffStation;        // BLANK_STRAIN when blank in sentence
};

class GPGGAnmea : public NMEAbase {
public:
                    GPGGAnmea();
                    ~GPGGAnmea() {};
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);
    bool            CriticalDataAreValid();

private:
    bool            Validate_utcTime();
    bool            Validate_latlon();
    bool            Validate_gpsQual();
    bool            Validate_satNum();
    bool            Validate_hdop();
    bool            Validate_altMSL();
    bool            Validate_altGeoid();

public:
    bool            GetInfo(infoGPGGA& curInfo);
    bool            Get_timeUTC(utcTime& timeUtc);
    bool            Get_latlon(NMEAgeog& gLat, NMEAgeog& gLon);
    bool            Get_latlonValues(double& dLat, double& dLon);
    bool            Get_gpsQual(char& cGpsQual);
    bool            Get_satNum(unsigned short& uiSatNum);
    bool            Get_hdop(double& hdop);
    bool            Get_altMSL(double &altMSL);
    bool            Get_altGeoid(double &altGeoid);
    bool            Get_diffAge(std::string& diffAge);
    bool            Get_diffStation(std::string& diffStation);

    bool            Set_timeUTC(const utcTime& t);
    bool            Set_latlon(const NMEAgeog& gLat, const NMEAgeog& gLon);
    bool            Set_latlonValues(const double& dLat, const double& dLon);
    bool            Set_gpsQual(const char& c);
    bool            Set_satNum(const unsigned short& u);
    bool            Set_hdop(const double& d);
    bool            Set_altMSL(const double& d);
    bool            Set_altGeoid(const double& d);
    bool            Set_diffAge(const std::string& s);
    bool            Set_diffStation(const std::string& s);

    bool            FromString_timeUTC(std::string sVal);
    bool            FromString_latlon(std::string strLat, std::string strLatHemi, std::string strLon, std::string strLonHemi);
    bool            FromString_gpsQual(std::string sVal);
    bool            FromString_satNum(std::string sVal);
    bool            FromString_hdop(std::string sVal);
    bool            FromString_altMSL(std::string sVal);
    bool            FromString_altGeoid(std::string sVal);
    bool            FromString_diffAge(std::string sVal);
    bool            FromString_diffStation(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);

private:
    std::string     ToString_utcTime();
    std::string     ToString_lat();
    std::string     ToString_latHemi();
    std::string     ToString_lon();
    std::string     ToString_lonHemi();
    std::string     ToString_gpsQual();
    std::string     ToString_satNum();
    std::string     ToString_hdop();
    std::string     ToString_altMSL();
    std::string     ToString_altGeoid();
    std::string     ToString_diffAge();
    std::string     ToString_diffstation();

    infoGPGGA       info;

};
#endif
