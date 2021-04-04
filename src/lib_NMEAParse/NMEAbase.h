/*
 * NMEAbase.h
 *
 *  Created on: Nov 14, 2013
 *      Author: Alon Yaari
 */

#ifndef NMEABASE_H_
#define NMEABASE_H_

#include <map>
#include "MBUtils.h"
#include <vector>
#include <sstream>
#include "NMEAdefs.h"
#include "NMEAgeog.h"
#include "NMEAUtils.h"
#include "NMEAparsing.h"



/*

    NMEAbase is an abstract class inherited by all NMEA sentence classes.

    Metadata stored about a sentence:
        string          nmeaKey     The 5-char code defining the sentence
        unsigned short  nmeaLen     Number of data elements this sentence is expected to have
        bool            bValid      TRUE when all elements in data map are valid

    Data Element Validation


    Sentence data is stored in two ways:
        (1) String values in a map
            std::map<unsigned short, std::string> data
            - Index describes number of commas preceding the data element
            - Index of 0 is undefined
            - All data is stored as a string
            - Populated in one of two ways:
                - on ingesting an existing sentence
                - As elements are populated to build new sentence
        (2) In a struct infoXXXX
            - Base class has no info struct
            - Each child class must have its own info struct
            - Populated in one of two ways:
                - on ingesting an existing sentence

    To parse an existing sentence:

        bool NMEAbase::ParseSentenceIntoData(string sentence)
            - Pass sentence in as string
            - TRUE if valid NMEA sentence with no checksum or data errors
            - FALSE if invald sentence with bad checksum or invalid contents
            - Base class function
                 - Validates checksum
                 - Breaks data apart into the data map
            - Child classes overload to:
                 - Validate data elements
                 - Store values into info struct

    To create a new sentence:

        bool Define_XXXXX_Data(infoXXXXX curInfo)
            - No function for this in the base class
            - Each child class does has its own Define_ function
            - Populate the curInfo variable with proper data
            - Pass curInfo to Define_XXXXX_Data
            - Returns TRUE if all data was valid
            - Returns FALSE if any data element was invalid
 */

class nmeaDetail {
public:
    nmeaDetail() {};
    ~nmeaDetail() {};
    std::string         nmeaSentence;
    std::string         nmeaKey;
    unsigned short      numElements;
    bool                bGoodChecksum;
    std::vector<std::string> elements;
};


class NMEAbase {
public:
	                    NMEAbase();
	virtual             ~NMEAbase() {};
    virtual bool        CriticalDataAreValid() = 0;
    virtual bool        ProduceNMEASentence(std::string& newSentence);
    void                SetKey(std::string key);
    std::string         GetKey() { return nmeaKey; };
    void                SetDataLength(unsigned len);
    std::string         GetErrorString() { return errors; };
    virtual bool        ParseSentenceIntoData(std::string sentence, bool bAllowBlankChecksum=true);
    static bool         IsValidChecksum(std::string strNMEA, bool bAllowBlankChecksum);
    static std::string  GetKeyFromSentence(std::string strNMEA);

protected:
    void                AddError(std::string error);
    void                BuildFullSentence(std::string& fullSentence, const std::string& strBody);

    // Parameters needed by the derived NMEA types
    std::string         nmeaKey;                    // Assigned 5-char key of this sentence
    unsigned short      nmeaLen;                    // Assigned number of index positions in this sentence
    std::string         errors;                     // Error string to pass back to caller when formatting
    std::ostringstream  errorBuild;
    nmeaDetail          curSentence;                // If sentence sourced from an NMEA string, metadata for that string
                                                    // otherwise, metadata for newly created NMEA sentence
};
#endif
