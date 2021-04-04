/*
 * PYDEPnmea.h
 *
 *  Created on: Jun 26, 2014
 *      Author: Alon Yaari
 */

#ifndef PYDEPNMEA_H_
#define PYDEPNMEA_H_

// NOTE
//  PYDEP message is ALLOWED TO have no checksum
//      - If checksum is present it must be correct
//      - If it is missing then message is ok

// Sentence Definition
//      $PYDEP,<1>,<2>*hh<CR><LF>
//      <1>  [DesYawRate] Desired rate of yaw, in radians/sec.
//      <2>  [DesThrustPct] Desired percent of thrust, -100 to 100.
//           Stopped = 0, positive thrust = forward motion.

#define NUMELEM_PYDEP   2

#include "NMEAbase.h"

class infoPYDEP {
public:
    infoPYDEP() {};
    ~infoPYDEP() {};
    double          desYawRate;         // BLANK_DOUBLE when blank in sentence
    double          desThrustPct;       // BLANK_DOUBLE when blank in sentence
};

class PYDEPnmea: public NMEAbase {
public:
                    PYDEPnmea();                                    // Standard constructor
    virtual         ~PYDEPnmea() {};                                // Standard destructor
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

// E. Validation functions
private:
    bool            Validate_desYawRate();
    bool            Validate_desThrustPct();

// F. Get and set functions
public:
    bool            GetInfo(infoPYDEP& curInfo);                    // Always include a GetInfo function that returns the info struct
    bool            Get_desYawRate(double& d);
    bool            Get_desThrustPct(double& d);
    bool            Get_desYawAndThrust(double& dYaw, double& dThrust);

    bool            Set_desYawRate(const double d);
    bool            Set_desThrustPct(const double d);
    bool            Set_desYawAndThrust(const double dYaw, const double dThrust);

// G. FromString_ functions
    bool            FromString_desYawRate(std::string sVal);
    bool            FromString_desThrustPct(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:

// H. ToString_ functions
//      Note that:
//      1) Should be a 1-to-1 correspondence of ToString_ functions to elements in the NMEA sentence
//      2) Unit conversion should happen here. If the info struct has a value stored in different units than in the NMEA
//         NMEA sentence, handle the conversion in ToString_ and be sure to include comments.
    std::string     ToString_desYawRate();
    std::string     ToString_desThrustPct();

    infoPYDEP       info;
};
#endif
