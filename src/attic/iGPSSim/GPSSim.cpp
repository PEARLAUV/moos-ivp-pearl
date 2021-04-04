/*
 * GPSSim.cpp
 *
 *  Created on: Nov 6, 2014
 *      Author: Alon Yaari
 */


#include "MBUtils.h"
#include "GPSSim.h"

using namespace std;

GPSSim::GPSSim()
{
  m_msg_lat     = "SIM_LAT";
  m_msg_lon     = "SIM_LONG";
  m_msg_heading = "SIM_HEADING";
  m_msg_speed   = "SIM_SPEED";

  m_lat         = 0.0;
  m_lon         = 0.0;
  m_heading     = 0.0;
  m_speed       = 0.0;

  m_publish_GPGGA = true;
  m_publish_GPGST = false;
  m_publish_GPHDT = false;
  m_publish_GPRMC = true;
  m_publish_GPRME = false;
  m_publish_GPTXT = false;
  m_publish_PASHR = false;

  m_count_GPGGA   = 0;
  m_count_GPGST   = 0;
  m_count_GPHDT   = 0;
  m_count_GPRMC   = 0;
  m_count_GPRME   = 0;
  m_count_GPTXT   = 0;
  m_count_PASHR   = 0;

  m_curMOOSTime   = MOOSTime();
  m_goodParams    = false;
  m_serialPort    = "";
  m_baudRate      = 19200;
  m_input_prefix  = "SIM";
  m_serial        = NULL;
  m_lastSerialTry = 0.0;
  m_count_serial  = 0;
}

bool GPSSim::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);
  MOOSMSG_LIST::iterator p;
  for (p=NewMail.begin(); p!=NewMail.end(); ++p) {
      CMOOSMsg & rMsg = *p;
      if (MOOSStrCmp(rMsg.GetKey(), m_msg_lat))
        m_lat = p->GetDouble();
      else if (MOOSStrCmp(rMsg.GetKey(), m_msg_lon))
        m_lon = p->GetDouble();
      else if (MOOSStrCmp(rMsg.GetKey(), m_msg_heading))
        m_heading = p->GetDouble();
      else if (MOOSStrCmp(rMsg.GetKey(), m_msg_speed))
        m_speed = p->GetDouble(); }
  return UpdateMOOSVariables(NewMail);
}

bool GPSSim::Iterate()
{
  AppCastingMOOSApp::Iterate();
  m_curMOOSTime = MOOSTime();
  bool bGoodSComms = (m_serial && m_serial->IsGoodSerialComms());
  if (bGoodSComms) {
    PublishGPSSentences(); }
  else if (m_goodParams) {
    double timeDiff = m_curMOOSTime - m_lastSerialTry;
    if (timeDiff > SERIAL_TRY_DELAY) {
      m_lastSerialTry = m_curMOOSTime;
      SerialSetup();
      m_count_serial++; } }
  AppCastingMOOSApp::PostReport();
  return true;
}

bool GPSSim::OnConnectToServer()
{
  return false;
}

bool GPSSim::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();

  STRING_LIST sParams;
  if (!m_MissionReader.GetConfiguration(GetAppName(), sParams))
    reportConfigWarning("No config block found for " + GetAppName());

  STRING_LIST::iterator p;
  for (p = sParams.begin(); p != sParams.end(); p++) {
    string orig  = *p;
    string line  = *p;
    string param = toupper(biteStringX(line, '='));
    string value = line;

    if (param == "PORT")
      m_goodParams = SetParam_PORT(value);
    else if (param == "BAUDRATE")
      m_goodParams = SetParam_BAUDRATE(value);
    else if (param == "INPUT_PREFIX")
      m_goodParams = SetParam_INPUT_PREFIX(value);
    else if (param == "GPGGA")
      m_goodParams = SetParam_GPGGA(value);
    else if (param == "GPGST")
      m_goodParams = SetParam_GPGST(value);
    else if (param == "GPHDT")
      m_goodParams = SetParam_GPHDT(value);
    else if (param == "GPRMC")
      m_goodParams = SetParam_GPRMC(value);
    else if (param == "GPRME")
      m_goodParams = SetParam_GPRME(value);
    else if (param == "GPTXT")
      m_goodParams = SetParam_GPTXT(value);
    else if (param == "PASHR")
      m_goodParams = SetParam_PASHR(value);
    else
      reportUnhandledConfigWarning(orig); }

  if (!m_goodParams)
    reportConfigWarning("Invalid mission file parameters. GPS data will not be parsed.");
  else
    RegisterForMOOSMessages();
  return true;
}

bool GPSSim::RegisterForMOOSMessages()
{
  AppCastingMOOSApp::RegisterVariables();
  m_msg_lat     = m_input_prefix + "LAT";
  m_msg_lon     = m_input_prefix + "LONG";
  m_msg_speed   = m_input_prefix + "SPEED";
  m_msg_heading = m_input_prefix + "HEADING";
  m_Comms.Register(m_msg_lat, 0);
  m_Comms.Register(m_msg_lon, 0);
  m_Comms.Register(m_msg_heading, 0);
  m_Comms.Register(m_msg_speed, 0);
  return RegisterMOOSVariables();

}

bool GPSSim::SerialSetup()
{
  if (m_serial && m_serial->IsGoodSerialComms())
    return true;
  string errMsg = "";
  m_serial = new SerialComms(m_serialPort, m_baudRate, errMsg);
  if (m_serial->IsGoodSerialComms()) {
    m_serial->Set_Delims('$', '\n', "\r");
    m_serial->Run();
    string msg = "Serial port opened. ";
    msg       += "Communicating on port ";
    msg       += m_serialPort;
    reportEvent(msg);
    return true; }
  reportConfigWarning("Unable to open serial port: " + errMsg);
  return false;
}

bool GPSSim::SetParam_PORT(std::string sVal)
{
  // TODO: Validate that sVal is a valid path to describe a serial port
  m_serialPort = sVal;
  if (m_serialPort.empty())
    reportConfigWarning("Mission file parameter PORT must not be blank.");
  return true;
}

bool GPSSim::SetParam_BAUDRATE(std::string sVal)
{
  if (sVal.empty())
    reportConfigWarning("Mission file parameter BAUDRATE may not be blank.");
  else if (sVal ==   "2400") m_baudRate = 2400;
  else if (sVal ==   "4800") m_baudRate = 4800;
  else if (sVal ==  "19200") m_baudRate = 19200;
  else if (sVal ==  "38400") m_baudRate = 38400;
  else if (sVal ==  "57600") m_baudRate = 57600;
  else if (sVal == "115200") m_baudRate = 115200;
  else
    reportConfigWarning("Mission file parameter BAUDRATE must be one of 2400, 4800, 9600, 19200, 38400, 57600, 115200. Unable to process: " + sVal);
  return true;
}

bool GPSSim::SetParam_INPUT_PREFIX(std::string sVal)
{
  // TODO: Check sVal for legal MOOS message name characters
  m_input_prefix = toupper(sVal);
  size_t strLen = m_input_prefix.length();
  if (strLen > 0 && m_input_prefix.at(strLen - 1) != '_')
    m_input_prefix += "_";
  return true;
}

bool GPSSim::SetParam_GPGGA(std::string sVal)
{
  if (tolower(sVal) == "true")
    m_publish_GPGGA = true;
  else if (tolower(sVal) == "false")
    m_publish_GPGGA = false;
  else {
    string err = "Mission file parameter GPGGA must be 'true' or 'false'. ";
    err       += "Unhandled: ";
    err       += sVal;
    err       += ". Defaulting to ";
    err       += (m_publish_GPGGA ? "true" : "false");
    err       += ".";
    reportConfigWarning(err); }
  return true;
}

bool GPSSim::SetParam_GPGST(std::string sVal)
{
  if (tolower(sVal) == "true")
    m_publish_GPGST = true;
  else if (tolower(sVal) == "false")
    m_publish_GPGST = false;
  else {
    string err = "Mission file parameter GPGST must be 'true' or 'false'. ";
    err       += "Unhandled: ";
    err       += sVal;
    err       += ". Defaulting to ";
    err       += (m_publish_GPGST ? "true" : "false");
    err       += ".";
    reportConfigWarning(err); }
  return true;
}

bool GPSSim::SetParam_GPHDT(std::string sVal)
{
  if (tolower(sVal) == "true")
    m_publish_GPHDT = true;
  else if (tolower(sVal) == "false")
    m_publish_GPHDT = false;
  else {
    string err = "Mission file parameter GPHDT must be 'true' or 'false'. ";
    err       += "Unhandled: ";
    err       += sVal;
    err       += ". Defaulting to ";
    err       += (m_publish_GPHDT ? "true" : "false");
    err       += ".";
    reportConfigWarning(err); }
  return true;
}

bool GPSSim::SetParam_GPRMC(std::string sVal)
{
  if (tolower(sVal) == "true")
    m_publish_GPRMC = true;
  else if (tolower(sVal) == "false")
    m_publish_GPRMC = false;
  else {
    string err = "Mission file parameter GPRMC must be 'true' or 'false'. ";
    err       += "Unhandled: ";
    err       += sVal;
    err       += ". Defaulting to ";
    err       += (m_publish_GPRMC ? "true" : "false");
    err       += ".";
    reportConfigWarning(err); }
  return true;
}

bool GPSSim::SetParam_GPRME(std::string sVal)
{
  if (tolower(sVal) == "true")
    m_publish_GPRME = true;
  else if (tolower(sVal) == "false")
    m_publish_GPRME = false;
  else {
    string err = "Mission file parameter GPRME must be 'true' or 'false'. ";
    err       += "Unhandled: ";
    err       += sVal;
    err       += ". Defaulting to ";
    err       += (m_publish_GPRME ? "true" : "false");
    err       += ".";
    reportConfigWarning(err); }
  return true;
}

bool GPSSim::SetParam_GPTXT(std::string sVal)
{
  if (tolower(sVal) == "true")
    m_publish_GPTXT = true;
  else if (tolower(sVal) == "false")
    m_publish_GPTXT = false;
  else {
    string err = "Mission file parameter GPTXT must be 'true' or 'false'. ";
    err       += "Unhandled: ";
    err       += sVal;
    err       += ". Defaulting to ";
    err       += (m_publish_GPTXT ? "true" : "false");
    err       += ".";
    reportConfigWarning(err); }
  return true;
}

bool GPSSim::SetParam_PASHR(std::string sVal)
{
  if (tolower(sVal) == "true")
    m_publish_PASHR = true;
  else if (tolower(sVal) == "false")
    m_publish_PASHR = false;
  else {
    string err = "Mission file parameter PASHR must be 'true' or 'false'. ";
    err       += "Unhandled: ";
    err       += sVal;
    err       += ". Defaulting to ";
    err       += (m_publish_PASHR ? "true" : "false");
    err       += ".";
    reportConfigWarning(err); }
  return true;
}

bool GPSSim::PublishGPSSentences()
{
  bool bGood = true;
  string nmeaToSend;
  if (m_publish_GPGGA) {
    nmeaToSend = ProduceGPGGA();
    bGood &= PublishToSerial(nmeaToSend);
    m_count_GPGGA++; }
  if (m_publish_GPGST) {
    bGood &= PublishToSerial(ProduceGPGST());
    m_count_GPGST++; }
  if (m_publish_GPHDT) {
    bGood &= PublishToSerial(ProduceGPHDT());
    m_count_GPHDT++; }
  if (m_publish_GPRMC) {
    nmeaToSend = ProduceGPRMC();
    bGood &= PublishToSerial(nmeaToSend);
    m_count_GPRMC++; }
  if (m_publish_GPRME) {
    bGood &= PublishToSerial(ProduceGPRME());
    m_count_GPRME++; }
  if (m_publish_GPTXT) {
    bGood &= PublishToSerial(ProduceGPTXT());
    m_count_GPTXT++; }
  if (m_publish_PASHR) {
    bGood &= PublishToSerial(ProducePASHR());
    m_count_PASHR++; }

  return bGood;
}

bool GPSSim::PublishToSerial(string nmea)
{
  if (nmea.empty())
    return true;
  if (m_serial->IsGoodSerialComms()) {
    bool bGoodWrite = m_serial->WriteToSerialPort(nmea);
    if (bGoodWrite)
      return true;
    reportRunWarning("Could not write to serial port."); }
  return false;
}

string GPSSim::ProduceGPGGA()
{
  string nmea = "";
  GPGGAnmea gga;
  bool bGood = true;

  // UTC Time
  utcTime t;
  t.Set_utcTimeFromMOOSTime(m_curMOOSTime);
  bGood &= gga.Set_timeUTC(t);

  // Lat and Lon
  NMEAgeog lat, lon;
  lat.StoreDouble(GEOG_LAT, m_lat);
  lon.StoreDouble(GEOG_LON, m_lon);
  bGood &= gga.Set_latlon(lat, lon);

  // GPS Quality
  bGood &= gga.Set_gpsQual('2');

  // Sat num
  int seed = (int) (m_curMOOSTime / 100.0);
  unsigned short int satNum = 5 + (seed % 10);
  bGood &= gga.Set_satNum(satNum);

  // HDOP
  double hdop = 5.0;
  // TODO: Improve a random hdop value
  bGood &= gga.Set_hdop(hdop);

  // Alt
  double alt = 99.9;
  // TODO: Improve random alt value;
  bGood &= gga.Set_altGeoid(alt);
  bGood &= gga.Set_altMSL(alt);

  bGood &= gga.ProduceNMEASentence(nmea);
  if (bGood)
    return nmea;
  return "";
}

string GPSSim::ProduceGPRMC()
{
  string nmea = "";
  GPRMCnmea rmc;
  bool bGood = true;

  // UTC Time
  utcTime t;
  t.Set_utcTimeFromMOOSTime(m_curMOOSTime);
  bGood &= rmc.Set_timeUTC(t);

  // GPS Status
  bGood &= rmc.Set_status('A');

  // Lat and Lon
  NMEAgeog lat, lon;
  lat.StoreDouble(GEOG_LAT, m_lat);
  lon.StoreDouble(GEOG_LON, m_lon);
  bGood &= rmc.Set_latlon(lat, lon);

  // Speed over ground
  bGood &= rmc.Set_speedMPS(m_speed);

  // Course over ground (heading)
  bGood &= rmc.Set_headingTrueN(m_heading);

  // UTC Date
  utcDate d;
  d.Set_utcDateFromMOOSTime(m_curMOOSTime);
  bGood &= rmc.Set_dateUTC(d);

  // MagVar
  bGood &= rmc.Set_magVarValue(0.0);

  // Mode indicator
  bGood &= rmc.Set_modeIndicator('D');

  bGood &= rmc.ProduceNMEASentence(nmea);
  if (bGood)
    return nmea;
  return "";
}

string GPSSim::ProduceGPGST()
{
  string nmea = "";

  return nmea;
}

string GPSSim::ProduceGPHDT()
{
  string nmea = "";

  return nmea;
}

string GPSSim::ProduceGPRME()
{
  string nmea = "";

  return nmea;
}

string GPSSim::ProduceGPTXT()
{
  string nmea = "";

  return nmea;
}

string GPSSim::ProducePASHR()
{
  string nmea = "";

  return nmea;
}

bool GPSSim::buildReport()
{
  if (!m_goodParams) {
    m_msgs << "Invalid configuration parameters, nothing can happen until config is repaired." << endl;
    return true; }
  m_msgs << "Registered for:" << endl;
  m_msgs << "                " << m_msg_lat << "   " << m_lat << endl;
  m_msgs << "                " << m_msg_lon << "   " << m_lon << endl;
  m_msgs << "                " << m_msg_heading << "   " << m_heading << endl;
  m_msgs << "                " << m_msg_speed << "   " << m_speed << endl;
  if (m_serial->IsGoodSerialComms()) {
    m_msgs << "Serial Comms" << endl;
    m_msgs << "             Port " << m_serialPort << "  Baudrate " << m_baudRate << endl;
    m_msgs << "Publishing: " << endl;
    m_msgs << "            GPGGA  ";
    if (m_publish_GPGGA)  m_msgs << m_count_GPGGA;
    else                  m_msgs << "OFF";
    m_msgs << endl;
    m_msgs << "            GPGST  ";
    if (m_publish_GPGST)  m_msgs << m_count_GPGST;
    else                  m_msgs << "OFF";
    m_msgs << endl;
    m_msgs << "            GPGGA  ";
    if (m_publish_GPHDT)  m_msgs << m_count_GPHDT;
    else                  m_msgs << "OFF";
    m_msgs << endl;
    m_msgs << "            GPRMC  ";
    if (m_publish_GPRMC)  m_msgs << m_count_GPRMC;
    else                  m_msgs << "OFF";
    m_msgs << endl;
    m_msgs << "            GPRME  ";
    if (m_publish_GPRME)  m_msgs << m_count_GPRME;
    else                  m_msgs << "OFF";
    m_msgs << endl;
    m_msgs << "            GPTXT  ";
    if (m_publish_GPTXT)  m_msgs << m_count_GPTXT;
    else                  m_msgs << "OFF";
    m_msgs << endl;
    m_msgs << "            PASHR  ";
    if (m_publish_PASHR)  m_msgs << m_count_PASHR;
    else                  m_msgs << "OFF";
    m_msgs << endl;

    m_msgs << "Input data prefix:" << endl;
    m_msgs << "                   " << m_input_prefix << endl; }
  else {
    m_msgs << "No serial comms. Attempting to connect, try " << m_count_serial << endl;
    m_msgs << "Configured for port " << m_serialPort << ", baudrate " << m_baudRate << endl; }
    return true;
}






















//
