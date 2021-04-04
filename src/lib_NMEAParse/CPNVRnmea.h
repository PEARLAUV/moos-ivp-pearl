/*
 * CPNVTnmea.h
 *
 *  Created on: Jan 9, 2015
 *      Author: Alon Yaari
 */

#ifndef CPNVRNMEA_H_
#define CPNVRNMEA_H_

#include "NMEAbase.h"
#include "NMEAutcTime.h"
#include "NMEAutcDate.h"

#define NUMELEM_CPNVR 7

//     CPNVR - Velocity and rate updated as determined by the front-seat estimation filter
//
//     $CPNVR,<1>,<2>,<3>,<4>,<5>,<6>,<7>*hh<CR><LF>
//     <1>  UTC time,  format hhmmss.s
//     <2>  vel_east   East component of vehicle transit velocity
//     <3>  vel_north  North component of vehicle transit velocity
//     <4>  vel_down   Vertical component of vehicle transit velocity
//     <5>  rate_pitch Degree per second of pitch rate
//     <6>  rate_roll  Degree per second of roll rate
//     <7>  rate_yaw   Degree per second of yaw rate

class infoCPNVR {
public:
    infoCPNVR() {};
    ~infoCPNVR() {};
    utcTime         timeUTC;            // info.utcTime.IsValid() = false when blank in sentence
    double          velEast;            // BLANK_DOUBLE when blank in sentence
    double          velNorth;           // BLANK_DOUBLE when blank in sentence
    double          velDown;            // BLANK_DOUBLE when blank in sentence
    double          ratePitch;          // BLANK_DOUBLE when blank in sentence
    double          rateRoll;           // BLANK_DOUBLE when blank in sentence
    double          rateYaw;            // BLANK_DOUBLE when blank in sentence
};

class CPNVRnmea : public NMEAbase {
public:
                    CPNVRnmea();
                    ~CPNVRnmea() {};
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);
    bool            CriticalDataAreValid();

private:
    bool            Validate_timeUTC(utcTime& t);
    bool            Validate_velocity(double& dVal);
    bool            Validate_rate(double& dVal);

public:
    bool            GetInfo(infoCPNVR& curInfo);
    bool            Get_timeUTC(utcTime& timeUtc);
    bool            Get_velEast(double& dVal);
    bool            Get_velNorth(double& dVal);
    bool            Get_velDown(double& dVal);
    bool            Get_ratePitch(double& dVal);
    bool            Get_rateRoll(double& dVal);
    bool            Get_rateYaw(double& dVal);

    bool            Set_timeUTC(const utcTime& t);
    bool            Set_velEast(const double& dVal);
    bool            Set_velNorth(const double& dVal);
    bool            Set_velDown(const double& dVal);
    bool            Set_ratePitch(const double& dVal);
    bool            Set_rateRoll(const double& dVal);
    bool            Set_rateYaw(const double& dVal);

    bool            FromString_timeUTC(std::string sVal);
    bool            FromString_velEast(std::string sVal);
    bool            FromString_velNorth(std::string sVal);
    bool            FromString_velDown(std::string sVal);
    bool            FromString_ratePitch(std::string sVal);
    bool            FromString_rateRoll(std::string sVal);
    bool            FromString_rateYaw(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);

private:
    std::string     ToString_utcTime();
    std::string     ToString_velocity(double& dVal);
    std::string     ToString_rate(double& dVal);

    infoCPNVR       info;

};
#endif














//
