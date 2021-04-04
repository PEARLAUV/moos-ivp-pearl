/*
 * CAREVnmea.h
 *
 *  Created on: June 16, 2015
 *      Author: Alon Yaari
 */

#ifndef CAREVNMEA_H_
#define CAREVNMEA_H_


/* A. Sentence Definition
        Software revision message, modem to host
        $CAREV,<1>,<2>,<3>*hh<CR><LF>
        <1>  Time unit has been on, format HHMMSS
        <2>  Software identifier string (INIT at boot); ex: AUV
        <3>  Revision number, V.VV.V.VV (for example, 0.89.0.17)
*/

// $CAREV,000052,AUV,0.93.0.09*07

// B. Number of Elements
#define NUMELEM_CAREV   3

// C. Relevant includes
#include "NMEAbase.h"                   // Always include the base class
#include "NMEAutcTime.h"

// D. info Struct
class infoCAREV {
    public:
    infoCAREV() {};
    ~infoCAREV() {};
    utcTime         timeOn;             // info.utcTime.IsValid() = false when blank in sentence
    std::string     ident;              // BLANK_STRING
    std::string     revision;           // BLANK_STRING
};

// Class Definition
class CAREVnmea: public NMEAbase {
public:
                    CAREVnmea();                                    // Standard constructor
    virtual         ~CAREVnmea() {};                                // Standard destructor
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

// E. Validation functions
private:
    bool            Validate_timeOn();
    bool            Validate_ident();
    bool            Validate_revision();

// F. Get and set functions
public:
    bool            GetInfo(infoCAREV& curInfo);                    // Always include a GetInfo function that returns the info struct
    bool            Get_timeOn(utcTime& timeUtc);
    bool            Get_ident(std::string& ident);
    bool            Get_revision(std::string& version);

    bool            Set_timeOn(const utcTime& t);
    bool            Set_ident(const std::string str);
    bool            Set_revision(const std::string str);

// G. FromString_ functions
    bool            FromString_timeOn(std::string sVal);
    bool            FromString_ident(std::string sVal);
    bool            FromString_revision(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:

// H. ToString_ functions
    std::string     ToString_timeOn();
    std::string     ToString_ident();
    std::string     ToString_revision();

    infoCAREV       info;
};
#endif
















//
