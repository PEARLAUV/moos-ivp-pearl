/*
 * SunTracking.h
 * 
 * Created on: 2/7/2021
 * Author: Ethan Rolland
 *
 */

#ifndef SUNTRACKING_H_
#define SUNTRACKING_H_

#include <string>
#include <cmath>
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "MOOS/libMOOS/Utils/MOOSUtilityFunctions.h"

class SOLAR : public AppCastingMOOSApp
{
 public:
        SOLAR();
        ~SOLAR() {};
   bool   OnNewMail(MOOSMSG_LIST &NewMail);
   bool   Iterate();
   bool   OnConnectToServer();
   bool   OnStartUp();
   bool   buildReport();
   
 protected:
   void   RegisterForMOOSMessages();
   
   //Handle Config Params
   bool   SetParam_PREFIX(std::string sVal);              //m_prefix
   bool   SetParam_SUN_HEADING_OFFSET(std::string sVal);      //m_heading_offset
   bool   SetParam_PUBLISH_ANGLES(std::string sVal);
   
   void   GetSunData();
   double GetJulianDate();
   bool   SetPublishNames();
   void   PublishData(double dElevation, double dAzimuth);
   void   PublishHeading(double dHeading);
   double degToRadians(double degrees);
   double radToDegrees(double radians);
   
   //MOOS file parameters 
   std::string  m_prefix;
   double       m_sun_heading_offset;
   bool         m_pub_angles;
  
   //Variables read in from MOOSDB
   double       m_lat;
   double       m_lon;
   std::string  m_utc_time;
   std::string  m_utc_date;
   
   //Variables for sun angle calculations
   double       m_hour;
   double       m_minute;
   double       m_second;
   double       m_day;
   double       m_month;
   double       m_year;
   double       m_time_zone;
   double       m_time_local;
	
   //Variable names to publish
   std::string  m_pubNameSunElevation;
   std::string  m_pubNameSunAzimuth;
   std::string  m_pubNameHeading;
  
   double       curElevation;
   double       curAzimuth;
   double       curHeading;
   
};

#endif
