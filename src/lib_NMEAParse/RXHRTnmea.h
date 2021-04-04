/*
 * RXHRTnmea.h
 *
 *  Created on: June 17, 2014
 *      Author: Alon Yaari
 */

#ifndef RXHRTNMEA_H_
#define RXHRTNMEA_H_


/* To create a class for a new NMEA sentence:
        1.  Identify the 5-character NMEA sentence key you are creating a class for
        2.  Copy RXHRTnmea.h and RXHRTnmea.cpp, to new files, replacing 'RXHRT' with the new key
        3.  In this .h file, replace every 'RXHRT' with the new key (your editor's search and replace is ok to use)
        4.  Populate date and author name at the top of this file
        5.  In Section A, replace sentence definition details with the new relevant details
        6.  In Section B, replace value with number of elements defined in the sentence definition
        7.  In Section C, uncomment and add #include statements as needed
        8.  In Section D, replace eRXHRTe struct variables with ones relevant to the sentence
        9.  In Section E, add appropriate validation functions for elements of the sentence.
                          Usually, each elements has its own validation but in cases such as lat or lon that always
                          arrive together (and are each made of two parts), one validation works for all four pieces
        10. In Section F, add custom Get_ and Set_ functions that correspond with the validation functions
        11. In Section G, add custom FromString_ functions
        12. In Section H, add custom ToString functions, with direct correspondence to the sentence definition
        12. Follow instructions in the corresponding .cpp file
*/

/* A. Sentence Definition
        $RXHRT,<1>,<2>,<3>,<4>,<5>,<6>*hh<CR><LF>
        <1>  UTC time, format hhmmss
        <2>  Lat, format decimal degrees
        <3>  Lat hemisphere, N(+) or S(-)
        <4>  Lon, format decimal degrees
        <5>  Lon hemisphere, E(+) or W(-)
        <6>  Team ID
        <7>  Current vehicle mode (1 = remotely operated, 2 = autonomous)
        <8>  Current task number (1, 2, 3, 4, or 5)

        $RXHRT,161229,37.267458,N,12.376548,W,AUVSI,2,3*0D
*/

// B. Number of Elements
#define NUMELEM_RXHRT   8

// C. Relevant includes
#include "NMEAbase.h"                   // Always include the base class
#include "NMEAutcTime.h"                // Uncomment if using utcTime class with your sentence

// D. info Struct
class infoRXHRT {
public:
    infoRXHRT() {};
    ~infoRXHRT() {};
    utcTime         timeUTC;            // timeUTC.IsBlank()
    NMEAgeog        latGeog;            // latDD.IsBlank()
    NMEAgeog        lonGeog;            // lonDD.IsBlank()
    std::string     teamID;             // BLANK_STRING when blank in sentence
    char            curMode;            // BLANK_CHAR when blank in sentence
    char            curTask;            // BLANK_CHAR when blank in sentence
};


// Class Definition
class RXHRTnmea: public NMEAbase {
public:
                    RXHRTnmea();                                    // Standard constructor
    virtual         ~RXHRTnmea() {};                                // Standard destructor
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

// E. Validation functions
private:
    bool            Validate_utcTime();
    bool            Validate_latlon();
    bool            Validate_teamID();
    bool            Validate_curMode();
    bool            Validate_curTask();

// F. Get and set functions
public:
    bool            GetInfo(infoRXHRT& curInfo);                    // Always include a GetInfo function that returns the info struct
    bool            Get_timeUTC(utcTime& t);
    bool            Get_latlon(NMEAgeog& dLat, NMEAgeog& dLon);
    bool            Get_latlonValues(double& dLat, double& dLon);
    bool            Get_teamID(std::string& teamID);
    bool            Get_curMode(char& mode);
    bool            Get_curTask(char& task);

    bool            Set_utcTime(const utcTime t);
    bool            Set_utcTime(const unsigned short int hour, const unsigned short int minute, const unsigned short int second);
    bool            Set_latlon(const NMEAgeog dLat, const NMEAgeog dLon);
    bool            Set_latlonValues(const double dLat, const double dLon);
    bool            Set_teamID(const std::string s);
    bool            Set_curMode(const char c);
    bool            Set_curTask(const char c);

// G. FromString_ functions
    bool            FromString_utcTime(std::string sVal);
    bool            FromString_latlon(std::string strLat, std::string strLatHemi, std::string strLon, std::string strLonHemi);
    bool            FromString_teamID(std::string sVal);
    bool            FromString_curMode(std::string sVal);
    bool            FromString_curTask(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:

// H. ToString_ functions
//      Note that:
//      1) Should be a 1-to-1 correspondence of ToString_ functions to elements in the NMEA sentence
//      2) Unit conversion should happen here. If the info struct has a value stored in different units than in the NMEA
//         NMEA sentence, handle the conversion in ToString_ and be sure to include comments.
    std::string     ToString_utcTime();
    std::string     ToString_lat();
    std::string     ToString_latHemi();
    std::string     ToString_lon();
    std::string     ToString_lonHemi();
    std::string     ToString_teamID();
    std::string     ToString_curMode();
    std::string     ToString_curTask();

    infoRXHRT       info;
};
#endif
