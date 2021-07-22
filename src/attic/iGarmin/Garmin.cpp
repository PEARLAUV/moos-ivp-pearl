/*
 * Garmin.cpp
 * 
 * Created on: 6/30/2021
 * Auther: Ethan Rolland
 * 
 */

#include "MBUtils.h"
#include "NMEAdefs.h"
#include "Garmin.h"
//#include "NMEA2000_CAN.h"
//#include "N2kMessages.h"
//#include "N2kMessagesEnumToStr.h" //Necessary if Printing to a stream


using namespace std;

/*
 * Static NMEA string exists because overwritten Handler must be static so the handler can 
 * be reassigned as the handler called by the ParseMessages() function provided. ParseMessages
 * must be overwritten because the default ParseMessages simply publishes to a stream.
 */
vector<double> GARMIN::m_heading_vals = {};
int GARMIN::m_sources = 0;

GARMIN::GARMIN()
{
	//MOOS file parameters
	m_prefix        = "GAR";
	m_num_devices   = 0;
	m_device_names  = {};
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

		if      (param == "PREFIX")         handled = SetParam_PREFIX(value);
		else if (param == "NUM_DEVICES")	handled = SetParam_NUM_DEVICES(value);
		else if (param == "DEVICES_NAMES")  handled = SetParam_DEVICES_NAMES(value);
		else
		  reportUnhandledConfigWarning(orig); }
	
	RegisterForMOOSMessages();
	MOOSPause(500);
	
	// Set up and start NMEA messages
//	NMEA2000.EnableForward(false);
//	NMEA2000.SetMsgHandler(HandleNMEA2000Msg);
//	NMEA2000.Open();
	
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
	
	if (m_num_devices>0) {
		//Overwritten ParseMessages function will now call HandleNMEA2000Msg
		//NMEA2000.ParseMessages();
	
		PublishHeadings(m_device_names,m_heading_vals);
	}
	
	AppCastingMOOSApp::PostReport();
	
	return true;
}

bool GARMIN::SetParam_PREFIX(string sVal)
{
	m_prefix = toupper(sVal);
	size_t strLen = m_prefix.length();
	if (strLen > 0 && m_prefix.at(strLen -1) != '_')
		m_prefix += "_";
	
	return true;
}

bool GARMIN::SetParam_NUM_DEVICES(string sVal)
{
	stringstream ssMsg;
	if (!isNumber(sVal))
		ssMsg << "Param NUM_DEVICES must be an integer. Defaulting to 0.";
	else
		m_num_devices = stoi(sVal);
		m_sources = m_num_devices;
	string msg = ssMsg.str();
	if (!msg.empty())
		reportConfigWarning(msg);
	
	return true;
}

bool GARMIN::SetParam_DEVICES_NAMES(string sVal)
{
	stringstream ssMsg;
	if (!sVal.empty()) {
		string names = toupper(sVal);
		vector<string> m_device_names = parseString(names, ','); 
		if (m_device_names.size() != m_num_devices) {
			ssMsg << "Number of device names specified does not equal NUM_DEVICES. Setting NUM_DEVICES to 0.";
			m_num_devices = 0;
			m_sources = m_num_devices;
		}
		else {
			for (int i = 0; i < m_num_devices; i++) {
				m_heading_vals.push_back(BAD_DOUBLE);
			}
		}
	}
	else {
		if (m_num_devices>0) {
			ssMsg << "No device names specified. Setting NUM_DEVICES to 0.";
			m_num_devices = 0;
			m_sources = m_num_devices;
		}
	}
	string msg = ssMsg.str();
	if (!msg.empty())
		reportConfigWarning(msg);
		
	return true;
}

void GARMIN::PublishHeadings(vector<string> deviceNames, vector<double> headingVals)
{
	for ( int i = 0; i < m_num_devices; i++) {
		double dHeading = headingVals[i];
		string name = deviceNames[i];
		if (dHeading != BAD_DOUBLE)
			m_Comms.Notify(m_prefix + name, dHeading);
		else
			reportRunWarning("Did not receive heading info from: " + name);
	}
}

bool GARMIN::buildReport()
{
	m_msgs << endl << "SETUP" << endl;
	m_msgs << "----------------------------------------" << endl;
	m_msgs << "   Publish PREFIX:          " << m_prefix << endl;
	m_msgs << "   Number of NMEA Devices:  " << m_num_devices << endl;
	for (int i = 0; i = m_num_devices-1; i++) {
		m_msgs << "   NMEA Device " << i << ":           " << m_device_names[i] << endl;
	}
	
	m_msgs << endl << "DEVICE STATUS" << endl;
	m_msgs << "----------------------------------------" << endl;
	if (m_num_devices > 0) {
		for (int i = 0; i = m_num_devices-1; i++) {
			m_msgs << "   " << m_device_names[i] << " Heading [deg]: " << m_heading_vals[i] << endl;
		}
	}
	else
		m_msgs << "   No connected devices to read from." << endl;
	
	return true;
}

/*
 * HandleNMEA2000Msg:
 * DESCRIPTION: Overwritten handler function to parse input string and record heading
 * records to a static variable which can be written to the MOOSDB. Similar to Data Display
 * Examples given on GitHub here: https://github.com/ttlappalainen/NMEA2000
 */
//void GARMIN::HandleNMEA2000Msg(const tN2kMsg &N2kMsg)
//{
//	int device;
//	for (device = 0; device < m_sources; device++) {
//		unsigned char SID;
//		tN2kHeadingReference HeadingReference;
//		double Heading = 0;
//		double Deviation = 0;
//		double Variation = 0;
//		int idx = 0;
//		//Provided Parsing function, Individual parsing functions given by N2kMessage file
//		if(N2kMsg.Source == device && ParseN2kHeading(N2kMsg, SID, Heading, Deviation, Variation, HeadingReference)){
//			m_heading_vals[device] = Heading*180/M_PI;
//		}
//	}
//}





