/*
 * CCMUCnmea.h
 *
 *  Created on: May 22, 2015
 *      Author: Alon Yaari
 */

#ifndef CCMUCNMEA_H_
#define CCMUCNMEA_H_

/* A. Sentence Definition
        User Mini-Packet command, host to modem
        $CCMUC,<1>,<2>,<3>*hh<CR><LF>
        <1>  SRC Source (data originator), Valid: 0 to 127
        <2>  DEST Destination (data receiver), Valid: 0 to 127
        <3>  ASCII-coded hex data (2 hex values). Values in the range 0 to 1FFF are legal
*/

// B. Number of Elements
#define NUMELEM_CCMUC   3

// C. Relevant includes
#include "NMEAbase.h"                   // Always include the base class

// D. info Struct
class infoCCMUC {
    public:
    infoCCMUC() {};
    ~infoCCMUC() {};
    char            status;             // BLANK_CHAR when blank in sentence
    unsigned char   src;                // BLANK_UCHAR
    unsigned char   dest;               // BLANK_UCHAR
    unsigned short  msg;                // BLANK_USHORT
};

// Class Definition
class CCMUCnmea: public NMEAbase {
public:
                    CCMUCnmea();                                    // Standard constructor
    virtual         ~CCMUCnmea() {};                                // Standard destructor
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

// E. Validation functions
private:
    bool            Validate_modemAddress(unsigned char uc);
    bool            Validate_msg();

// F. Get and set functions
public:
    bool            GetInfo(infoCCMUC& curInfo);                    // Always include a GetInfo function that returns the info struct
    bool            Get_srcAddress(unsigned char& uc);
    bool            Get_destAddress(unsigned char& uc);
    bool            Get_msgData(unsigned short& msg);

    bool            Set_srcAddress(const unsigned char uc);
    bool            Set_destAddress(const unsigned char uc);
    bool            Set_msgData(const unsigned short msg);

// G. FromString_ functions
    unsigned char   FromString_modemAddress(std::string sVal);
    bool            FromString_srcAddress(std::string sVal);
    bool            FromString_destAddress(std::string sVal);
    bool            FromString_msgBytes(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:

// H. ToString_ functions
    std::string     ToString_srcAddress();
    std::string     ToString_destAddress();
    std::string     ToString_msg();

    infoCCMUC       info;
};
#endif
















//
