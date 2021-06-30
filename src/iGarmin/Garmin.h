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
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"

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
	
	//MOOS file parameters
	std::string  m_prefix;
	
};
#endif
		
