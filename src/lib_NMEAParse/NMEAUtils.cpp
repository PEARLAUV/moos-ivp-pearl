/*
 * NMEAUtils.cpp
 *
 *  Created on: Feb 3, 2014
 *      Author: Alon Yaari
 */

#include "NMEAUtils.h"
#include <stdlib.h>     // Must explicitly include stdlib.h for clang to find abs()
#include <sstream>
#include <iomanip>
#include "MOOS/libMOOS/MOOSLib.h"
#include "MBUtils.h"
#include "NMEAutcTime.h"
#include "NMEAparsing.h"
#include "math.h"       // pow()

using namespace std;

// ValidCharInString()
//      Returns true if the char is in the string
//      Input:      char c              the char in question
//                  string validChars   all possible valid values that char can be to be true
//                  blankIsValid        the BLANK_CHAR character evaluates to blankIsValid
//                  caseInensitive      TRUE means ignore case, FALSE means upper and lower aren't the same
bool ValidCharInString(char c, std::string validChars, bool blankIsValid, bool caseInsensitive)
{
    if (c == BLANK_CHAR)
        return blankIsValid;
    if (caseInsensitive) {
        validChars = toupper(validChars);
        if (c >= 'a' && c <= 'z')
            c -= 32; }
    return (validChars.find(c, 0) != string::npos);
}

// OneCharToUShort()
//      - Converts single char [c] into unsigned integer value
//      - Returns true if [c] is '0' through '9'
//      - Any other value is FALSE
bool OneCharToUShort(unsigned short& intVal, char c)
{
    bool bGood = !(c < '0' || c > '9');
    if (bGood)
        intVal = (unsigned short) c - 48;
    return bGood;
}

// CharsToUShort()
//      - Converts three characters into an unsigned short integer
//      - [ones] must be specified
//      - [tens] and [hundreds] may be omitted
//      - TRUE if [ones], [tens], [hundreds] converts to 000 through 999
//      - FALSE if any chars not '0' through '9'
bool CharsToUShort(unsigned short &intVal, char ones, char tens, char hundreds)
{
    unsigned short iHundreds, iTens, iOnes;
    bool goodH = OneCharToUShort(iHundreds, hundreds);
    bool goodT = OneCharToUShort(iTens, tens);
    bool goodO = OneCharToUShort(iOnes, ones);
    bool bGood = (goodH && goodT && goodO);
    if (bGood)
        intVal = iOnes + (iTens * 10) + (iHundreds * 100);
    return bGood;
}

string UnsignedShortToStringWithTwoChars(unsigned short n)
{
    string retStr = "";
    n = n % 100;        // Only dealing with tens and ones place
    char ones = '0' + (n % 10);
    n = n / 10;
    char tens = '0' + (n % 10);
    retStr += tens;
    retStr += ones;
    return retStr;
}

bool IsValidBoundedInt(const int& i, const int& min, const int& max, bool inclusive, bool blankOK)
{
  if (i == BLANK_INT)
    return blankOK;
  if (inclusive)
    return (i >= min && i <= max);
  return (i > min && i < max);
}

bool IsValidBoundedDouble(const double& d, const double& min, const double& max, bool inclusive, bool blankOK)
{
    if (d == BLANK_DOUBLE)
        return blankOK;
    if (inclusive)
        return (d >= min && d <= max);
    return (d > min && d < max);
}

bool IsValidBoundedUChar(const unsigned char& uc, const unsigned char& min, const unsigned char max, bool inclusive, bool blankOK)
{
    if (uc == (unsigned char) BLANK_UCHAR)
        return blankOK;
    if (inclusive)
        return (uc >= min && uc <= max);
    return (uc > min && uc < max);
}

bool IsValidBoundedUShort(const unsigned short& u, const unsigned short& min, const unsigned short max, bool inclusive, bool blankOK)
{
    if (u == BLANK_USHORT)
        return blankOK;
    if (inclusive)
        return (u >= min && u <= max);
    return (u > min && u < max);
}

void StoreDoubleFromNMEAstring(double& dStoreHere, const string &sStoreThis)
{
    if (sStoreThis.empty())
        dStoreHere = BLANK_DOUBLE;
    else
        ParseDouble(dStoreHere, sStoreThis);
}


void StoreUShortFromNMEAstring(unsigned short& uStoreHere, const std::string &sStoreThis)
{
    if (sStoreThis.empty())
        uStoreHere = BLANK_USHORT;
    else
        ParseUShort(uStoreHere, sStoreThis);
}

void StoreIntFromNMEAstring(int&iStoreHere, const std::string &sStoreThis)
{
    if (sStoreThis.empty())
        iStoreHere = BLANK_USHORT;
    else
        ParseInt(iStoreHere, sStoreThis);
}

void StoreCharFromNMEAstring(char& cStoreHere, const std::string &sStoreThis)
{
    if (sStoreThis.empty())
        cStoreHere = BLANK_CHAR;
    else
        ParseChar(cStoreHere, sStoreThis);
}


string CharToString(char c)
{
    if (c == BLANK_CHAR || c == BAD_CHAR)
        return "";
    string strRet = "";
    strRet += c;
    return strRet;
}

string SingleDigitToString(unsigned short n)
{
    return SingleDigitToString((int) n);
}

string SingleDigitToString(int n)
{
    n = abs(n);
    char c = '0' + (n % 10);
    return CharToString(c);
}

string TwoDigitsToString(unsigned short n)
{
    return TwoDigitsToString((int) n);
}

string TwoDigitsToString(int n)
{
    if (n == BLANK_USHORT || n == BAD_USHORT)
        return "";
    n = abs(n);
    char ones = '0' + (n % 10);
    n = n / 10;
    char tens = '0' + (n % 10);
    string strRet = "";
    strRet += tens;
    strRet += ones;
    return strRet;
}

// BoundedDoubleToString()
//      Constrains dVal between upper and lower, inclusive
string BoundedDoubleToString(double lower, double upper, unsigned short decimalPlaces, double dVal)
{
    if (dVal == BLANK_DOUBLE || dVal == BAD_DOUBLE)
        return "";
    if (dVal < lower)
        dVal = lower;
    if (dVal > upper)
        dVal = upper;
    return doubleToString(dVal, decimalPlaces);
}

string FormatHDOP(double hdop)
{
    if (hdop > 99.9)
        hdop = 99.9;
    if (hdop < 0.5)
        hdop = 0.5;
    int hdopInt = hdop * 10;
    char tenths = '0' + (hdopInt % 10);
    hdopInt /= 10;
    char ones = '0' + (hdopInt % 10);
    hdopInt /= 10;
    char tens = '0' + (hdopInt % 10);
    string hdopStr = "";
    if (tens != '0')
        hdopStr += tens;
    hdopStr += ones;
    hdopStr += '.';
    hdopStr += tenths;
    return hdopStr;
}

// FormatAltMSL()
//      Alt above MSL -9999.9 to 999999.9 meters
string FormatAltMSL(double altMSL)
{
    return BoundedDoubleToString(-9999.9, 999999.9, 1, altMSL);
}

// FormatAltGeoid()
//      Geoid separation -999.9 to 9999.9 meters
string FormatAltGeoid(double altGeoid)
{
    return BoundedDoubleToString(-999.9, 9999.9, 1, altGeoid);
}

// FormatPosDouble()
//      leftDigits    Number of digits to the left of the decimal point
//      rightDigits   Number of digits to the right of the decimal point
//      Returns a string formatted with this number of digits
//      Returns null string if:
//          - Negative values
string FormatPosDouble(double dVal, unsigned short leftDigits, unsigned short rightDigits)
{
    string retVal = "";
    if (dVal < 0.0)
        return retVal;
    ostringstream doubleToString;
    int width = leftDigits + rightDigits + 1;
    doubleToString << fixed << setprecision(rightDigits) << setw(width) << setfill('0') << dVal;
    retVal = doubleToString.str().c_str();
    return retVal;
}

// FormatDouble()
//      leftDigits    Number of digits to the left of the decimal point
//      rightDigits   Number of digits to the right of the decimal point
//      Returns a string formatted with this number of digits
string FormatDouble(double dVal, unsigned short leftDigits, unsigned short rightDigits)
{
    ostringstream doubleToString;
    int width = leftDigits + rightDigits + 1;
    doubleToString << fixed << setprecision(rightDigits) << setw(width) << setfill('0') << dVal;
    string retVal = doubleToString.str().c_str();
    return retVal;
}

// ChecksumCalc()
//      sentence        Checksum will be created from this string
string ChecksumCalc(string sentence)
{
    string ck = "00";
    if (sentence.empty())
        return ck;

    unsigned char xCheckSum = 0;
    biteString(sentence,'$');
    string sToCheck = biteString(sentence,'*');
    string sRxCheckSum = sentence;
    string::iterator p;
    for (p = sToCheck.begin(); p != sToCheck.end(); p++)
        xCheckSum ^= *p;
    ostringstream os;
    os.flags(ios::hex);
    os << (int) xCheckSum;
    ck = toupper(os.str());
    if (ck.length() < 2)
        ck = "0" + ck;
    return ck;
}

// IsUInt()
//      str         Input number as string
//      minLen      minimum .length() number of characters the string can be
//      maxLen      max .length() number of characters the string can be
bool IsUInt(std::string str, unsigned minLen, unsigned maxLen)
{
    // Empty strings are not unsigned integers
    if (str.empty())
        return false;

    // Entire string must be a number, no whitespace allowed
    if (!isNumber(str, false))
        return false;

    // Unsigned integers do not have a negative sign
    if (str.find('-') != string::npos)
        return false;

    // Must fit within the proper length
    unsigned len = str.length();
    if (len < minLen || len > maxLen)
        return false;

    // No decimal point allowed
    if (str.find('.') != string::npos)
        return false;

    return true;
}

// IsFormattedDouble()
//      str         Input string representing a double
//      minLen      minimum .length() number of characters the string can be
//      maxLen      max .length() number of characters the string can be
//      decPoint    Zero-based index position where the decimal point should be (< 0 if not to check)
bool IsFormattedDouble(string str, unsigned minLen, unsigned maxLen, short decPoint)
{
    // Empty strings are not double
    if (str.empty())
        return false;

    // Entire string must be a number, no whitespace allowed
    if (!isNumber(str, false))
        return false;

    // Must fit within the proper length
    unsigned len = str.length();
    if (len < minLen || len > maxLen)
        return false;

    // Decimal point in expected location
    //      - Do we need to check for dec point?
    //      - Expected position must be within valid length
    //      - Dec point must be at exact position
    if (decPoint < 0)
        return true;
    if ((unsigned) decPoint >= len)
        return false;
    if (str.at(decPoint) != '.')
        return false;

    return true;
}

// IsSimpleDouble()
//      Checks if the string is an nmea-valid floating point with no unallowed characters
//      str     Input string representing a double
bool IsSimpleDouble(string str)
{
    if (str.empty())
        return false;
    string validChars = "01233456789-+.";
    for (string::iterator it = str.begin(); it != str.end(); it++) {
        if (validChars.find(*it) == string::npos)
            return false; }
    return true;
}

// IsSingleChar()
//      str                 Input string to check that we are validating only has one char in it
//      listOfValidChars    Single string containing all the chars that the char may be
bool IsSingleChar(std::string str, std::string listOfValidChars)
{
    // Input string must only contain one char
    if (str.length() == 1) {

        // That one char must be present in the list of valid characters
        char c = str.at(0);
        if (listOfValidChars.find(c) != string::npos)
            return true; }

    return false;
}

// IsGenericField()
//      str                 Input string to validate
bool IsGenericField(string str)
{
    // No space characters allowed
    if (str.find(' ') != string::npos)
        return false;
    return true;
}

bool HexInAStringToUInt(const std::string hex, unsigned int& ui)
{
  ui = 0;
  int len = hex.length();
  if (!len)
    return true;
  ui = HexCharToInt(hex.at(len - 1));             // start with the rightmost column (16th place)
  for (int i = 1; i < len; i++) {
    int val = HexCharToInt(hex.at(i - 1)) * pow(16, len - i);
    if (val == -1)
      return false;
    ui += val; }
  return true;
}

bool UIntToHexInAString(const unsigned int ui, std::string& hex, unsigned int padZeroToThisLen)
{
  hex = "";
  if (ui > 16777215)  return false;   // Can only handle numbers <= 16^6
  bool nonZero = false;
  unsigned int num = ui;
  for (int i = 5; i != 0; i--) {
    unsigned int curPow = pow(16, i);
    unsigned int calc = num / curPow;
    if (calc)     nonZero = true;
    if (nonZero) {
      char c = IntToHexChar(calc);
      if (c == 'X')
        return false;
      hex += c; }
    num -= (calc * curPow); }
  char c = IntToHexChar(num);
  if (c == 'X')
    return false;
  hex += c;
  while (hex.length() < padZeroToThisLen)
    hex = '0' + hex;
  return true;
}

// Input:   0123456789ABCDEF or abcdef
// Output:  0 to 15
// Invalid input char will return -1
int HexCharToInt(char h)
{
  if (h >= '0' && h <= '9')
    return (int) (h - '0');
  if (h >= 'A' && h <= 'F')
    return (int) (h - 'A' + 10);
  if (h >= 'a' && h <= 'f')
    return (int) (h - 'a' + 10);
  return -1;
}

// Input: 0 to 15
// Output: 0123456789ABCDEF
// input < 0 or > 15 will return 'X'
char IntToHexChar(int i)
{
  if (i >= 0 && i < 10)
    return (char) (i + '0');
  if (i < 16)
    return (char) (i - 10 + 'A');
  return 'X';
}

























/* ATTIC


// FormatGeog()
//      INPUT:  geogVal struct with doubleDD populated
//      OUTPUT: fully populated struct
//      - Converts a double into an NMEA formatted lat or lon with hemisphere character
//      - gVal.doubleDD is a double that is in the proper range for lat or lon
//      - isLat is a bool; true if lat, false if lon
bool FormatGeog(geogVal& gVal, bool isLat)
{
    bool bValid = (isLat ? IsValidLat(gVal) : IsValidLon(gVal));
    if (bValid) {
        double d = gVal.doubleDD;
        if (d < 0) {
            gVal.hemisphere = (isLat ? 'S' : 'E');
            d *= -1.0; }
        else {
            gVal.hemisphere = (isLat ? 'N' : 'W'); }

        // Format the value string
        //      - Always positive, no negative or positive signs
        //      - Format: ddmm.mmmmmm where d=degrees and m=minutes
        //      - Must have leading zeros
        //      - Decimal Degrees has the minutes and seconds rolled up into a fraction of a degree
        //      - Need to convert the fraction of the degree into minutes only
        //          - Isolate the fractional part (right side of the decimal point)
        //          - Multiply by 60 to get the number of degrees
        int intVal = (int) d;
        double degFraction = d - (double) intVal;
        double mins = degFraction * 60.0;
        double dddmm = (intVal * 100.0) + mins;

        int width = (isLat ? 11 : 12);
        ostringstream doubleToString;
        doubleToString << fixed << setprecision(6) << setw(width) << setfill('0') << dddmm;
        gVal.strValue = doubleToString.str();
        gVal.bValid = true; }
    return bValid;
}


// FormatLatStr()
//      - Converts a double latitude value into an NMEA formatted string and hemisphere char
bool FormatLatStr(geogVal lat)
{
    return FormatGeog(lat, true);
}

// FormatLonStr()
//      - Converts a double longitude value into an NMEA formatted string and hemisphere char
bool FormatLonStr(geogVal lon)
{
    return FormatGeog(lon, false);
}


// FormatMagVar()
//      INPUT:  geogVal struct with doubleDD populated, holding the magvar value
//      OUTPUT: fully populated struct
//      - Converts a double into a string representation of a double
//      - mag.doubleDD is a double that is in the proper range for a magnetic offset
//      - Valid MAGVAR values are -360.0 to 360.0
//      - Negative values mean SUBTRACT this value from the magnetic compass heading
bool FormatMagVar(geogVal mag)
{
    double d = mag.doubleDD;
    bool bValid = IsValidBoundedDouble(d, 360.0, -360.0);
    if (bValid) {
        mag.hemisphere = (d < 0 ? 'W' : 'E');
        d *= (d < 0 ? -1.0 : 1.0);
        mag.strValue = doubleToString(d, 1);
        mag.bValid = true; }
     return bValid;
}



// IsValidLat
//      Checks contents of a geogVal struct for a valid latitude
//      Only checks the DD value
//      Does not store results in struct, only reports
bool IsValidLat(const geogVal& lat)
{
    return IsValidLatValue(lat.doubleDD);
}

// IsValidLon
//      Checks contents of a geogVal struct for a valid longitude
//      Only checks the DD value
//      Does not store results in struct, only reports
bool IsValidLon(const geogVal& lon)
{
    return IsValidLonValue(lon.doubleDD);
}









 */













//
