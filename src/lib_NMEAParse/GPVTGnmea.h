/*
*  GPVTGnmea.h
*
*  Created on: Dec 31, 2014
*      Author: Alon Yaari
*/


#ifndef GPVTGNMEA_H_
#define GPVTGNMEA_H_

//      GPVTG - Track Made Good and Ground Speed
//
//      $GPVTG,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>,<9>*hh<CR><LF>
//      <1>  Track made good, degrees clockwise from true N
//      <2>  Always 'T' to indicate <1> is from true N
//      <3>  (optional) Track made good, degrees clockwise from magnetic N
//      <4>  Always 'M' to indicate <3> is from magnetic N
//      <5>  Speed over ground in knots
//      <6>  Always 'N' to indicate '5' is in knots
//      <7>  Speed over ground in kilometers per hour
//      <8>  Always 'K' to indicate <7> is in kph
//      <9>  (optional) Unknown, appears to contain 'A' in clearpath wire protocol
//
//      Example: $GPVTG,339.83,T,,M,0.011,N,0.020,K,A*3D

//#include "MOOS/libMOOS/MOOSLib.h"
//#include "MBUtils.h"
#include "NMEAbase.h"
#include "NMEAutcTime.h"

#define NUMELEM_GPVTG 8

class infoGPVTG {
public:
    infoGPVTG() {};
    ~infoGPVTG() {};
    double          headingTrueN;       // BLANK_DOUBLE when blank in sentence
    double          headingMagN;        // BLANK_DOUBLE when blank in sentence
    double          speedKTS;           // BLANK_DOUBLE when blank in sentence
    double          speedKPH;           // BLANK_DOUBLE when blank in sentence
};

class GPVTGnmea: public NMEAbase {

public:
                    GPVTGnmea();
    virtual         ~GPVTGnmea() {};
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

private:
    bool            Validate_heading(double d);
    bool            Validate_speed(double d);
    bool            Validate_headingTrueN(double d);
    bool            Validate_headingMagN(double d);
    bool            Validate_speedKTS(double d);
    bool            Validate_speedKPH(double d);

public:
    bool            GetInfo(infoGPVTG& curInfo);
    bool            Get_headingTrueN(double& dVal);
    bool            Get_headingMagN(double& dVal);
    bool            Get_speedKTS(double& dVal);
    bool            Get_speedMPS(double& dVal);
    bool            Get_speedKPH(double& dVal);

    bool            Set_headingTrueN(const double& d);
    bool            Set_headingMagN(const double& d);
    bool            Set_speedKTS(const double& d);
    bool            Set_speedKPH(const double& d);

    bool            FromString_headingTrueN(std::string sVal);
    bool            FromString_headingMagN(std::string sVal);
    bool            FromString_speedKTS(std::string sVal);
    bool            FromString_speedKPH(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:
    std::string     ToString_headingTrueN();
    std::string     ToString_headingMagN();
    std::string     ToString_speedKTS();
    std::string     ToString_speedKPH();

    infoGPVTG       info;
};

#endif





//
