/*
 * Garmin.cpp
 * 
 * Created on: 6/30/2021
 * Auther: Ethan Rolland
 * 
 */

#include "MBUtils.h"
#include "Garmin.h"
#include "NMEA2000_CAN.h"
#include "N2kMessages.h"
//#include "N2kMessagesEnumToStr.h" //Nescessary if Printing to a stream


using namespace std;

/*
 * Static NMEA string exists because overwritten Handler must be static so the handler can 
 * be reassigned as the handler called by the ParseMessages() function provided. ParseMessages
 * must be overwritten because the default ParseMessages simply publishes to a stream.
 */
double GARMIN::m_nmea_heading_deg = 0;

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
	
	// Set up and start NMEA messages
	NMEA2000.EnableForward(false);
	NMEA2000.SetMsgHandler(HandleNMEA2000Msg);
	
	NMEA2000.Open();
	
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
	
	//Overwritten ParseMessages function will now call HandleNMEA2000Msg
	NMEA2000.ParseMessages();
	
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
	m_msgs << endl << "NMEA HEADING :    " << m_nmea_heading_deg << endl;
	return true;
}

/*
 * HandleNMEA2000Msg:
 * DESCRIPTION: Overwritten handler function to parse input string and record heading
 * records to a static variable which can be written to the MOOSDB. Similar to Data Display
 * Examples given by the NMEA2000 library on github
 */
void GARMIN::HandleNMEA2000Msg(const tN2kMsg &N2kMsg){

	unsigned char SID;
	tN2kHeadingReference HeadingReference;
	double Heading = 0;
	double Deviation = 0;
	double Variation = 0;
	//Provided Parsing function, Individual parsing functions given by N2kMessage file
	if(ParseN2kHeading(N2kMsg, SID, Heading, Deviation, Variation, HeadingReference)){
		m_nmea_heading_deg = Heading*180/M_PI;
	}

	
}





