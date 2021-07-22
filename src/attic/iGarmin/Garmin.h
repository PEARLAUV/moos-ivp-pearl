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
//#include "N2kMsg.h"

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
	bool SetParam_PREFIX(std::string sVal);
	bool SetParam_NUM_DEVICES(std::string sVal);
	bool SetParam_DEVICES_NAMES(std::string sVal);
	
	void PublishHeadings(std::vector<std::string> deviceNames, std::vector<double> headingVals);
	
	//MOOS file parameters
	std::string          m_prefix;
	int      m_num_devices;
	std::vector<std::string>  m_device_names;
	
	//NMEA 2000 custom message handler
	//static void HandleNMEA2000Msg(const tN2kMsg &N2kMsg);
	// void Heading(const tN2kMsg &N2kMsg)
	
	//Garmin Variables
	static int                      m_sources;
	static std::vector<double>      m_heading_vals;
	
};
#endif
		
