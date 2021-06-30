/*
 * Garmin.cpp
 * 
 * Created on: 6/30/2021
 * Auther: Ethan Rolland
 * 
 */

#include "MBUtils.h"
#include "Garmin.h"

using namespace std;

GARMIN::GARMIN()
{
	//MOOS file parameters
	m_prefix        = "GAR";
}

bool GARMIN::OnNewMail(MOOSMSG_LIST &NewMail)
{
	AppCastingMOOSApp::OnNewMail(NewMail);
	MOOSMSG_LIST::iterator p;
	for (p=NewMail.begin(); p!=NewMail.end(); ++p) {
	  CMOOSMsg &rMsg = *p;
	  string key     = rMsg.GetKey();
	  string sVal    = rMsg.GetString();
	}
	return UpdateMOOSVariables(NewMail);
}

void GARMIN::RegisterForMOOSMessages()
{
	AppCastingMOOSApp::RegisterVariables();
}

bool GARMIN::OnStartUp()
{
	AppCastingMOOSApp::OnStartUp();
	STRING_LIST sParams;
	if (!m_MissionReader.GetConfiguration(GetAppName(), sParams))
		reportConfigWarning("No config block found for " + GetAppName());
	
	bool handled = true;
	STRING_LIST::iterator p;
	for (p = sParams.begin(); p != sParams.end(); p++) {
		string orig   = *p;
		string line   = *p;
		string param  = toupper(biteStringX(line, '='));
		string value  = line;

		if (param == "PREFIX")          handled = SetParam_PREFIX(value);
		else
		  reportUnhandledConfigWarning(orig); }
	
	RegisterForMOOSMessages();
	MOOSPause(500);
	
	return true;
}

bool GARMIN::OnConnectToServer()
{
	RegisterForMOOSMessages();
	return true;
}

bool GARMIN::Iterate()
{
	AppCastingMOOSApp::Iterate();
	
	AppCastingMOOSApp::PostReport();
	
	return true;
}

bool GARMIN::SetParam_PREFIX(std::string sVal)
{
	m_prefix = toupper(sVal);
	size_t strLen = m_prefix.length();
	if (strLen > 0 && m_prefix.at(strLen -1) != '_')
		m_prefix += "_";
	
	return true;
}

bool GARMIN::buildReport()
{
	
	return true;
}
