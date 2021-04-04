/*
 * WHOIuModemUtils.cpp
 *
 *  Created on: May 29, 2015
 *      Author: Alon Yaari
 */

#include "NMEAdefs.h"
#include "WHOIuModemUtils.h"


unsigned char uModemAddressFromString(std::string sVal)
{
  sVal = removeWhite(sVal);
  int len = sVal.length();
  if (len == 1 || len == 2) {
    unsigned char uc = (unsigned char) (sVal.at(0) - '0');
    if (uc < 10) {
      if (len == 1)
        return uc;
      unsigned char uc1 = (unsigned char) (sVal.at(1) - '0');
      if (uc1 < 10)
        return 10u * uc + uc1; } }
  return BAD_UCHAR;
}


























//
