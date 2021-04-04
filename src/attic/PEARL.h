/*
 * PEARL.h
 * 
 * Created on: 1/21/2021
 * Author: Ethan Rolland
 * Adapted from: 
 *
 */

#ifndef PEARL_H
#define PEARL_H

#include <string>
#include "MOOS/libMOOS/Thirdparty/AppCastingMOOSApp.h"
#include "PLIMUnmea.h"

#define MAX_RUDDER      50.0
#define MAX_THRUST      100.0
#define MAX_IN_BYTES    1000
#define MAX_BUFF        10000

class PEARL : public AppCastingMOOSApp
{
 public:
        PEARL();
        ~PEARL() {};
   bool OnNewMail(MOOSMSG_LIST &NewMail);
   bool Iterate();
   bool OnConnectToServer();
   bool OnStartUp();
   bool buildReport();

 protected:
   void RegisterForMOOSMessages();
   bool ThrustRudderToLR();
   bool ParserSetup();

   //Handle Config Params
   bool SetParam_PORT_NUMBER(std::string sVal);         //m_Port
   bool SetParam_HEADING_OFFSET(std::string sVal);      //m_heading_offset
   bool SetParam_PUBLISH_RAW(std::string sVal);         //m_bPubRawFromFront
   bool SetParam_MAX_RUDDER(std::string sVal);          //m_dMaxRudder
   bool SetParam_MAX_THRUST(std::string sVal);          //m_dMaxThrust
   bool SetParam_PREFIX(std::string sVal);              //m_prefix
   bool SetParam_DIRECT_THRUST_MODE(std::string sVal);  //m_bDirect_thrust
   bool SetParam_PUBLISH_THRUST(std::string sVal);      //m_bPublish_thrust

   bool handleSetThrustMode(bool setDirectThrustMode);
   bool SetPublishNames();

   //NMEA Processing
   bool DealWithNMEA(std::string nmea);
   bool ParsePLIMU(std::string nmea);
   bool NMEAChecksumIsValid(std::string nmea);
   bool ParseUnknownNMEA(std::string nmea);

   bool StaleModeCheck();
   void PublishHeading(double dHeading);

   //MOOS file parameters
   std::string   m_Port;
   int           m_PortNum;
   double        m_heading_offset;
   bool          m_bPubRawFromFront;
   double        m_dMaxRudder;
   double        m_dMaxThrust;
   std::string   m_prefix;
   bool          m_bDirect_thrust;
   bool          m_bPublish_thrust;

   //Publish names
   std::string   m_pubNameHeading;
   std::string   m_pubNameYaw;
   std::string   m_pubNamePitch;
   std::string   m_pubNameRoll;

   //Appcast details
   std::string   m_why_not_valid;       //Explanation of why app isn't doing anything
   unsigned int  m_msgs_to_front;       //Number of messages sent to front seat (Arduino)
   unsigned int  m_msgs_from_front;     //Number of messages received from front seat (Arduino)
   bool          m_rpt_unhandled_nmea;  //When true, appcast unhandled nmea sentences
   std::string   m_sLastMsgToFront;     //String holding last message posted to front seat

   //Stale mode
   bool          m_bOKtoReportStale;
   double        m_stale_threshold;
   unsigned int  m_stale_detections;
   bool          m_stale_mode;
   double        m_timestamp_des_L;
   double        m_timestamp_des_R;
   double        m_timestamp_des_rudder;
   double        m_timestamp_des_thrust;

   //Motor related
   double        m_commanded_L;
   double        m_commanded_R;
   double        m_des_thrust;
   double        m_des_rudder;
   double        m_des_L;
   double        m_des_R;
   int           m_des_count_L;
   int           m_des_count_R;
   int           m_des_count_rudder;
   int           m_des_count_thrust;
   bool          m_ivpAllstop;

   //Stores number of messages processed, keyed on NMEA sentence name
   std::map<std::string, unsigned int>  m_counters;
};

#endif
