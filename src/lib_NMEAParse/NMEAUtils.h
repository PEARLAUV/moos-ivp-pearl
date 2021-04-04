/*
 * NMEAUtils.h
 *
 *  Created on: Feb 3, 2014
 *      Author: Alon Yaari
 */

#ifndef NMEAUTILS_H_
#define NMEAUTILS_H_

#include "MBUtils.h"
#include "NMEAdefs.h"
#include "NMEAgeog.h"

struct doubleVal {
    double          d;
    bool            valid;
};

struct intVal {
    int             i;
    bool            valid;
};

struct strVal {
    std::string     str;
    bool            valid;
};

struct charVal {
    char            c;
    bool            valid;
};

struct uShortVal {
    unsigned short  uI;
    bool            valid;
};


bool                ValidCharInString(char c, std::string validChars, bool blankIsValid = false, bool caseInsensitive = true);
bool                OneCharToUShort(unsigned short& intVal, char c);
bool                CharsToUShort(unsigned short &intVal, char ones, char tens = '0', char hundreds = '0');
std::string         UnsignedShortToStringWithTwoChars(unsigned short n);
bool                HexInAStringToUInt(const std::string hex, unsigned int& ui);
bool                UIntToHexInAString(const unsigned int ui, std::string& hex, unsigned int padZeroToThisLen);
int                 HexCharToInt(char h);
char                IntToHexChar(int i);


void                StoreDoubleFromNMEAstring(double& dStoreHere, const std::string &sStoreThis);
void                StoreUShortFromNMEAstring(unsigned short& uStoreHere, const std::string &sStoreThis);
void                StoreIntFromNMEAstring(int& iStoreHere, const std::string &sStoreThis);
void                StoreCharFromNMEAstring(char& cStoreHere, const std::string &sStoreThis);

// Convenience functions for validating data
bool                IsValidBoundedDouble(const double& d, const double& min, const double& max, bool inclusive=true, bool blankOK = true);
bool                IsValidBoundedUShort(const unsigned short& u, const unsigned short& min, const unsigned short max, bool inclusive=true, bool blankOK = true);
bool                IsValidBoundedUChar(const unsigned char& uc, const unsigned char& min, const unsigned char max, bool inclusive=true, bool blankOK=true);
bool                IsValidBoundedInt(const int& i, const int& min, const int& max, bool inclusive=true, bool blankOK = true);

// Functions for building NMEA element strings
std::string         CharToString(char c);
std::string         SingleDigitToString(unsigned short n);
std::string         SingleDigitToString(int n);
std::string         TwoDigitsToString(unsigned short n);
std::string         TwoDigitsToString(int n);
std::string         BoundedDoubleToString(double lower, double upper, unsigned short decimalPlaces, double dVal);
std::string         FormatHDOP(double hdop);
std::string         FormatAltMSL(double altMSL);
std::string         FormatAltGeoid(double altGeoid);
std::string         FormatPosDouble(double dVal, unsigned short leftDigits, unsigned short rightDigits);
std::string         FormatDouble(double dVal, unsigned short leftDigits, unsigned short rightDigits);


// Convenience functions for validating string formats
std::string         ChecksumCalc(std::string sentence);
bool                IsUInt(std::string str, unsigned minLen, unsigned maxLen);
bool                IsFormattedDouble(std::string str, unsigned minLen, unsigned maxLen, short decPoint);
bool                IsSimpleDouble(std::string str);
bool                IsSingleChar(std::string str, std::string listOfValidChars);
bool                IsGenericField(std::string str);

#endif
