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
	m_prefix        = "RPISER";

    //Serial Variables
    m_baudrate = 115200;
    m_serial_port = "/dev/ttyUSB0";
    m_serial = NULL;
    m_bValidSerialConn = false;

    //Serial strings
    m_msgs_from_serial = 0;
    m_last_SCC_from_sensor="Nothing received yet";
    m_last_Light_from_sensor= "Nothing received yet";
    m_last_Wind_from_sensor= "Nothing received yet";

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

		if (param == "PREFIX")          handled = SetParam_PREFIX(value);
        else if (param == "PORT")       handled = SetParam_PORT(value);
        else if (param == "BAUDRATE")   handled = SetParam_BAUDRATE(value);
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
    m_msgs << endl << "SERIAL PORT:     " << m_serial_port << endl;
    m_msgs << endl << "IS CONNECTED:    " << m_bValidSerialConn<< endl;
    m_msgs << endl << "SCC STRING:      " << m_last_SCC_from_sensor << endl;
    m_msgs << endl << "LIGHT STRING:    " << m_last_Light_from_sensor << endl;
    m_msgs << endl << "WIND STRING:     " << m_last_Wind_from_sensor << endl;
    m_msgs << endl << "SCC BAT VOLTAGE: " << m_batteryVoltage << endl;
    m_msgs << endl << "LIGHT WHITE:     " << m_white << endl;
    m_msgs << endl << "WIND SPEED:      " << m_windSpeed << endl;
    
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
  if   (nmeaHeader == "$PLSCC") {
    m_last_SCC_from_sensor = nmea; 
    HandleSCC(toParse); }
  else if (nmeaHeader == "$PLLUX") {
    m_last_Light_from_sensor = nmea; 
    HandleLight(toParse); }
  else if (nmeaHeader == "$PLWND") {
    m_last_Wind_from_sensor = nmea; 
    HandleWind(toParse); }
  else {
    reportRunWarning("Line from front seat has improper header: " + nmea);
    return false; }
  
  return true;
}

//Parsing Functions
bool RPISERIAL::HandleSCC(string toParse){
    vector<string> parts = parseString(toParse, ',');
    string pvVoltageStr             =parts[1];
    string pvCurrentStr             =parts[2];
    string pvPowerLStr              =parts[3];
    string pvPowerHStr              =parts[4];
    string batteryVoltageStr        =parts[5];
    string batteryCurrentStr        =parts[6];
    string batteryNetCurrentStr     =parts[7];
    string batteryPowerLStr         =parts[8];
    string batteryPowerHStr         =parts[9];
    string loadVoltageStr           =parts[10];
    string loadCurrentStr           =parts[11];
    string loadPowerLStr            =parts[12];
    string loadPowerHStr            =parts[13];
    string batterySOCStr            =parts[14];
    string batteryTemperatureStr    =parts[15];
    string deviceTemperatureStr     =parts[16];

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
    
    if(!pvVoltageStr.empty()){
        m_pvVoltage = stod(pvVoltageStr);
    }
    if(!pvCurrentStr.empty()){
        m_pvCurrent = stod(pvCurrentStr);
    }
    if(!pvPowerLStr.empty()){
        m_pvPowerL = stod(pvPowerLStr);
    }
    if(!pvPowerHStr.empty()){
        m_pvPowerH = stod(pvPowerHStr);
    }
    if(!batteryVoltageStr.empty()){
        m_batteryVoltage = stod(batteryVoltageStr);
    }
    if(!batteryCurrentStr.empty()){
        m_batteryCurrent = stod(batteryCurrentStr);
    }
    if(!batteryNetCurrentStr.empty()){
        m_batteryNetCurrent = stod(batteryNetCurrentStr);
    }
    if(!batteryPowerLStr.empty()){
        m_batteryPowerL = stod(batteryPowerLStr);
    }
    if(!batteryPowerHStr.empty()){
        m_batteryPowerH = stod(batteryPowerHStr);
    }
    if(!loadVoltageStr.empty()){
        m_loadVoltage = stod(loadVoltageStr);
    }
    if(!loadCurrentStr.empty()){
        m_loadCurrent = stod(loadCurrentStr);
    }
    if(!loadPowerLStr.empty()){
        m_loadPowerL = stod(loadPowerLStr);
    }
    if(!loadPowerHStr.empty()){
        m_loadPowerH = stod(loadPowerHStr);
    }
    if(!batterySOCStr.empty()){
        m_batterySOC = stod(batterySOCStr);
    }
    if(!batteryTemperatureStr.empty()){
        m_batteryTemperature = stod(batteryTemperatureStr);
    }
    if(!deviceTemperatureStr.empty()){
        m_deviceTemperature = stod(deviceTemperatureStr);
    }
    
    PublishSCCRaw(m_pvVoltage, m_pvCurrent, m_pvPowerL, m_pvPowerH, m_batteryVoltage, m_batteryCurrent, m_batteryNetCurrent, m_batteryPowerL, m_batteryPowerH, m_loadVoltage, m_loadCurrent, m_loadPowerL, m_loadPowerH, m_batterySOC, m_batteryTemperature, m_deviceTemperature); 

    return true;
}

bool RPISERIAL::HandleLight(string toParse){

    vector<string> parts = parseString(toParse, ',');
    string luxStr = parts[1];
    string whiteStr = parts[2];
    string rawALSStr = parts[3];

    if(!luxStr.empty()){
        m_lux = stod(luxStr);
    }
    if(!whiteStr.empty()){
        m_white = stod(whiteStr);
    }
    if(!rawALSStr.empty()){
        m_rawALS = stod(rawALSStr);
    }

    PublishLightRaw(m_lux, m_white, m_rawALS);

    return true;
}

bool RPISERIAL::HandleWind(string toParse){

    vector<string> parts = parseString(toParse, ',');
    
    string windReadingStr = parts[1];
    string windSpeedStr = parts[2];
    if(!windReadingStr.empty())
    {
        m_windReading = stod(windReadingStr);
    }
    if(!windReadingStr.empty())
    {
        m_windSpeed = stod(windSpeedStr);
    }
    PublishWindRaw(m_windReading, m_windSpeed);
    
    return true;
}

//Publishing Functions
void RPISERIAL::PublishSCCRaw(double pvVoltage,    double pvCurrent,    double pvPowerL,    double pvPowerH,    double batteryVoltage,    double batteryCurrent,    double batteryNetCurrent,    double batteryPowerL,    double batteryPowerH,    double loadVoltage,    double loadCurrent,    double loadPowerL,    double loadPowerH,    double batterySOC,    double batteryTemperature,    double deviceTemperature){
    m_Comms.Notify("CHG_PV_VOLTAGE",pvVoltage);
    m_Comms.Notify("CHG_PV_CURRENT",pvCurrent);
    m_Comms.Notify("CHG_PV_POWER_L",pvPowerL);
    m_Comms.Notify("CHG_PV_POWER_H",pvPowerH);
    m_Comms.Notify("CHG_BATTERY_VOLTAGE",batteryVoltage);
    m_Comms.Notify("CHG_BATTERY_CURRENT",batteryCurrent);
    m_Comms.Notify("CHG_BATTERY_NET_CURRENT",batteryNetCurrent);
    m_Comms.Notify("CHG_BATTERY_POWER_L",batteryPowerL);
    m_Comms.Notify("CHG_BATTERY_POWER_H",batteryPowerH);
    m_Comms.Notify("CHG_LOAD_VOLTAGE",loadVoltage);
    m_Comms.Notify("CHG_LOAD_CURRENT",loadCurrent);
    m_Comms.Notify("CHG_LOAD_POWER_L",loadPowerL);
    m_Comms.Notify("CHG_LOAD_POWER_H",loadPowerH);
    m_Comms.Notify("CHG_BATTERY_SOC",batterySOC);
    m_Comms.Notify("CHG_BATTERY_TEMP",batteryTemperature);
    m_Comms.Notify("CHG_DEVICE_TEMP",deviceTemperature);
}
void RPISERIAL::PublishLightRaw(double lux, double white, double rawALS){
    m_Comms.Notify("LIGHT_LUX",lux);
    m_Comms.Notify("LIGHT_WHITE",white);
    m_Comms.Notify("LIGHT_RAW_ALS",rawALS);
}
void RPISERIAL::PublishWindRaw(double windReading, double windSpeed){
    m_Comms.Notify("WIND_READING",windReading);
    m_Comms.Notify("WIND_SPEED", windSpeed);
}




