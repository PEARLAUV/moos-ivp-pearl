/*
 * DualGPS.h
 * 
 * Created on: 6/21/2021
 * Author: Ethan Rolland
 * 
 */

#ifndef DUALGPS_H_
#define DUALGPS_H_

#include <map>
#include "MOOS/libMOOS/MOOSLib.h"
#include "MOOS/libMOOSGeodesy/MOOSGeodesy.h"
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "SerialComms.h"
#include "gpsParser.h"

class DGPS : public AppCastingMOOSApp
{
public:
	        DGPS();
	virtual ~DGPS() {};
	bool    OnNewMail(MOOSMSG_LIST &NewMail);
	bool    Iterate();
	bool    OnConnectToServer();
	bool    OnStartUp();
	bool    buildReport();
	
protected:
	bool    RegisterForMOOSMessages();
	
	void    IngestFromGPS();
	void    HandleLeftMessage(gpsValueToPublish gVal);
	void    HandleRightMessage(gpsValueToPublish gVal);
	void    PublishMessage(gpsValueToPublish gVal);
	bool    DualSerialSetup();
	bool    SerialSetup();
	bool    GeodesySetup();
	bool    ParserSetup();
	bool    SetParam_DUAL_GPS(std::string sVal);
	bool    SetParam_PORT_LEFT(std::string sVal);
	bool    SetParam_PORT_RIGHT(std::string sVal);
	bool    SetParam_BAUDRATE(std::string sVal);
	bool    SetParam_PREFIX(std::string sVal);
	bool    SetParam_HEADING_OFFSET(std::string sVal);
	bool    SetParam_PUBLISH_RAW(std::string sVal);
	bool    SetParam_PUBLISH_HDOP(std::string sVal);
	bool    SetParam_TRIGGER_MSG(std::string sVal);
	
	bool           m_bValidSerialConn;
	CMOOSGeodesy   m_geodesy;
	SerialComms*   m_serial_L;
	SerialComms*   m_serial_R;
	gpsParser*     m_parser_L;
	gpsParser*     m_parser_R;
	bool           m_report_unhandled;
	bool           m_dual_gps;
	std::string    m_serial_port_left;
	std::string    m_serial_port_right;
	int            m_baudrate;
	std::string    m_prefix;
	double         m_heading_offset;
	bool           m_pub_raw;
	bool           m_pub_hdop;
	std::string    m_trigger_key;
	
	
	double         m_curX_L;
	double         m_curY_L;
	double         m_curX_R;
	double         m_curY_R;
	double         m_curLat_L;
	double         m_curLon_L;
	double         m_curLat_R;
	double         m_curLon_R;
	double         m_curSpeed;
	double         m_curHeading_L;
	double         m_curHeading_R;
	double         m_curHeadingDUAL;
	std::string    m_curQuality_L;
	std::string    m_curQuality_R;
	
	std::map<std::string, unsigned int>   m_counters;
	
};

#endif
