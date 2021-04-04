/*
 * PEARL.h
 * 
 * Created on: 1/21/2021
 * Author: Ethan Rolland
 * Adapted from: 
 *
 */

#ifndef PEARL_H_
#define PEARL_H_

#include <string>
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "SerialComms.h"

#define MAX_THRUST    100.0
#define MAX_RUDDER    50.0


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
   
   //Various
   bool SerialSetup();
   void GetData();
   bool SendToFront();

   //Handle Config Params
   bool SetParam_PORT(std::string sVal);                //m_serial_port
   bool SetParam_BAUDRATE(std::string sVal);            //m_baudrate
   bool SetParam_PREFIX(std::string sVal);              //m_prefix
   bool SetParam_HEADING_OFFSET(std::string sVal);      //m_heading_offset
   bool SetParam_PUBLISH_RAW(std::string sVal);         //m_pub_raw
   bool SetParam_MAX_THRUST(std::string sVal);          //m_max_thrust
   bool SetParam_MAX_RUDDER(std::string sVal);          //m_max_rudder
   
   bool SetPublishNames();
   void PublishIMU(double dHeading, double dYaw, double dPitch, double dRoll);

   //NMEA Processing
   bool ParseNMEAString(std::string nmea);
   bool NMEAChecksumIsValid(std::string nmea);

   //MOOS file parameters
   std::string   m_serial_port;
   int           m_baudrate;
   std::string   m_prefix;
   double        m_heading_offset;
   bool          m_pub_raw;
   double        m_max_thrust;
   double        m_max_rudder;
   
   SerialComms*  m_serial;
   bool          m_bValidSerialConn;

   //Publish names
   std::string   m_pubNameHeading;
   std::string   m_pubNameYaw;
   std::string   m_pubNamePitch;
   std::string   m_pubNameRoll;
   
   double        currHeading;
   double        currYaw;
   double        currPitch;
   double        currRoll;   

   //Appcast details
   unsigned int  m_msgs_from_front;     //Number of messages received from front seat
   unsigned int  m_msgs_to_front;       //Number of messages sent to front seat
   std::string   m_last_msg_from_front; //Last raw message received from front seat
   std::string   m_last_msg_to_front;   //Last raw message sent to front seat
   
   //Motor related
   double        m_commanded_L;
   double        m_commanded_R;
   double        m_des_thrust;
   double        m_des_rudder;
   double        m_des_L;
   double        m_des_R;
   int           m_des_count_thrust;
   int           m_des_count_rudder;
   bool          m_ivpALLSTOP;
   
   //Stores number of messages processed, keyed on NMEA sentence name
   std::map<std::string, unsigned int>  m_counters;
};

#endif
