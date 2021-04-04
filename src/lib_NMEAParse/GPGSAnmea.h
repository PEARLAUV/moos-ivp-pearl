/*
 * GPGSAnmea.h
 *
 *  Created on: Jan 9, 2015
 *      Author: Alon Yaari


#ifndef GPGSANMEA_H_
#define GPGSANMEA_H_

#include "NMEAbase.h"
#include "NMEAutcTime.h"
#include "NMEAutcDate.h"

#define NUMELEM_GPGSA 17

//     GPGSA - GPSA DOP and Active Satellites
//
//     $GPGSA<
//     $GPGSA,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>,<9>,<10>,<11>,<12>,<13>,<14>,<15>,<16>,<17>*hh<CR><LF>
//     <1>   Mode         M=Manual, forced to operate in 2D or 3D, A=Automatic, 3D/2D
//     <2>   Mode         1=Fix not available, 2=2D, 3=3D
//     <3-14> SV id       IDs of SVs used in position fix (null for unused fields)
//     <15>   PDOP
//     <16>   HDOP
//     <17>   VDOP

#define NO_SV 999
enum gpsMode { GPS_MODE_BLANK = ' ', GPS_MODE_MANUAL = 'M', GPS_MODE_AUTOMATIC = 'A' };
enum fixMode { FIX_MODE_BLANK = ' ', FIX_MODE_NOT_AVAIL = '1', FIX_MODE_2D = '2', FIX_MODE_3D = '3' };

class infoGPGSA {
public:
    infoGPGSA() {};
    ~infoGPGSA() {};
    gpsMode         modeGPS;            // GPS_MODE_BLANK when blank in sentence
    fixMode         modeFix;            // FIX_MODE_BLANK when blank in sentence
    unsigned short  svID[12];           // NO_SV when blank in sentence
    double          pdop;               // BLANK_DOUBLE when blank in sentence
    double          hdop;               // BLANK_DOUBLE when blank in sentence
    double          vdop;               // BLANK_DOUBLE when blank in sentence
};

class GPGSAnmea : public NMEAbase {
public:
                    GPGSAnmea();
                    ~GPGSAnmea() {};
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);
    bool            CriticalDataAreValid();

private:
    bool            Validate_modeGPS(char& cVal);
    bool            Validate_modeFix(char& cVal);
    bool            Validate_svID(unsigned short usVal);
    bool            Validate_DOP(double& dVal);

public:
    bool            GetInfo(infoGPGSA& curInfo);
    bool            Get_modeGPS(char& cVal);
    bool            Get_modeFix(char& cVal);
    bool            Get_satNum(unsigned short& usVal);
    bool            Get_satID(unsigned short slotNum, unsigned short& usVal);
    bool            Get_pdop(double& dVal);
    bool            Get_hdop(double& dVal);
    bool            Get_vdop(double& dVal);

    bool            Set_modeGPS(const char& cVal);
    bool            Set_modeFix(const char& cVal);
    bool            Set_satID(unsigned short slotNum, const unsigned short& iVal);
    bool            Set_satIDintoNextOpenSlot(const unsigned short& usVal);
    bool            Set_pdop(const double& dVal);
    bool            Set_hdop(const double& dVal);
    bool            Set_vdop(const double& dVal);

    bool            FromString_modeGPS(std::string sVal);
    bool            FromString_modeFix(std::string sVal);
    bool            FromString_svID(unsigned short slotNum, std::string sVal);
    bool            FromString_pdop(std::string sVal);
    bool            FromString_hdop(std::string sVal);
    bool            FromString_vdop(std::string sVal);

    bool            ProduceNMEASentence(std::string& newSentence);

private:
    std::string     ToString_modeGPS(char& cVal);
    std::string     ToString_modeFix(char& cVal);
    std::string     ToString_svID(unsigned short usVal);
    std::string     ToString_pdop(double& dVal);
    std::string     ToString_hdop(double& dVal);
    std::string     ToString_vdop(double& dVal);

    infoGPGSA       info;

};
#endif

*/
