/*
 * GPRMEnmea.h
 *
 *  Created on: July 19, 2014
 *      Author: Alon Yaari
 */

#ifndef GPRMENMEA_H_
#define GPRMENMEA_H_



//     GPRME - Report of position error
//     $GPRME,<1>,<2>,<3>*hh<CR><LF>
//     <1>  Estimated horizontal position error, 0.0 to 999.99 meters
//     <2>  Horizontal error units, always M=meters
//     <3>  Estimated vertical position error, 0.0 to 999.99 meters
//     <4>  Vertical error units, always M=meters
//     <5>  Estimated position error, 0.0 to 999.99 meters
//     <6>  Position error units, always M=meters

// B. Number of Elements
#define NUMELEM_GPRME   6

// C. Relevant includes
#include "NMEAbase.h"                   // Always include the base class

// D. info Struct
class infoGPRME {
public:
    infoGPRME() {};
    ~infoGPRME() {};
    double          estHPE;             // BLANK_DOUBLE when blank in sentence
    double          estVPE;             // BLANK_DOUBLE when blank in sentence
    double          estPOSerr;          // BLANK_DOUBLE when blank in sentence
};


// Class Definition
class GPRMEnmea: public NMEAbase {
public:
                    GPRMEnmea();                                    // Standard constructor
    virtual         ~GPRMEnmea() {};                                // Standard destructor
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

private:
    bool            Validate_utcTime();
    bool            Validate_posErr(const double d);

public:
    bool            GetInfo(infoGPRME& curInfo);                    // Always include a GetInfo function that returns the info struct
    bool            Get_estHPE(double& d);
    bool            Get_estVPE(double& d);
    bool            Get_estPOSerr(double& d);
    bool            Set_estHPE(const double d);
    bool            Set_estVPE(const double d);
    bool            Set_estPOSerr(const double d);

    bool            FromString_estHPE(std::string sVal);
    bool            FromString_estVPE(std::string sVal);
    bool            FromString_estPOSerr(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:
    std::string		ToString_PosErr(const double d);

    infoGPRME       info;
};


#endif
