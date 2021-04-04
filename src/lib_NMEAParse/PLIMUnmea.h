/* 
 * PLIMUnmea.h
 * Created on: 1/23/2021
 * Author: Ethan Rolland
 * 
 */

#ifndef PLIMUNMEA_H_
#define PLIMUNMEA_H_

#include "NMEAbase.h"

#define NUMELEM_PLIMU 3

//     PLIMU - PEARL IMU Data
//
//     $PLIMU,<1>,<2>,<3>,*hh
//     <1>  HEADING       Raw reading from compass for degrees clockwise from true north
//     <2>  PITCH         Raw reading from compass for degrees of pitch
//     <3>  ROLL          Raw reading from compass for degrees of roll

class infoPLIMU {
public:
	infoPLIMU() {};
	~infoPLIMU() {};
	double     heading;   //BLANK_DOUBLE when blank in sentence
	double     pitch;     //BLANK_DOUBLE when blank in sentence
	double     roll;      //BLANK_DOUBLE when blank in sentence
};

class PLIMUnmea : public NMEAbase {
public:
	PLIMUnmea();
	~PLIMUnmea() {};
	bool       ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);
	bool       CriticalDataAreValid();
	
private:
    bool            Validate_heading(double& dVal);
    bool            Validate_pitch(double& dVal);
    bool            Validate_roll(double& dVal);
	
public:
    bool            GetInfo(infoPLIMU& curInfo);
    bool            Get_heading(double& dVal);
    bool            Get_pitch(double& dVal);
    bool            Get_roll(double& dVal);

    bool            Set_heading(const double& dVal);
    bool            Set_pitch(const double& dVal);
    bool            Set_roll(const double& dVal);

    bool            FromString_heading(std::string sVal);
    bool            FromString_pitch(std::string sVal);
    bool            FromString_roll(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);
    
private:
    std::string     ToString_heading(double& dVal);
    std::string     ToString_pitch(double& dVal);
    std::string     ToString_roll(double& dVal);

    infoPLIMU       info;

};
#endif
