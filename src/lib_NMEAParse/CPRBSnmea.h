/*
 * CPRBSnmea.h
 *
 *  Created on: Dec 17, 2013
 *      Author: Alon Yaari
 */

#ifndef CREATECPRBS_H_
#define CREATECPRBS_H_

//		CPRBS - Clearpath Wire Protocol Battery Status Message
//
//      $CPRBS,<1>,<2>,<3>,<4>,<5>,<6>*hh<CR><LF>
//      <1>  [Timestamp]    Timestamp of the sentence
//      < >  [ID_Battery]   Unique ID number of the battery being reported on
//      <2>  [V_Batt_Stack] Voltage of the battery bank
//      <3>  [V_Batt_Min]   Lowest voltage read from cells in the bank
//      <4>  [V_Batt_Max]   Highest voltage read from the cells in the bank
//      <5>  [TemperatureC] Temperature of the battery pack in C
//      NOTE
//          As of Aug 2013, this message is being published with no [ID_BATTERY]
//          and a 0 value published for [TEMPERATUREC]
//      example: $CPRBS,172909.322,15.121597,15.121597,15.121597,0*76

#define NUMELEM_CPRBS 5   // Note: When CPRBS is corrected, this will = 6

#include "MOOS/libMOOS/MOOSLib.h"
#include "MBUtils.h"
#include "NMEAbase.h"
#include "NMEAutcTime.h"

class infoCPRBS {
public:
    infoCPRBS() {};
    ~infoCPRBS() {};
    utcTime         timestamp;      // utcTime.valid = false when blank in sentence
    unsigned short  battID;
    double          battStackVoltage;
    double          battMin;
    double          battMax;
    double          tempC;
};


class CPRBSnmea: public NMEAbase {
public:
                    CPRBSnmea();
    virtual         ~CPRBSnmea() {};
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);
    bool            CriticalDataAreValid();

private:
    bool            Validate_timestamp();
    bool            Validate_battID();
    bool            Validate_battStackVoltage();
    bool            Validate_battMin();
    bool            Validate_battMax();
    bool            Validate_tempC();

public:
    bool            GetInfo(infoCPRBS& curInfo);
    bool            Get_timestamp(utcTime& t);
    bool            Get_battID(unsigned short& d);
    bool            Get_battStackVoltage(double& d);
    bool            Get_battMin(double& d);
    bool            Get_battMax(double& d);
    bool            Get_tempC(double& d);

    bool            Set_timestamp(const utcTime t);
    bool            Set_timestamp(const unsigned short int hour, const unsigned short int minute, const float second);
    bool            Set_battID(const unsigned short& d);
    bool            Set_battStackVoltage(const double& d);
    bool            Set_battMin(const double& d);
    bool            Set_battMax(const double& d);
    bool            Set_tempC(const double& d);

    bool            FromString_timestamp(std::string sVal);
    bool            FromString_battID(std::string sVal);
    bool            FromString_battStackVoltage(std::string sVal);
    bool            FromString_battMin(std::string sVal);
    bool            FromString_battMax(std::string sVal);
    bool            FromString_tempC(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:
    std::string     ToString_timestamp();
    std::string     ToString_battID();
    std::string     ToString_battStackVoltage();
    std::string     ToString_battMin();
    std::string     ToString_battMax();
    std::string     ToString_tempC();

private:
    infoCPRBS       info;
};

#endif
