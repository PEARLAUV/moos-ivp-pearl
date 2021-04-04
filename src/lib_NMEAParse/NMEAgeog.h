/*
 * NMEAgeog.h
 *
 *  Created on: May 30, 2014
 *      Author: Alon Yaari
 */

#ifndef NMEAGEOG_H_
#define NMEAGEOG_H_

#include "NMEAdefs.h"

//  UNK unknown type
//  LAT latitude
//  LON longitude
//  MAG magnetic variation
enum geogType { GEOG_UNK, GEOG_LAT, GEOG_LATDD, GEOG_LON, GEOG_LONDD, GEOG_MAG, GEOG_BAD };

class NMEAgeog {

public:
                    NMEAgeog();
                    ~NMEAgeog() {};
    void            Clear();        // zero out all member variables
    bool            Set_GeogType(unsigned short typeOfGeog);
    void            SetInvalid() { bValid = false; };
    bool            Validate();     // Validates value and hemisphere based on the type
    bool            IsBlank() { return bIsBlank; };
    void            SetBlank() { bIsBlank = true; };
    bool            ParseFromNMEAstring(const std::string strNumber, const std::string strHemi);
    bool            ParseFromNMEAstring(unsigned short typeOfGeog, const std::string strNumber, const std::string strHemi);
    bool            StoreDouble(double d);
    bool            StoreDouble(unsigned short typeOfGeog, double d);
    bool            Get_ValueAsDouble(double& gVal);
    bool            Get_ValueString(std::string& str);
    bool            Get_HemisphereString(std::string& str);

private:
    unsigned short  geogType;       // What type of geog value being stored (see enum of geogType)
    double          doubleDD;       // Full value, negative or positive, in decimal degrees
    std::string     strValue;       // Absolute value of the number in string form
    char            strHemisphere;  // 'N' or 'E' = positive, 'S' or 'W' = negative
    bool            bIsBlank;       // TRUE = represents blank in nmea sentence
    bool            bValid;         // TRUE = this struct currently stores a valid value

    bool            Valid_GeogType();
    void            DoubleDDtoStrValue();
};
#endif
