/*
 * CCCFQnmea.h
 *
 *  Created on: May 22, 2015
 *      Author: Alon Yaari
 */

#ifndef CCCFQNMEA_H_
#define CCCFQNMEA_H_

/* A. Sentence Definition
        Query configuration parameter, host to modem
        $CCCFQ,<1>,<2>*hh<CR><LF>
        <1>  Name of parameter or "ALL" to show all
        <2>  DEST Destination (data receiver), Valid: 0 to 127
*/

// B. Number of Elements
#define NUMELEM_CCCFQ   2

// C. Relevant includes
#include "NMEAbase.h"                   // Always include the base class

// D. info Struct
class infoCCCFQ {
    public:
    infoCCCFQ() {};
    ~infoCCCFQ() {};
    char            status;             // BLANK_CHAR when blank in sentence
    unsigned char   src;                // BLANK_UCHAR
    unsigned char   dest;               // BLANK_UCHAR
};

// Class Definition
class CCCFQnmea: public NMEAbase {
public:
                    CCCFQnmea();                                    // Standard constructor
    virtual         ~CCCFQnmea() {};                                // Standard destructor
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

// E. Validation functions
private:
    bool            Validate_modemAddress(unsigned char uc);

// F. Get and set functions
public:
    bool            GetInfo(infoCCCFQ& curInfo);                    // Always include a GetInfo function that returns the info struct
    bool            Get_srcAddress(unsigned char& uc);
    bool            Get_destAddress(unsigned char& uc);

    bool            Set_srcAddress(const unsigned char uc);
    bool            Set_destAddress(const unsigned char uc);

// G. FromString_ functions
    unsigned char   FromString_modemAddress(std::string sVal);
    bool            FromString_srcAddress(std::string sVal);
    bool            FromString_destAddress(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:

// H. ToString_ functions
    std::string     ToString_srcAddress();
    std::string     ToString_destAddress();
    std::string     ToString_msg();

    infoCCCFQ       info;
};
#endif
















//
