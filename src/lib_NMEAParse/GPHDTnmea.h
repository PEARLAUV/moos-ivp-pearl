/*
 * GPHDTnmea.h
 *
 *  Created on: July 18, 2014
 *      Author: Alon Yaari
 */

#ifndef GPHDTNMEA_H_
#define GPHDTNMEA_H_


//      GPHDT or HEHDT - Hemisphere GPS message for direction bow is pointing (yaw)
//

//      http://www.hemispheregps.com/gpsreference/GPHDT.htm
//      YAW (this is not HEADING)
//      This is the direction that the vessel (antennas) is pointing and is not
//      necessarily the direction of vessel motion (the course over ground).
//
//      $GPHDT,<1>,<2>*hh<CR><LF>
//          or
//      $HEHDT,<1>,<2>*hh<CR><LF>
//      <1>  Current heading in degrees
//      <2>  Always 'T' to indicate true heading


// B. Number of Elements
#define NUMELEM_GPHDT   2

// C. Relevant includes
#include "NMEAbase.h"                   // Always include the base class

// D. info Struct
class infoGPHDT {
public:
    infoGPHDT() {};
    ~infoGPHDT() {};
    double          yaw;                // BLANK_DOUBLE when blank in sentence
};

// Class Definition
class GPHDTnmea: public NMEAbase {
public:
                    GPHDTnmea();                                    // Standard constructor
    virtual         ~GPHDTnmea() {};                                // Standard destructor
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

// E. Validation functions
private:
    bool            Validate_yaw();

// F. Get and set functions
public:
    bool            GetInfo(infoGPHDT& curInfo);                    // Always include a GetInfo function that returns the info struct
    bool            Get_yaw(double& d);

    bool            Set_yaw(const double d);

// G. FromString_ functions
    bool            FromString_yaw(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:

// H. ToString_ functions
    std::string     ToString_yaw();

    infoGPHDT       info;
};


#endif
