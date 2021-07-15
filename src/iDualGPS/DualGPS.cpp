/*
 * DualGPS.cpp
 * 
 * Created on: 6/21/2021
 * Author: Ethan Rolland
 * 
 */

#include "MBUtils.h"
#include "NMEAdefs.h"
#include "DualGPS.h"
#include <cmath>

using namespace std;

DGPS::DGPS()
{
	m_bValidSerialConn  = false;
	m_report_unhandled  = false;
	m_dual_gps          = false;
	m_serial_port_left  = "";
	m_serial_port_right = "";
	m_baudrate          = 9600;
	m_serial_L          = NULL;
	m_serial_R          = NULL;
	m_parser_L          = NULL;
	m_parser_R          = NULL;
	m_prefix            = "GPS";
	m_heading_offset    = 0.0;
	m_pub_raw           = false;
	m_pub_hdop          = false;
	m_trigger_key       = "";
	
	m_curX_L            = BAD_DOUBLE;
	m_curY_L            = BAD_DOUBLE;
	m_curX_R            = BAD_DOUBLE;
	m_curY_R            = BAD_DOUBLE;
	m_curLat_L          = BAD_DOUBLE;
	m_curLon_L          = BAD_DOUBLE;
	m_curLat_R          = BAD_DOUBLE;
	m_curLon_R          = BAD_DOUBLE;
	m_curHeading_L      = BAD_DOUBLE;
	m_curHeading_R      = BAD_DOUBLE;
	m_curHeadingDUAL    = BAD_DOUBLE;
}

bool DGPS::OnStartUp()
{
	AppCastingMOOSApp::OnStartUp();
	STRING_LIST sParams;
	if (!m_MissionReader.GetConfiguration(GetAppName(), sParams))
		reportConfigWarning("No config block found for " + GetAppName());
	
	bool bHandled = true;
	STRING_LIST::iterator p;
	for (p = sParams.begin(); p != sParams.end(); p++) {
		string orig  = *p;
		string line  = *p;
		string param = toupper(biteStringX(line, '='));
		string value = line;
		
		if (param == "DUAL_GPS")
			bHandled = SetParam_DUAL_GPS(value);
		else if (param == "PORT_1")
			bHandled = SetParam_PORT_LEFT(value);
		else if (param == "PORT_2")
			bHandled = SetParam_PORT_RIGHT(value);
		else if (param == "BAUDRATE")
			bHandled = SetParam_BAUDRATE(value);
		else if (param == "PREFIX")
			bHandled = SetParam_PREFIX(value);
		else if (param == "HEADING_OFFSET")
			bHandled == SetParam_HEADING_OFFSET(value);
		else if (param == "PUBLISH_RAW")
			bHandled = SetParam_PUBLISH_RAW(value);
		else if (param == "PUBLISH_HDOP")
			bHandled = SetParam_PUBLISH_HDOP(value);
		else if (param == "TRIGGER_MSG")
			bHandled = SetParam_TRIGGER_MSG(value);
		else
			reportUnhandledConfigWarning(orig); }
	
	bHandled &= (GeodesySetup() && ParserSetup());
	if (!bHandled) {
		reportConfigWarning("Invalid mission file parameters. GPS data will not be parsed.");
		m_bValidSerialConn = false; }
	else {
	  if (m_dual_gps)
	    m_bValidSerialConn = DualSerialSetup();
	  else
	    m_bValidSerialConn = SerialSetup(); }
	RegisterForMOOSMessages();
	MOOSPause(500);
	
	return true;
}

bool DGPS::OnConnectToServer()
{
	RegisterForMOOSMessages();
	return true;
}

bool DGPS::Iterate()
{
	AppCastingMOOSApp::Iterate();
        IngestFromGPS();
	AppCastingMOOSApp::PostReport();
	return true;
}

void DGPS::IngestFromGPS()
{
	static int msgCount = 0;
	
	if (!m_bValidSerialConn)
	  return;
	  
	//Grab sentences from GPS and ingest them into GPS parser
	while (m_serial_L->DataAvailable()) {
	  string nmea_left = m_serial_L->GetNextSentence();
	  m_parser_L->NMEASentenceIngest(nmea_left); }
	if (m_dual_gps)
	  while (m_serial_R->DataAvailable()) {
	    string nmea_right = m_serial_R->GetNextSentence();
	    m_parser_R->NMEASentenceIngest(nmea_right); }
	  
	// Increment the message counter to know about throwing away first few messages
	//    - Increments after each group of messages, not necessarily after each message
	msgCount++;
	
	
	// Retrieve parsed data from the GPS parser
	//    - The parser makes them available only after the trigger
	if (m_parser_L->MessagesAvailable()) {
	  vector<gpsValueToPublish> toPub = m_parser_L->GetDataToPublish();
	  vector<gpsValueToPublish>::iterator it = toPub.begin();
	  for (;it !=toPub.end(); it++) {
	    HandleLeftMessage(*it); }}
	if (m_dual_gps && m_parser_R->MessagesAvailable()) {
	  vector<gpsValueToPublish> toPub = m_parser_R->GetDataToPublish();
	  vector<gpsValueToPublish>::iterator it = toPub.begin();
	  for (;it !=toPub.end(); it++) {
	    HandleRightMessage(*it); }}
	    
	// Expect that first few messages received will be garbled so throw them out
	if (msgCount < 5)
	  m_parser_L->ClearErrorStrings();
	  if (m_dual_gps)
	    m_parser_R->ClearErrorStrings();
	
	//Retrieve messages for appcast
	while (m_parser_L->ErrorsAvailable())
	  reportRunWarning(m_parser_L->GetNextErrorString());
	if (m_dual_gps)
	  while (m_parser_R->ErrorsAvailable())
	    reportRunWarning(m_parser_R->GetNextErrorString());
}

/*
  Messages from the GPS parser
  Message Name    Message Type  When to Publish
  ------------------------------------------
  LATITUDE        double        Always
  LONGITUDE       double        Always
  X               double        Always
  Y               double        Always
  SPEED           double        Always
  UTC_TIME        double        Always
  UTC_DATE        double        Always
  HEADING_GPRMC   double        Always
  QUALITY         string        On value change
  SAT             double        On value change
  MAGVAR          double        On value change
  NMEA_TXT        string        On value change
  HEADING_DUAL    double        (m_dual_gps    == true)
  HDOP            double        (m_pub_hdop    == true) && on value change
  NMEA_FROM_GPS   string        (m_pub_raw     == true)
  Counter messages
  ------------------------------------------
  #BAD_SENTENCE
  #UNHANDLED
  #GPGGA
  #GPRMC
 */

bool DGPS::OnNewMail(MOOSMSG_LIST &NewMail)
{
	AppCastingMOOSApp::OnNewMail(NewMail);
	MOOSMSG_LIST::iterator p;
	for (p=NewMail.begin(); p!=NewMail.end(); ++p) {
		CMOOSMsg &rMsg = *p;
		string key     = rMsg.GetKey();
		string sVal    = rMsg.GetString(); }
	return UpdateMOOSVariables(NewMail);
}

bool DGPS::RegisterForMOOSMessages()
{
	AppCastingMOOSApp::RegisterVariables();
	return RegisterMOOSVariables();
}

void DGPS::HandleLeftMessage(gpsValueToPublish gVal)
{
	string key = gVal.m_key;
	if (key.empty()) {
	  return; }

	// Handle counters for appcasting
	if (key.at(0) == '#'){
	  key = key.substr(1);
	  m_counters[key] = (unsigned int) gVal.m_dVal;
	  return; }
	  
	if (gVal.m_isDouble) {
	  if (gVal.m_key == "X")          m_curX_L    = gVal.m_dVal;
	  if (gVal.m_key == "Y")          m_curY_L    = gVal.m_dVal;
	  if (gVal.m_key == "LATITUDE")   m_curLat_L  = gVal.m_dVal;
	  if (gVal.m_key == "LONGITUDE")  m_curLon_L  = gVal.m_dVal;
	  if (gVal.m_key == "HEADING_GPRMC") m_curHeading_L = gVal.m_dVal; }
	  
	PublishMessage(gVal);
}

void DGPS::HandleRightMessage(gpsValueToPublish gVal)
{
	string key = gVal.m_key;
	if (key.empty()) {
	  return; }

	// Handle counters for appcasting
	if (key.at(0) == '#'){
	  key = key.substr(1);
	  m_counters[key] = (unsigned int) gVal.m_dVal;
	  return; }
	  
	if (gVal.m_isDouble) {
	  if (gVal.m_key == "X")          m_curX_R    = gVal.m_dVal;
	  if (gVal.m_key == "Y")          m_curY_R    = gVal.m_dVal;
	  if (gVal.m_key == "LATITUDE")   m_curLat_R  = gVal.m_dVal;
	  if (gVal.m_key == "LONGITUDE")  m_curLon_R  = gVal.m_dVal;
	  if (gVal.m_key == "HEADING_GPRMC") m_curHeading_R = gVal.m_dVal; }
}

void DGPS::PublishMessage(gpsValueToPublish gVal)
{
	string key = m_prefix + gVal.m_key;
	if (gVal.m_isDouble)
	  m_Comms.Notify(key, gVal.m_dVal);
	else
	  m_Comms.Notify(key, gVal.m_sVal);

	if (m_dual_gps) {
	  m_curHeadingDUAL = atan2((m_curY_R - m_curY_L),(m_curX_R - m_curX_L)) + 90;
	  m_Comms.Notify(m_prefix + "HEADING_DUAL", m_curHeadingDUAL);
	}
}

bool DGPS::ParserSetup()
{
	m_parser_L = new gpsParser(&m_geodesy, m_trigger_key, m_report_unhandled);
	m_parser_L->SetHeadingOffset(m_heading_offset);
	m_parser_L->SetPublish_hdop(m_pub_hdop);
	m_parser_L->SetPublish_raw(m_pub_raw);
	if (m_dual_gps) {
	  m_parser_R = new gpsParser(&m_geodesy, m_trigger_key, m_report_unhandled);
	  m_parser_R->SetHeadingOffset(m_heading_offset);
	  m_parser_R->SetPublish_hdop(m_pub_hdop);
	  m_parser_R->SetPublish_raw(m_pub_raw); }
	
	return true;
}

bool DGPS::DualSerialSetup()
{
	string errMsg = "";
	m_serial_L = new SerialComms(m_serial_port_left, m_baudrate, errMsg);
	m_serial_R = new SerialComms(m_serial_port_right, m_baudrate, errMsg);
	if (m_serial_L->IsGoodSerialComms() && m_serial_R->IsGoodSerialComms()) {
		m_serial_L->Run();
		m_serial_R->Run();
		string msg = "Serial ports opened. ";
		msg       += "Communicating over port ";
		msg       += m_serial_port_left;
		msg       += " and ";
		msg       += m_serial_port_right;
		reportEvent(msg);
		return true; }
	reportConfigWarning("Unable to open serial port: " + errMsg);
	return false;
}

bool DGPS::SerialSetup()
{
  	string errMsg = "";
	m_serial_L = new SerialComms(m_serial_port_left, m_baudrate, errMsg);
	if (m_serial_L->IsGoodSerialComms()) {
		m_serial_L->Run();
		string msg = "Serial port opened. ";
		msg       += "Communicating over port ";
		msg       += m_serial_port_left;
		reportEvent(msg);
		return true; }
	reportConfigWarning("Unable to open serial port: " + errMsg);
	return false;
}

bool DGPS::GeodesySetup()
{
  double dLatOrigin = 0.0;
  double dLonOrigin = 0.0;
  bool geoOK = m_MissionReader.GetValue("LatOrigin", dLatOrigin);
  if (!geoOK) {
    reportConfigWarning("Latitude origin missing in MOOS file. Could not configure geodesy.");
    return false; }
  else {
    geoOK = m_MissionReader.GetValue("LongOrigin", dLonOrigin);
    if (!geoOK) {
      reportConfigWarning("Longitude origin missing in MOOS file. Could not configure geodesy.");
      return false; } }
  geoOK = m_geodesy.Initialise(dLatOrigin, dLonOrigin);
  if (!geoOK) {
    reportConfigWarning("Could not initialize geodesy with given origin.");
    return false; }
  return true;
}


bool DGPS::SetParam_DUAL_GPS(string sVal)
{
  if (tolower(sVal) == "true")
    m_dual_gps = true;
  else if (tolower(sVal) == "false")
    m_dual_gps = false;
  else {
    string err = "Mission file parameter DUAL_GPS must be 'true' or 'false'. ";
    err       += "Unhandled: ";
    err       += sVal;
    err       += ". Defaulting to ";
    err       += (m_dual_gps ? "true" : "false");
    err       += ".";
    reportConfigWarning(err); }
  return true;
}

bool DGPS::SetParam_PORT_LEFT(string sVal)
{
  m_serial_port_left = sVal;
  if (m_serial_port_left.empty())
    reportConfigWarning("Mission file parameter PORT_LEFT must not be blank.");
  return true;
}

bool DGPS::SetParam_PORT_RIGHT(string sVal)
{
  m_serial_port_right = sVal;
  if (m_serial_port_right.empty())
    reportConfigWarning("Mission file parameter PORT_RIGHT must not be blank.");
  return true;
}

bool DGPS::SetParam_BAUDRATE(string sVal)
{
  if (sVal.empty())
    reportConfigWarning("Mission file parameter BAUDRATE may not be blank.");
  else if (sVal ==   "2400") m_baudrate = 2400;
  else if (sVal ==   "4800") m_baudrate = 4800;
  else if (sVal ==   "9600") m_baudrate = 9600;
  else if (sVal ==  "19200") m_baudrate = 19200;
  else if (sVal ==  "38400") m_baudrate = 38400;
  else if (sVal ==  "57600") m_baudrate = 57600;
  else if (sVal == "115200") m_baudrate = 115200;
  else
    reportConfigWarning("Mission file parameter BAUDRATE must be one of 2400, 4800, 9600, 19200, 38400, 57600, 115200. Unable to process: " + sVal);
  return true;
}

bool DGPS::SetParam_PREFIX(string sVal)
{
  m_prefix = toupper(sVal);
  size_t strLen = m_prefix.length();
  if (strLen > 0 && m_prefix.at(strLen - 1) != '_')
      m_prefix += "_";
  return true;
}

bool DGPS::SetParam_HEADING_OFFSET(string sVal)
{
  if (sVal.empty()) {
    string err = "Mission file parameter HEADING_OFFSET is empty. ";
    err       += "Defaulting to 0.0.";
    reportConfigWarning(err); }
  else {
    m_heading_offset = strtod(sVal.c_str(), 0);
    if (m_heading_offset >= 180.0 || m_heading_offset <= -180.0) {
      m_heading_offset = 0.0;
      string err = "Mission file parameter HEADING_OFFSET must be between -180.0 and 180.0. ";
      err       += "Unhandled: ";
      err       += sVal;
      err       += ". Defaulting to 0.0";
      reportConfigWarning(err); } }
  return true;
}

bool DGPS::SetParam_PUBLISH_HDOP(string sVal)
{
  if (tolower(sVal) == "true")
    m_pub_hdop = true;
  else if (tolower(sVal) == "false")
    m_pub_hdop = false;
  else {
    string err = "Mission file parameter PUBLISH_HDOP must be 'true' or 'false'. ";
    err       += "Unhandled: ";
    err       += sVal;
    err       += ". Defaulting to ";
    err       += (m_pub_hdop ? "true" : "false");
    err       += ".";
    reportConfigWarning(err); }
  return true;
}

bool DGPS::SetParam_PUBLISH_RAW(string sVal)
{
  if (tolower(sVal) == "true")
    m_pub_raw = true;
  else if (tolower(sVal) == "false")
    m_pub_raw = false;
  else {
    string err = "Mission file parameter PUBLISH_RAW must be 'true' or 'false'. ";
    err       += "Unhandled: ";
    err       += sVal;
    err       += ". Defaulting to ";
    err       += (m_pub_raw ? "true" : "false");
    err       += ".";
    reportConfigWarning(err); }
  return true;
}

bool DGPS::SetParam_TRIGGER_MSG(string sVal)
{
  m_trigger_key = sVal;
  return true;
}

bool DGPS::buildReport()
{
  string sDualMode    = boolToString(m_dual_gps);
  string sLeftX       = doubleToString(m_curX_L, 1);
  string sLeftY       = doubleToString(m_curY_L, 1);
  string sRightX      = doubleToString(m_curX_R, 1);
  string sRightY      = doubleToString(m_curY_R, 1);
  string sLeftLat     = doubleToString(m_curLat_L, 6);
  string sLeftLon     = doubleToString(m_curLon_L, 6);
  string sRightLat    = doubleToString(m_curLat_R, 6);
  string sRightLon    = doubleToString(m_curLon_R, 6);
  string sGPSHeading  = doubleToString(m_curHeading_L, 1);
  string sDualHeading = doubleToString(m_curHeadingDUAL, 1);
  
  m_msgs << endl << "SETUP" << endl << "-----" << endl;
  m_msgs <<   "     Dual GPS Mode Enabled:   " << m_dual_gps << endl;
  if (m_dual_gps) {
    m_msgs << "     Left PORT (BAUDRATE):    " << m_serial_port_left << " (" << m_baudrate << ")" << endl;
    m_msgs << "     Right PORT (BAUDRATE):   " << m_serial_port_right << " (" << m_baudrate << ")" << endl; }
  else 
    m_msgs << "     PORT (BAUDRATE):         " << m_serial_port_left << " (" << m_baudrate << ")" << endl;
  m_msgs <<   "     Publish PREFIX:          " << m_prefix << endl;
  m_msgs << endl << "DEVICE STATUS" << "-------------" << endl;
  if (!m_bValidSerialConn) {
    m_msgs << "*** Not parsing GPS due to MOOS configuration error. ***" << endl;
    return true; }
  if (m_dual_gps) {
    if (m_serial_L->IsGoodSerialComms())
      m_msgs << "Serial communicating properly on port " << m_serial_port_left << " at " << m_baudrate << " baud." << endl;
    else
      m_msgs << "Serial not connected to port " << m_serial_port_left << " at " << m_baudrate << " baud." << endl;
    if (m_serial_L->IsGoodSerialComms())
      m_msgs << "Serial communicating properly on port " << m_serial_port_left << " at " << m_baudrate << " baud." << endl;
    else
      m_msgs << "Serial not connected to port " << m_serial_port_left << " at " << m_baudrate << " baud." << endl;
  }
  else {
    if (m_serial_L->IsGoodSerialComms())
      m_msgs << "Serial communicating properly on port " << m_serial_port_left << " at " << m_baudrate << " baud." << endl;
    else
      m_msgs << "Serial not connected to port " << m_serial_port_left << " at " << m_baudrate << " baud." << endl;
  }
  m_msgs << endl;
  if (m_counters.size() == 0)
    m_msgs << "Not yet parsed any sentences." << endl;
  else {
    if (m_dual_gps) {
      m_msgs << "X, Y:        " << sLeftX << ", " << sLeftY << endl;
      m_msgs << "Lat, Lon:    " << sLeftLat << ", " << sLeftLon << endl; }
    else {
      m_msgs << "Port X, Y:          " << sLeftX << ", " << sLeftY << endl;
      m_msgs << "Starboard X, Y:     " << sRightX << ", " << sRightY << endl;
      m_msgs << "Port Lat, Lon:      " << sLeftLat << ", " << sLeftLon << endl;
      m_msgs << "Starboard Lat, Lon: " << sRightLat << ", " << sRightLon << endl; }
    m_msgs << endl;
    m_msgs <<  "Vector Heading:   " << sGPSHeading << endl;
    if (m_dual_gps)
      m_msgs << "Dual GPS Heading: " << sDualHeading << endl;
    }
  
  return true;
}
