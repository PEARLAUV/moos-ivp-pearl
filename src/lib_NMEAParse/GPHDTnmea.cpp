/*
 * GPHDTnmea.cpp
 *
 *  Created on: July 18, 2014
 *      Author: Alon Yaari
 */

#include "MBUtils.h"
#include "NMEAUtils.h"
#include "GPHDTnmea.h"
#include "stdlib.h"      //strtod

using namespace std;


// A. Constructor
GPHDTnmea::GPHDTnmea()
{
    nmeaLen     = NUMELEM_GPHDT;
    SetKey("GPHDT");

    // Invalidate the info struct
    info.yaw        = BAD_DOUBLE;
}

/* B. Sentence Definition
      YAW (this is not HEADING)
      This is the direction that the vessel (antennas) is pointing and is not
      necessarily the direction of vessel motion (the course over ground).
        $GPHDT,<1>,<2>*hh<CR><LF>
		    or
		$HEHDT,<1>,<2>*hh<CR><LF>
        <1>  Current heading in degrees
        <2>  Always 'T' to indicate true heading
*/

// C. Parse incoming NMEA sentence

// Parse an incoming NMEA sentence string into the info buffer
//      - First call base class which populates inSentence struct
//      - Parse the elements of the inSentence.elements vector
bool GPHDTnmea::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
	// This message could come in as "GPHDT" or "HEHDT"
	if (sentence.at(1) == 'H' && sentence.at(2) == 'E') {
		sentence.at(1) = 'G';
		sentence.at(2) = 'P'; }

    if (!NMEAbase::ParseSentenceIntoData(sentence, bAllowBlankChecksum))
        return false;

    for (unsigned short i = 1; i < curSentence.numElements; i++) {
        string sVal = curSentence.elements.at(i);
        switch(i) {
            case 1: FromString_yaw(sVal);                               break;
            default:                                                    break; } }
    return CriticalDataAreValid();
}

// D. Data validation for info buffer
bool GPHDTnmea::CriticalDataAreValid()
{
    bool bValid = Validate_yaw();
    return bValid;
}

// E. Validation functions
//      - Many common data types have validations that can be called from NMEAUtils

bool GPHDTnmea::Validate_yaw()
{
	return IsValidBoundedDouble(info.yaw, 0.0, 360.0, INCLUSIVE_YES, MAY_NOT_BE_BLANK);
}

// F. Getters

// GetInfo()
bool GPHDTnmea::GetInfo(infoGPHDT& curInfo)
{
    curInfo = info;
    return CriticalDataAreValid();
}

bool GPHDTnmea::Get_yaw(double& d)
{
	d = info.yaw;
    return Validate_yaw();
}

// G. Setters

bool GPHDTnmea::Set_yaw(const double d)
{
    info.yaw = d;
    return Validate_yaw();
}

// H. FromString_ functions

bool GPHDTnmea::FromString_yaw(string sVal)
{
	info.yaw = strtod(sVal.c_str(), 0);
    return Validate_yaw();
}

// I. Create an NMEA sentence from data stored in info buffer

// ProduceNMEASentence()
//      - Produces a string suitable for publishing as a fully-qualified and valid NMEA sentence
bool GPHDTnmea::ProduceNMEASentence(string& newSentence)
{
    // Call base class version to do basic checks
    //      - Includes call to CriticalDataAreValid()
    if (!NMEAbase::ProduceNMEASentence(newSentence))
        return false;

    // Create string for the data portion of the sentence
    //      - BuildFullSentence() is called at end to prepend $GPHDT and postpend *ch (ch = checksum)
    string dataBody = ToString_yaw();
    dataBody += ",T";

    // Pre and post-pend the mechanics of the NMEA sentence
    BuildFullSentence(curSentence.nmeaSentence, dataBody);
    newSentence = curSentence.nmeaSentence;
    return true;
}

// J. Produce strings for NMEA sentences
//      Note: Each part of an NMEA sentence has its own string producer.
//            Even coupled data such as latitude (which is stored as a value-hemisphere pair)
//            requires one producer for the value and one for the hemisphere character.
//            This is for convenient bookkeeping when assembling the full sentence.

string GPHDTnmea::ToString_yaw()
{
    return BoundedDoubleToString(0.0, 360.0, 1, info.yaw);
}














//
