/*
 * ChargeController.h
 * 
 * Created on: 2/6/2021
 * Author: Ethan Rolland
 *
 */

#ifndef CHARGECONTROLLER_H_
#define CHARGECONTROLLER_H_

#include <string>
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "ModbusComms.h"

class SCC : public AppCastingMOOSApp
{
 public:
        SCC();
        ~SCC() {delete m_scc;};
   bool OnNewMail(MOOSMSG_LIST &NewMail);
   bool Iterate();
   bool OnConnectToServer();
   bool OnStartUp();
   bool buildReport();
 protected:
   void RegisterForMOOSMessages();
   
   //bool ModbusSetup();
   void GetData();
   void PublishData();
   
   //Handle config params
   bool SetParam_PORT(std::string sVal);                //m_serial_port
   bool SetParam_BAUDRATE(std::string sVal);            //m_baudrate
   bool SetParam_PREFIX(std::string sVal);              //m_prefix
   bool SetParam_READ_DATA(std::string sVal);           //m_read_data
   bool SetParam_MAX_THRUST(std::string sVal);          //m_max_thrust
   bool SetParam_MAX_RUDDER(std::string sVal);          //m_max_rudder
   
   bool SetPublishNames();
   bool ConnectToSCC(std::string port, int baud);
   
   //MOOS file parameters
   std::string   m_serial_port;
   int           m_baudrate;
   std::string   m_prefix;
   bool          m_read_data;
   double        m_max_thrust;
   double        m_max_rudder;
   
   ModbusComms   *m_scc;
   bool          m_bValidModbusConn;
   int           m_maxTurnOnRetries;
   
   double        currMaxThrust;
   double        currMaxRudder;
   
   //Publish name
   std::string   m_pubNameMaxThrust;
   std::string   m_pubNameMaxRudder;
   
   //Appcast details
   unsigned int  m_reads_from_device;
   
   //SCC data
   double         m_pvVoltage;
   double         m_pvCurrent;
   double         m_pvPower;
   double         m_batteryVoltage;
   double         m_batteryCurrent;
   double         m_batteryPower;
   double         m_loadVoltage;
   double         m_loadCurrent;
   double         m_loadPower;
   double         m_batteryTemp;
   double         m_deviceTemp;
   
   double         m_batterySOC;
   double         m_batteryNetCurr;
   
};

#endif
