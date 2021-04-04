/*
 * CAMPRnmea.h
 *
 *  Created on: May 22, 2015
 *      Author: Alon Yaari
 */

#ifndef CAMPRNMEA_H_
#define CAMPRNMEA_H_

/* A. Sentence Definition
        Reply to Ping has been received, modem to host
        $CAMPR,<1>,<2>,<3>*hh<CR><LF>
        <1>  SRC Source (data originator), Valid: 0 to 127
        <2>  DEST Destination (data receiver), Valid: 0 to 127
        <3>  TRAVEL TIME in seconds (double), Valid >= 0.0.
*/

// B. Number of Elements
#define NUMELEM_CAMPR   3

// C. Relevant includes
#include "NMEAbase.h"                   // Always include the base class

// D. info Struct
class infoCAMPR {
    public:
    infoCAMPR() {};
    ~infoCAMPR() {};
    char            status;             // BLANK_CHAR when blank in sentence
    unsigned char   src;                // BLANK_UCHAR
    unsigned char   dest;               // BLANK_UCHAR
    double          travelTime;         // BLANK_DOUBLE
};

// Class Definition
class CAMPRnmea: public NMEAbase {
public:
                    CAMPRnmea();                                    // Standard constructor
    virtual         ~CAMPRnmea() {};                                // Standard destructor
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

// E. Validation functions
private:
    bool            Validate_modemAddress(unsigned char uc);
    bool            Validate_travelTime();

// F. Get and set functions
public:
    bool            GetInfo(infoCAMPR& curInfo);                    // Always include a GetInfo function that returns the info struct
    bool            Get_srcAddress(unsigned char& uc);
    bool            Get_destAddress(unsigned char& uc);
    bool            Get_travelTime(double& d);

    bool            Set_srcAddress(const unsigned char uc);
    bool            Set_destAddress(const unsigned char uc);
    bool            Set_travelTime(const double d);

// G. FromString_ functions
    unsigned char   FromString_modemAddress(std::string sVal);
    bool            FromString_srcAddress(std::string sVal);
    bool            FromString_destAddress(std::string sVal);
    bool            FromString_travelTime(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:

// H. ToString_ functions
    std::string     ToString_srcAddress();
    std::string     ToString_destAddress();
    std::string     ToString_travelTime();

    infoCAMPR       info;
};
#endif
















//
