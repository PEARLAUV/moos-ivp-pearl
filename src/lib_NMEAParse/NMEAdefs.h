/*
 * NMEADefs.h
 *
 *  Created on: Feb 3, 2014
 *      Author: Alon Yaari
 */


#ifndef NMEADEFS_H_
#define NMEADEFS_H_


#define BLANK_CHAR           (char) 0x00
#define BLANK_UCHAR          (char) 0xFFu
#define BLANK_STRING         ""
#define BLANK_INT            -9999999
#define BLANK_DOUBLE         -9999999.99
#define BLANK_DOUBLE_STRING  "-9999999.99"
#define BLANK_USHORT         8888

#define BAD_CHAR             (char) 0xFF
#define BAD_UCHAR            (char) 0xFE
#define BAD_DOUBLE           9999999.99
#define BAD_DOUBLE_STRING    "9999999.99"
#define BAD_USHORT           9999
#define BAD_INT              9999999

#define MPStoKNOTS           1.94384
#define KNOTS2METERSperSEC   0.51444444
#define METERSperSEC2KNOTS   1.9438444924574

#define MAY_BE_BLANK         true
#define MAY_NOT_BE_BLANK     false
#define INCLUSIVE_YES        true
#define INCLUSIVE_NO         false
#define CASE_INSENSITIVE     true
#define CASE_SENSITIVE       false
#define ALLOW_BLANK_CHECKSUM true
#define MUST_HAVE_CHECKSUM   false

#define WithinBounds(d,max,min) ((d)>=(min)&&(d)<=(max))

#endif
