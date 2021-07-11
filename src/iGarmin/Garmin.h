/*
 * Garmin.h
 * 
 * Created on: 6/30/2021
 * Author: Ethan Rolland
 * 
 */

#ifndef GARMIN_H_
#define GARMIN_H_

#include <string>
#include <stdio.h>
#include <iostream>
#include <math.h>
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "N2kMsg.h"


using namespace std;

class GARMIN : public AppCastingMOOSApp
{
 public:
		 GARMIN();
		 ~GARMIN() {};
	bool OnNewMail(MOOSMSG_LIST &NewMail);
	bool Iterate();
	bool OnConnectToServer();
	bool OnStartUp();
	bool buildReport();
	
	
 protected:
	void RegisterForMOOSMessages();
	
	//Handle Config Params
	bool SetParam_PREFIX(string sVal);
	bool SetParam_DEV_TO_READ(string sVal);
	
	//MOOS file parameters
	std::string  m_prefix;
	static unsigned int m_source;
	
	//NMEA 2000 custom message handler
	static void HandleNMEA2000Msg(const tN2kMsg &N2kMsg);
	// void Heading(const tN2kMsg &N2kMsg)
	
	//Garmin Variables
	static double m_nmea_heading_deg;
	
	
};
#endif
		
