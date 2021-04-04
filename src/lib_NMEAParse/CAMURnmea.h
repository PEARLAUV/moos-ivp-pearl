/*
 * CAMURnmea.h
 *
 *  Created on: May 22, 2015
 *      Author: Alon Yaari
 */

#ifndef CAMURNMEA_H_
#define CAMURNMEA_H_

/* A. Sentence Definition
        Reply to Mini-Packet received, modem to host
        $CAMUR,<1>,<2>*hh<CR><LF>
        <1>  SRC Source (data originator), Valid: 0 to 127
        <2>  DEST Destination (data receiver), Valid: 0 to 127
*/

// B. Number of Elements
#define NUMELEM_CAMUR   2

// C. Relevant includes
#include "NMEAbase.h"                   // Always include the base class

// D. info Struct
class infoCAMUR {
    public:
    infoCAMUR() {};
    ~infoCAMUR() {};
    char            status;             // BLANK_CHAR when blank in sentence
    unsigned char   src;                // BLANK_UCHAR
    unsigned char   dest;               // BLANK_UCHAR
};

// Class Definition
class CAMURnmea: public NMEAbase {
public:
                    CAMURnmea();                                    // Standard constructor
    virtual         ~CAMURnmea() {};                                // Standard destructor
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

// E. Validation functions
private:
    bool            Validate_modemAddress(unsigned char uc);

// F. Get and set functions
public:
    bool            GetInfo(infoCAMUR& curInfo);                    // Always include a GetInfo function that returns the info struct
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

    infoCAMUR       info;
};
#endif
















//
