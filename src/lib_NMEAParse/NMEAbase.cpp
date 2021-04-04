/*
 * NMEAbase.cpp
 *
 *  Created on: Nov 14, 2013
 *      Author: Alon Yaari
 */

#include "math.h"
#include "MOOS/libMOOS/MOOSLib.h"
//#include "MOOS/libMOOS/Utils/MOOSUtilityFunctions.h"
#include "MBUtils.h"
#include "NMEAbase.h"

using namespace std;

// NMEAbase() CONSTRUCTOR
//      - Use when instantiating this class when NMEA key is not yet known
NMEAbase::NMEAbase()
{
    nmeaLen     = 1;        // By default every NMEA sentence must have at least 1 entry
    nmeaKey     = "";       // At time of construction, NMEA key is not yet defined
}

// GetKeyFromSentence()
//      - Get the NMEA's key without instantiating a full NMEA derivative class
//      - BAD CHECKSUM RETURNS ""
//      - BLANK CHECKSUM RETURNS the key, let the missing checksum be handled by the parser later
//      - Sample NMEA: $CPRBS,172909.322,15.121597,15.121597,15.121597,0*76
//          - The key is the five chars from index 1 to index 5 ('$' is char at position 0)
string NMEAbase::GetKeyFromSentence(string strNMEA)
{
    string key = "";
    if (IsValidChecksum(strNMEA, ALLOW_BLANK_CHECKSUM)) {
        if (strNMEA.length() > 6)
            key = strNMEA.substr(1, 5); }
    return key;
}

// IsValidChecksum()
//      - Returns TRUE when:
//          - Non-empty
//          - First character is '$'
//          - Checksum in last position is valid for the sentence
//          - Sentence is properly formatted between '$' and '*' characters
bool NMEAbase::IsValidChecksum(std::string strNMEA, bool bAllowBlankChecksum)
{
    bool bEmpty = strNMEA.empty();
    if (bEmpty && !bAllowBlankChecksum)
        return false;
    if (strNMEA.at(0) != '$')
        return false;
    string sToCheck = biteString(strNMEA,'*');
    if (strNMEA.empty())
        return bAllowBlankChecksum;
    if (strNMEA.length() < 2)
        return false;
    sToCheck += '*';
    string csCalculated = ChecksumCalc(sToCheck);
    if (csCalculated.length() < 2)
        csCalculated = "0" + csCalculated;
    char a1 = csCalculated.at(0);
    char a2 = csCalculated.at(1);
    char b1 = strNMEA.at(0);
    char b2 = strNMEA.at(1);
    bool theSame = (a1 == b1 && a2 == b2);
    return theSame;
}

// ParseSentenceIntoData()
//      - Validate checksum
//      - Validate key is what we expect
bool NMEAbase::ParseSentenceIntoData(string sentence, bool bAllowBlankChecksum)
{
    curSentence.bGoodChecksum = IsValidChecksum(sentence, bAllowBlankChecksum);
    curSentence.nmeaSentence = sentence;
    if (curSentence.bGoodChecksum) {

        // Parse data into string vector
        //      - String to parse is between the '$' and the '*'
        //      - vector.at(0) is the sentence key
        string toParse = biteString(sentence, '$');
        toParse = biteString(sentence, '*');
        curSentence.elements = parseString(toParse, ',');       // Populate the vector
        curSentence.numElements = curSentence.elements.size();

        // Must have at least two elements, the NMEA key and one data element
        if (curSentence.numElements < 2)
            return false;

        // Must have at least the number of elements expected for this type of sentence
        if (curSentence.numElements < nmeaLen)
            return false;

        curSentence.nmeaKey = curSentence.elements[0]; }
    return curSentence.bGoodChecksum;
}


// SetKey()
//      Set the 5-uppercase-character NMEA key
void NMEAbase::SetKey(string key)
{
	//nmeaID = UNDEF;
	size_t len = key.length();
	if (len != 5) {
		nmeaLen = 0;
		nmeaKey = "";
		return; }
	nmeaKey = toupper(key);
}

// SetDataLength()
//      - Assigns the nmeaLen this positive value
void NMEAbase::SetDataLength(unsigned len)
{
    nmeaLen = len;
}

bool NMEAbase::ProduceNMEASentence(string& newSentence)
{
    newSentence = "";
    return CriticalDataAreValid();
}

void NMEAbase::BuildFullSentence(string& fullSentence, const string& strBody)
{
    fullSentence = "$";
    fullSentence += nmeaKey;
    fullSentence += ",";
    fullSentence += strBody;
    fullSentence += "*";
    string csCalculated = ChecksumCalc(fullSentence);
    fullSentence += csCalculated;
    fullSentence += "\r\n";
}

void NMEAbase::AddError(string error)
{
    if (!errors.empty())
        errors += "; ";
    errors += error;
    errorBuild.clear();
}







// ATTIC

/*

// ProduceNMEAString()
string NMEAbase::ProduceNMEASentence()
{
    nmeaSentence = "INVALID";
    if (CriticalDataAreValid()) {
        nmeaSentence = "$";
        nmeaSentence += nmeaKey;
        for (unsigned short i = 1; i <= nmeaLen; i++) {
            nmeaSentence += ',';
            nmeaSentence += data[i]; }
        nmeaSentence += '*';
        string chk = ChecksumCalc(nmeaSentence);
        nmeaSentence += chk; }
    return nmeaSentence;
}


bool NMEAbase::CriticalDataAreValid()
{
    // Handle unknown NMEA types
    return ValidateUNK();
}

// ValidateUNK()
//      Not a known NMEA type so validate what we can guess
//      - Is the NMEA key 5 uppercase chars?
//      - At spaces in any data elements?
//      - No invalid chars in any data elements?
bool NMEAbase::ValidateUNK()
{
    // NMEA key must always be 5 uppercase chars
    if (nmeaKey.length() != 5)
        return false;
    unsigned i;
    for (i = 0; i < 6; i++) {
        if (nmeaKey.at(i) < 'A' || nmeaKey.at(i) > 'Z')
            return false; }

    // Validate each element in the sentence
    //      - For now, only check for spaces in elements (which means invalid)
    for (unsigned i = 1; i < nmeaLen; i++) {
        string element = data[i];
        if (element.find(' ') != string::npos)
            return false; }
    return true;
}


*/















//
