/*
 * GPSDevice.cpp
 *
 *  Created on: Nov 6, 2014
 *      Author: Alon Yaari
 */

#include "MBUtils.h"
#include "NMEAdefs.h"
#include "GPSDevice.h"

using namespace std;

GPSDevice::GPSDevice()
{
  m_bValidSerialConn      = false;
  m_pub_utc           = false;
  m_pub_hpe           = false;
  m_pub_hdop          = false;
  m_pub_yaw           = false;
  m_pub_raw           = false;
  m_pub_pitch_roll    = false;
  m_swap_pitch_roll   = false;
  m_report_unhandled  = false;
  m_serial_port       = "";
  m_baudrate          = 19200;
  m_serial            = NULL;
  m_parser            = NULL;
  m_prefix            = "GPS";
  m_trigger_key       = "";
  m_heading_source    = HEADING_SOURCE_NONE;
  m_heading_offset    = 0.0;
  m_compass_heading   = BAD_DOUBLE;

  m_curX              = BAD_DOUBLE;
  m_curY              = BAD_DOUBLE;
  m_curLat            = BAD_DOUBLE;
  m_curLon            = BAD_DOUBLE;
}

bool GPSDevice::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);
  MOOSMSG_LIST::iterator p;
  for (p=NewMail.begin(); p!=NewMail.end(); ++p) {
      CMOOSMsg & rMsg = *p;
      if (MOOSStrCmp(rMsg.GetKey(), "COMPASS_HEADING")) {
        m_compass_heading = p->GetDouble();
        gpsValueToPublish compassHeadingToPublish = gpsValueToPublish(true,
                                                                      m_compass_heading,
                                                                      "",
                                                                      "HEADING_COMPASS");
        HandleOneMessage(compassHeadingToPublish); } }
  return UpdateMOOSVariables(NewMail);
}

bool GPSDevice::Iterate()
{
  AppCastingMOOSApp::Iterate();
  IngestFromGPSDevice();
  AppCastingMOOSApp::PostReport();
  return true;
}

void GPSDevice::IngestFromGPSDevice()
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
  QUALITY         string        On value change
  SAT             double        On value change
  MAGVAR          double        On value change
  NMEA_TXT        string        On value change
  HEADING_GPRMC   double        (m_heading_source == HEADING_SOURCE_GPRMC)
  HEADING_PASHR   double        (m_heading_source == HEADING_SOURCE_PASHR)
  HDOP            double        (m_pub_hdop       == true) && on value change
  YAW             double        (m_pub_yaw        == true)
  UTC             double        (m_pub_utc        == true)
  HPE             double        (m_pub_hpe        == true)
  PITCH           double        (m_pub_pitch_roll == true)
  ROLL            double        (m_pub_pitch_roll == true)
  NMEA_FROM_GPS   string        (m_pub_raw        == true)

  Counter messages
  ------------------------------------------
  #BAD_SENTENCE
  #UNHANDLED
  #GPGGA
  #GPHDT
  #GPRMC
  #GPRME
  #GPTXT
  #PASHR

 */

// HandleOneMessage()
void GPSDevice::HandleOneMessage(gpsValueToPublish gVal)
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
  if (key == "HEADING_GPRMC") {
    if (m_heading_source != HEADING_SOURCE_GPRMC)
      return;
    else {
      key = "HEADING";
      gVal.m_key = "HEADING"; } }

  else if (key == "HEADING_PASHR") {
    if (m_heading_source != HEADING_SOURCE_PASHR)
       return;
    else {
      key = "HEADING";
      gVal.m_key = "HEADING"; } }

  else if (key == "HEADING_COMPASS") {
    if (m_heading_source != HEADING_SOURCE_COMPASS)
      return;
    else {
      key = "HEADING";
      gVal.m_key = "HEADING"; } }

  // Reaching here means it gets published
  PublishMessage(gVal);
}

void GPSDevice::PublishMessage(gpsValueToPublish gVal)
{
  string key = m_prefix + gVal.m_key;
  if (gVal.m_isDouble) {
    m_Comms.Notify(key, gVal.m_dVal);
    if (gVal.m_key == "X")          m_curX    = gVal.m_dVal;
    if (gVal.m_key == "Y")          m_curY    = gVal.m_dVal;
    if (gVal.m_key == "LATITUDE")   m_curLat  = gVal.m_dVal;
    if (gVal.m_key == "LONGITUDE")  m_curLon  = gVal.m_dVal; }
  else
    m_Comms.Notify(key, gVal.m_sVal);
}

bool GPSDevice::OnConnectToServer()
{
  return true;
}

bool GPSDevice::OnStartUp()
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
    else if (param == "PUBLISH_UTC")
      bHandled = SetParam_PUBLISH_UTC(value);
    else if (param == "PUBLISH_HPE")
      bHandled = SetParam_PUBLISH_HPE(value);
    else if (param == "PUBLISH_HDOP")
      bHandled = SetParam_PUBLISH_HDOP(value);
    else if (param == "PUBLISH_YAW")
      bHandled = SetParam_PUBLISH_YAW(value);
    else if (param == "PUBLISH_RAW")
      bHandled = SetParam_PUBLISH_RAW(value);
    else if (param == "PUBLISH_PITCH_ROLL")
      bHandled = SetParam_PUBLISH_PITCH_ROLL(value);
    else if (param == "TRIGGER_MSG")
      bHandled = SetParam_TRIGGER_MSG(value);
    else if (param == "SWAP_PITCH_ROLL")
      bHandled = SetParam_SWAP_PITCH_ROLL(value);
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

bool GPSDevice::RegisterForMOOSMessages()
{
  AppCastingMOOSApp::RegisterVariables();
  m_Comms.Register("COMPASS_HEADING", 0);
  return RegisterMOOSVariables();
}

bool GPSDevice::ParserSetup()
{
  m_parser = new gpsParser(&m_geodesy, m_trigger_key, m_report_unhandled);
  m_parser->SetSwapPitchAndRoll(m_swap_pitch_roll);
  m_parser->SetHeadingOffset(m_heading_offset);
  m_parser->SetPublish_raw(m_pub_raw);
  m_parser->SetPublish_hdop(m_pub_hdop);
  m_parser->SetPublish_yaw(m_pub_yaw);
  m_parser->SetPublish_utc(m_pub_utc);
  m_parser->SetPublish_hpe(m_pub_hpe);
  m_parser->SetPublish_pitch_roll(m_pub_pitch_roll);
  return true;
}

bool GPSDevice::SerialSetup()
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

bool GPSDevice::GeodesySetup()
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

bool GPSDevice::SetParam_PORT(std::string sVal)
{
  // TODO: Validate that sVal is a valid path to describe a serial port
  m_serial_port = sVal;
  if (m_serial_port.empty())
    reportConfigWarning("Mission file parameter PORT must not be blank.");
  return true;
}

bool GPSDevice::SetParam_BAUDRATE(std::string sVal)
{
  if (sVal.empty())
    reportConfigWarning("Mission file parameter BAUDRATE may not be blank.");
  else if (sVal ==   "2400") m_baudrate = 2400;
  else if (sVal ==   "4800") m_baudrate = 4800;
  else if (sVal ==  "19200") m_baudrate = 19200;
  else if (sVal ==  "38400") m_baudrate = 38400;
  else if (sVal ==  "57600") m_baudrate = 57600;
  else if (sVal == "115200") m_baudrate = 115200;
  else
    reportConfigWarning("Mission file parameter BAUDRATE must be one of 2400, 4800, 9600, 19200, 38400, 57600, 115200. Unable to process: " + sVal);
  return true;
}

bool GPSDevice::SetParam_PREFIX(std::string sVal)
{
  // TODO: Check sVal for legal MOOS message name characters
  m_prefix = toupper(sVal);
  size_t strLen = m_prefix.length();
  if (strLen > 0 && m_prefix.at(strLen - 1) != '_')
      m_prefix += "_";
  return true;
}

bool GPSDevice::SetParam_HEADING_SOURCE(std::string sVal)
{
  sVal = toupper(sVal);
  if (sVal.empty() || MOOSStrCmp(sVal, "NONE"))
    m_heading_source = HEADING_SOURCE_NONE;
  else if (MOOSStrCmp(sVal, "GPRMC"))
    m_heading_source = HEADING_SOURCE_GPRMC;
  else if (MOOSStrCmp(sVal, "COMPASS"))
    m_heading_source = HEADING_SOURCE_COMPASS;
  else if (MOOSStrCmp(sVal, "PASHR"))
    m_heading_source = HEADING_SOURCE_PASHR;
  else {
    m_heading_source = HEADING_SOURCE_NONE;
    string err = "Mission file parameter HEADING_SOURCE should be one of: ";
    err       += "NONE, GPRMC, COMPASS, PASHR. ";
    err       += "Value '";
    err       += sVal;
    err       += "' is unknown. Defaulting to NONE.";
    reportConfigWarning(err); }
  return true;
}

bool GPSDevice::SetParam_HEADING_OFFSET(std::string sVal)
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

bool GPSDevice::SetParam_PUBLISH_UTC(std::string sVal)
{
  if (tolower(sVal) == "true")
    m_pub_utc = true;
  else if (tolower(sVal) == "false")
    m_pub_utc = false;
  else {
    string err = "Mission file parameter PUBLISH_UTC must be 'true' or 'false'. ";
    err       += "Unhandled: ";
    err       += sVal;
    err       += ". Defaulting to ";
    err       += (m_pub_utc ? "true" : "false");
    err       += ".";
    reportConfigWarning(err); }
  return true;
}

bool GPSDevice::SetParam_PUBLISH_HPE(std::string sVal)
{
  if (tolower(sVal) == "true")
    m_pub_hpe = true;
  else if (tolower(sVal) == "false")
    m_pub_hpe = false;
  else {
    string err = "Mission file parameter PUBLISH_HPE must be 'true' or 'false'. ";
    err       += "Unhandled: ";
    err       += sVal;
    err       += ". Defaulting to ";
    err       += (m_pub_hpe ? "true" : "false");
    err       += ".";
    reportConfigWarning(err); }
  return true;
}

bool GPSDevice::SetParam_PUBLISH_HDOP(std::string sVal)
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

bool GPSDevice::SetParam_PUBLISH_YAW(std::string sVal)
{
  if (tolower(sVal) == "true")
    m_pub_yaw = true;
  else if (tolower(sVal) == "false")
    m_pub_yaw = false;
  else {
    string err = "Mission file parameter PUBLISH_YAW must be 'true' or 'false'. ";
    err       += "Unhandled: ";
    err       += sVal;
    err       += ". Defaulting to ";
    err       += (m_pub_yaw ? "true" : "false");
    err       += ".";
    reportConfigWarning(err); }
  return true;
}

bool GPSDevice::SetParam_PUBLISH_RAW(std::string sVal)
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

bool GPSDevice::SetParam_PUBLISH_PITCH_ROLL(std::string sVal)
{
  if (tolower(sVal) == "true")
    m_pub_pitch_roll = true;
  else if (tolower(sVal) == "false")
    m_pub_pitch_roll = false;
  else {
    string err = "Mission file parameter PUBLISH_PITCH_ROLL must be 'true' or 'false'. ";
    err       += "Unhandled: ";
    err       += sVal;
    err       += ". Defaulting to ";
    err       += (m_pub_pitch_roll ? "true" : "false");
    err       += ".";
    reportConfigWarning(err); }
  return true;
}

bool GPSDevice::SetParam_SWAP_PITCH_ROLL(std::string sVal)
{
  if (tolower(sVal) == "true")
    m_swap_pitch_roll = true;
  else if (tolower(sVal) == "false")
    m_swap_pitch_roll = false;
  else {
    string err = "Mission file parameter SWAP_PITCH_ROLL must be 'true' or 'false'. ";
    err       += "Unhandled: ";
    err       += sVal;
    err       += ". Defaulting to ";
    err       += (m_swap_pitch_roll ? "true" : "false");
    err       += ".";
    reportConfigWarning(err); }
  return true;
}

bool GPSDevice::SetParam_TRIGGER_MSG(std::string sVal)
{
  m_trigger_key = sVal;
  return true;
}

bool GPSDevice::buildReport()
{
  m_msgs << endl << "SETUP" << endl << "-----" << endl;
  m_msgs << "     PORT (BAUDRATE):         " << m_serial_port << "(" << m_baudrate << ")" << endl;
  m_msgs << "     Publish PREFIX:          " << m_prefix << endl;
  string strSource = "none";
  switch (m_heading_source) {
    case HEADING_SOURCE_COMPASS:    strSource = "compass";                  break;
    case HEADING_SOURCE_GPRMC:      strSource = "GPRMC sentence from GPS";  break;
    case HEADING_SOURCE_PASHR:      strSource = "PASHR sentence from GPS";  break;
    case HEADING_SOURCE_NONE:
    default:                                                                break; }
  m_msgs << "     HEADING_SOURCE:          " << strSource << endl;
  string strPub = "";
  if (m_pub_hdop)       strPub += "HDOP, ";
  if (m_pub_hpe)        strPub += "HPE, ";
  if (m_pub_pitch_roll) strPub += "PITCH, ROLL, ";
  if (m_pub_raw)        strPub += "raw GPS sentences, ";
  if (m_pub_utc)        strPub += "UTC time, ";
  if (m_pub_yaw)        strPub += "YAW, ";
  if (strPub.empty())
    strPub = "No additional publications from GPS";
  else {
    int len = strPub.length();
    if (len > 2)
      strPub = strPub.substr(0, len - 2); }
  m_msgs << "     Additional publications: " << strPub << endl;
  if (m_pub_pitch_roll && m_swap_pitch_roll)
    m_msgs << "   Pitch and Roll:          Values are swapped";
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
    for (;it != m_counters.end(); it++)
      m_msgs << "    " << it->first << ":  " << it->second << endl; }
  return true;
}


























//
