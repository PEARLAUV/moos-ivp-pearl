/*
 * CPIMUnmea.h
 *
*  Created on: January 8 2015
*      Author: Alon Yaari
*/


#ifndef CPIMUNMEA_H_
#define CPIMUNMEA_H_

//      CPIMU - Clearpath wire protocol raw IMU message
//
//      $CPIMU,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>*hh<CR><LF>
//      <1>  Timestamp      Timestamp of the sentence.
//      <2>  AngularRate_X  Angular rate about the X axis in deg/s, right-hand rule
//      <3>  AngularRate_Y  Angular rate about the Y axis in deg/s, right-hand rule
//      <4>  AngularRate_Z  Angular rate about the Z axis in deg/s, right-hand rule
//      <5>  Accel_X        Acceleration along the X axis in m/s^2, forward positive
//      <6>  Accel_Y        Acceleration along the Y axis in m/s^2, forward positive
//      <7>  Accel_Z        Acceleration along the Z axis in m/s^2, forward positive
//      <8>  NavTimestamp   Timestamp for time compass reported this data. If blank, use [Timestamp]

//#include "MOOS/libMOOS/MOOSLib.h"
//#include "MBUtils.h"
#include "NMEAbase.h"
#include "NMEAutcTime.h"

#define NUMELEM_CPIMU 8

class infoCPIMU {
public:
    infoCPIMU() {};
    ~infoCPIMU() {};
    utcTime         timeUTC;            // timeUTC.IsBlank()
    double          angRateX;           // BLANK_DOUBLE when blank in sentence
    double          angRateY;           // BLANK_DOUBLE when blank in sentence
    double          angRateZ;           // BLANK_DOUBLE when blank in sentence
    double          accelX;             // BLANK_DOUBLE when blank in sentence
    double          accelY;             // BLANK_DOUBLE when blank in sentence
    double          accelZ;             // BLANK_DOUBLE when blank in sentence
    utcTime         navTimestamp;       // timeUTC.IsBlank()
};

class CPIMUnmea: public NMEAbase {

public:
                    CPIMUnmea();
    virtual         ~CPIMUnmea() {};
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

private:
    bool            Validate_timeUTC(utcTime& t);
    bool            Validate_angRate(double& dVal);
    bool            Validate_accel(double& dVal);
    bool            Validate_navTimestamp(utcTime& t);

public:
    bool            GetInfo(infoCPIMU& curInfo);
    bool            Get_timeUTC(utcTime& timeUtc);
    bool            Get_angRateX(double& dVal);
    bool            Get_angRateY(double& dVal);
    bool            Get_angRateZ(double& dVal);
    bool            Get_accelX(double& dVal);
    bool            Get_accelY(double& dVal);
    bool            Get_accelZ(double& dVal);
    bool            Get_navTimestamp(utcTime& navTime);

    bool            Set_timeUTC(const utcTime& t);
    bool            Set_angRateX(const double& dVal);
    bool            Set_angRateY(const double& dVal);
    bool            Set_angRateZ(const double& dVal);
    bool            Set_accelX(const double& dVal);
    bool            Set_accelY(const double& dVal);
    bool            Set_accelZ(const double& dVal);
    bool            Set_navTimestamp(const utcTime& t);

    bool            FromString_timeUTC(std::string sVal);
    bool            FromString_angRateX(std::string sVal);
    bool            FromString_angRateY(std::string sVal);
    bool            FromString_angRateZ(std::string sVal);
    bool            FromString_accelX(std::string sVal);
    bool            FromString_accelY(std::string sVal);
    bool            FromString_accelZ(std::string sVal);
    bool            FromString_navTimestamp(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:
    std::string     ToString_utcTime();
    std::string     ToString_angRate(double& dVal);
    std::string     ToString_accel(double& dVal);
    std::string     ToString_navTimestamp();

    infoCPIMU       info;
};

#endif
