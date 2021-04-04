/*
 * NMEAparsing.h
 *
 *  Created on: Feb 3, 2014
 *      Author: Alon Yaari
 */

#ifndef NMEAPARSING_H_
#define NMEAPARSING_H_

#include "NMEAbase.h"
#include <stddef.h>

// Convenience functions for parsing NMEA elements
bool            ParseInt(int& i, std::string strI);
bool            ParseUInt(unsigned int& uI, std::string strUI);
bool            ParseUShort(unsigned short& uI, std::string strUI);
bool            ParseIntBounded(int& i, std::string strI, int min, int max);
bool            ParseUIntBounded(unsigned int& uI, std::string strUI, unsigned int min, unsigned int max);
bool            ParseDouble(double& dbl, std::string strDbl);
bool            ParseDoubleBounded(double& dbl, std::string strDbl, double min, double max);
bool            ParseHeading(double& heading, std::string headingStr);
bool            ParseSatNum(unsigned short& satNum, std::string satNumStr);
bool            ParseHDOP(double& hdop, std::string hdopStr);
bool            ParseAlt(double& alt, std::string altStr, std::string unitsStr = "M");
void            ParseChar(char& c, std::string cStr);
bool            ParsePlusMinus100(double& dVal, std::string dValString);
double          GPSnmeaDDDMMtoDecDeg(const double dddmmm);

#endif /* NMEAPARSING_H_ */
