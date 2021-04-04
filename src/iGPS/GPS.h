/*
 * GPS.h
 * 
 * Created on: 1/12/2021
 * Author: Ethan Rolland
 * 
 */

#ifndef GPSDEVICE_H_
#define GPSDEVICE_H_

#include <map>
#include "MOOS/libMOOS/MOOSLib.h"
#include "MOOS/libMOOSGeodesy/MOOSGeodesy.h"
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "SerialComms.h"
#include "gpsParser.h"

enum HeadingSource { HEADING_SOURCE_NONE,
                     HEADING_SOURCE_GPRMC};

class GPS : public AppCastingMOOSApp
{
public:
            GPS();
    virtual ~GPS() {};
    bool    OnNewMail(MOOSMSG_LIST &NewMail);
    bool    Iterate();
    bool    OnConnectToServer();
    bool    OnStartUp();
    bool    buildReport();

protected:
    bool    RegisterForMOOSMessages();
    
    void    IngestFromGPS();
    void    HandleOneMessage(gpsValueToPublish gVal);
    void    PublishMessage(gpsValueToPublish gVal);
    bool    SerialSetup();
    bool    GeodesySetup();
    bool    ParserSetup();
    bool    SetParam_PORT(std::string sVal);
    bool    SetParam_BAUDRATE(std::string sVal);
    bool    SetParam_PREFIX(std::string sVal);
    bool    SetParam_HEADING_SOURCE(std::string sVal);
    bool    SetParam_HEADING_OFFSET(std::string sVal);
    bool    SetParam_PUBLISH_HDOP(std::string sVal);
    bool    SetParam_PUBLISH_RAW(std::string sVal);
    bool    SetParam_TRIGGER_MSG(std::string sVal);
    
    bool            m_bValidSerialConn;
    CMOOSGeodesy    m_geodesy;
    SerialComms*    m_serial;
    gpsParser*      m_parser;
    bool            m_pub_hdop;
    bool            m_pub_raw;
    bool            m_report_unhandled;
    std::string     m_serial_port;
    int             m_baudrate;
    std::string     m_prefix;
    std::string     m_trigger_key;
    unsigned short  m_heading_source;
    double          m_heading_offset;

    double          m_curX;
    double          m_curY;
    double          m_curLat;
    double          m_curLon;
    double          m_curHeading;
    

    // Stores number of messages processed, keyed on NMEA sentence name
    std::map<std::string, unsigned int>   m_counters;
    
};

#endif
