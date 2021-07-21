/*
 * RPISerial.h
 * 
 * Created on: 7/20/2021
 * Author: Henry
 * 
 */

#ifndef RPISERIAL_H_
#define RPISERIAL_H_


#include <string>
#include <stdio.h>
#include <iostream>
#include <math.h>
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "SerialComms.h"


using namespace std;

class RPISERIAL : public AppCastingMOOSApp
{
 public:
		 RPISERIAL();
		 ~RPISERIAL() {};
	bool OnNewMail(MOOSMSG_LIST &NewMail);
	bool Iterate();
	bool OnConnectToServer();
	bool OnStartUp();
	bool buildReport();
	
	
 protected:
	void RegisterForMOOSMessages();
	
    string          m_prefix;

    //Serial Variables
    int             m_baudrate;
    string          m_serial_port;
    SerialComms*    m_serial;
    bool            m_bValidSerialConn;
    
    //Appcast details
    unsigned int    m_msgs_from_serial;
    string          m_last_SCC_from_sensor;
    string          m_last_Light_from_sensor;
    string          m_last_Wind_from_sensor;

    //Values pulled from serial Strings
    //SCC values
    double m_pvVoltage;
    double m_pvCurrent;
    double m_pvPowerL;
    double m_pvPowerH;
    double m_batteryVoltage;
    double m_batteryCurrent;
    double m_batteryNetCurrent;
    double m_batteryPowerL;
    double m_batteryPowerH;
    double m_loadVoltage;
    double m_loadCurrent;
    double m_loadPowerL;
    double m_loadPowerH;
    double m_batterySOC;
    double m_batteryTemperature;
    double m_deviceTemperature;

    //Light sensor Values
    double m_lux;
    double m_white;
    double m_rawALS;
    
    //Wind Sensor Data
    double m_windReading;
    double m_windSpeed;

    //Various
    bool SerialSetup();
    void GetData();
    bool ParseNMEAString(string nmea);

    //Serial Handling Functions
    bool HandleSCC(string toParse);
    bool HandleLight(string toParse);
    bool HandleWind(string toParse);

    //Publishing functions
    void PublishSCCRaw(double pvVoltage,    double pvCurrent,    double pvPowerL,    double pvPowerH,    double batteryVoltage,    double batteryCurrent,    double batteryNetCurrent,    double batteryPowerL,    double batteryPowerH,    double loadVoltage,    double loadCurrent,    double loadPowerL,    double loadPowerH,    double batterySOC,    double batteryTemperature,    double deviceTemperature);
    void PublishLightRaw(double lux, double white, double rawALS);
    void PublishWindRaw(double windReading, double windSpeed);

	//Handle Config Params
	bool SetParam_PREFIX(string sVal);
	bool SetParam_PORT(string sVal);
    bool SetParam_BAUDRATE(string sVal);
	
};
#endif
		
