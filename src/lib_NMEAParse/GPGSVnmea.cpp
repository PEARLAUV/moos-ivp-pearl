/*
 * GPGSVnmea.cpp
 *
 *  Created on: Dec 31, 2014
 *      Author: Alon Yaari


#include "GPGSVnmea.h"

using namespace std;

bool infoGPGSV::ValidateSat(int& slotNum)
{
  if (slotNum >= 0 && slotNum < 5)
    return satDetail[slotNum].ValidateSat();
  else
    return false;
}

// A. Constructor
GPGSVnmea::GPGSVnmea()
{
    nmeaLen     = NUMELEM_GPGSV;
    SetKey("GPGSV");
    info.satCount    = BAD_INT;
}

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

// Message#  slot#  :  PRN    Elev  Azumith  SNR    (i/4)*(m#*4) =
//     1       0    :    4     5       6       7      1   1  = 1
//     1       1    :    8     9      10      11      2   1  = 2
//     1       2    :   12    13      14      15      3   1  = 3
//     1       3    :   16    17      18      19      4   1  = 4
//     2       4    :    4     5       6       7      1   2  = 2
//     2       5    :    8     9      10      11      2       1    =
//     2       6    :   12    13      14      15      3       1    =
//     2       7    :   16    17      18      19      4       1    =
//     3       8    :    4     5       6       7      1       2    =
//     3       9    :    8     9      10      11      2       2    =
//     3      10    :   12    13      14      15      3       2    =
//     3      11    :   16    17      18      19      4       2    =
bool GPGSVnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
  // Always call base class version of this function to validate basics
  //      and populate inSentence.elements
  if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
    return false;

  int messNum = 0;
  for (unsigned short i = 1; i < curSentence.numElements; i++) {
    string sVal = curSentence.elements.at(i);
    switch(i) {
      case 2:   FromString_messNum(messNum, sVal);                 break;  // message #
      case 3:   FromString_satCount(sVal);                         break;  // total # of SVs in view

      // PRN in position 4, 8, 12, 16
      case 4:
      case 8:
      case 12:
      case 16:  int slotNum = ((i - 0) / 4) * messNum - 1;
                FromString_oneSatPRN(slotNum, sVal);               break;

      // Elev in position 5, 9, 13, 17
      case 5:
      case 9:
      case 13:
      case 17:  int slotNum = ((i - 1) / 4) * messNum - 1;
                FromString_oneSatElev(slotNum, sVal);              break;

      // Azimuth in position 6, 10, 14, 18
      case 6:
      case 10:
      case 14:
      case 18:  int slotNum = ((i - 2) / 4) * messNum - 1;
                FromString_oneSatAzimuth(slotNum, sVal);           break;

      // SNR in position 7, 11, 15, 19
      case 6:
      case 10:
      case 14:
      case 18:  int slotNum = ((i - 3) / 4) * messNum - 1;
                FromString_oneSatSNR(slotNum, sVal);               break;

      default:                                                     break; } }
    return CriticalDataAreValid();
}

bool GPGSVnmea::CriticalDataAreValid()
{
    return info.Validate();
}

bool GPGSVnmea::Validate_satCount(const int i)
{
  return (i >=0 && i <= MAXSAT);
}

bool GPGSVnmea::GetInfo(infoGPGSV& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool GPGSVnmea::Get_satCount(int& i)
{
    i = info.satCount;
    return Validate_satCount(i);
}

bool GPGSVnmea::Get_oneSat(oneSat& sat, int slotNum)
{
  if (slotNum >= info.satCount || slotNum >= MAXSAT)
    return false;
  sat = info.satDetail[slotNum];
  return sat.Validate();
}

bool GPGSVnmea::Get_PRN(int& i, int slotNum)
{
  i = BLANK_INT;
  oneSat sat;
  bool bGood = Get_oneSat(sat, slotNum);
  i = sat.svPRN;
  return bGood;
}

bool GPGSVnmea::Get_elev(int& i, int slotNum)
{
  i = BLANK_INT;
  oneSat sat;
  bool bGood = Get_oneSat(sat, slotNum);
  i = sat.degElev;
  return bGood;
}

bool GPGSVnmea::Get_azimuth(int& i, int slotNum)
{
  i = BLANK_INT;
  oneSat sat;
  bool bGood = Get_oneSat(sat, slotNum);
  i = sat.degAzimuth;
  return bGood;
}

bool GPGSVnmea::Get_SNR(int& i, int slotNum)
{
  i = BLANK_INT;
  oneSat sat;
  bool bGood = Get_oneSat(sat, slotNum);
  i = sat.snr;
  return bGood;
}

bool GPGSVnmea::FromString_satCount(string sVal)
{
  StoreIntFromNMEAstring(info.satCount, sVal);
    return Validate_satCount(info.satCount);
}

bool GPGSVnmea::FromString_messNum(string sVal)
{
  StoreIntFromNMEAstring(info.me)
  StoreDoubleFromNMEAstring(info.estVPE, sVal);
    return Validate_posErr(info.estVPE);
}

bool GPGSVnmea::FromString_estPOSerr(string sVal)
{
  StoreDoubleFromNMEAstring(info.estPOSerr, sVal);
    return Validate_posErr(info.estPOSerr);
}

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool GPGSVnmea::ProduceNMEASentence(string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $GPGSV and postpend *ch (ch = checksum)
    string dataBody = "";
    dataBody += ToString_PosErr(info.estHPE);
    dataBody += ",M";
    dataBody += ToString_PosErr(info.estVPE);
    dataBody += ",M";
    dataBody += ToString_PosErr(info.estPOSerr);
    dataBody += ",M";

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    newSentence = curSentence.nmeaSentence;
    return true;
}

string GPGSVnmea::ToString_PosErr(const double d)
{
  if (d == BLANK_DOUBLE || d == BAD_DOUBLE)
    return "";
  return doubleToString(d, 2);
}


*/














//
