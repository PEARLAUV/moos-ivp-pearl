/*
 * PYDEVnmea.h
 *
 *  Created on: Jun 26, 2014
 *      Author: Alon Yaari
 */

#ifndef PYDEVNMEA_H_
#define PYDEVNMEA_H_

// NOTE
//  PYDEV message is ALLOWED TO have no checksum
//      - If checksum is present it must be correct
//      - If it is missing then message is ok

// Sentence Definition
//      $PYDEV,<1>,<2>*hh<CR><LF>
//      <1>  [DesHeading] Desired heading relative to true north, 0 to 359.
//      <2>  [DesSpeed] Desired speed over ground, 0.0 and positive real numbers.

#define NUMELEM_PYDEV   2

#include "NMEAbase.h"

class infoPYDEV {
public:
    infoPYDEV() {};
    ~infoPYDEV() {};
    double          desHeading;         // BLANK_DOUBLE when blank in sentence
    double          desSpeed;           // BLANK_DOUBLE when blank in sentence
};

class PYDEVnmea: public NMEAbase {
public:
                    PYDEVnmea();                                    // Standard constructor
    virtual         ~PYDEVnmea() {};                                // Standard destructor
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

// E. Validation functions
private:
    bool            Validate_desHeading();
    bool            Validate_desSpeed();

// F. Get and set functions
public:
    bool            GetInfo(infoPYDEV& curInfo);                    // Always include a GetInfo function that returns the info struct
    bool            Get_desHeading(double& d);
    bool            Get_desSpeed(double& d);
    bool            Get_desHeadingAndSpeed(double& dHeading, double& dSpeed);

    bool            Set_desHeading(const double d);
    bool            Set_desSpeed(const double d);
    bool            Set_desHeadingAndSpeed(const double dHeading, const double dSpeed);

// G. FromString_ functions
    bool            FromString_desHeading(std::string sVal);
    bool            FromString_desSpeed(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:

// H. ToString_ functions
//      Note that:
//      1) Should be a 1-to-1 correspondence of ToString_ functions to elements in the NMEA sentence
//      2) Unit conversion should happen here. If the info struct has a value stored in different units than in the NMEA
//         NMEA sentence, handle the conversion in ToString_ and be sure to include comments.
    std::string     ToString_desHeading();
    std::string     ToString_desSpeed();

    infoPYDEV       info;
};
#endif
