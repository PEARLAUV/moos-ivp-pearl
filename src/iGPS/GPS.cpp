/*
 * GPS.cpp
 * 
 * Created on: 1/12/2021
 * Author: Ethan Rolland
 *
 */

#include "MBUtils.h"
#include "NMEAdefs.h"
#include "GPS.h"

using namespace std;

GPS::GPS()
{
  m_bValidSerialConn  = false;
  m_pub_hdop          = false;
  m_pub_raw           = false;
  m_report_unhandled  = false;
  m_serial_port       = "";
  m_baudrate          = 9600;
  m_serial            = NULL;
  m_parser            = NULL;
  m_prefix            = "GPS";
  m_trigger_key       = "";
  m_heading_source    = HEADING_SOURCE_NONE;
  m_heading_offset    = 0.0;
//   m_compass_heading   = BAD_DOUBLE;

  m_curX              = BAD_DOUBLE;
  m_curY              = BAD_DOUBLE;
  m_curLat            = BAD_DOUBLE;
  m_curLon            = BAD_DOUBLE;
  m_curHeading        = BAD_DOUBLE;
}

bool GPS::OnStartUp()
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
    
    if (param == "PORT")
      bHandled = SetParam_PORT(value);
    else if (param == "BAUDRATE")
      bHandled = SetParam_BAUDRATE(value);
    else if (param == "PREFIX")
      bHandled = SetParam_PREFIX(value);
    else if (param == "HEADING_SOURCE")
      bHandled = SetParam_HEADING_SOURCE(value);
    else if (param == "HEADING_OFFSET")
      bHandled = SetParam_HEADING_OFFSET(value);
    else if (param == "PUBLISH_HDOP")
      bHandled = SetParam_PUBLISH_HDOP(value);
    else if (param == "PUBLISH_RAW")
      bHandled = SetParam_PUBLISH_RAW(value);
    else if (param == "TRIGGER_MSG")
      bHandled = SetParam_TRIGGER_MSG(value);
    else
      reportUnhandledConfigWarning(orig); }
  
  bHandled &= (GeodesySetup() && ParserSetup());
  if (!bHandled) {
    reportConfigWarning("Invalid mission file parameters. GPS data will not be parsed.");
    m_bValidSerialConn = false;}
  else {
    m_bValidSerialConn = SerialSetup(); }
  RegisterForMOOSMessages();
  MOOSPause(500);
  
  return true;
}

bool GPS::OnConnectToServer()
{
  RegisterForMOOSMessages();
  return true;
}

bool GPS::Iterate()
{
  AppCastingMOOSApp::Iterate();
  IngestFromGPS();
  AppCastingMOOSApp::PostReport();
  return true;
}

bool GPS::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);
  MOOSMSG_LIST::iterator p;
  for (p=NewMail.begin(); p!=NewMail.end(); ++p) {
      CMOOSMsg &rMsg = *p;
//       if (MOOSStrCmp(rMsg.GetKey(), "COMPASS_HEADING")) {
//         m_compass_heading = p->GetDouble();
//         gpsValueToPublish compassHeadingToPublish = gpsValueToPublish(true,
//                                                                       m_compass_heading,
//                                                                       "",
//                                                                       "HEADING_COMPASS");
//         HandleOneMessage(compassHeadingToPublish); }
  }
  return UpdateMOOSVariables(NewMail);
}

void GPS::IngestFromGPS()
{
  static int msgCount = 0;

  if (!m_bValidSerialConn)
    return;

  // Grab sentences from the GPS and ingest them into the GPS parser
  while (m_serial->DataAvailable()) {
    string nmea = m_serial->GetNextSentence();
    m_parser->NMEASentenceIngest(nmea); }

  // Increment the message counter to know about throwing away first few messages
  //    - Increments after each group of messages, not necessarily after each message
  msgCount++;

  // Retrieve parsed data from the GPS parser
  //    - The parser makes them available only after the trigger
  if (m_parser->MessagesAvailable()) {
    vector<gpsValueToPublish> toPub = m_parser->GetDataToPublish();
    vector<gpsValueToPublish>::iterator it = toPub.begin();
    for (;it != toPub.end(); it++) {
      HandleOneMessage(*it); } }

  // Expect that first few messages received will be garbled so throw them out
  if (msgCount < 5)
    m_parser->ClearErrorStrings();

  // Retrieve messages for appcast
  while (m_parser->ErrorsAvailable())
      reportRunWarning(m_parser->GetNextErrorString());
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
  QUALITY         string        On value change
  SAT             double        On value change
  MAGVAR          double        On value change
  NMEA_TXT        string        On value change
  HEADING_GPRMC   double        (m_heading_source == HEADING_SOURCE_GPRMC)
  HDOP            double        (m_pub_hdop       == true) && on value change
  NMEA_FROM_GPS   string        (m_pub_raw        == true)
  Counter messages
  ------------------------------------------
  #BAD_SENTENCE
  #UNHANDLED
  #GPGGA
  #GPRMC
 */

bool GPS::RegisterForMOOSMessages()
{
  AppCastingMOOSApp::RegisterVariables();
  //m_Comms.Register("COMPASS_HEADING", 0);
  return RegisterMOOSVariables();
}

void GPS::PublishMessage(gpsValueToPublish gVal)
{
  string key = m_prefix + gVal.m_key;
  if (gVal.m_isDouble) {
    m_Comms.Notify(key, gVal.m_dVal);
    if (gVal.m_key == "X")          m_curX    = gVal.m_dVal;
    if (gVal.m_key == "Y")          m_curY    = gVal.m_dVal;
    if (gVal.m_key == "LATITUDE")   m_curLat  = gVal.m_dVal;
    if (gVal.m_key == "LONGITUDE")  m_curLon  = gVal.m_dVal;
    if (gVal.m_key == "HEADING_GPRMC") m_curHeading = gVal.m_dVal; }
  else
    m_Comms.Notify(key, gVal.m_sVal);
}

void GPS::HandleOneMessage(gpsValueToPublish gVal)
{
  string key = gVal.m_key;
  if (key.empty()) {
    return; }

  // Handle counters for appcasting
  if (key.at(0) == '#'){
    key = key.substr(1);
    m_counters[key] = (unsigned int) gVal.m_dVal;
    return; }

  // Deal with heading
  //if (key == "HEADING_GPRMC") {
   // if (m_heading_source != HEADING_SOURCE_GPRMC)
    //  return;
   // else {
   //   key = "HEADING";
   //   gVal.m_key = "HEADING"; } }

//   else if (key == "HEADING_COMPASS") {
//     if (m_heading_source != HEADING_SOURCE_COMPASS)
//       return;
//     else {
//       key = "HEADING";
//       gVal.m_key = "HEADING"; } }

  // Reaching here means it gets published
  PublishMessage(gVal);
}

bool GPS::ParserSetup()
{
  m_parser = new gpsParser(&m_geodesy, m_trigger_key, m_report_unhandled);
  m_parser->SetHeadingOffset(m_heading_offset);
  m_parser->SetPublish_raw(m_pub_raw);
  m_parser->SetPublish_hdop(m_pub_hdop);
  return true;
}

bool GPS::SerialSetup()
{
  string errMsg = "";
  m_serial = new SerialComms(m_serial_port, m_baudrate, errMsg);
  if (m_serial->IsGoodSerialComms()) {
    m_serial->Run();
    string msg = "Serial port opened. ";
    msg       += "Communicating over port ";
    msg       += m_serial_port;
    reportEvent(msg);
    return true; }
  reportConfigWarning("Unable to open serial port: " + errMsg);
  return false;
}

bool GPS::GeodesySetup()
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

bool GPS::SetParam_PORT(std::string sVal)
{
  // TODO: Validate that sVal is a valid path to describe a serial port
  m_serial_port = sVal;
  if (m_serial_port.empty())
    reportConfigWarning("Mission file parameter PORT must not be blank.");
  return true;
}

bool GPS::SetParam_BAUDRATE(std::string sVal)
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

bool GPS::SetParam_PREFIX(std::string sVal)
{
  // TODO: Check sVal for legal MOOS message name characters
  m_prefix = toupper(sVal);
  size_t strLen = m_prefix.length();
  if (strLen > 0 && m_prefix.at(strLen - 1) != '_')
      m_prefix += "_";
  return true;
}

bool GPS::SetParam_HEADING_SOURCE(std::string sVal)
{
  sVal = toupper(sVal);
  if (sVal.empty() || MOOSStrCmp(sVal, "NONE"))
    m_heading_source = HEADING_SOURCE_NONE;
  else if (MOOSStrCmp(sVal, "GPRMC"))
    m_heading_source = HEADING_SOURCE_GPRMC;
//   else if (MOOSStrCmp(sVal, "COMPASS"))
//     m_heading_source = HEADING_SOURCE_COMPASS;
  else {
    m_heading_source = HEADING_SOURCE_NONE;
    string err = "Mission file parameter HEADING_SOURCE should be one of: ";
    err       += "NONE, GPRMC, COMPASS. ";
    err       += "Value '";
    err       += sVal;
    err       += "' is unknown. Defaulting to NONE.";
    reportConfigWarning(err); }
  return true;
}

bool GPS::SetParam_HEADING_OFFSET(std::string sVal)
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

bool GPS::SetParam_PUBLISH_HDOP(std::string sVal)
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

bool GPS::SetParam_PUBLISH_RAW(std::string sVal)
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

bool GPS::SetParam_TRIGGER_MSG(std::string sVal)
{
  m_trigger_key = sVal;
  return true;
}

bool GPS::buildReport()
{
  m_msgs << endl << "SETUP" << endl << "-----" << endl;
  m_msgs << "     PORT (BAUDRATE):         " << m_serial_port << "(" << m_baudrate << ")" << endl;
  m_msgs << "     Publish PREFIX:          " << m_prefix << endl;
  string strSource = "none";
  switch (m_heading_source) {
//     case HEADING_SOURCE_COMPASS:    strSource = "compass";                  break;
    case HEADING_SOURCE_GPRMC:      strSource = "GPRMC sentence from GPS";  break;
    case HEADING_SOURCE_NONE:
    default:                                                                break; }
  m_msgs << "     HEADING_SOURCE:          " << strSource << endl;
  string strPub = "";
  if (m_pub_hdop)       strPub += "HDOP, ";
  if (m_pub_raw)        strPub += "raw GPS sentences, ";
  if (strPub.empty())
    strPub = "No additional publications from GPS";
  else {
    int len = strPub.length();
    if (len > 2)
      strPub = strPub.substr(0, len - 2); }
  m_msgs << endl << "DEVICE STATUS" << endl << "-------------" << endl;
  if (!m_bValidSerialConn) {
    m_msgs << "*** Not parsing GPS due to configuration error. ***" << endl;
    m_msgs <<" Try repairing settings in MOOS file.";
    return true; }
  if (m_serial->IsGoodSerialComms())
    m_msgs << "Serial communicating properly on port " << m_serial_port << " at " << m_baudrate << " baud." << endl;
  else
    m_msgs << "Serial not connected to port " << m_serial_port << " at " << m_baudrate << " baud." << endl;
  std::map<std::string, unsigned int>::iterator it = m_counters.begin();
 if (m_counters.size() == 0)
    m_msgs << "Not yet parsed any sentences." << endl;
  else {
    m_msgs << "     X, Y:     " << doubleToString(m_curX, 1) << ", " << doubleToString(m_curY, 1) << endl;
    m_msgs << "     Lat, Lon: " << doubleToString(m_curLat, 6) << ", " << doubleToString(m_curLon, 6) << endl;
    m_msgs << "     GPS Heading: " << doubleToString(m_curHeading, 1) << endl;
    for (;it != m_counters.end(); it++)
      m_msgs << "    " << it->first << ":  " << it->second << endl; }
  
  return true;
}

