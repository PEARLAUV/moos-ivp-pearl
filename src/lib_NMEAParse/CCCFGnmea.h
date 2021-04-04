/*
 * CCCFGnmea.h
 *
 *  Created on: May 22, 2015
 *      Author: Alon Yaari
 */

#ifndef CCCFGNMEA_H_
#define CCCFGNMEA_H_

/* A. Sentence Definition
        Set NVRAM configuration parameter, host to modem
        $CCCFG,<1>,<2>*hh<CR><LF>
        <1>  Name of NVRAM parameter to set
        <2>  New value
*/

// B. Number of Elements
#define NUMELEM_CCCFG   2

// C. Relevant includes
#include "NMEAbase.h"                   // Always include the base class

#define MAX_INT  2147483647

class validRange {
public:
    validRange() : lo(0), hi(0) {}
    validRange(int l, int h) : lo(l), hi(h) {}
    int             lo;
    int             hi;
};

// D. info Struct
class infoCCCFG {
    public:
    infoCCCFG() {};
    ~infoCCCFG() {};
    char            status;             // BLANK_CHAR when blank in sentence
    std::string     paramName;          // BLANK_STRING
    int             newValue;           // BLANK_STRING
};

// Class Definition
class CCCFGnmea: public NMEAbase {
public:
                    CCCFGnmea();                                    // Standard constructor
    virtual         ~CCCFGnmea() {};                                // Standard destructor
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

// E. Validation functions
private:
    bool            Validate_paramName();
    bool            Validate_newValue();

// F. Get and set functions
public:
    bool            GetInfo(infoCCCFG& curInfo);                    // Always include a GetInfo function that returns the info struct
    bool            Get_paramName(std::string& str);
    bool            Get_newValue(int& i);

    bool            Set_paramName(const std::string str);
    bool            Set_newValue(const int i);

// G. FromString_ functions
    bool            FromString_paramName(std::string sVal);
    bool            FromString_newValue(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:

// H. ToString_ functions
    std::string     ToString_paramName();
    std::string     ToString_newValue();

    infoCCCFG       info;

    std::map<std::string, validRange> validParams;
};
#endif
















//
