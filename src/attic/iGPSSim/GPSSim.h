/*
 * GPSSim.cpp
 *
 *  Created on: Nov 5, 2014
 *      Author: Alon Yaari
 */

#include <iostream>
#include <math.h>
#include <map>

#include "MOOS/libMOOS/MOOSLib.h"
#include "MOOS/libMOOSGeodesy/MOOSGeodesy.h"
#include "SerialComms.h"

//#include "NMEAutcTime.h"  // Uncomment if modifying this class to do something with NMEA time
#include "CPNVGnmea.h"
#include "CPRBSnmea.h"
#include "GPGGAnmea.h"
#include "GPGSTnmea.h"
#include "GPHDTnmea.h"
#include "GPRMCnmea.h"
#include "GPRMEnmea.h"
#include "PASHRnmea.h"
#include "PYDEPnmea.h"
#include "PYDEVnmea.h"
#include "PYDIRnmea.h"
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"

#define KNOTS2METERSperSEC  0.51444444
#define DEG2RAD             0.01745329251994
#define SERIAL_TRY_DELAY    2.5

struct gpsPub {
    bool        isDouble;
    double        dVal;
    std::string    sVal;
};

class GPSSim : public AppCastingMOOSApp
{
public:
  GPSSim();
    virtual ~GPSSim() {};
    bool    OnNewMail(MOOSMSG_LIST &NewMail);
    bool    Iterate();
    bool    OnConnectToServer();
    bool    OnStartUp();
    bool    buildReport();

protected:
    bool    RegisterForMOOSMessages();
    bool    ParseNMEAString(std::string nmea);
    bool    SerialSetup();
    void    PrefixCleanup();
    double  DMS2DecDeg(double dfVal);

    bool    SetParam_GPGGA(std::string sVal);
    bool    SetParam_GPGST(std::string sVal);
    bool    SetParam_GPHDT(std::string sVal);
    bool    SetParam_GPRMC(std::string sVal);
    bool    SetParam_GPRME(std::string sVal);
    bool    SetParam_GPTXT(std::string sVal);
    bool    SetParam_PASHR(std::string sVal);
    bool    SetParam_INPUT_PREFIX(std::string sVal);
    bool    SetParam_PORT(std::string sVal);
    bool    SetParam_BAUDRATE(std::string sVal);

    bool    PublishGPSSentences();
    bool    PublishToSerial(std::string nmea);
    std::string ProduceGPGGA();
    std::string ProduceGPGST();
    std::string ProduceGPHDT();
    std::string ProduceGPRMC();
    std::string ProduceGPRME();
    std::string ProduceGPTXT();
    std::string ProducePASHR();

    std::string     m_msg_lat;
    std::string     m_msg_lon;
    std::string     m_msg_heading;
    std::string     m_msg_speed;
    double          m_lat;
    double          m_lon;
    double          m_heading;
    double          m_speed;

    bool            m_publish_GPGGA;
    bool            m_publish_GPGST;
    bool            m_publish_GPHDT;
    bool            m_publish_GPRMC;
    bool            m_publish_GPRME;
    bool            m_publish_GPTXT;
    bool            m_publish_PASHR;

    unsigned int    m_count_GPGGA;
    unsigned int    m_count_GPGST;
    unsigned int    m_count_GPHDT;
    unsigned int    m_count_GPRMC;
    unsigned int    m_count_GPRME;
    unsigned int    m_count_GPTXT;
    unsigned int    m_count_PASHR;

    utcTime         m_utcTime;
    GPGGAnmea       m_gga;
    GPGSTnmea       m_gst;
    GPHDTnmea       m_hdt;
    GPRMCnmea       m_rmc;
    GPRMEnmea       m_rme;
    //GPTXTnmea     m_txt;        // lib_NMEAParse does not contain this sentence as of this writing
    PASHRnmea       m_shr;

    double          m_curMOOSTime;
    bool            m_goodParams;
    SerialComms*    m_serial;
    std::string     m_serialPort;
    int             m_baudRate;
    std::string     m_input_prefix;
    double          m_lastSerialTry;
    unsigned int    m_count_serial;

};











//







