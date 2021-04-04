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
   bool StaleShoreCheck(std::string node_msg);

   //Handle Config Params
   bool SetParam_PORT(std::string sVal);                //m_serial_port
   bool SetParam_BAUDRATE(std::string sVal);            //m_baudrate
   bool SetParam_PREFIX(std::string sVal);              //m_prefix
   bool SetParam_HEADING_OFFSET(std::string sVal);      //m_heading_offset
   bool SetParam_LEFT_MOTOR_FRAC(std::string sVal);     //m_left_motor_offset
   bool SetParam_RIGHT_MOTOR_FRAC(std::string sVal);    //m_right_motor_offset
   
   bool SetPublishNames();
   void PublishIMUEuler(double dHeading, double dPitch, double dRoll);
   void PublishIMURaw(double dAccX, double dAccY, double dAccZ, double dGyroX, double dGyroY, double dGyroZ, double dMagX, double dMagY, double dMagZ);

   //NMEA Processing
   bool ParseNMEAString(std::string nmea);
   bool NMEAChecksumIsValid(std::string nmea);
   bool HandlePLIMU(std::string toParse);
   bool HandlePLRAW(std::string toParse);
   bool HandlePLMOT(std::string toParse);

   //MOOS file parameters
   std::string   m_serial_port;
   int           m_baudrate;
   std::string   m_prefix;
   double        m_heading_offset;
   double        m_left_motor_offset;
   double        m_right_motor_offset;
   
   SerialComms*  m_serial;
   bool          m_bValidSerialConn;

   //Publish names
   std::string   m_pubNameHeading;
   std::string   m_pubNamePitch;
   std::string   m_pubNameRoll;
   std::string   m_pubNameAccX;
   std::string   m_pubNameAccY;
   std::string   m_pubNameAccZ;
   std::string   m_pubNameGyroX;
   std::string   m_pubNameGyroY;
   std::string   m_pubNameGyroZ;
   std::string   m_pubNameMagX;
   std::string   m_pubNameMagY;
   std::string   m_pubNameMagZ;
   
   double        currHeading;
   double        currPitch;
   double        currRoll;
   double        currAccX;
   double        currAccY;
   double        currAccZ;
   double        currGyroX;
   double        currGyroY;
   double        currGyroZ;
   double        currMagX;
   double        currMagY;
   double        currMagZ;
   
   double        arduinoThrustLeft;
   double        arduinoThrustRight;

   //Appcast details
   unsigned int  m_msgs_from_front;       //Number of messages received from front seat
   unsigned int  m_msgs_to_front;         //Number of messages sent to front seat
   std::string   m_last_PLIMU_from_front; //Last raw PLIMU message received from front seat
   std::string   m_last_PLRAW_from_front; //Last raw PLRAW message received from front seat
   std::string   m_last_PLMOT_from_front; //Last raw PLMOT message received from front seat
   std::string   m_last_msg_to_front;     //Last raw message sent to front seat
   
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
   bool          m_autonomous_control;
   bool          m_manual_control_flag;
   
   double        m_direct_thrust_mode;
   bool          m_direct_thrust_up;
   bool          m_direct_thrust_down;
   double        m_direct_L;
   double        m_direct_R;
   
   double        m_max_thrust;
   double        m_max_rudder;
   
   std::string   node_broker_msg;
   int           last_node_ack;
   int           new_node_ack;
   int           node_ack_counter;
   bool          m_bStaleShore;
   
   //Stores number of messages processed, keyed on NMEA sentence name
   std::map<std::string, unsigned int>  m_counters;
   
};

#endif
