/*
 * PYDIRnmea.h
 *
 *  Created on: Jun 26, 2014
 *      Author: Alon Yaari
 */

#ifndef PYDIRNMEA_H_
#define PYDIRNMEA_H_

// NOTE
//  PYDIR message is ALLOWED TO have no checksum
//      - If checksum is present it must be correct
//      - If it is missing then message is ok

// Sentence Definition
//      $PYDIR,<1>,<2>*hh<CR><LF>
//      <1>  [DesThrustPctL] Desired percent of thrust for the portside motor, -100 to 100.
//      <2>  [DesThrustPctR] Desired percent of thrust for the starboard motor, -100 to 100.

#define NUMELEM_PYDIR   2

#include "NMEAbase.h"

class infoPYDIR {
public:
    infoPYDIR() {};
    ~infoPYDIR() {};
    double          desThrustPctL;          // BLANK_DOUBLE when blank in sentence
    double          desThrustPctR;          // BLANK_DOUBLE when blank in sentence
};

class PYDIRnmea: public NMEAbase {
public:
                    PYDIRnmea();                                    // Standard constructor
    virtual         ~PYDIRnmea() {};                                // Standard destructor
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

// E. Validation functions
private:
    bool            Validate_desThrustPctL();
    bool            Validate_desThrustPctR();

// F. Get and set functions
public:
    bool            GetInfo(infoPYDIR& curInfo);                    // Always include a GetInfo function that returns the info struct
    bool            Get_desThrustPctL(double& d);
    bool            Get_desThrustPctR(double& d);
    bool            Get_desThrustPctLandR(double& dL, double& dR);

    bool            Set_desThrustPctL(const double d);
    bool            Set_desThrustPctR(const double d);
    bool            Set_desThrustPctLandR(const double dL, const double dR);

// G. FromString_ functions
    bool            FromString_desThrustPctL(std::string sVal);
    bool            FromString_desThrustPctR(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:

// H. ToString_ functions
//      Note that:
//      1) Should be a 1-to-1 correspondence of ToString_ functions to elements in the NMEA sentence
//      2) Unit conversion should happen here. If the info struct has a value stored in different units than in the NMEA
//         NMEA sentence, handle the conversion in ToString_ and be sure to include comments.
    std::string     ToString_desThrustPctL();
    std::string     ToString_desThrustPctR();

    infoPYDIR       info;
};
#endif
