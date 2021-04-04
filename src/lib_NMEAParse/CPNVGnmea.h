/*
 * CPNVGnmea.h
 *
*  Created on: Dec 17, 2013
*      Author: Alon Yaari
*/


#ifndef CPNVGNMEA_H_
#define CPNVGNMEA_H_

//      CPNVG - Clearpath wire protocol navigation message
//
//      $CPNVG,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>,<9>,<10>,<11>,<12>*hh<CR><LF>
//      <1>  [Timestamp]    Timestamp of the sentence
//      <2>  [Lat_NMEA]     Calculated latitude, in NMEA format
//      <3>  [LatNS_NMEA]   Hemisphere (N or S) of latitude
//      <4>  [Lon_NMEA]     Calculated longitude, in NMEA format
//      <5>  [LonEW_NMEA]   Hemisphere (E or W) of longitude
//      <6>  [PosQual]      Quality of position estimate (no GPS = 0, otherwise = 1)
//      <7>  [AltBottom]    Altitude in meters from bottom, for surface vehicles this field is blank
//      <8>  [DepthTop]     Depth in meters from top, for surface vehicles this field is blank
//      <9>  [Heading]      Direction of travel in degrees clockwise from true north
//      <10> [Roll]         Degrees of roll
//      <11> [Pitch]        Degrees of pitch
//      <12> [NavTimestamp] Timestamp for time this pose/position was calculated. If blank, use [Timestamp].

//#include "MOOS/libMOOS/MOOSLib.h"
//#include "MBUtils.h"
#include "NMEAbase.h"
#include "NMEAutcTime.h"

#define NUMELEM_CPNVG 12

class infoCPNVG {
public:
    infoCPNVG() {};
    ~infoCPNVG() {};
    utcTime         timeUTC;            // timeUTC.IsBlank()
    NMEAgeog        latGeog;            // latDD.IsBlank()
    NMEAgeog        lonGeog;            // lonDD.IsBlank()
    char            posQual;            // BLANK_CHAR when blank in sentence
    double          altBottom;          // BLANK_DOUBLE when blank in sentence
    double          depthTop;           // BLANK_DOUBLE when blank in sentence
    double          headingTrueN;       // BLANK_DOUBLE when blank in sentence
    double          roll;               // BLANK_DOUBLE when blank in sentence
    double          pitch;              // BLANK_DOUBLE when blank in sentence
    utcTime         navTimestamp;       // timeUTC.IsBlank()
};

class CPNVGnmea: public NMEAbase {

public:
                    CPNVGnmea();
    virtual         ~CPNVGnmea() {};
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

private:
    bool            Validate_utcTime();
    bool            Validate_latlon();
    bool            Validate_posQual();
    bool            Validate_altBottom();
    bool            Validate_depthTop();
    bool            Validate_headingTrueN();
    bool            Validate_roll();
    bool            Validate_pitch();
    bool            Validate_navTimestamp();

public:
    bool            GetInfo(infoCPNVG& curInfo);
    bool            Get_timeUTC(utcTime& timeUtc);
    bool            Get_latlon(NMEAgeog& gLat, NMEAgeog& gLon);
    bool            Get_latlonValues(double& dLat, double& dLon);
    bool            Get_posQual(char& c);
    bool            Get_altBottom(double& dVal);
    bool            Get_depthTop(double& dVal);
    bool            Get_headingTrueN(double& dHeadingTrue);
    bool            Get_roll(double& dVal);
    bool            Get_pitch(double& dVal);
    bool            Get_navTimestamp(utcTime& navTime);

    bool            Set_utcTime(const utcTime& t);
    bool            Set_latlon(const NMEAgeog& gLat, const NMEAgeog& gLon);
    bool            Set_latlonValues(const double& dLat, const double& dLon);
    bool            Set_posQual(const char& c);
    bool            Set_altBottom(const double& d);
    bool            Set_depthTop(const double& d);
    bool            Set_headingTrueN(const double& d);
    bool            Set_roll(const double& d);
    bool            Set_pitch(const double& d);
    bool            Set_navTimestamp(const utcTime& t);

    bool            FromString_utcTime(std::string sVal);
    bool            FromString_latlon(std::string strLat, std::string strLatHemi, std::string strLon, std::string strLonHemi);
    bool            FromString_posQual(std::string sVal);
    bool            FromString_altBottom(std::string sVal);
    bool            FromString_depthTop(std::string sVal);
    bool            FromString_headingTrueN(std::string sVal);
    bool            FromString_roll(std::string sVal);
    bool            FromString_pitch(std::string sVal);
    bool            FromString_navTimestamp(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:
    std::string     ToString_utcTime();
    std::string     ToString_lat();
    std::string     ToString_latHemi();
    std::string     ToString_lon();
    std::string     ToString_lonHemi();
    std::string     ToString_posQual();
    std::string     ToString_altBottom();
    std::string     ToString_depthTop();
    std::string     ToString_headingTrueN();
    std::string     ToString_roll();
    std::string     ToString_pitch();
    std::string     ToString_navTimestamp();

    infoCPNVG       info;
};

#endif
