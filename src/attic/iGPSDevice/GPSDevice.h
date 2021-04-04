/*
 * GPSDevice.h
 *
 *  Created on: Nov 6, 2014
 *      Author: Alon Yaari
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
                     HEADING_SOURCE_GPRMC,
                     HEADING_SOURCE_COMPASS,
                     HEADING_SOURCE_PASHR };

class GPSDevice : public AppCastingMOOSApp
{
public:
            GPSDevice();
    virtual ~GPSDevice() {};
    bool    OnNewMail(MOOSMSG_LIST &NewMail);
    bool    Iterate();
    bool    OnConnectToServer();
    bool    OnStartUp();
    bool    buildReport();

protected:
    bool    RegisterForMOOSMessages();

    void    IngestFromGPSDevice();
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
    bool    SetParam_PUBLISH_UTC(std::string sVal);
    bool    SetParam_PUBLISH_HPE(std::string sVal);
    bool    SetParam_PUBLISH_HDOP(std::string sVal);
    bool    SetParam_PUBLISH_YAW(std::string sVal);
    bool    SetParam_PUBLISH_RAW(std::string sVal);
    bool    SetParam_PUBLISH_PITCH_ROLL(std::string sVal);
    bool    SetParam_SWAP_PITCH_ROLL(std::string sVal);
    bool    SetParam_TRIGGER_MSG(std::string sVal);

    bool            m_bValidSerialConn;
    CMOOSGeodesy    m_geodesy;
    SerialComms*    m_serial;
    gpsParser*      m_parser;
    bool            m_pub_utc;
    bool            m_pub_hpe;
    bool            m_pub_hdop;
    bool            m_pub_yaw;
    bool            m_pub_raw;
    bool            m_pub_pitch_roll;
    bool            m_swap_pitch_roll;
    bool            m_report_unhandled;
    std::string     m_serial_port;
    int             m_baudrate;
    std::string     m_prefix;
    std::string     m_trigger_key;
    unsigned short  m_heading_source;
    double          m_heading_offset;
    double          m_compass_heading;

    double          m_curX;
    double          m_curY;
    double          m_curLat;
    double          m_curLon;

    // Stores number of messages processed, keyed on NMEA sentence name
    std::map<std::string, unsigned int>   m_counters;
};

#endif


















//

