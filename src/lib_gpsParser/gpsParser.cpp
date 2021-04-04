/*
 * gpsParser.cpp
 *
 *  Created on: Oct 28, 2014
 *      Author: Alon Yaari
 */

#include "NMEAbase.h"
#include "gpsParser.h"
#include "GPGGAnmea.h"
#include "GPRMCnmea.h"

using namespace std;

gpsParser::gpsParser(CMOOSGeodesy* geo, string triggerKey="GPGGA", bool reportUnhandledNMEA = false)
{
    m_geo                   = geo;
    m_bReportUnhandledNMEA  = reportUnhandledNMEA;
    m_triggerKey            = triggerKey;
    m_readyCount            = 0;
    m_pub_raw               = false;
    m_pub_hdop              = false;
    m_last_quality          = "";
    m_last_sat              = BAD_DOUBLE;
    m_last_magvar           = BAD_DOUBLE;
    m_last_nmea_text        = "";
    m_totalBadSentences     = 0;
    m_totalUnhandled        = 0;
    m_totalGPGGA            = 0;
    m_totalGPRMC            = 0;
    m_headingOffset         = 0.0;
    m_errorStrings.clear();
}

vector<gpsValueToPublish> gpsParser::GetDataToPublish()
{
  vector<gpsValueToPublish> toPub;
  std::map<std::string, gpsValueToPublish>::iterator it = m_publishQ.begin();
    for (;it != m_publishQ.end(); it++) {
      if (it->second.m_status == STATUS_READY) {
        gpsValueToPublish gVal = it->second;
        toPub.push_back(gVal);                // Add the record to the vector being sent out
        gVal.m_status = STATUS_SENT;
        m_publishQ[gVal.m_key] = gVal; } }    // Reset the value in the map to show it's no longer ready to send
  return toPub;
}

string gpsParser::GetNextErrorString()
{
    if (m_errorStrings.size() > 0) {
        string err = m_errorStrings.at(0);
        m_errorStrings.pop_front();
        return err; }
    return "";
}

// NMEASentenceIngest()
//      Parse an NMEA sentence, add extracted values to the publish queue
//      FALSE on error, call LastError() to retrieve error text
//      TRUE on properly parsed NMEA sentence
bool gpsParser::NMEASentenceIngest(string nmea)
{
  cout << "+++ gpsParser nmea in: " << nmea << endl;

  if (nmea.empty()) {
    string err = "Cannot parse empty NMEA_MSG.";
    m_errorStrings.push_back(err);
    return false; }
  if (m_pub_raw)
    AddToPublishQueue(nmea, "NMEA_FROM_GPS");

  bool bProcessedOK = false;
  string key = NMEAbase::GetKeyFromSentence(nmea);
  if      (MOOSStrCmp(key, "GPGGA"))  bProcessedOK = HandleGPGGA(nmea);
  else if (MOOSStrCmp(key, "GPRMC"))  bProcessedOK = HandleGPRMC(nmea);
  else {
    m_totalUnhandled++;
    if (m_bReportUnhandledNMEA) {
      AddToPublishQueue((double) m_totalUnhandled, "#UNHANDLED");
      string err = "Unparsed NMEA_MSG: ";
      err       += nmea;
      m_errorStrings.push_back(err); }
    else
      // Unparsed NMEA is not an error when flag set to not report as errors
      bProcessedOK = true; }

  cout << "+++ KEY : " << key << endl;
  CheckIfTriggered(key);

  if (!bProcessedOK) {
    m_totalBadSentences++;
    AddToPublishQueue((double) m_totalBadSentences, "#BAD_SENTENCES");
    return false; }
  return true;
}

// ManageTriggering()
//      Check if the most recent NMEA sentence key is the publish trigger
//      Triggered when:
//          - No defined trigger
//          - Defined trigger = nmeaKey
//      When triggered:
//          - queue items move from VALID to READY
void gpsParser::CheckIfTriggered(string nmeaKey)
{
  cout << "+++ Trigger check... " << m_triggerKey << " to [" << nmeaKey << "]" << endl;
  bool bNotBlank = !m_triggerKey.empty();
  if (bNotBlank) {
    bool bNotSame   = !MOOSStrCmp(nmeaKey, m_triggerKey);
    if (bNotSame) return; }

    cout << "+++ TRIGGERED" << endl;

  m_readyCount = 0;
  std::map<std::string, gpsValueToPublish>::iterator it = m_publishQ.begin();
  for (;it != m_publishQ.end(); it++) {
    gpsValueToPublish gVal = it->second;
    if (gVal.m_status == STATUS_VALID)
      gVal.m_status = STATUS_READY;
    if (gVal.m_status == STATUS_READY) {
      m_readyCount++;
      m_publishQ[gVal.m_key] = gVal; } }
}

//     $GPGGA,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>,<9>,M,<10>,M,<11>,<12>*hh<CR><LF>
//     <1>  UTC time, format hhmmss.s
//     <2>  Lat, format ddmm.mmmmm (with leading 0s)
//     <3>  Lat hemisphere, N(+) or S(-)
//     <4>  Lon, format dddmm.mmmmm (with leading 0s)
//     <5>  Lon hemisphere, E(+) or W(-)
//     <6>  GPS Quality, 0=No fix, 1=Non-diff, 2=Diff, 6=estimated
//     <7>  Number of Satellites
//     <8>  HDOP, 0.5 to 99.9
//     <9>  Alt above MSL -9999.99 to 999999.9 meters
//     <10> Alt MSL units, M=meters
//     <11> Geoid separation -999.9 to 9999.9 meters
//     <12> Geoid separation units, M=meters
//     <13> Age of differential correction, <blank> when not using differential
//     <14> Differential station ID, 0000 when not using differential
bool gpsParser::HandleGPGGA(string nmea)
{
    GPGGAnmea gpgga;
    if (!gpgga.ParseSentenceIntoData(nmea, false)) {
        string err = "Could not parse GPGGA message: ";
        err        += nmea;
        err        += "   ";
        err        += gpgga.GetErrorString();
        m_errorStrings.push_back(err);
        m_totalBadSentences++;
        AddToPublishQueue((double) m_totalBadSentences, "#BAD_SENTENCES");
        return false; }
    double curLat = BAD_DOUBLE;
    double curLon = BAD_DOUBLE;
    if (!gpgga.Get_latlonValues(curLat, curLon)) {
        string err = "GPGGA message contains invalid lat/lon: ";
        err       += nmea;
        m_errorStrings.push_back(err);
        m_totalBadSentences++;
        AddToPublishQueue((double) m_totalBadSentences, "#BAD_SENTENCES");
        return false; }
    AddToPublishQueue(curLat, "LATITUDE");
    AddToPublishQueue(curLon, "LONGITUDE");
    double curX = BAD_DOUBLE;
    double curY = BAD_DOUBLE;
    bool bGeoSuccess = m_geo->LatLong2LocalUTM(curLat, curLon, curY, curX);
    if (bGeoSuccess) {
        AddToPublishQueue(curX,       "X");
        AddToPublishQueue(curY,       "Y"); }
    utcTime utcTimeFromSentence;
    if (gpgga.Get_timeUTC(utcTimeFromSentence)) {
        string strtimeUTC;
        if (utcTimeFromSentence.Get_utcTimeString(strtimeUTC))
            AddToPublishQueue(strtimeUTC, "UTC_TIME"); }
    char q;
    if (gpgga.Get_gpsQual(q)) {
        string curQuality = "X";
        curQuality.at(0) = q;
        AddToPublishQueue(curQuality, "QUALITY"); }
    unsigned short u;
    if (gpgga.Get_satNum(u))
        AddToPublishQueue((double) u, "SAT");
    double h;
    if (gpgga.Get_hdop(h))
        AddToPublishQueue(h, "HDOP");
    m_totalGPGGA++;
    return AddToPublishQueue((double) m_totalGPGGA, "#GPGGA");
}

//     $GPRMC,<1>,<2>,<3>,<4>,<5>,<6>,<7>,<8>,<9>,<10>,<11>,<12>*hh<CR><LF>
//     <1>  UTC time, format hhmmss.s
//     <2>  Status, A=Valid, V=Receiver warning
//     <3>  Lat, format ddmm.mmmmm (with leading 0s)
//     <4>  Lat hemisphere, N(+) or S(-)
//     <5>  Lon, format dddmm.mmmmm (with leading 0s)
//     <6>  Lon hemisphere, E(+) or W(-)
//     <7>  Speed over ground in KNOTS, format 000.00 (with leading 0s)
//     <8>  Course over ground in deg from true North, format ddd.d (with leading 0s)
//     <9>  UTC date, format ddmmyy
//     <10> Magnetic variation true North, format ddd.d (with leading 0s)
//     <11> Magnetic variation direction, E(-) or W(+)
//     <12> Mode indicator, A=Autonomous, D=Differential, E=Estimated, N=bad
bool gpsParser::HandleGPRMC(string nmea)
{
    GPRMCnmea gprmc;
    if (!gprmc.ParseSentenceIntoData(nmea, false)) {
        string err = "Could not parse GPRMC message: ";
        err       += nmea;
        err       += "  ";
        err       += gprmc.GetErrorString();
        m_errorStrings.push_back(err);
        m_totalBadSentences++;
        AddToPublishQueue((double) m_totalBadSentences, "#BAD_SENTENCES");
        return false; }
    double curLat = BAD_DOUBLE;
    double curLon = BAD_DOUBLE;
    if (!gprmc.Get_latlonValues(curLat, curLon)) {
        string err = "GPGGA message contains invalid lat/lon: ";
        err       += nmea;
        m_errorStrings.push_back(err);
        m_totalBadSentences++;
        AddToPublishQueue((double) m_totalBadSentences, "#BAD_SENTENCES");
        return false; }
    AddToPublishQueue(curLat, "LATITUDE");
    AddToPublishQueue(curLon, "LONGITUDE");
    double curX = BAD_DOUBLE;
    double curY = BAD_DOUBLE;
    bool bGeoSuccess = m_geo->LatLong2LocalUTM(curLat, curLon, curY, curX);
    if (bGeoSuccess) {
        AddToPublishQueue(curX, "X");
        AddToPublishQueue(curY, "Y"); }
    utcTime utcTimeFromSentence;
    if (gprmc.Get_timeUTC(utcTimeFromSentence)) {
        string strtimeUTC;
        if (utcTimeFromSentence.Get_utcTimeString(strtimeUTC))
            AddToPublishQueue(strtimeUTC, "UTC_TIME"); }
    double curSpeed = BAD_DOUBLE;
    if (gprmc.Get_speedMPS(curSpeed))
        AddToPublishQueue(curSpeed, "SPEED");
    double curHeading;
    if (gprmc.Get_headingTrueN(curHeading)) {
        curHeading += m_headingOffset;
        AddToPublishQueue(curHeading, "HEADING_GPRMC"); }
    double curMagVar = BAD_DOUBLE;
    if (gprmc.Get_magVar(curMagVar))
        AddToPublishQueue(curMagVar, "MAGVAR");
    utcDate utcDateFromSentence;
    if (gprmc.Get_dateUTC(utcDateFromSentence)) {
        string strdateUTC;
        if (utcDateFromSentence.Get_utcDateString(strdateUTC))
            AddToPublishQueue(strdateUTC, "UTC_DATE"); }
            //strtod(strdateUTC.c_str(), 0)
    m_totalGPRMC++;
    return AddToPublishQueue((double) m_totalGPRMC, "#GPRMC");
}

// AddToPublishQueue()
//        Pushes a new record with a double value onto the outgoing map for publishing MOOS messages
bool gpsParser::AddToPublishQueue(double dVal, string sMsgName)
{
    gpsValueToPublish toPub(true, dVal, "", sMsgName);
    return AddToPublishQueue(toPub);
}

// AddToPublishQueue()
//        Pushes a new record with a string value onto the outgoing map for publishing MOOS messages
bool gpsParser::AddToPublishQueue(string sVal, string sMsgName)
{
    gpsValueToPublish toPub(false, BAD_DOUBLE, sVal, sMsgName);
    return AddToPublishQueue(toPub);
}

// AddToPublishQueue()
//        Checks conditions before actually adding the value
//        Newly-added items get STATUS_NEW
bool gpsParser::AddToPublishQueue(gpsValueToPublish gVal)
{
  string key = toupper(gVal.m_key);
  gVal.m_key = key;

  gpsValueToPublish queueVal;
  std::map<std::string, gpsValueToPublish>::iterator it = m_publishQ.find(key);
  if (it != m_publishQ.end())
    queueVal = it->second;

  // Four situations for publishing:
  //  1. Ready        - Item already in queue with STATUS_READY
  //  2. Conditional  - Only published if user sets flag to publish it
  //  3. Always       - Value always gets published
  //  4. On Change    - Value only gets published if different than last publish
  bool bGoodToAdd = false;

  // 1. Already has STATUS_READY
  if      (queueVal.m_status == STATUS_READY)              bGoodToAdd = true;

  // 2. Conditional publications
  else if (key == "UNHANDLED" && !m_bReportUnhandledNMEA)  bGoodToAdd = true;
  else if (key == "HDOP"      && !m_pub_hdop)              bGoodToAdd = true;

  // 3. Values that are always published
  else if (key == "LATITUDE")                              bGoodToAdd = true;
  else if (key == "LONGITUDE")                             bGoodToAdd = true;
  else if (key == "X")                                     bGoodToAdd = true;
  else if (key == "Y")                                     bGoodToAdd = true;
  else if (key == "SPEED")                                 bGoodToAdd = true;
  else if (key == "HEADING_GPRMC")                         bGoodToAdd = true;
  else if (key == "UTC_TIME")                              bGoodToAdd = true;
  else if (key == "UTC_DATE")                              bGoodToAdd = true;

  // 4. Publish only when value changes (or it was never previously published)
  else {
    bGoodToAdd = (it == m_publishQ.end() || queueVal != gVal); }

  // Update the values in the map when:
  //    double value is not BAD_DOUBLE
  //    string value is not ""
  if (bGoodToAdd) {
    if (gVal.m_isDouble)
      bGoodToAdd = (gVal.m_dVal != BAD_DOUBLE);
    else
      bGoodToAdd = (!gVal.m_sVal.empty());

    if (bGoodToAdd) {
        if (queueVal.m_status == STATUS_READY)
          gVal.m_status = STATUS_READY;
        else
          gVal.m_status = STATUS_VALID;
        m_publishQ[gVal.m_key] = gVal; } }

  // LATITUDE and LONGITUDE also get published as LAT and LONG
  if (key == "LATITUDE") {
    gpsValueToPublish toPubLat(true, gVal.m_dVal, "", "LAT");
    return AddToPublishQueue(toPubLat); }
  if (key == "LONGITUDE") {
    gpsValueToPublish toPubLon(true, gVal.m_dVal, "", "LONG");
    return AddToPublishQueue(toPubLon); }
  return true;
}

bool gpsParser::SetParam_TYPE(string sVal)
{
  return true;
}

bool gpsParser::SetParam_SHOW_CEP(string sVal)
{
  return true;
}

bool gpsParser::SetParam_PREFIX(string sVal)
{
  return true;
}

bool gpsParser::SetParam_PORT(string sVal)
{
  return true;
}

bool gpsParser::SetParam_BAUDRATE(string sVal)
{
  return true;
}

bool gpsParser::SetParam_TRIGGER_MSG(string sVal)
{
  return true;
}

bool gpsParser::SetParam_PUBLISH_HEADING(string sVal)
{
  return true;
}

bool gpsParser::SetParam_REPORT_NMEA(string sVal)
{
  return true;
}

bool gpsParser::SetParam_HEADING_OFFSET(string sVal)
{
  return true;
}

