/*
 * PEARL.cpp
 * 
 * Created on: 1/21/2021
 * Author: Ethan Rolland
 * Adapted from: 
 *
 */

#include <algorithm>
#include <cstring>
#include "MBUtils.h"
#include "AngleUtils.h"
#include "PEARL.h"

using namespace std;

//Procedure: clamp()
//Purpose: clamps the vlaue of v between minv and maxv
double clamp(double v, double minv, double maxv)
{
  return min(maxv,max(minv,v));
}

iPEARL::iPEARL()
{
  //MOOS file parameters
  m_Port                  = "/dev/ttyAMC0";
  m_PortNum               = -1;
  m_heading_offset        = 0.0;
  m_bPubRawFromFront      = false;
  m_dMaxRudder            = MAX_RUDDER;
  m_dMaxThrust            = MAX_THRUST;
  m_prefix                = "NAV";
  m_bDirect_thrust        = false;
  m_bPublish_thrust       = false;

  //Publish names
  m_pubNameHeading        = "HEADING";
  m_pubNameYaw            = "YAW";
  m_pubNamePitch          = "PITCH";
  m_pubNameRoll           = "ROLL";

  //Appcast details
  m_why_not_valid         = "";
  m_msgs_to_front         = 0;
  m_msgs_from_front       = 0;
  m_rpt_unhandled_nmea    = false;
  m_sLastMsgToFront       = "";

  //Stale mode
  m_bOKtoReportStale      = false;
  m_stale_threshold       = 1.5;   //seconds
  m_stale_detections      = 0;
  m_stale_mode            = false;
  m_timestamp_des_L       = 0.0;
  m_timestamp_des_R       = 0.0;
  m_timestamp_des_rudder  = MOOSTime() + 10.0;
  m_timestamp_des_thrust  = MOOSTime() + 10.0;

  //Motor related
  m_commanded_L           = 0.0;
  m_commanded_R           = 0.0;
  m_des_thrust            = 0.0;
  m_des_rudder            = 0.0;
  m_des_L                 = 0.0;
  m_des_R                 = 0.0;
  m_des_count_L           = 0;
  m_des_count_R           = 0;
  m_des_count_rudder      = 0;
  m_des_count_thrust      = 0;
  m_ivpAllstop            = true;
}

bool iPEARL::OnNewMail(MOOSMSG_LIST &NewMail)
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
      sVal         = toupper(sVal);
      m_ivpAllstop = (sVal != "CLEAR"); }

    //Pay attention to desired direct thrust only when in direct thrust mode
    if (m_bDirect_thrust) {
      m_des_thrust = 0.0;
      m_des_rudder = 0.0;
      if (key == "DESIRED_THRUST_L") {
	if (dVal > 0.0) {
	  m_des_count_L++;
	  m_bOKtoReportStale = true; }
        m_des_L = dVal;
	m_timestamp_des_L = MOOSTime(); }
      else if (key == "DESIRED_THRUST_R") {
	if (dVal > 0.0) {
	  m_des_count_R++;
	  m_bOKtoReportStale = true; }
        m_des_R = dVal;
	m_timestamp_des_R = MOOSTime(); } }

    //Pay attention to desired rudder/thrust only when NOT in direct thrust mode
    else {
      m_des_L = 0.0;
      m_des_R = 0.0;
      if (key == "DESIRED_THRUST") {
	if (dVal > 0.0) {
	  m_des_count_thrust++;
	  m_bOKtoReportStale = true; }
        m_des_thrust = dVal;
	m_timestamp_des_thrust = MOOSTime(); }
      else if (key == "DESIRED_RUDDER") {
	if (dVal > 0.0) {
	  m_des_count_rudder++;
	  m_bOKtoReportStale = true; }
        m_des_rudder = dVal;
	m_timestamp_des_rudder = MOOSTime(); } }

    if (key == "THRUST_MODE_DIFFERENTIAL") {
      bool bMode = (tolower(sVal) == "true");
      handlSetThrustMode(bMode); } }
  return true;
}

void iPEARL:: RegisterForMOOSMessages()
{
  AppCastingMOOSApp::RegisterVariables();
  Register("DESIRED_THRUST_L",         0.0);
  Register("DESIRED_THRUST_R",         0.0);
  Register("DESIRED_THRUST",           0.0);
  Register("DESIRED_RUDDER",           0.0);
  Register("DIRECT_THRUST_MODE",       0.0);
  Register("THRUST_MODE_DIFFERENTIAL", 0.0);
  Register("IVPHELM_ALLSTOP",          0.0);
}

bool iPEARL::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();
  STRING_LIST sParams;
  if (!m_MissionReader.GetConfiguration(GetAppName(), sParams))
    reportConfigWarning("No config block found for " + GetAppName());
  bool goodParams = true;
  STRING_LIST::iterator p;
  for (p = sParams.begin(); p != sParams.end(); p++) {
    string orgi   = *p;
    string line   = *p;
    string param  = tolower(biteStringX(line, '='));
    string value  = line;

    bool handled = false;
    if      (param == "port_number")     handled = SetParam_PORT_NUMBER(value);
    else if (param == "heading_offset")  handled = SetParam_HEADING_OFFSET(value);
    else if (param == "publish_raw")     handled = SetParam_PUBLISH_RAW(value);
    else if (param == "max_rudder")      handled = SetParam_MAX_RUDDER(value);
    else if (param == "max_thrust")      handled = SetParam_MAX_THRUST(value);
    else if (param == "prefix")          handled = SetParam_PREFIX(value);
    else if (param == "direct_thrust")   handled = SetParam_DIRECT_THRUST_MODE(value);
    else if (param == "publish_thrust")  handled = SetParam_PUBLISH_THRUST(value);
    if (!handled)
      reportUnhandledConfigWarning(orig); }

  SetPublishNames();
  ParserSetup();

  return true;
}

