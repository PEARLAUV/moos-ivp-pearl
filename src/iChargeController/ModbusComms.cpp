/*
 * ModbusComms.cpp
 * 
 * Created on: 3/27/2021
 * Author: Ethan Rolland
 *
 */

#include "ModbusComms.h"
#include "ColorParse.h"
#include <string.h>
#include <cerrno>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>

using namespace std;

ModbusComms::ModbusComms(string port, int baud)
{
	on = false;
	
	Modbus = modbus_new_rtu((char*)port.c_str(), baud, 'N', 8, 1);
	modbus_set_slave(Modbus, 1);
	modbus_set_debug(Modbus, TRUE);
	modbus_set_debug(Modbus, FALSE);
	modbus_set_error_recovery(Modbus, (modbus_error_recovery_mode)(MODBUS_ERROR_RECOVERY_LINK|MODBUS_ERROR_RECOVERY_PROTOCOL));
	
	if (modbus_connect(Modbus)==-1) {
		modbus_free(Modbus);
		return;
	}
	else {
		on = true;
	}
	
	updateRegTab(allInfo, battSOC, netCurr);
}

ModbusComms::~ModbusComms()
{
	modbus_close(Modbus);
}

bool ModbusComms::getOn()
{
	int ret = -1;
	ret = updateRegTab(allInfo, battSOC, netCurr);
	if (ret!=-1) {
		return true; }
	else {
		return false; }
}

void ModbusComms::setMaxRetries(int m)
{
	maxRetries = m;
}

int ModbusComms::updateRegTab(uint16_t* all, uint16_t* soc, uint16_t* net)
{
	int registers_read_all = -1;
	int registers_read_soc = -1;
	int registers_read_net = -1;
	for(int w = 0 ; w < MAX_RETRIES ; w ++)
	{
		registers_read_all = modbus_read_input_registers(Modbus,0x3100,0x14,all);
		registers_read_soc = modbus_read_input_registers(Modbus,0x311A,0x01,soc);
		registers_read_net = modbus_read_input_registers(Modbus,0x331B,0x01,net);
		if(registers_read_all != -1)
			break;
	}
	if (registers_read_all != -1)
	{
		return 1;
	} else {
		return 0;
	}
}

int ModbusComms::updateAll()
{
	int ret;
	ret = updateRegTab(allInfo,battSOC,netCurr);
	
	pvVolt         = ((double) allInfo[ 0x00 ]) / 100.0;
	pvCurr         = ((double) allInfo[ 0x01 ]) / 100.0;
	pvPowerL       = ((double) allInfo[ 0x02 ]) / 100.0;
	pvPowerH       = ((double) allInfo[ 0x03 ]) / 100.0;
	batteryVolt    = ((double) allInfo[ 0x04 ]) / 100.0;
	batteryCurr    = ((double) allInfo[ 0x05 ]) / 100.0;
	batteryPowerL  = ((double) allInfo[ 0x06 ]) / 100.0;
	batteryPowerH  = ((double) allInfo[ 0x07 ]) / 100.0;
	loadVolt       = ((double) allInfo[ 0x08 ]) / 100.0;
	loadCurr       = ((double) allInfo[ 0x09 ]) / 100.0;
	loadPowerL     = ((double) allInfo[ 10 ]) / 100.0;
	loadPowerH     = ((double) allInfo[ 11 ]) / 100.0;
	batteryTemp    = ((double) allInfo[ 12 ]) / 100.0;
	deviceTemp     = ((double) allInfo[ 13 ]) / 100.0;
	
	batterySOC     = ((double) battSOC[0x00]) / 100.0;
	
	batteryNetCurr = ((double) netCurr[0x00]) / 100.0;
	
	return ret;	
}

double ModbusComms::getPVVolt()
{
	return pvVolt;
}

double ModbusComms::getPVCurr()
{
	return pvCurr;
}

double ModbusComms::getPVPower()
{
	return pvPowerL;
}

double ModbusComms::getBatteryVolt()
{
	return batteryVolt;
}

double ModbusComms::getBatteryCurr()
{
	return batteryCurr;
}

double ModbusComms::getBatteryPower()
{
	return batteryPowerL;
}

double ModbusComms::getLoadVolt()
{
	return loadVolt;
}

double ModbusComms::getLoadCurr()
{
	return loadCurr;
}

double ModbusComms::getLoadPower()
{
	return loadPowerL;
}

double ModbusComms::getBatteryTemp()
{
	return batteryTemp;
}

double ModbusComms::getDeviceTemp()
{
	return deviceTemp;
}

double ModbusComms::getBatterySOC()
{
	return batterySOC;
}

double ModbusComms::getBatteryNetCurr()
{
  if (batteryNetCurr > 327.67) {
    batteryNetCurr -= 655.36;
  }
	return batteryNetCurr;
}	

