/*
 * SunTracking.cpp
 * 
 * Created on: 2/7/2021
 * Author: Ethan Rolland
 *
 */

#include "MBUtils.h"
#include "SunTracking.h"
#include "NMEAdefs.h"

using namespace std;

SOLAR::SOLAR()
{  
   //MOOS file parameters 
   m_prefix               = "SOLAR";
   m_sun_heading_offset       = 0.0;
   m_pub_angles           = false;
  
   //Variables read in from MOOSDB
   m_lat                  = 0.0;
   m_lon                  = 0.0;
   m_utc_time             = "";
   m_utc_date             = "";
   
   //Variables for sun angle calculations
   m_hour                 = 0.0;
   m_minute               = 0.0;
   m_second               = 0.0;
   m_day                  = 1.0;
   m_month                = 1.0;
   m_year                 = 2000.0;
   m_time_zone            = 0.0;
   m_time_local           = 0.0;
	
   //Variable names to publish
   m_pubNameSunElevation  = "SUN_ELEVATION";
   m_pubNameSunAzimuth    = "SUN_AZIMUTH";
   m_pubNameHeading       = "HEADING";
  
   curElevation           = BAD_DOUBLE;
   curAzimuth             = BAD_DOUBLE;
   curHeading             = BAD_DOUBLE;
}

bool SOLAR::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  double curr_time = MOOSTime();
  MOOSMSG_LIST::iterator p;
  for (p=NewMail.begin(); p!=NewMail.end(); ++p) {
    CMOOSMsg &rMsg = *p; 
    string key     = rMsg.GetKey();
    double dVal    = rMsg.GetDouble();
    string sVal    = rMsg.GetString();
	
	if (key == "GPS_LATITUDE") {
		m_lat = dVal; }
	
	else if (key == "GPS_LONGITUDE") {
		m_lon = dVal; }
	
	else if (key == "GPS_UTC_TIME") {
		m_utc_time = sVal; }
	
	else if (key == "GPS_UTC_DATE") {
		m_utc_date = sVal; } }
      
  return UpdateMOOSVariables(NewMail);
}

void SOLAR::RegisterForMOOSMessages()
{
  AppCastingMOOSApp::RegisterVariables();
  Register("GPS_LATITUDE");
  Register("GPS_LONGITUDE");
  Register("GPS_UTC_TIME");
  Register("GPS_UTC_DATE");
}

bool SOLAR::OnStartUp()
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

    if      (param == "PREFIX")              handled = SetParam_PREFIX(value);
    else if (param == "SUN_HEADING_OFFSET")  handled = SetParam_SUN_HEADING_OFFSET(value);
    else if (param == "PUBLISH_ANGLES")      handled = SetParam_PUBLISH_ANGLES(value);
    else
      reportUnhandledConfigWarning(orig); }

  SetPublishNames();
  
  RegisterForMOOSMessages();
  MOOSPause(500);

  return true;
}

bool SOLAR::OnConnectToServer()
{
	RegisterForMOOSMessages();
	return true;
}

bool SOLAR::Iterate()
{
	AppCastingMOOSApp::Iterate();
      
	GetSunData();
  
  PublishHeading(curHeading);
  
	AppCastingMOOSApp::PostReport();
  
	return true;
}

void SOLAR::GetSunData()
{
  
  double dElevation  = BAD_DOUBLE;
  double dAzimuth    = BAD_DOUBLE;
  double hourAngle   = BAD_DOUBLE;
  
  double julianDay = GetJulianDate();

  double julianCentury = (julianDay - 2451545.0)/36525.0;
  double geoMeanLongSun = fmod(280.46646+julianCentury*(36000.76983 + julianCentury*0.0003032),360.0);
  double geoMeanAnomSun = 357.52911+julianCentury*(35999.05029 - 0.0001537*julianCentury);
  double eccentEarthOrbit = 0.016708634-julianCentury*(0.000042037+0.0000001267*julianCentury);
  double sunEqOfCtr = sin(degToRadians(geoMeanAnomSun))*(1.914602-julianCentury*(0.004817+0.000014*julianCentury))+sin(degToRadians(2*geoMeanAnomSun))*(0.019993-0.000101*julianCentury)+sin(degToRadians(3*geoMeanAnomSun))*0.000289;
  double sunTrueLong = geoMeanLongSun + sunEqOfCtr;
  double sunTrueAnom = geoMeanAnomSun + sunEqOfCtr;
  double sunRadVector = (1.000001018*(1-eccentEarthOrbit*eccentEarthOrbit))/(1+eccentEarthOrbit*cos(degToRadians(sunTrueAnom)));
  double sunAppLong = sunTrueLong-0.00569-0.00478*sin(degToRadians(125.04-1934.136*julianCentury));
  double meanObliqEcliptic = 23.0+(26.0+((21.448-julianCentury*(46.815+julianCentury*(0.00059-julianCentury*0.001813))))/60)/60;
  double obliqCorr = meanObliqEcliptic+0.00256*cos(degToRadians(125.04-1934.136*julianCentury));
  double sunRtAscen = radToDegrees(atan2(cos(degToRadians(obliqCorr))*sin(degToRadians(sunAppLong)),cos(degToRadians(sunAppLong))));
  double sunDeclin = radToDegrees(asin(sin(degToRadians(obliqCorr))*sin(degToRadians(sunAppLong))));
  double varY = tan(degToRadians(obliqCorr/2.0))*tan(degToRadians(obliqCorr/2.0));
  double eqOfTime = 4.0*radToDegrees(varY*sin(2.0*degToRadians(geoMeanLongSun))-2*eccentEarthOrbit*sin(degToRadians(geoMeanAnomSun))+4*eccentEarthOrbit*varY*sin(degToRadians(geoMeanAnomSun))*cos(2*degToRadians(geoMeanLongSun))-0.5*varY*varY*sin(4*degToRadians(geoMeanLongSun))-1.25*eccentEarthOrbit*eccentEarthOrbit*sin(2*degToRadians(geoMeanAnomSun)));
  double haSunrise = radToDegrees(acos(cos(degToRadians(90.833))/(cos(degToRadians(m_lat))*cos(degToRadians(sunDeclin)))-tan(degToRadians(m_lat))*tan(degToRadians(sunDeclin))));
  double solarNoon = (720.0-4.0*m_lon-eqOfTime+m_time_zone*60.0)/1440.0;
  double sunriseTime = solarNoon-haSunrise*4.0/1440.0;
  double sunsetTime = solarNoon+haSunrise*4.0/1440.0;
  double sunlightDuration = 8.0*haSunrise;
  double trueSolarTime = fmod(m_time_local*1440.0+eqOfTime+4.0*m_lon-60*m_time_zone,1440.0);
  if((trueSolarTime/4.0) < 0.0)
      hourAngle = trueSolarTime/4.0 + 180.0;
  else
      hourAngle = trueSolarTime/4.0 - 180.0;
  double solarZenithAngle = radToDegrees(acos(sin(degToRadians(m_lat))*sin(degToRadians(sunDeclin))+cos(degToRadians(m_lat))*cos(degToRadians(sunDeclin))*cos(degToRadians(hourAngle))));
  dElevation = 90.0-solarZenithAngle;
  if(hourAngle > 0.0)
      dAzimuth = fmod(radToDegrees(acos(((sin(degToRadians(m_lat))*cos(degToRadians(solarZenithAngle)))-sin(degToRadians(sunDeclin)))/(cos(degToRadians(m_lat))*sin(degToRadians(solarZenithAngle)))))+180.0,360.0);
  else
      dAzimuth = fmod(540.0-radToDegrees(acos(((sin(degToRadians(m_lat))*cos(degToRadians(solarZenithAngle)))-sin(degToRadians(sunDeclin)))/(cos(degToRadians(m_lat))*sin(degToRadians(solarZenithAngle))))),360.0);
  
  curElevation = dElevation;
  curAzimuth   = dAzimuth;
  if(curElevation<0) {
    curHeading = 0.0; }
  else {
    curHeading   = dAzimuth + m_sun_heading_offset; }
  
  if(m_pub_angles)
    PublishData(curElevation, curAzimuth);
}

bool SOLAR::SetPublishNames()
{
  m_prefix = toupper(m_prefix);
  size_t strLen = m_prefix.length();
  if (strLen > 0 && m_prefix.at(strLen - 1) != '_')
    m_prefix += "_";
  m_pubNameSunElevation   = m_prefix + m_pubNameSunElevation;
  m_pubNameSunAzimuth     = m_prefix + m_pubNameSunAzimuth;
  m_pubNameHeading        = m_prefix + m_pubNameHeading;
  
  return true;
}

void SOLAR::PublishData(double dElevation, double dAzimuth)
{
  if (dElevation != BAD_DOUBLE)
    m_Comms.Notify(m_pubNameSunElevation, dElevation);
  else
    reportRunWarning("Did not compute sun elevation data.");
  if (dAzimuth != BAD_DOUBLE)
    m_Comms.Notify(m_pubNameSunAzimuth, dAzimuth);
  else
    reportRunWarning("Did not compute sun azimuth data.");
}

void SOLAR::PublishHeading(double dHeading)
{
  if (dHeading != BAD_DOUBLE)
    m_Comms.Notify(m_pubNameHeading, dHeading);
  else
    reportRunWarning("Did not compute heading data.");
}

bool SOLAR::SetParam_PREFIX(std::string sVal)
{
  m_prefix = toupper(sVal);
  size_t strLen = m_prefix.length();
  if (strLen > 0 && m_prefix.at(strLen - 1) != '_')
    m_prefix += "_";
  
  return true;
}

bool SOLAR::SetParam_SUN_HEADING_OFFSET(std::string sVal)
{
  stringstream ssMsg;
  if (!isNumber(sVal))
    ssMsg << "Param SUN_HEADING_OFFSET must be a number in range (-180.0 180.0). Defaulting to 0.0.";
  else 
    m_sun_heading_offset = stod(sVal);
  if (m_sun_heading_offset <= -180.0 || m_sun_heading_offset >= 180.0) {
    ssMsg << "Param SUN_HEADING_OFFSET cannot be " << m_sun_heading_offset << ". Must be in range (-180.0, 180.0). Defaulting to 0.0.";
    m_sun_heading_offset = 0.0; }
  string msg = ssMsg.str();
  if (!msg.empty())
    reportConfigWarning(msg);
  
  return true;
}

bool SOLAR::SetParam_PUBLISH_ANGLES(std::string sVal)
{
  sVal = removeWhite(sVal);
  if (sVal.empty())
    sVal = "blank";
  stringstream ssMsg;
  sVal = tolower(sVal);
  if (sVal == "true" || sVal == "false")
    m_pub_angles = (sVal == "true");
  else {
    ssMsg << "Param PUBLISH_ANGLES cannot be " << sVal << ". It must be TRUE or FALSE. Defaulting to FALSE.";
    m_pub_angles = false; }
  string msg = ssMsg.str();
  if (!msg.empty())
    reportConfigWarning(msg);
  
  return true;
}

double SOLAR::GetJulianDate()
{
  string strTime = m_utc_time;
  if (strTime.length() > 0) {
    strTime.erase (6,2);
    strTime.insert(2,",");
    strTime.insert(5,",");
    vector<string> time = parseString(strTime, ',');
    m_hour   = stod(time[0]);
    m_minute = stod(time[1]);
    m_second = stod(time[2]); }
  
  string strDate = m_utc_date;
  if (strDate.length() > 0) {
    strDate.insert(2,",");
    strDate.insert(5,",");
    vector<string> date = parseString(strDate, ',');
    m_day   = stod(date[0]);
    m_month = stod(date[1]);
    m_year  = stod(date[2]) + 2000.0; }
    
  double a = (m_month - 14.0) / 12.0;
  double b = 1461.0 * (m_year + 4800.0 + a) / 4.0;
  double c = 367.0 * (m_month - 2.0 - (12.0 * a)) / 12.0;
  double d = (3.0 * ((m_year + 4900.0 + a) / 100.0)) / 4.0;
  double e = m_day - 32075.0;
  double julian = b + c - d + e;
  
  double timeZone = floor(m_lon / 15.0) + 1.0;
  m_time_zone = timeZone;
  
  double timeLocal = (m_hour + (m_minute / 60.0) + (m_second / 3600.0) + timeZone) / 24.0;
  if(timeLocal < 0.0) {
    timeLocal += 1;
    m_day -= 1; }
  m_time_local = timeLocal;
  
  return (julian + timeLocal - (timeZone / 24.0));
}

double SOLAR::degToRadians(double degrees)
{
  return degrees * (PI/180);
}

double SOLAR::radToDegrees(double radians)
{
  return radians * (180/PI);
}

bool SOLAR::buildReport()
{  
  // Format doubles ahead of time
  string sDay        = doubleToString(m_day, 1);
  string sMonth      = doubleToString(m_month, 1);
  string sYear       = doubleToString(m_year, 1);
  string sHour       = doubleToString(m_hour, 1);
  string sMinute     = doubleToString(m_minute, 1);
  string sSecond     = doubleToString(m_second, 1);
  string sTimeLocal  = doubleToString(m_time_local, 4);
  string sTimeZone   = doubleToString(m_time_zone, 1);
  string sElevation  = doubleToString(curElevation, 1);
  string sAzimuth    = doubleToString(curAzimuth, 1);
  string sHeading    = doubleToString(curHeading, 1);
  
  m_msgs << endl << "uSunTracking Variables" << endl << "---------------------------------" << endl;

  m_msgs << "   Day:                 " << sDay << endl;
  m_msgs << "   Month:               " << sMonth << endl;
  m_msgs << "   Year:                " << sYear << endl;
  m_msgs << "   Hour:                " << sHour << endl;
  m_msgs << "   Minute:              " << sMinute << endl;
  m_msgs << "   Second:              " << sSecond << endl;
  m_msgs << "   Local Time:          " << sTimeLocal << endl;
  m_msgs << "   Time Zone:           " << sTimeZone << endl;
  
  m_msgs << endl << "uSunTracking Calculations" << endl << "---------------------------------" << endl;
  m_msgs << "   Sun Elevation:       " << sElevation << endl;
  m_msgs << "   Sun Azimuth:         " << sAzimuth << endl;
  m_msgs << "   Heading:             " << sHeading << endl;
  m_msgs << endl;
  
  return true;
}

