/*
 * PEARL.cpp
 * 
 * Created on: 1/21/2021
 * Author: Ethan Rolland
 * Adapted from: 
 *
 */

#include "NMEAdefs.h"
#include "MBUtils.h"
#include "AngleUtils.h"
#include "PEARL.h"

using namespace std;

//Procedure: clamp()
//Purpose: clamps the value of v between minv and maxv
double clamp(double v, double minv, double maxv)
{
  return min(maxv, max(minv, v));
}

PEARL::PEARL()
{
  //MOOS file parameters
  m_serial_port           = "/dev/ttyAMC0";
  m_baudrate              = 115200;
  m_prefix                = "IMU";
  m_heading_offset        = 0.0;
  m_pub_raw               = false;
  m_max_thrust            = MAX_THRUST;
  m_max_rudder            = MAX_RUDDER;     
  
  m_serial                = NULL;
  m_bValidSerialConn      = false;

  //Publish names
  m_pubNameHeading        = "HEADING";
  m_pubNameYaw            = "YAW";
  m_pubNamePitch          = "PITCH";
  m_pubNameRoll           = "ROLL";
  
  currHeading             = 0.0;
  currYaw                 = 0.0;
  currPitch               = 0.0;
  currRoll                = 0.0;

  //Appcast details
  m_msgs_from_front       = 0;
  m_msgs_to_front         = 0;
  m_last_msg_from_front   = "";
  m_last_msg_to_front     = "";
  
  //Motor related
  m_commanded_L           = 0.0;
  m_commanded_R           = 0.0;
  m_des_rudder            = 0.0;
  m_des_thrust            = 0.0;
  m_des_count_thrust      = 0;
  m_des_count_rudder      = 0;
  m_ivpALLSTOP            = true;

}

bool PEARL::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  double curr_time = MOOSTime();
  MOOSMSG_LIST::iterator p;
  for (p=NewMail.begin(); p!=NewMail.end(); ++p) {
    CMOOSMsg &rMsg = *p; 
    string key     = rMsg.GetKey();
    double dVal    = rMsg.GetDouble();
    string sVal    = rMsg.GetString();
    
    if (key == "IVPHELM_ALLSTOP") {
      sVal          = toupper(sVal);
      m_ivpALLSTOP  = (sVal != "CLEAR"); }
    
    if (key == "DESIRED_THRUST") {
      if (dVal > 0.0) {
        m_des_count_thrust++; }
      m_des_thrust = dVal; }
      
    else if (key == "DESIRED_RUDDER") {
      if (dVal > 0.0) {
        m_des_count_rudder++; }
      m_des_rudder = dVal; } }
      
  return UpdateMOOSVariables(NewMail);
}

void PEARL:: RegisterForMOOSMessages()
{
  AppCastingMOOSApp::RegisterVariables();
  Register("DESIRED_THRUST");
  Register("DESIRED_RUDDER");
  Register("DESIRED_THRUST_L");
  Register("DESIRED_THRUST_R");
  Register("IVPHELM_ALLSTOP");
}

bool PEARL::OnStartUp()
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
    else if (param == "HEADING_OFFSET")  handled = SetParam_HEADING_OFFSET(value);
    else if (param == "PUBLISH_RAW")     handled = SetParam_PUBLISH_RAW(value);
    else if (param == "MAX_THRUST")      handled = SetParam_MAX_THRUST(value);
    else if (param == "MAX_RUDDER")      handled = SetParam_MAX_THRUST(value);
    else
      reportUnhandledConfigWarning(orig); }

  SetPublishNames();

  m_bValidSerialConn = SerialSetup();
  
  RegisterForMOOSMessages();
  MOOSPause(500);

  return true;
}

bool PEARL::OnConnectToServer()
{
	RegisterForMOOSMessages();
	return true;
}

bool PEARL::Iterate()
{
	AppCastingMOOSApp::Iterate();
  
  if (m_bValidSerialConn) {
    bool send_ok = SendToFront();
    if (send_ok)
      m_msgs_to_front++;
    else
      reportRunWarning("Failure sending commands to front seat."); }
      
	GetData();
  
	AppCastingMOOSApp::PostReport();
  
	return true;
}

bool PEARL::SerialSetup()
{
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

void PEARL::GetData()
{
  
  if (!m_bValidSerialConn)
    return;
    
  //Grab sentences from Arduino and ingest them in the NMEA parser
  while (m_serial->DataAvailable()) {
    string nmea = m_serial->GetNextSentence();
    m_msgs_from_front++;
    m_last_msg_from_front = nmea;
    ParseNMEAString(nmea); 
  }
}

bool PEARL::SendToFront()
{
  ThrustRudderToLR();
  
  string message = "$";
  m_commanded_L = m_des_L;
  m_commanded_R = m_des_R;
  message += "PICOM,";
  message += doubleToString(m_des_L);
  message += ",";
  message += doubleToString(m_des_R);
  message += "*";
  
  m_last_msg_to_front = message;
  
  m_serial->WriteToSerialPort(message);
  
  return true;
}

bool PEARL::ThrustRudderToLR()
{
  // 1. Constrain Values
  //      DESIRED_RUDDER value to MAX_RUDDER
  //          - Anything more extreme than +/-50.0 is turn-in-place
  //      DESIRED_THRUST value to MAX_THRUST
  //          - Anything greater than +/-100.0% makes no sense
  double desiredRudder = clamp(m_des_rudder, (-1.0 * m_max_rudder), m_max_rudder);
  double desiredThrust = clamp(m_des_thrust, (-1.0 * m_max_thrust), m_max_thrust);
  
  // 2. Calculate turn
  //      - ADD rudder to left thrust
  //      - SUBTRACT rudder from right thrust
  double percentLeft  = desiredThrust + desiredRudder;
  double percentRight = desiredThrust - desiredRudder;
  
  // 3. Map desired thrust values to motor bounds
  //      - Range of DESIRED_THRUST: [-MAX_THRUST, MAX_THRUST]
  //      -          ...map to...
  //      - Range of valid thrust values: [-m_max_thrust, m_max_thrust]
  double fwdOrRevL   = (percentLeft  > 0.0) ? 1.0 : -1.0;
  double fwdOrRevR   = (percentRight > 0.0) ? 1.0 : -1.0;
  double pctThrustL  = fabs(percentLeft)  / MAX_THRUST;
  double pctThrustR  = fabs(percentRight) / MAX_THRUST;
  double mappedLeft  = pctThrustL * m_max_thrust * fwdOrRevL;
  double mappedRight = pctThrustR * m_max_thrust * fwdOrRevR;
  
  // 4. Deal with overages
  //      - Any value over m_max_thrust gets subtracted from both sides equally
  //      - Constrain to [-m_max_thrust, m_max_thrust]
  double maxThrustNeg = -1.0 * m_max_thrust;
  if (mappedLeft  > m_max_thrust)
    mappedRight -= (mappedLeft  - m_max_thrust);
  if (mappedLeft  < maxThrustNeg)
    mappedRight -= (mappedLeft  + m_max_thrust);
  if (mappedRight > m_max_thrust)
    mappedLeft  -= (mappedRight - m_max_thrust);
  if (mappedRight < maxThrustNeg)
    mappedLeft  -= (mappedRight + m_max_thrust);
    
  m_des_L = clamp(mappedLeft,  (-1.0 * m_max_thrust), m_max_thrust);
  m_des_R = clamp(mappedRight, (-1.0 * m_max_thrust), m_max_thrust);
  
  return true;
}

bool PEARL::SetParam_PORT(std::string sVal)
{
  m_serial_port = sVal;
  if (m_serial_port.empty())
    reportConfigWarning("Mission file parameter PORT must not be blank.");
  
  return true;
}

bool PEARL::SetParam_BAUDRATE(std::string sVal)
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

bool PEARL::SetParam_PREFIX(std::string sVal)
{
  m_prefix = toupper(sVal);
  size_t strLen = m_prefix.length();
  if (strLen > 0 && m_prefix.at(strLen - 1) != '_')
    m_prefix += "_";
  
  return true;
}

bool PEARL::SetParam_HEADING_OFFSET(std::string sVal)
{
  stringstream ssMsg;
  if (!isNumber(sVal))
    ssMsg << "Param HEADING_OFFSET must be a number in range (-180.0 180.0). Defaulting to 0.0.";
  else 
    m_heading_offset = strtod(sVal.c_str(), 0);
  if (m_heading_offset <= -180.0 || m_heading_offset >= 180.0) {
    ssMsg << "Param HEADING_OFFSET cannot be " << m_heading_offset << ". Must be in range (-180.0, 180.0). Defaulting to 0.0.";
    m_heading_offset = 0.0; }
  string msg = ssMsg.str();
  if (!msg.empty())
    reportConfigWarning(msg);
  
  return true;
    
}

bool PEARL::SetParam_PUBLISH_RAW(std::string sVal)
{
  sVal = removeWhite(sVal);
  if (sVal.empty())
    sVal = "blank";
  stringstream ssMsg;
  sVal = tolower(sVal);
  if (sVal == "true" || sVal == "false")
    m_pub_raw = (sVal == "true");
  else {
    ssMsg << "Param PUBLISH_RAW cannot be " << sVal << ". It must be TRUE or FALSE. Defaulting to FALSE.";
    m_pub_raw = false; }
  string msg = ssMsg.str();
  if (!msg.empty())
    reportConfigWarning(msg);
  
  return true;
}

bool PEARL::SetParam_MAX_THRUST(std::string sVal)
{
  stringstream ssMsg;
  if (!isNumber(sVal))
    ssMsg << "Param MAX_THRUST must be a number in range (0.0, 100.0]. Defaulting to " << MAX_THRUST << ".";
  else
    m_max_thrust = strtod(sVal.c_str(), 0);
  if (m_max_thrust <= 0.0 || m_max_thrust > 100) {
    ssMsg << "Param MAX_THRUST cannot be " << m_max_thrust << ". Must be in range (0.0, 100.0]. Defaulting to " << MAX_THRUST << ".";
    m_max_thrust = MAX_THRUST; }
  string msg = ssMsg.str();
  if (!msg.empty())
    reportConfigWarning(msg);
  return true;
}

bool PEARL::SetParam_MAX_RUDDER(std::string sVal)
{
  stringstream ssMsg;
  if (!isNumber(sVal))
    ssMsg << "Param MAX_RUDDER must be a number in range (0.0, 180.0]. Defaulting to " << MAX_RUDDER << ".";
  else
    m_max_rudder = strtod(sVal.c_str(), 0);
  if (m_max_rudder <= 0.0 || m_max_rudder > 100) {
    ssMsg << "Param MAX_RUDDER cannot be " << m_max_rudder << ". Must be in range (0.0, 180.0]. Defaulting to " << MAX_RUDDER << ".";
    m_max_thrust = MAX_RUDDER; }
  string msg = ssMsg.str();
  if (!msg.empty())
    reportConfigWarning(msg);
  return true;
}

bool PEARL::SetPublishNames()
{
  m_prefix = toupper(m_prefix);
  size_t strLen = m_prefix.length();
  if (strLen > 0 && m_prefix.at(strLen - 1) != '_')
    m_prefix += "_";
  m_pubNameHeading   = m_prefix + m_pubNameHeading;
  m_pubNameYaw       = m_prefix + m_pubNameYaw;
  m_pubNamePitch     = m_prefix + m_pubNamePitch;
  m_pubNameRoll      = m_prefix + m_pubNameRoll;
  
  return true;
}

void PEARL::PublishIMU(double dHeading, double dYaw, double dPitch, double dRoll)
{
  if (dHeading != BAD_DOUBLE)
    m_Comms.Notify(m_pubNameHeading, dHeading);
  else
    reportRunWarning("Did not receive heading data.");
  if (dYaw != BAD_DOUBLE)
    m_Comms.Notify(m_pubNameYaw, dYaw);
  else
    reportRunWarning("Did not receive yaw data.");
  if (dPitch != BAD_DOUBLE)
    m_Comms.Notify(m_pubNamePitch, dPitch);
  else
    reportRunWarning("Did not receive pitch data.");
  if (dRoll != BAD_DOUBLE)
    m_Comms.Notify(m_pubNameRoll, dRoll);
  else
    reportRunWarning("Did not receive roll data.");
}

bool PEARL::ParseNMEAString(string nmea)
{
  size_t len = nmea.length();
  
  if (nmea.empty()) {
    reportRunWarning("Received empty data string from front seat.");
    return false; }
    
  if (m_pub_raw)
    m_Comms.Notify("PEARL_RAW_NMEA", nmea);
    
  if (!(nmea.at(0) == '$')) {
      reportRunWarning("Line from front seat does not properly start with '$': " + nmea);
      return false; }
  if (!(nmea.at(len - 1) == '*')) {
      reportRunWarning("Line from front seat does not properly terminate with '*': " + nmea);
      return false; }
  //string checksum = nmea.substr(len - 2, 2);
  //string calcdsum = ChecksumCalc(nmea);
  //if (!MOOSStrCmp(checksum, calcdsum)) {
  //    reportRunWarning("Line from front seat has improper checksum: " + nmea);
  //    return false; }
  
  string yValStr = "";
  string pValStr = "";
  string rValStr = "";
    
  string toParse = MOOSChomp(nmea, "*");
  vector<string> parts = parseString(toParse, ',');
  string nmeaHeader = parts[0];
  if   (nmeaHeader == "$PLIMU")  {
    yValStr = parts[1];
    pValStr = parts[2];
    rValStr = parts[3]; 
    }
  else {
    reportRunWarning("Line from front seat has improper header: " + nmea);
    return false; }
    
  double dHeading  = BAD_DOUBLE;
  double dYaw      = BAD_DOUBLE;
  double dPitch    = BAD_DOUBLE;
  double dRoll     = BAD_DOUBLE;
  
  if (!yValStr.empty()) {
    dHeading = strtod(yValStr.c_str(), 0) + m_heading_offset;
    dYaw     = strtod(yValStr.c_str(), 0);
    if (dHeading > 360.0)
      dHeading -= 360.0;
    if (dHeading < 0.0)
      dHeading += 360.0;
    currHeading = dHeading;
    currYaw     = dYaw;
    }
  if (!pValStr.empty()) {
    dPitch = strtod(pValStr.c_str(), 0);
    currPitch = dPitch;
    }
  if (!rValStr.empty()) {
    dRoll = strtod(rValStr.c_str(), 0);
    currRoll = dRoll;
    }
      
  PublishIMU(dHeading,dYaw,dPitch,dRoll);
  
  return true;
}

bool PEARL::NMEAChecksumIsValid(string nmea)
{
  unsigned char xCheckSum = 0;
  string sToCheck;
  MOOSChomp(nmea,"$");
  sToCheck = MOOSChomp(nmea,"*");
  string sRxCheckSum = nmea;
  
  //now calculate what we think the check sum should be
  string::iterator p;
  for (p= sToCheck.begin(); p != sToCheck.end(); p++)
    xCheckSum^=*p;
  ostringstream os;
  os.flags(ios::hex);
  os << (int) xCheckSum;
  string sExpected = os.str();
  
  if (sExpected.length() < 2)
    sExpected = "0" + sExpected;
  
  bool result = MOOSStrCmp(sExpected,sRxCheckSum);
  
  return(result);
}

bool PEARL::buildReport()
{
  m_msgs << endl << "DEVICE STATUS" << endl << "-------------" << endl;
  if (!m_bValidSerialConn) {
    m_msgs << "*** No communications with front seat. ***" << endl;
    return true; }
  if (m_serial->IsGoodSerialComms())
    m_msgs << "Serial communicating properly on port " << m_serial_port << " at " << m_baudrate << " baud." << endl;
  else
    m_msgs << "Serial not connected to port " << m_serial_port << " at " << m_baudrate << "baud." << endl;
    
  // Format doubles ahead of time
  string sOffset  = doubleToString(m_heading_offset, 1);
  string sMaxRud  = doubleToString(m_max_rudder, 1);
  string sMaxThr  = doubleToString(m_max_thrust, 1);
  string sReqL    = doubleToString(m_des_L, 1);
  string sReqR    = doubleToString(m_des_R, 1);
  string sReqRud  = doubleToString(m_des_rudder, 1);
  string sReqThr  = doubleToString(m_des_thrust, 1);
  string sCommL   = doubleToString(m_commanded_L, 1);
  string sCommR   = doubleToString(m_commanded_R, 1);
  string sHeading = doubleToString(currHeading, 1);
  string sYaw     = doubleToString(currYaw, 1);
  string sPitch   = doubleToString(currPitch, 1);
  string sRoll    = doubleToString(currRoll, 1);
  
  m_msgs << endl << "SETUP" << endl << "-----" << endl;
  m_msgs << "     PORT (BAUDRATE):         " << m_serial_port << "(" << m_baudrate << ")" << endl;
  m_msgs << "     Publish PREFIX:          " << m_prefix << endl;
  m_msgs << "     HEADING OFFSET:          " << sOffset << endl;
  m_msgs << "     MAX RUDDER:           +/-" << sMaxRud << endl;
  m_msgs << "     MAX THRUST:           +/-" << sMaxThr << "%" << endl;
  m_msgs << endl;
  
  if (m_ivpALLSTOP) {
    m_msgs << "   --- IVPHELM ALLSTOP ENGAGED ---" << endl; }

  m_msgs << "   Requested rudder, thrust:     " << sReqRud << ", " << sReqThr << endl;
  m_msgs << "   Commanded to motors L, R:     " << sCommL << ", " << sCommR << endl;
  m_msgs << "   Messages from front seat:     " << m_msgs_from_front << endl;
  m_msgs << "   Messages to front seat:       " << m_msgs_to_front << endl;
  m_msgs << "   Last message from front seat: " << m_last_msg_from_front << endl;
  m_msgs << "   Last message to front seat:   " << m_last_msg_to_front << endl;
  m_msgs << "   DESIRED_THRUST count:         " << m_des_count_thrust << endl;
  m_msgs << "   DESIRED_RUDDER count:         " << m_des_count_rudder << endl;
  m_msgs << "   Heading:                      " << sHeading << endl;
  m_msgs << "   Yaw:                          " << sYaw << endl;
  m_msgs << "   Pitch:                        " << sPitch << endl;
  m_msgs << "   Roll:                         " << sRoll << endl;
  m_msgs << endl;
  
  return true;
}
    
