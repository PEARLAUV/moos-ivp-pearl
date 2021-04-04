/*
 * XAMPLnmea.h
 *
 *  Created on: ENTER DATE HERE
 *      Author: ENTER AUTHOR NAME HERE
 */

#ifndef XAMPLNMEA_H_
#define XAMPLNMEA_H_


/* To create a class for a new NMEA sentence:
        1.  Identify the 5-character NMEA sentence key you are creating a class for
        2.  Copy XAMPLnmea.h and XAMPLnmea.cpp, to new files, replacing 'XAMPL' with the new key
        3.  In this .h file, replace every 'XAMPL' with the new key (your editor's search and replace is ok to use)
        4.  Populate date and author name at the top of this file
        5.  In Section A, replace sentence definition details with the new relevant details
        6.  In Section B, replace value with number of elements defined in the sentence definition
        7.  In Section C, uncomment and add #include statements as needed
        8.  In Section D, replace example struct variables with ones relevant to the sentence
        9.  In Section E, add appropriate validation functions for elements of the sentence.
                          Usually, each elements has its own validation but in cases such as lat or lon that always
                          arrive together (and are each made of two parts), one validation works for all four pieces
        10. In Section F, add custom Get_ and Set_ functions that correspond with the validation functions
        11. In Section G, add custom FromString_ functions
        12. In Section H, add custom ToString functions, with direct correspondence to the sentence definition
        12. Follow instructions in the corresponding .cpp file
*/

/* A. Sentence Definition
        $XAMPL,<1>,<2>,<3>,<4>,<5>,<6>*hh<CR><LF>
        <1>  UTC time, format hhmmss.s
        <2>  Status, A=Valid, V=Receiver warning
        <3>  Lat, format ddmm.mmmmm (with leading 0s)
        <4>  Lat hemisphere, N(+) or S(-)
        <5>  Lon, format dddmm.mmmmm (with leading 0s)
        <6>  Lon hemisphere, E(+) or W(-)
*/

// B. Number of Elements
#define NUMELEM_XAMPL   6

// C. Relevant includes
#include "NMEAbase.h"                   // Always include the base class
#include "NMEAutcTime.h"                // Uncomment if using utcTime class with your sentence
//#include "NMEAutcDate.h"              // Uncomment if using utcDate class with your sentence

// D. info Struct
//          Note that there is not necessarily a one-to-one correspondence with the sentence definition.
//              Ex: Latitude is stored in two fields in the definition (value and hemisphere) but translates
//                  to a single positive or negative number in the info struct.
//              Ex: UTC time is one fied in the definition (UTC time) but translates into three
//                  variables in the info struct (hour, minute, second).
//          Each value MUST HAVE a stated value for "blank" when incoming sentence or outgoing sentence is a blank
class infoXAMPL {
    public:
    infoXAMPL() {};
    ~infoXAMPL() {};
    utcTime         timeUTC;            // timeUTC.IsBlank()
    char            status;             // BLANK_CHAR when blank in sentence
    NMEAgeog        latGeog;            // latDD.IsBlank()
    NMEAgeog        lonGeog;            // lonDD.IsBlank()
};


// Class Definition
class XAMPLnmea: public NMEAbase {
public:
                    XAMPLnmea();                                    // Standard constructor
    virtual         ~XAMPLnmea() {};                                // Standard destructor
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

// E. Validation functions
private:
    bool            Validate_utcTime();
    bool            Validate_status();
    bool            Validate_latlon();

// F. Get and set functions
public:
    bool            GetInfo(infoXAMPL& curInfo);                    // Always include a GetInfo function that returns the info struct
    bool            Get_timeUTC(utcTime& t);
    bool            Get_status(char& c);
    bool            Get_latlon(NMEAgeog& gLat, NMEAgeog& gLon);
    bool            Get_latlonValues(double& dLat, double& dLon);

//      Note:
//      Set functions expect input of values as destination's type
//          ex: status as a char, speed as a double, etc.
//      Strings direct from the NMEA sentence are passed into the FromString_ functions below
//
//      Additional Note:
//      It may make sense to create more than one version of Set_.
//      utcTime and utcDate are examples of this, where their classes already
//      have a set function where the source string can be passed to.
//      In other cases, the code calling the set function needs to call for
//      conversion. For example, in status, the calling code would need to
//      extract a single char to pass to Set_status(char status).
    bool            Set_utcTime(const utcTime t);
    bool            Set_utcTime(const unsigned short int hour, const unsigned short int minute, const float second);
    bool            Set_status(const char c);
    bool            Set_latlon(const NMEAgeog gLat, const NMEAgeog gLon);
    bool            Set_latlonValues(const double dLat, const double dLon);

// G. FromString_ functions
    bool            FromString_utcTime(std::string sVal);
    bool            FromString_status(std::string sVal);
    bool            FromString_latlon(std::string strLat, std::string strLatHemi, std::string strLon, std::string strLonHemi);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:

// H. ToString_ functions
//      Note that:
//      1) Should be a 1-to-1 correspondence of ToString_ functions to elements in the NMEA sentence
//      2) Unit conversion should happen here. If the info struct has a value stored in different units than in the NMEA
//         NMEA sentence, handle the conversion in ToString_ and be sure to include comments.
    std::string     ToString_utcTime();
    std::string     ToString_status();
    std::string     ToString_lat();
    std::string     ToString_latHemi();
    std::string     ToString_lon();
    std::string     ToString_lonHemi();

    infoXAMPL       info;
};
#endif
