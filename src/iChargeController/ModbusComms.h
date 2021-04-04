/*
 * ModbusComms.h
 * 
 * Created on: 3/27/2021
 * Author: Ethan Rolland
 *
 */

#ifndef MODBUSCOMMS_H
#define MODBUSCOMMS_H

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include "modbus/modbus.h"
#include "MOOS/libMOOS/Utils/MOOSLinuxSerialPort.h"

#define MAX_RETRIES 10

class ModbusComms
{
	public:
		ModbusComms(std::string port, int baud);
		~ModbusComms();
		
		
		void setMaxRetries(int max);
		int updateRegTab(uint16_t*, uint16_t*, uint16_t*);
		bool getOn();
		
		int updateAll();
		double getPVVolt();
		double getPVCurr();
		double getPVPower();
		double getBatteryVolt();
		double getBatteryCurr();
		double getBatteryPower();
		double getLoadVolt();
		double getLoadCurr();
		double getLoadPower();
		double getBatteryTemp();
		double getDeviceTemp();
		
		double getBatterySOC();
		double getBatteryNetCurr();
		
	private:
		modbus_t *Modbus;
		bool on;
		int maxRetries;
		uint16_t allInfo[120];
		uint16_t battSOC[8];
		uint16_t netCurr[8];
		double pvVolt;
		double pvCurr;
		double pvPowerL;
		double pvPowerH;
		double batteryVolt;
		double batteryCurr;
		double batteryPowerL;
		double batteryPowerH;
		double loadVolt;
		double loadCurr;
		double loadPowerL;
		double loadPowerH;
		double batteryTemp;
		double deviceTemp;
		double batterySOC;
		double batteryNetCurr;
};
	
	#endif
