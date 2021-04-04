/*
 * GPGSVnmea.h
 *
 *  Created on: Dec 31, 2014
 *      Author: Alon Yaari


#ifndef GPGSVNMEA_H_
#define GPGSVNMEA_H_

//     GPGSV - GPS Satellites in view
//     $GPGSV,<1>,<2>,<3>,<4>,<5>,<6>,<7>,[<8-11>],[<12-15>],[<16-19>]*hh<CR><LF>
//     <1>  Total number of messages of this type in this cycle
//     <2>  Message number
//     <3>  Total number of SVs in view
//     <4>  SV PRN number
//     <5>  Elevation in degrees, 90 maximum
//     <6>  Azimuth, degrees from true north, 000 to 359
//     <7>  SNR, 00-99 dB (null when not tracking)
//     <8-11> repeat 4-7, if needed, otherwise checksum
//     <12-15> repeat 4-7, if needed, otherwise checksum
//     <16-19> repeat 4-7, id needed, otherwise checksum

#define NUMELEM_GPGSV      7
#define MAXSATID         256

#include "NMEAbase.h"                   // Always include the base class

class oneSat {
public:
  oneSat() {
    svPRN      = BLANK_INT;
    degElev    = BLANK_INT;
    degAzimuth = BLANK_INT;
    snr        = BLANK_INT; };
  ~oneSat() {};
  bool            ValidateSat()
                    { return (Validate_PRN() &&
                              Validate_elev() &&
                              Validate_azimuth() &&
                              Validate_SNR()); };
  int             svPRN;            // BLANK_INT when blank in sentence
  int             degElev;          // BLANK_INT when blank in sentence
  int             degAzimuth;       // BLANK_INT when blank in sentence
  int             snr;              // BLANK_INT when blank in sentence

private:
    bool            Validate_PRN()
                      { return (svPRN == BLANK_INT ||
                                 (svPRN > 0 &&
                                  svPRN < MAXSATID)); };
    bool            Validate_elev()
                      { return (svPRN == BLANK_INT ||
                                 (degElev >= 0 &&
                                  degElev <= 90)); };
    bool            Validate_azimuth()
                      { return (svPRN == BLANK_INT ||
                                 (degAzimuth >=0 &&
                                  degAzimuth <= 360)); };
    bool            Validate_SNR()
                      { return (svPRN == BLANK_INT || snr >= 0); };
};

class infoGPGSV {
public:
    infoGPGSV() {};
    ~infoGPGSV() {};

    bool            ValidateSat(int& slotNum);
    int             messCount;          // BLANK_INT when blank in sentence
    int             messNum;            // BLANK_INT when blank in sentence
    int             satCount;           // BLANK_INT when blank in sentence
    oneSat          satDetail[4];       // Each sentence can store details for up to 4 satellites
};

// Class Definition
class GPGSVnmea: public NMEAbase {
public:
                    GPGSVnmea();                                    // Standard constructor
    virtual         ~GPGSVnmea() {};                                // Standard destructor
    bool            ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum);    // Populate from parsing an NMEA string sentence
    bool            CriticalDataAreValid();                         // Validate data loaded into the local info buffer

private:
    bool            Validate_satCount(const int i);

public:
    bool            GetInfo(infoGPGSV& curInfo);                    // Always include a GetInfo function that returns the info struct
    bool            Get_messCount(int& i);
    bool            Get_messNum(int& i);
    bool            Get_satCount(int& i);
    bool            Get_oneSat(oneSat& sat, int slotNum);
    bool            Get_PRN(int& i, int slotNum);
    bool            Get_elev(int& i, int slotNum);
    bool            Get_azimuth(int& i, int slotNum);
    bool            Get_SNR(int& i, int slotNum);

    bool            FromString_messCount(std::string sVal);
    bool            FromString_messNum(int& messNum, std::string sVal);
    bool            FromString_satCount(std::string sVal);
    bool            FromString_oneSatPRN(int slotNum, std::string prn);
    bool            FromString_oneSatElev(int slotNum, std::string elev);
    bool            FromString_oneSatAzimuth(int slotNum, std::string azimuth);
    bool            FromString_oneSatSNR(int slotNum, std::string snr);

    bool            ProduceNMEASentence(std::string& newSentence);  // Create valid NMEA sentence from local info buffer

private:
    std::string   ToString_satNum();
    std::string   ToString_oneSat(int slotNum);

    infoGPGSV       info;
};


#endif

*/





