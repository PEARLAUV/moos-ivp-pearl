/*
 * RPISerial.cpp
 * 
 * Created on: 7/20/2021
 * Auther: Henry Crawford-Eng
 * 
 */

#include "MBUtils.h"
#include "RPISerial.h"
#include "NMEAdefs.h"


using namespace std;

RPISERIAL::RPISERIAL()
{
	//MOOS file parameters
	m_prefix        = "RPI";

    //Serial Variables
    m_baudrate = 115200;
    m_serial_port = "/dev/ttyUSB0";
    m_serial = NULL;
    m_bValidSerialConn = false;

    //Serial strings
    m_msgs_from_serial = 0;
    m_last_PLSCC_from_sensor="Nothing received yet";
    m_last_PLLUX_from_sensor= "Nothing received yet";
    m_last_PLWND_from_sensor= "Nothing received yet";
    m_scc_connected = false;
    m_light_connected = false;
    m_wind_connected = false;

    //Values pulled from serial Strings
    //SCC values
    m_pvVoltage = 0.0;
    m_pvCurrent = 0.0;
    m_pvPowerL = 0.0;
    m_pvPowerH = 0.0;
    m_batteryVoltage = 0.0;
    m_batteryCurrent = 0.0;
    m_batteryNetCurrent = 0.0;
    m_batteryPowerL = 0.0;
    m_batteryPowerH = 0.0;
    m_loadVoltage = 0.0;
    m_loadCurrent = 0.0;
    m_loadPowerL = 0.0;
    m_loadPowerH = 0.0;
    m_batterySOC = 0.0;
    m_batteryTemperature = 0.0;
    m_deviceTemperature = 0.0;

    //Light sensor Values
    m_lux = 0.0;
    m_white = 0.0;
    m_rawALS = 0.0;
    
    //Wind Sensor Data
    m_windReading = 0.0;
    m_windSpeed = 0.0;
}

bool RPISERIAL::OnNewMail(MOOSMSG_LIST &NewMail)
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

void RPISERIAL::RegisterForMOOSMessages()
{
	AppCastingMOOSApp::RegisterVariables();
}

bool RPISERIAL::OnStartUp()
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

		if (param == "PORT")            handled = SetParam_PORT(value);
        else if (param == "BAUDRATE")   handled = SetParam_BAUDRATE(value);
        else if (param == "PREFIX")     handled = SetParam_PREFIX(value);
		else
		  reportUnhandledConfigWarning(orig); }
          
    m_bValidSerialConn = SerialSetup();
	
	RegisterForMOOSMessages();
	MOOSPause(500);
	
	return true;
}

bool RPISERIAL::OnConnectToServer()
{
	RegisterForMOOSMessages();
	return true;
}

bool RPISERIAL::Iterate()
{
	AppCastingMOOSApp::Iterate();
	
	GetData();
	AppCastingMOOSApp::PostReport();
	
	return true;
}

bool RPISERIAL::SetParam_PREFIX(string sVal)
{
	m_prefix = toupper(sVal);
	size_t strLen = m_prefix.length();
	if (strLen > 0 && m_prefix.at(strLen -1) != '_')
		m_prefix += "_";
	
	return true;
}

bool RPISERIAL::SetParam_BAUDRATE(std::string sVal)
{
  if (sVal.empty())
    reportConfigWarning("Mission file parameter BAUDRATE must not be blank.");
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

bool RPISERIAL::SetParam_PORT(std::string sVal)
{
  m_serial_port = sVal;
  if (m_serial_port.empty())
    reportConfigWarning("Mission file parameter PORT must not be blank.");
  
  return true;
}

bool RPISERIAL::buildReport()
{
    m_msgs << endl << "SETUP" << endl;
    m_msgs << "----------------------------------------" << endl;
    m_msgs << "   PORT (BAUDRATE):    " << m_serial_port << " (" << m_baudrate << ")" << endl;
    m_msgs << "   Publish PREFIX:     " << m_prefix << endl;
    
    m_msgs << endl << "DEVICE STATUS" << endl;
    m_msgs << "----------------------------------------" << endl;
    if (!m_bValidSerialConn) {
        m_msgs << "*** Not parsing RPI messages due to MOOS configuration error. ***" << endl;
        return true; }
    if (m_serial->IsGoodSerialComms()) {
        m_msgs << "   Serial communicating properly on port " << m_serial_port << " at " << m_baudrate << " baud." << endl;
        m_msgs << "   Messages received from RPI:     " << m_msgs_from_serial << endl; }
    else
        m_msgs << "   Serial not connected to port " << m_serial_port << " at " << m_baudrate << " baud." << endl;
    m_msgs << endl;
    if (m_scc_connected) {
        m_msgs << "   Last PLSCC string received:      " << m_last_PLSCC_from_sensor << endl;
        m_msgs << "   Battery Voltage [V]/Current [A]: " << m_batteryVoltage << " / " << m_batteryCurrent << endl;
        m_msgs << "   PV Voltage [V] / Current [A]:    " << m_pvVoltage << " / " << m_pvCurrent << endl;
        m_msgs << "   Load Voltage [V] / Current [A]:  " << m_loadVoltage << " / " << m_loadCurrent << endl;
        m_msgs << "   Battery SOC [%]:                 " << m_batterySOC << endl;   
    }
    else {
        m_msgs << "   No data received from solar charge controller." << endl;
        m_msgs << "      Last PLSCC string received: " << m_last_PLSCC_from_sensor << endl;
    }
    m_msgs << endl;
    if (m_light_connected) {
        m_msgs << "   Last PLLUX string received:      " << m_last_PLLUX_from_sensor << endl;
        m_msgs << "   Lux:                             " << m_lux << endl;
    }
    else {
        m_msgs << "   No data received from ambient light sensor." << endl;
        m_msgs << "      Last PLLUX string received: " << m_last_PLLUX_from_sensor << endl;
    }
    m_msgs << endl;
    if (m_wind_connected) {
        m_msgs << "   Last PLWND string received:      " << m_last_PLWND_from_sensor << endl;
        m_msgs << "   Wind Speed [m/s]:                " << m_windSpeed << endl;
    }
    else {
        m_msgs << "   No data received from wind sensor." << endl;
        m_msgs << "      Last PLWND string received: " << m_last_PLWND_from_sensor << endl;
    }
    
	return true;
}

bool RPISERIAL::SerialSetup(){
  string errMsg = "";
  m_serial = new SerialComms(m_serial_port, m_baudrate, errMsg);
  if (m_serial->IsGoodSerialComms()) {
    m_serial->Run();
    string msg = "Serial port opened. ";
    msg       += "Communicating on port ";
    msg       += m_serial_port;
    reportEvent(msg);
    return true; }
  reportConfigWarning("Unable to open serial port: " + errMsg);
  return false;
}

void RPISERIAL::GetData()
{
  if (!m_bValidSerialConn)
    return;
  
  //Grab sentences from Arduino and ingest them in the NMEA parser
  while (m_serial->DataAvailable()) {
    string nmea = m_serial->GetNextSentence();
    m_msgs_from_serial++;
    ParseNMEAString(nmea); 
  }
  
}

bool RPISERIAL::ParseNMEAString(string nmea)
{
  size_t len = nmea.length();
  
  if (nmea.empty()) {
    reportRunWarning("Received empty data string from front seat.");
    return false; }
    
  if (!(nmea.at(0) == '$')) {
      reportRunWarning("Line from front seat does not properly start with '$': " + nmea);
      return false; }
  if (!(nmea.at(len - 1) == '*')) {
      reportRunWarning("Line from front seat does not properly terminate with '*': " + nmea);
      return false; }

  string toParse = nmea.substr(0, nmea.size()-1);
  vector<string> parts = parseString(toParse, ',');
  string nmeaHeader = parts[0];
  string sensor_connected = parts[1];
  if   (nmeaHeader == "$PLSCC") {
    m_last_PLSCC_from_sensor = nmea;
    if (sensor_connected=="1") {
        m_scc_connected = true;
        HandlePLSCC(toParse); }
    else
        m_scc_connected = false;
    }
  else if (nmeaHeader == "$PLLUX") {
    m_last_PLLUX_from_sensor = nmea; 
    if (sensor_connected=="1") {
        m_light_connected = true;
        HandlePLLUX(toParse); }
    else
        m_light_connected = false;
    }
  else if (nmeaHeader == "$PLWND") {
    m_last_PLWND_from_sensor = nmea; 
    if (sensor_connected=="1") {
        m_wind_connected = true;
        HandlePLWND(toParse); }
    else
        m_wind_connected = false;
    }
  else {
    reportRunWarning("Line from front seat has improper header: " + nmea);
    return false; }
  
  return true;
}

//Parsing Functions
bool RPISERIAL::HandlePLSCC(string toParse){
    vector<string> parts = parseString(toParse, ',');
    string pvVoltageStr             =parts[2];
    string pvCurrentStr             =parts[3];
    string pvPowerLStr              =parts[4];
    string pvPowerHStr              =parts[5];
    string batteryVoltageStr        =parts[6];
    string batteryCurrentStr        =parts[7];
    string batteryNetCurrentStr     =parts[8];
    string batteryPowerLStr         =parts[9];
    string batteryPowerHStr         =parts[10];
    string loadVoltageStr           =parts[11];
    string loadCurrentStr           =parts[12];
    string loadPowerLStr            =parts[13];
    string loadPowerHStr            =parts[14];
    string batterySOCStr            =parts[15];
    string batteryTemperatureStr    =parts[16];
    string deviceTemperatureStr     =parts[17];

    m_pvVoltage = BAD_DOUBLE;
    m_pvCurrent = BAD_DOUBLE;
    m_pvPowerL = BAD_DOUBLE;
    m_pvPowerH = BAD_DOUBLE;
    m_batteryVoltage = BAD_DOUBLE;
    m_batteryCurrent = BAD_DOUBLE;
    m_batteryNetCurrent = BAD_DOUBLE;
    m_batteryPowerL = BAD_DOUBLE;
    m_batteryPowerH = BAD_DOUBLE;
    m_loadVoltage = BAD_DOUBLE;
    m_loadCurrent = BAD_DOUBLE;
    m_loadPowerL = BAD_DOUBLE;
    m_loadPowerH = BAD_DOUBLE;
    m_batterySOC = BAD_DOUBLE;
    m_batteryTemperature = BAD_DOUBLE;
    m_deviceTemperature = BAD_DOUBLE;
    
    if(!pvVoltageStr.empty())
        m_pvVoltage = stod(pvVoltageStr);
    if(!pvCurrentStr.empty())
        m_pvCurrent = stod(pvCurrentStr);
    if(!pvPowerLStr.empty())
        m_pvPowerL = stod(pvPowerLStr);
    if(!pvPowerHStr.empty())
        m_pvPowerH = stod(pvPowerHStr);
    if(!batteryVoltageStr.empty())
        m_batteryVoltage = stod(batteryVoltageStr);
    if(!batteryCurrentStr.empty())
        m_batteryCurrent = stod(batteryCurrentStr);
    if(!batteryNetCurrentStr.empty())
        m_batteryNetCurrent = stod(batteryNetCurrentStr);
    if(!batteryPowerLStr.empty())
        m_batteryPowerL = stod(batteryPowerLStr);
    if(!batteryPowerHStr.empty())
        m_batteryPowerH = stod(batteryPowerHStr);
    if(!loadVoltageStr.empty())
        m_loadVoltage = stod(loadVoltageStr);
    if(!loadCurrentStr.empty())
        m_loadCurrent = stod(loadCurrentStr);
    if(!loadPowerLStr.empty())
        m_loadPowerL = stod(loadPowerLStr);
    if(!loadPowerHStr.empty())
        m_loadPowerH = stod(loadPowerHStr);
    if(!batterySOCStr.empty())
        m_batterySOC = stod(batterySOCStr);
    if(!batteryTemperatureStr.empty())
        m_batteryTemperature = stod(batteryTemperatureStr);
    if(!deviceTemperatureStr.empty())
        m_deviceTemperature = stod(deviceTemperatureStr);
    
    PublishPLSCCRaw(m_pvVoltage, m_pvCurrent, m_pvPowerL, m_pvPowerH, m_batteryVoltage, m_batteryCurrent, m_batteryNetCurrent, m_batteryPowerL, m_batteryPowerH, m_loadVoltage, m_loadCurrent, m_loadPowerL, m_loadPowerH, m_batterySOC, m_batteryTemperature, m_deviceTemperature); 

    return true;
}

bool RPISERIAL::HandlePLLUX(string toParse){

    vector<string> parts = parseString(toParse, ',');
    string luxStr = parts[2];
    string whiteStr = parts[3];
    string rawALSStr = parts[4];

    if(!luxStr.empty())
        m_lux = stod(luxStr);
    if(!whiteStr.empty())
        m_white = stod(whiteStr);
    if(!rawALSStr.empty())
        m_rawALS = stod(rawALSStr);

    PublishPLLUXRaw(m_lux, m_white, m_rawALS);

    return true;
}

bool RPISERIAL::HandlePLWND(string toParse){

    vector<string> parts = parseString(toParse, ',');
    
    string windReadingStr = parts[2];
    string windSpeedStr = parts[3];
    
    if(!windReadingStr.empty())
        m_windReading = stod(windReadingStr);
    if(!windReadingStr.empty())
        m_windSpeed = stod(windSpeedStr);
        
    PublishPLWNDRaw(m_windReading, m_windSpeed);
    
    return true;
}

//Publishing Functions
void RPISERIAL::PublishPLSCCRaw(double pvVoltage,    double pvCurrent,    double pvPowerL,    double pvPowerH,    double batteryVoltage,    double batteryCurrent,    double batteryNetCurrent,    double batteryPowerL,    double batteryPowerH,    double loadVoltage,    double loadCurrent,    double loadPowerL,    double loadPowerH,    double batterySOC,    double batteryTemperature,    double deviceTemperature){
    m_Comms.Notify(m_prefix + "PV_VOLTAGE",pvVoltage);
    m_Comms.Notify(m_prefix + "PV_CURRENT",pvCurrent);
    m_Comms.Notify(m_prefix + "PV_POWER_L",pvPowerL);
    m_Comms.Notify(m_prefix + "PV_POWER_H",pvPowerH);
    m_Comms.Notify(m_prefix + "BATTERY_VOLTAGE",batteryVoltage);
    m_Comms.Notify(m_prefix + "BATTERY_CURRENT",batteryCurrent);
    m_Comms.Notify(m_prefix + "BATTERY_NET_CURRENT",batteryNetCurrent);
    m_Comms.Notify(m_prefix + "BATTERY_POWER_L",batteryPowerL);
    m_Comms.Notify(m_prefix + "BATTERY_POWER_H",batteryPowerH);
    m_Comms.Notify(m_prefix + "LOAD_VOLTAGE",loadVoltage);
    m_Comms.Notify(m_prefix + "LOAD_CURRENT",loadCurrent);
    m_Comms.Notify(m_prefix + "LOAD_POWER_L",loadPowerL);
    m_Comms.Notify(m_prefix + "LOAD_POWER_H",loadPowerH);
    m_Comms.Notify(m_prefix + "BATTERY_SOC",batterySOC);
    m_Comms.Notify(m_prefix + "BATTERY_TEMP",batteryTemperature);
    m_Comms.Notify(m_prefix + "DEVICE_TEMP",deviceTemperature);
}
void RPISERIAL::PublishPLLUXRaw(double lux, double white, double rawALS){
    m_Comms.Notify(m_prefix + "LUX",lux);
    m_Comms.Notify(m_prefix + "WHITE",white);
    m_Comms.Notify(m_prefix + "RAW_ALS",rawALS);
}
void RPISERIAL::PublishPLWNDRaw(double windReading, double windSpeed){
    m_Comms.Notify(m_prefix + "WIND_READING",windReading);
    m_Comms.Notify(m_prefix + "WIND_SPEED", windSpeed);
}




