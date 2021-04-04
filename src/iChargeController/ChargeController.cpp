/*
 * ChargeController.cpp
 * 
 * Created on: 2/6/2021
 * Author: Ethan Rolland
 *
 */

#include "NMEAdefs.h"
#include "MBUtils.h"
#include "ChargeController.h"

using namespace std;

SCC::SCC()
{
  //MOOS file parameters
  m_serial_port           = "/dev/ttyXRUSB0";
  m_baudrate              = 115200;
  m_prefix                = "CHG";
  m_read_data             = false;
  m_max_thrust            = 0.0;
  m_max_rudder            = 0.0;

  m_bValidModbusConn      = false;
  m_maxTurnOnRetries      = 10;
  
  currMaxThrust           = 0.0;
  currMaxRudder           = 0.0;
  
  //Publish name
  m_pubNameMaxThrust      = "MAX_THRUST";
  m_pubNameMaxRudder      = "MAX_RUDDER";
  
  //Appcast details
  m_reads_from_device     = 0;
  
  //SCC data
  m_pvVoltage             = 0.0; 
  m_pvCurrent             = 0.0;
  m_pvPower               = 0.0;
  m_batteryVoltage        = 0.0;
  m_batteryCurrent        = 0.0;
  m_batteryPower          = 0.0;
  m_loadVoltage           = 0.0;
  m_loadCurrent           = 0.0;
  m_loadPower             = 0.0;
  m_batteryTemp           = 0.0;
  m_deviceTemp            = 0.0;
  
  m_batterySOC            = 0.0;
  m_batteryNetCurr        = 0.0;
  
}

bool SCC::OnNewMail(MOOSMSG_LIST &NewMail)
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

void SCC::RegisterForMOOSMessages()
{
  AppCastingMOOSApp::RegisterVariables();
}

bool SCC::OnStartUp()
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

    if      (param == "PORT")            handled = SetParam_PORT(value);
    else if (param == "BAUDRATE")        handled = SetParam_BAUDRATE(value);
    else if (param == "PREFIX")          handled = SetParam_PREFIX(value);
    else if (param == "READ_DATA")       handled = SetParam_READ_DATA(value);
    else if (param == "MAX_THRUST")      handled = SetParam_MAX_THRUST(value);
    else if (param == "MAX_RUDDER")      handled = SetParam_MAX_RUDDER(value);
    else
      reportUnhandledConfigWarning(orig); }

  SetPublishNames();
  
  m_bValidModbusConn = ConnectToSCC(m_serial_port,m_baudrate);
  
  currMaxThrust = m_max_thrust;
	m_Comms.Notify(m_pubNameMaxThrust, currMaxThrust); 
    
  currMaxRudder = m_max_rudder;
  m_Comms.Notify(m_pubNameMaxRudder, currMaxRudder);
  
  RegisterForMOOSMessages();
  MOOSPause(500);

  return true;
}

bool SCC::OnConnectToServer()
{
	RegisterForMOOSMessages();
	return true;
}

bool SCC::Iterate()
{
  AppCastingMOOSApp::Iterate();
  
  if (m_bValidModbusConn) {
    if (m_read_data) {
      m_reads_from_device++;
      m_scc->updateAll();
      m_pvVoltage = m_scc->getPVVolt();
      m_pvCurrent = m_scc->getPVCurr();
      m_pvPower = m_scc->getPVPower();
      m_batteryVoltage = m_scc->getBatteryVolt();
      m_batteryCurrent = m_scc->getBatteryCurr();
      m_batteryPower = m_scc->getBatteryPower();
      m_loadVoltage = m_scc->getLoadVolt();
      m_loadCurrent = m_scc->getLoadCurr();
      m_loadPower = m_scc->getLoadPower();
      m_batteryTemp = m_scc->getBatteryTemp();
      m_deviceTemp = m_scc->getDeviceTemp();
      m_batterySOC = m_scc->getBatterySOC();
      m_batteryNetCurr = m_scc->getBatteryNetCurr();
      m_Comms.Notify("CHG_PV_VOLTAGE",m_pvVoltage);
      m_Comms.Notify("CHG_PV_CURRENT",m_pvCurrent);
      m_Comms.Notify("CHG_PV_POWER",m_pvPower);
      m_Comms.Notify("CHG_BATTERY_VOLTAGE",m_batteryVoltage);
      m_Comms.Notify("CHG_BATTERY_CURRENT",m_batteryCurrent);
      m_Comms.Notify("CHG_BATTERY_POWER",m_batteryPower);
      m_Comms.Notify("CHG_LOAD_VOLTAGE",m_loadVoltage);
      m_Comms.Notify("CHG_LOAD_CURRENT",m_loadCurrent);
      m_Comms.Notify("CHG_LOAD_POWER",m_loadPower);
      m_Comms.Notify("CHG_BATTERY_TEMP",m_batteryTemp);
      m_Comms.Notify("CHG_DEVICE_TEMP",m_deviceTemp);
      m_Comms.Notify("CHG_BATTERY_SOC",m_batterySOC);
      m_Comms.Notify("CHG_BATTERY_NET_CURRENT",m_batteryNetCurr); } }
  else {
    reportConfigWarning("Unable to start Modbus connection."); }
	
  AppCastingMOOSApp::PostReport();
  
  return true;
}

bool SCC::SetParam_PORT(std::string sVal)
{
  m_serial_port = sVal;
  if (m_serial_port.empty())
    reportConfigWarning("Mission file parameter PORT must not be blank.");
  
  return true;
}

bool SCC::SetParam_BAUDRATE(std::string sVal)
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

bool SCC::SetParam_PREFIX(std::string sVal)
{
  m_prefix = toupper(sVal);
  size_t strLen = m_prefix.length();
  if (strLen > 0 && m_prefix.at(strLen - 1) != '_')
    m_prefix += "_";
  
  return true;
}

bool SCC::SetParam_READ_DATA(std::string sVal)
{
  sVal = removeWhite(sVal);
  if (sVal.empty())
    sVal = "blank";
  stringstream ssMsg;
  sVal = tolower(sVal);
  if (sVal == "true" || sVal == "false")
    m_read_data = (sVal == "true");
  else {
    ssMsg << "Param READ_DATA cannot be " << sVal << ". It must be TRUE or FALSE. Defaulting to FALSE.";
    m_read_data = false; }
  string msg = ssMsg.str();
  if (!msg.empty())
    reportConfigWarning(msg);
  
  return true;
}

bool SCC::SetParam_MAX_THRUST(std::string sVal)
{
  stringstream ssMsg;
  if (!isNumber(sVal))
    ssMsg << "Param MAX_THRUST must be a number in range (0.0, 100.0].";
  else
    m_max_thrust = strtod(sVal.c_str(), 0);
  if (m_max_thrust <= 0.0 || m_max_thrust > 100)
    ssMsg << "Param MAX_THRUST cannot be " << m_max_thrust << ". Must be in range (0.0, 100.0].";
  string msg = ssMsg.str();
  if (!msg.empty())
    reportConfigWarning(msg);
  return true;
}

bool SCC::SetParam_MAX_RUDDER(std::string sVal)
{
  stringstream ssMsg;
  if (!isNumber(sVal))
    ssMsg << "Param MAX_RUDDER must be a number in range (0.0, 50.0].";
  else
    m_max_rudder = strtod(sVal.c_str(), 0);
  if (m_max_rudder <= 0.0 || m_max_rudder > 50)
    ssMsg << "Param MAX_RUDDER cannot be " << m_max_rudder << ". Must be in range (0.0, 50.0].";
  string msg = ssMsg.str();
  if (!msg.empty())
    reportConfigWarning(msg);
  return true;
}

bool SCC::SetPublishNames()
{
  m_prefix = toupper(m_prefix);
  size_t strLen = m_prefix.length();
  if (strLen > 0 && m_prefix.at(strLen - 1) != '_')
    m_prefix += "_";
  m_pubNameMaxThrust   = m_prefix + m_pubNameMaxThrust;
  m_pubNameMaxRudder   = m_prefix + m_pubNameMaxRudder;
  
  return true;
}

bool SCC::ConnectToSCC(string port, int baud)
{
  bool on = false;
  m_scc = new ModbusComms(m_serial_port, m_baudrate);
  for (int i = 0;i < m_maxTurnOnRetries; ++i)
    if (m_scc->getOn()) {
      on = true;
      break; }
  return on;
  
}

bool SCC::buildReport()
{
  m_msgs << endl << "DEVICE STATUS" << endl << "-------------" << endl;
  if (!m_bValidModbusConn) {
    m_msgs << "*** No communications with solar charge controller. ***" << endl;
    return true; }
  else
    m_msgs << "Communicating with charge controller properly on port " << m_serial_port << " at " << m_baudrate << " baud." << endl;
  
  // Format doubles ahead of time
  string sMaxThr    = doubleToString(m_max_thrust, 1);
  string sMaxThrust = doubleToString(currMaxThrust, 1);
  string sMaxRud    = doubleToString(m_max_rudder, 1);
  string sMaxRudder = doubleToString(currMaxRudder, 1);
  string sReadData  = boolToString(m_read_data);
  
  string sPVVolt    = doubleToString(m_pvVoltage, 1);
  string sPVCurr    = doubleToString(m_pvCurrent, 1);
  string sPVPower   = doubleToString(m_pvPower, 1);
  string sBattVolt  = doubleToString(m_batteryVoltage, 1);
  string sBattCurr  = doubleToString(m_batteryCurrent, 1);
  string sBattPower = doubleToString(m_batteryPower, 1);
  string sLoadVolt  = doubleToString(m_loadVoltage, 1);
  string sLoadCurr  = doubleToString(m_loadCurrent, 1);
  string sLoadPower = doubleToString(m_loadPower, 1);
  string sBattTemp  = doubleToString(m_batteryTemp, 1);
  string sDevTemp   = doubleToString(m_deviceTemp, 1);
  string sBattSOC   = doubleToString(m_batterySOC, 1);
  string sBattNet   = doubleToString(m_batteryNetCurr, 1);
  
  m_msgs << endl << "SETUP" << endl << "-----" << endl;
  m_msgs << "     PORT (BAUDRATE):         " << m_serial_port << "(" << m_baudrate << ")" << endl;
  m_msgs << "     Publish PREFIX:          " << m_prefix << endl;
  m_msgs << "     MAX THRUST:           +/-" << sMaxThr << "%" << endl;
  m_msgs << "     MAX RUDDER:              " << sMaxRud << endl;
  m_msgs << endl;

  m_msgs << "   Reading from SCC:          " << sReadData << endl;
  m_msgs << "   Number of reads from SCC:  " << m_reads_from_device << endl;
  m_msgs << endl;
  m_msgs << "   PV Voltage:          " << m_pvVoltage << endl;
  m_msgs << "   PV Current:          " << m_pvCurrent << endl;
  m_msgs << "   PV Power:            " << m_pvPower << endl;
  m_msgs << "   Battery Voltage:     " << m_batteryVoltage << endl;
  m_msgs << "   Battery Current:     " << m_batteryCurrent << endl;
  m_msgs << "   Battery Power:       " << m_batteryPower << endl;
  m_msgs << "   Load Voltage:        " << m_loadVoltage << endl;
  m_msgs << "   Load Current:        " << m_loadCurrent << endl;
  m_msgs << "   Load Power:          " << m_loadPower << endl;
  m_msgs << "   Battery Temp:        " << m_batteryTemp << endl;
  m_msgs << "   Device Temp:         " << m_deviceTemp << endl;
  m_msgs << "   Battery SOC:         " << m_batterySOC << endl;
  m_msgs << "   Battery Net Current: " << m_batteryNetCurr << endl;
  m_msgs << endl;
  
  return true;
}
