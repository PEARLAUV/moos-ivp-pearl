/************************************************************/
/*    NAME: Charles Benjamin                               */
/*    ORGN: MIT, Cambridge MA                              */
/*    FILE: Rendezvous.cpp                                 */
/*    DATE: August 2026                                    */
/************************************************************/

#include <chrono>
#include <cmath>
#include <cstdint>
#include "MBUtils.h"
#include "ACTable.h"
#include "NodeMessage.h"
#include "Rendezvous.h"

using namespace std;

//---------------------------------------------------------
// Constructor

Rendezvous::Rendezvous()
{
  m_role                = "";
  m_ownship             = "";
  m_peer_node           = "";
  m_uav_speed           = 3.0;
  m_pearl_speed         = 0.5;
  m_min_battery         = 25.0;
  m_critical_battery    = 15.0;
  m_battery_max_age     = 3.0;
  m_min_pearl_battery   = 15.0;
  m_max_landing_wind_speed = 4.0;
  m_platform_data_max_age = 3.0;
  m_nav_stale_thresh    = 10.0;
  m_request_timeout     = 20.0;
  m_route_timeout       = 5.0;
  m_transit_timeout     = 180.0;
  m_arrival_radius      = 4.0;
  m_arrival_dwell       = 2.0;
  m_max_peer_separation = 3.0;
  m_landing_target_max_age = 0.5;
  m_landing_target_lock_dwell = 2.0;
  m_landing_target_acquire_timeout = 30.0;
  m_landing_target_max_offset = 1.5;
  m_landing_target_max_angle = 0.20;
  m_reacquire_update_interval = 2.0;
  m_reacquire_update_distance = 1.0;
  m_recovery_route_speed = 1.5;
  m_recovery_capture_radius = 0.5;
  m_secondary_landing_x = 10.0;
  m_secondary_landing_y = 15.0;
  m_secondary_arrival_radius = 2.0;
  m_secondary_arrival_dwell = 2.0;
  m_secondary_landing_delay = 3.0;
  m_expected_target_num = 0;
  m_state_post_interval = 1.0;
  m_require_health      = true;
  m_require_battery     = false;
  m_require_flight_state = true;
  m_require_landing_target = true;
  m_require_platform_ready = false;
  m_require_platform_health = true;
  m_secondary_landing_enabled = false;

  m_state                = "IDLE";
  m_reason               = "startup";
  m_remote_uav_phase     = "IDLE";
  m_remote_uav_session   = "";
  m_state_enter_time     = 0.0;
  m_last_state_post_time = 0.0;
  m_route_command_time   = 0.0;
  m_route_state_time     = 0.0;
  m_nav_x                = 0.0;
  m_nav_y                = 0.0;
  m_nav_x_time           = 0.0;
  m_nav_y_time           = 0.0;
  m_battery              = 0.0;
  m_battery_time         = 0.0;
  m_battery_valid_time   = 0.0;
  m_pearl_battery        = 0.0;
  m_pearl_battery_time   = 0.0;
  m_pearl_battery_valid_time = 0.0;
  m_pearl_wind_speed     = 0.0;
  m_pearl_wind_speed_time = 0.0;
  m_pearl_wind_valid_time = 0.0;
  m_platform_health_time = 0.0;
  m_target_x             = 0.0;
  m_target_y             = 0.0;
  m_peer_report_x        = 0.0;
  m_peer_report_y        = 0.0;
  m_peer_report_time     = 0.0;
  m_acquisition_start_time = 0.0;
  m_target_lock_start_time = 0.0;
  m_last_reacquire_route_time = 0.0;
  m_last_reacquire_x     = 0.0;
  m_last_reacquire_y     = 0.0;
  m_landing_target_available_time = 0.0;
  m_landing_target_age   = 0.0;
  m_landing_target_age_time = 0.0;
  m_landing_target_num   = 0.0;
  m_landing_target_num_time = 0.0;
  m_landing_target_position_valid = 0.0;
  m_landing_target_position_valid_time = 0.0;
  m_landing_target_x     = 0.0;
  m_landing_target_y     = 0.0;
  m_landing_target_position_time = 0.0;
  m_landing_target_angle_x = 0.0;
  m_landing_target_angle_y = 0.0;
  m_landing_target_angle_time = 0.0;
  m_pearl_activation_time = 0.0;
  m_arrival_start_time   = 0.0;
  m_secondary_state_enter_time = 0.0;
  m_secondary_arrival_start_time = 0.0;
  m_secondary_recovery_state = "IDLE";
  m_nav_x_set            = false;
  m_nav_y_set            = false;
  m_battery_set          = false;
  m_battery_valid        = false;
  m_battery_valid_set    = false;
  m_pearl_battery_set    = false;
  m_pearl_battery_valid  = false;
  m_pearl_battery_valid_set = false;
  m_pearl_wind_speed_set = false;
  m_pearl_wind_valid     = false;
  m_pearl_wind_valid_set = false;
  m_platform_health_ok   = false;
  m_platform_health_set  = false;
  m_health_ok            = false;
  m_health_set           = false;
  m_uav_armed            = false;
  m_uav_armed_set        = false;
  m_uav_in_air           = false;
  m_uav_landed_set       = false;
  m_target_set           = false;
  m_pearl_activation_pending = false;
  m_pearl_arrived        = false;
  m_uav_arrived          = false;
  m_pearl_deploy         = false;
  m_pearl_return         = false;
  m_pearl_station_keep   = false;
  m_pearl_manual_override = true;
  m_pearl_deploy_set     = false;
  m_pearl_return_set     = false;
  m_pearl_station_keep_set = false;
  m_pearl_manual_override_set = false;
  m_pearl_control_saved  = false;
  m_saved_pearl_deploy   = false;
  m_saved_pearl_return   = false;
  m_saved_pearl_station_keep = false;
  m_saved_pearl_manual_override = true;
  m_clearance_received   = false;
  m_landing_target_available = false;
  m_landing_target_gate_ready = false;
  m_completion_sent      = false;
  m_config_valid         = true;
  m_requests_sent        = 0;
  m_proposals_sent       = 0;
  m_acceptances_sent     = 0;
  m_clearances_sent      = 0;
  m_aborts               = 0;
}

Rendezvous::~Rendezvous()
{
}

//---------------------------------------------------------
// Procedure: OnNewMail

bool Rendezvous::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  for(MOOSMSG_LIST::iterator p=NewMail.begin(); p!=NewMail.end(); ++p) {
    CMOOSMsg &msg = *p;
    string key = msg.GetKey();
    bool handled = false;

    map<string, RequiredCheck>::iterator check = m_required_checks.find(key);
    if(check != m_required_checks.end()) {
      if(msg.IsString()) {
        check->second.result = tolower(stripBlankEnds(msg.GetString()));
        handled = true;
      }
    }
    else if(strEnds(key, "_STATE") &&
            ((check = m_required_checks.find(key.substr(0, key.size() - 6))) !=
             m_required_checks.end())) {
      if(msg.IsString()) {
        check->second.state_result = tolower(tokStringParse(
          msg.GetString(), "status", ',', '='));
        check->second.session = tokStringParse(
          msg.GetString(), "session", ',', '=');
        handled = true;
      }
    }
    else if((m_role == "uav") && (key == "RENDEZVOUS_START"))
      handled = handleMailStart(msg);
    else if(key == "RENDEZVOUS_ABORT")
      handled = handleMailAbort(msg);
    else if((m_role == "pearl") && (key == "RENDEZVOUS_REQUEST"))
      handled = handleMailRequest(msg);
    else if((m_role == "uav") && (key == "RENDEZVOUS_PROPOSAL"))
      handled = handleMailProposal(msg);
    else if(key == "RENDEZVOUS_RESPONSE")
      handled = handleMailResponse(msg);
    else if((m_role == "uav") && (key == "LANDING_CLEARANCE"))
      handled = handleMailClearance(msg);
    else if(key == "NODE_REPORT")
      handled = handleMailNodeReport(msg);
    else if((m_role == "uav") && (key == "UAV_PREC_LAND_REQUEST"))
      handled = handleMailPrecisionLandRequest(msg);
    else if(key == "NAV_X")
      handled = handleMailDouble(msg, m_nav_x, m_nav_x_time) &&
                (m_nav_x_set = true);
    else if(key == "NAV_Y")
      handled = handleMailDouble(msg, m_nav_y, m_nav_y_time) &&
                (m_nav_y_set = true);
    else if((m_role == "uav") && (key == "UAV_BATTERY_SOC"))
      handled = handleMailDouble(msg, m_battery, m_battery_time) &&
                (m_battery_set = true);
    else if((m_role == "uav") && (key == "UAV_BATTERY_DATA_VALID"))
      handled = handleMailTimedBool(msg, m_battery_valid,
                                    m_battery_valid_set,
                                    m_battery_valid_time);
    else if((m_role == "uav") && (key == "UAV_HEALTH_ALL_OK"))
      handled = handleMailBool(msg, m_health_ok, m_health_set);
    else if((m_role == "uav") && (key == "UAV_IS_ARMED"))
      handled = handleMailBool(msg, m_uav_armed, m_uav_armed_set);
    else if((m_role == "uav") && (key == "UAV_LANDED_STATE")) {
      if(msg.IsString()) {
        string value = toupper(stripBlankEnds(msg.GetString()));
        m_uav_in_air = ((value == "IN_AIR") || (value == "TAKING_OFF") ||
                        (value == "LANDING"));
        m_uav_landed_set = true;
        handled = true;
      }
    }
    else if(key == "PEARL_BATTERY_SOC")
      handled = handleMailDouble(msg, m_pearl_battery,
                                 m_pearl_battery_time) &&
                (m_pearl_battery_set = true);
    else if(key == "PEARL_BATTERY_DATA_VALID")
      handled = handleMailTimedBool(msg, m_pearl_battery_valid,
                                    m_pearl_battery_valid_set,
                                    m_pearl_battery_valid_time);
    else if(key == "PEARL_WIND_SPEED")
      handled = handleMailDouble(msg, m_pearl_wind_speed,
                                 m_pearl_wind_speed_time) &&
                (m_pearl_wind_speed_set = true);
    else if(key == "PEARL_WIND_DATA_VALID")
      handled = handleMailTimedBool(msg, m_pearl_wind_valid,
                                    m_pearl_wind_valid_set,
                                    m_pearl_wind_valid_time);
    else if(((m_role == "uav") && (key == "PEARL_PROC_WATCH_ALL_OK")) ||
            ((m_role == "pearl") && (key == "PROC_WATCH_ALL_OK")))
      handled = handleMailTimedBool(msg, m_platform_health_ok,
                                    m_platform_health_set,
                                    m_platform_health_time);
    else if((m_role == "uav") &&
            (key == "UAV_LANDING_TARGET_AVAILABLE")) {
      m_landing_target_available = mailIsTrue(msg);
      m_landing_target_available_time = m_curr_time;
      handled = true;
    }
    else if((m_role == "uav") && (key == "UAV_LANDING_TARGET_AGE"))
      handled = handleMailDouble(msg, m_landing_target_age,
                                 m_landing_target_age_time);
    else if((m_role == "uav") &&
            (key == "UAV_LANDING_TARGET_TARGET_NUM"))
      handled = handleMailDouble(msg, m_landing_target_num,
                                 m_landing_target_num_time);
    else if((m_role == "uav") &&
            (key == "UAV_LANDING_TARGET_POSITION_VALID"))
      handled = handleMailDouble(msg, m_landing_target_position_valid,
                                 m_landing_target_position_valid_time);
    else if((m_role == "uav") && (key == "UAV_LANDING_TARGET_X"))
      handled = handleMailDouble(msg, m_landing_target_x,
                                 m_landing_target_position_time);
    else if((m_role == "uav") && (key == "UAV_LANDING_TARGET_Y"))
      handled = handleMailDouble(msg, m_landing_target_y,
                                 m_landing_target_position_time);
    else if((m_role == "uav") && (key == "UAV_LANDING_TARGET_ANGLE_X"))
      handled = handleMailDouble(msg, m_landing_target_angle_x,
                                 m_landing_target_angle_time);
    else if((m_role == "uav") && (key == "UAV_LANDING_TARGET_ANGLE_Y"))
      handled = handleMailDouble(msg, m_landing_target_angle_y,
                                 m_landing_target_angle_time);
    else if((m_role == "uav") && (key == "ROUTE_BUFFER_VEHICLE_STATE")) {
      if(msg.IsString()) {
        m_route_state = toupper(stripBlankEnds(msg.GetString()));
        m_route_state_time = m_curr_time;
        handled = true;
      }
    }
    else if((m_role == "pearl") && (key == "PEARL_RENDEZVOUS_ARRIVED")) {
      m_pearl_arrived = mailIsTrue(msg);
      handled = true;
    }
    else if((m_role == "pearl") && (key == "UAV_RENDEZVOUS_PHASE") &&
            msg.IsString()) {
      m_remote_uav_phase = toupper(stripBlankEnds(msg.GetString()));
      handled = true;
    }
    else if((m_role == "pearl") && (key == "UAV_RENDEZVOUS_SESSION") &&
            msg.IsString()) {
      m_remote_uav_session = stripBlankEnds(msg.GetString());
      handled = true;
    }
    else if((m_role == "pearl") && (key == "DEPLOY"))
      handled = handleMailBool(msg, m_pearl_deploy, m_pearl_deploy_set);
    else if((m_role == "pearl") && (key == "RETURN"))
      handled = handleMailBool(msg, m_pearl_return, m_pearl_return_set);
    else if((m_role == "pearl") && (key == "STATION_KEEP"))
      handled = handleMailBool(msg, m_pearl_station_keep,
                               m_pearl_station_keep_set);
    else if((m_role == "pearl") && (key == "MOOS_MANUAL_OVERRIDE"))
      handled = handleMailBool(msg, m_pearl_manual_override,
                               m_pearl_manual_override_set);
    else if(key == "APPCAST_REQ")
      handled = true;

    if(!handled && (key != "APPCAST_REQ"))
      reportRunWarning("Unhandled Mail: " + key);
  }

  return(true);
}

//---------------------------------------------------------
// Procedure: Iterate

bool Rendezvous::Iterate()
{
  AppCastingMOOSApp::Iterate();

  if(m_state_enter_time <= 0)
    m_state_enter_time = m_curr_time;

  if(m_config_valid) {
    if(m_role == "uav")
      iterateUAV();
    else if(m_role == "pearl")
      iteratePearl();
    publishState(false);
  }

  AppCastingMOOSApp::PostReport();
  return(true);
}

bool Rendezvous::OnConnectToServer()
{
  registerVariables();
  return(true);
}

//---------------------------------------------------------
// Procedure: OnStartUp

bool Rendezvous::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();

  STRING_LIST sParams;
  m_MissionReader.EnableVerbatimQuoting(false);
  if(!m_MissionReader.GetConfiguration(GetAppName(), sParams))
    reportConfigWarning("No config block found for " + GetAppName());

  for(STRING_LIST::iterator p=sParams.begin(); p!=sParams.end(); ++p) {
    string orig  = *p;
    string line  = *p;
    string param = tolower(biteStringX(line, '='));
    string value = line;
    bool handled = false;

    if(param == "role") {
      value = tolower(stripBlankEnds(value));
      handled = ((value == "uav") || (value == "pearl"));
      if(handled)
        m_role = value;
    }
    else if(param == "ownship")
      handled = setNonWhiteVarOnString(m_ownship, value);
    else if(param == "peer_node")
      handled = setNonWhiteVarOnString(m_peer_node, value);
    else if(param == "uav_speed")
      handled = setPosDoubleOnString(m_uav_speed, value);
    else if(param == "pearl_speed")
      handled = setPosDoubleOnString(m_pearl_speed, value);
    else if(param == "min_battery")
      handled = setNonNegDoubleOnString(m_min_battery, value) &&
                (m_min_battery <= 100);
    else if(param == "critical_battery")
      handled = setNonNegDoubleOnString(m_critical_battery, value) &&
                (m_critical_battery <= 100);
    else if(param == "battery_max_age")
      handled = setPosDoubleOnString(m_battery_max_age, value);
    else if(param == "min_pearl_battery")
      handled = setNonNegDoubleOnString(m_min_pearl_battery, value) &&
                (m_min_pearl_battery <= 100);
    else if(param == "max_landing_wind_speed")
      handled = setNonNegDoubleOnString(m_max_landing_wind_speed, value);
    else if(param == "platform_data_max_age")
      handled = setPosDoubleOnString(m_platform_data_max_age, value);
    else if(param == "nav_stale_thresh")
      handled = setPosDoubleOnString(m_nav_stale_thresh, value);
    else if(param == "request_timeout")
      handled = setPosDoubleOnString(m_request_timeout, value);
    else if(param == "route_timeout")
      handled = setPosDoubleOnString(m_route_timeout, value);
    else if(param == "transit_timeout")
      handled = setPosDoubleOnString(m_transit_timeout, value);
    else if(param == "arrival_radius")
      handled = setPosDoubleOnString(m_arrival_radius, value);
    else if(param == "arrival_dwell")
      handled = setNonNegDoubleOnString(m_arrival_dwell, value);
    else if(param == "max_peer_separation")
      handled = setPosDoubleOnString(m_max_peer_separation, value);
    else if(param == "landing_target_max_age")
      handled = setPosDoubleOnString(m_landing_target_max_age, value);
    else if(param == "landing_target_lock_dwell")
      handled = setNonNegDoubleOnString(m_landing_target_lock_dwell, value);
    else if(param == "landing_target_acquire_timeout")
      handled = setPosDoubleOnString(m_landing_target_acquire_timeout, value);
    else if(param == "landing_target_max_offset")
      handled = setPosDoubleOnString(m_landing_target_max_offset, value);
    else if(param == "landing_target_max_angle")
      handled = setPosDoubleOnString(m_landing_target_max_angle, value);
    else if(param == "reacquire_update_interval")
      handled = setPosDoubleOnString(m_reacquire_update_interval, value);
    else if(param == "reacquire_update_distance")
      handled = setPosDoubleOnString(m_reacquire_update_distance, value);
    else if(param == "recovery_route_speed")
      handled = setPosDoubleOnString(m_recovery_route_speed, value);
    else if(param == "recovery_capture_radius")
      handled = setPosDoubleOnString(m_recovery_capture_radius, value);
    else if(param == "secondary_landing_x")
      handled = setDoubleOnString(m_secondary_landing_x, value);
    else if(param == "secondary_landing_y")
      handled = setDoubleOnString(m_secondary_landing_y, value);
    else if(param == "secondary_arrival_radius")
      handled = setPosDoubleOnString(m_secondary_arrival_radius, value);
    else if(param == "secondary_arrival_dwell")
      handled = setNonNegDoubleOnString(m_secondary_arrival_dwell, value);
    else if(param == "secondary_landing_delay")
      handled = setNonNegDoubleOnString(m_secondary_landing_delay, value);
    else if(param == "expected_target_num") {
      double expected = 0;
      handled = setNonNegDoubleOnString(expected, value) &&
                (expected <= 255) && (floor(expected) == expected);
      if(handled)
        m_expected_target_num = static_cast<unsigned int>(expected);
    }
    else if(param == "state_post_interval")
      handled = setPosDoubleOnString(m_state_post_interval, value);
    else if(param == "require_health")
      handled = setBooleanOnString(m_require_health, value);
    else if(param == "require_battery")
      handled = setBooleanOnString(m_require_battery, value);
    else if(param == "require_flight_state")
      handled = setBooleanOnString(m_require_flight_state, value);
    else if(param == "require_landing_target")
      handled = setBooleanOnString(m_require_landing_target, value);
    else if(param == "require_platform_ready")
      handled = setBooleanOnString(m_require_platform_ready, value);
    else if(param == "require_platform_health")
      handled = setBooleanOnString(m_require_platform_health, value);
    else if(param == "require_check") {
      string variable;
      // A result variable may itself end in _STATE (for example,
      // RECOVERY_PRECHECKS_STATE). Its diagnostic is then <VAR>_STATE,
      // yielding RECOVERY_PRECHECKS_STATE_STATE. Direct result lookup runs
      // before diagnostic-suffix lookup in OnNewMail, so this is unambiguous.
      handled = setNonWhiteVarOnString(variable, value) &&
                (m_required_checks.count(variable) == 0);
      if(handled)
        m_required_checks[variable] = RequiredCheck();
      else {
        reportConfigWarning("Invalid or duplicate require_check: " + value);
        m_config_valid = false;
        handled = true;
      }
    }
    else if(param == "secondary_landing_enabled")
      handled = setBooleanOnString(m_secondary_landing_enabled, value);

    if(!handled)
      reportUnhandledConfigWarning(orig);
  }

  if(m_ownship == "")
    m_ownship = m_host_community;

  if((m_role != "uav") && (m_role != "pearl")) {
    reportConfigWarning("role must be uav or pearl");
    m_config_valid = false;
  }
  if(m_peer_node == "") {
    reportConfigWarning("peer_node is required");
    m_config_valid = false;
  }
  if(m_critical_battery > m_min_battery) {
    reportConfigWarning("critical_battery must not exceed min_battery");
    m_config_valid = false;
  }
  if(!m_required_checks.empty() && (m_role != "pearl")) {
    reportConfigWarning("require_check is supported only in the pearl role");
    m_config_valid = false;
  }

  registerVariables();
  return(true);
}

//---------------------------------------------------------
// Procedure: registerVariables

void Rendezvous::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  Register("RENDEZVOUS_ABORT", 0);
  Register("NAV_X", 0);
  Register("NAV_Y", 0);
  Register("PEARL_BATTERY_SOC", 0);
  Register("PEARL_BATTERY_DATA_VALID", 0);
  Register("PEARL_WIND_SPEED", 0);
  Register("PEARL_WIND_DATA_VALID", 0);
  for(const auto& check : m_required_checks) {
    Register(check.first, 0);
    Register(check.first + "_STATE", 0);
  }

  if(m_role == "uav") {
    Register("RENDEZVOUS_START", 0);
    Register("RENDEZVOUS_PROPOSAL", 0);
    Register("LANDING_CLEARANCE", 0);
    Register("ROUTE_BUFFER_VEHICLE_STATE", 0);
    Register("UAV_BATTERY_SOC", 0);
    Register("UAV_BATTERY_DATA_VALID", 0);
    Register("PEARL_PROC_WATCH_ALL_OK", 0);
    Register("UAV_HEALTH_ALL_OK", 0);
    Register("UAV_IS_ARMED", 0);
    Register("UAV_LANDED_STATE", 0);
    Register("UAV_PREC_LAND_REQUEST", 0);
    Register("UAV_LANDING_TARGET_AVAILABLE", 0);
    Register("UAV_LANDING_TARGET_AGE", 0);
    Register("UAV_LANDING_TARGET_TARGET_NUM", 0);
    Register("UAV_LANDING_TARGET_POSITION_VALID", 0);
    Register("UAV_LANDING_TARGET_X", 0);
    Register("UAV_LANDING_TARGET_Y", 0);
    Register("UAV_LANDING_TARGET_ANGLE_X", 0);
    Register("UAV_LANDING_TARGET_ANGLE_Y", 0);
    Register("RENDEZVOUS_RESPONSE", 0);
    Register("NODE_REPORT", 0);
  }
  else if(m_role == "pearl") {
    Register("RENDEZVOUS_REQUEST", 0);
    Register("RENDEZVOUS_RESPONSE", 0);
    Register("PEARL_RENDEZVOUS_ARRIVED", 0);
    Register("UAV_RENDEZVOUS_PHASE", 0);
    Register("UAV_RENDEZVOUS_SESSION", 0);
    Register("DEPLOY", 0);
    Register("RETURN", 0);
    Register("STATION_KEEP", 0);
    Register("MOOS_MANUAL_OVERRIDE", 0);
    Register("PROC_WATCH_ALL_OK", 0);
    Register("NODE_REPORT", 0);
  }
}

//---------------------------------------------------------
// Mail handlers

bool Rendezvous::handleMailStart(const CMOOSMsg& msg)
{
  if(!mailIsTrue(msg))
    return(true);

  if((m_state == "REQUESTING") || (m_state == "RENDEZVOUS") ||
     (m_state == "ACQUIRING_TARGET") || (m_state == "LANDING")) {
    reportRunWarning("Rendezvous start rejected: mission already active");
    return(true);
  }

  return(beginRequest());
}

bool Rendezvous::handleMailAbort(const CMOOSMsg& msg)
{
  if(!mailIsTrue(msg))
    return(true);

  if(m_state == "LANDING") {
    reportRunWarning("Rendezvous abort ignored: precision landing committed");
    return(true);
  }

  abortMission("operator_abort");
  return(true);
}

bool Rendezvous::handleMailRequest(const CMOOSMsg& msg)
{
  if(!msg.IsString())
    return(false);

  string request = msg.GetString();
  string id = tokStringParse(request, "id", '#', '=');
  if(id == "") {
    reportRunWarning("Rejected rendezvous request without an ID");
    return(true);
  }

  if((m_state == "REQUESTING") || (m_state == "RENDEZVOUS") ||
     (m_state == "ACQUIRING_TARGET") || (m_state == "LANDING")) {
    // Network retries for the request currently waiting on checks are
    // idempotent. A different request remains fail-closed as busy.
    if((m_state == "REQUESTING") && !m_pending_request.empty() &&
       (id == m_session_id))
      return(true);
    string response = "id=" + id + "#status=rejected#reason=busy";
    sendMessage(m_peer_node, "RENDEZVOUS_RESPONSE", response);
    return(true);
  }

  if(!m_required_checks.empty()) {
    beginRequiredChecks(request, id);
    return(true);
  }
  return(proposeRendezvous(request));
}

bool Rendezvous::handleMailProposal(const CMOOSMsg& msg)
{
  if(!msg.IsString())
    return(false);

  if(m_state != "REQUESTING") {
    reportRunWarning("Ignored rendezvous proposal while not requesting");
    return(true);
  }

  return(acceptProposal(msg.GetString()));
}

bool Rendezvous::handleMailResponse(const CMOOSMsg& msg)
{
  if(!msg.IsString())
    return(false);

  string response = msg.GetString();
  string id = tokStringParse(response, "id", '#', '=');
  string status = tolower(tokStringParse(response, "status", '#', '='));
  string reason = tokStringParse(response, "reason", '#', '=');
  if((id == "") || (id != m_session_id)) {
    reportRunWarning("Ignored response for a stale rendezvous session");
    return(true);
  }

  if(m_role == "uav") {
    if((status == "rejected") || (status == "abort"))
      abortMission((reason == "") ? status : reason, false);
    else
      reportRunWarning("Ignored unexpected response status: " + status);
  }
  else if((status == "accepted") && (m_state == "REQUESTING")) {
    activatePearlTransit();
    transitionTo("RENDEZVOUS", "proposal_accepted");
  }
  else if((status == "landing") && (m_state == "ACQUIRING_TARGET"))
    transitionTo("LANDING", "uav_landing_committed");
  else if(status == "complete") {
    transitionTo("COMPLETE", "uav_landed");
    publishPoint(false);
  }
  else if((status == "rejected") || (status == "abort"))
    abortMission((reason == "") ? status : reason, false);

  return(true);
}

bool Rendezvous::handleMailClearance(const CMOOSMsg& msg)
{
  if(!msg.IsString())
    return(false);

  string clearance = msg.GetString();
  string id = tokStringParse(clearance, "id", '#', '=');
  string status = tolower(tokStringParse(clearance, "status", '#', '='));
  string sx = tokStringParse(clearance, "x", '#', '=');
  string sy = tokStringParse(clearance, "y", '#', '=');
  if((id != m_session_id) || (m_state != "RENDEZVOUS")) {
    reportRunWarning("Ignored stale or unexpected landing clearance");
    return(true);
  }
  if((status != "cleared") || !isNumber(sx) || !isNumber(sy)) {
    abortMission("invalid_landing_clearance");
    return(true);
  }

  m_clearance_received = true;
  beginTargetAcquisition(atof(sx.c_str()), atof(sy.c_str()));
  return(true);
}

bool Rendezvous::handleMailNodeReport(const CMOOSMsg& msg)
{
  if(!msg.IsString())
    return(false);

  string report = msg.GetString();
  string name = tolower(tokStringParse(report, "NAME"));
  if(name == "")
    name = tolower(tokStringParse(report, "name"));
  if(name != tolower(m_peer_node))
    return(true);

  string sx = tokStringParse(report, "X");
  string sy = tokStringParse(report, "Y");
  if(sx == "")
    sx = tokStringParse(report, "x");
  if(sy == "")
    sy = tokStringParse(report, "y");
  if(!isNumber(sx) || !isNumber(sy))
    return(true);

  m_peer_report_x = atof(sx.c_str());
  m_peer_report_y = atof(sy.c_str());
  m_peer_report_time = m_curr_time;
  return(true);
}

bool Rendezvous::handleMailPrecisionLandRequest(const CMOOSMsg& msg)
{
  if(!mailIsTrue(msg))
    return(true);

  // Precision landing is a state-machine commit, not an operator bypass.
  // The coordinated flow will publish UAV_PREC_LAND_COMMIT after every gate
  // has remained true for the configured lock dwell.
  Notify("UAV_PREC_LAND_RESULT",
         "status=rejected#reason=coordinated_clearance_required");
  Notify("UAV_PREC_LAND_REQUEST", "false");
  reportRunWarning("Precision-land request rejected: use rendezvous clearance");
  return(true);
}

bool Rendezvous::handleMailDouble(const CMOOSMsg& msg, double& value,
                                  double& value_time)
{
  if(!msg.IsDouble())
    return(false);
  double candidate = msg.GetDouble();
  if(!std::isfinite(candidate))
    return(false);
  value = candidate;
  value_time = m_curr_time;
  return(true);
}

bool Rendezvous::handleMailBool(const CMOOSMsg& msg, bool& value,
                                bool& value_set)
{
  value = mailIsTrue(msg);
  value_set = true;
  return(true);
}

bool Rendezvous::handleMailTimedBool(const CMOOSMsg& msg, bool& value,
                                     bool& value_set, double& value_time)
{
  if(!handleMailBool(msg, value, value_set))
    return(false);
  value_time = m_curr_time;
  return(true);
}

// Return -1 for a terminal check failure, 0 while checks are pending, and 1
// only when every scalar result and its session diagnostic agree on success.
int Rendezvous::requiredChecksStatus(string& reason) const
{
  // A confirmed failure wins even if another required check has not yet
  // published for this session.
  for(const auto& entry : m_required_checks) {
    const RequiredCheck& check = entry.second;
    if(check.session != m_session_id)
      continue;
    if((check.result == "fail") || (check.result == "expired")) {
      reason = "required_check_" + check.result + ":" + entry.first;
      return(-1);
    }
    if((check.state_result == "fail") ||
       (check.state_result == "expired")) {
      reason = "required_check_" + check.state_result + ":" + entry.first;
      return(-1);
    }
  }

  for(const auto& entry : m_required_checks) {
    const RequiredCheck& check = entry.second;
    if((check.session != m_session_id) ||
       (check.result != "success") ||
       (check.state_result != "success"))
      return(0);
  }
  return(1);
}

void Rendezvous::beginRequiredChecks(const string& request, const string& id)
{
  m_session_id = id;
  m_pending_request = request;
  m_target_set = false;
  m_arrival_start_time = 0;
  for(auto& entry : m_required_checks)
    entry.second = RequiredCheck();
  transitionTo("REQUESTING", "required_checks_pending");
}

//---------------------------------------------------------
// Role logic

void Rendezvous::iterateUAV()
{
  double elapsed = m_curr_time - m_state_enter_time;

  if(m_state == "REQUESTING") {
    if(m_route_command_time > 0) {
      bool recent_state = (m_route_state_time >= m_route_command_time);
      if(recent_state && (m_route_state == "DEPLOY_ACCEPTED")) {
        string response = "id=" + m_session_id + "#status=accepted";
        sendMessage(m_peer_node, "RENDEZVOUS_RESPONSE", response);
        m_acceptances_sent++;
        transitionTo("RENDEZVOUS", "route_accepted");
      }
      else if(recent_state && strContains(m_route_state, "REJECTED")) {
        string response = "id=" + m_session_id;
        response += "#status=rejected#reason=route_rejected";
        sendMessage(m_peer_node, "RENDEZVOUS_RESPONSE", response);
        abortMission("route_rejected", false);
      }
      else if((m_curr_time - m_route_command_time) > m_route_timeout) {
        string response = "id=" + m_session_id;
        response += "#status=rejected#reason=route_timeout";
        sendMessage(m_peer_node, "RENDEZVOUS_RESPONSE", response);
        abortMission("route_timeout", false);
      }
    }
    else if(elapsed > m_request_timeout)
      abortMission("proposal_timeout");
  }
  else if(m_state == "RENDEZVOUS") {
    string reason;
    if(!uavIsReady(reason)) {
      abortMission(reason);
      return;
    }
    if(elapsed > m_transit_timeout) {
      abortMission("transit_timeout");
      return;
    }
    checkUAVArrival();

    // Both vehicles may satisfy the rendezvous-point radius yet stop on
    // opposite sides of it. Close directly on PEARL until the actual
    // separation gate can be held, rather than waiting indefinitely at the
    // nominal point.
    if(m_uav_arrived && peerReportIsFresh()) {
      double separation = hypot(m_nav_x - m_peer_report_x,
                                m_nav_y - m_peer_report_y);
      Notify("UAV_PEER_DISTANCE", separation);
      if((separation > m_max_peer_separation) &&
         ((m_curr_time - m_last_reacquire_route_time) >=
          m_reacquire_update_interval))
        routeToPearl(true);
    }
  }
  else if(m_state == "ACQUIRING_TARGET")
    updateTargetAcquisition();
  else if((m_state == "LANDING") && m_uav_landed_set && !m_uav_in_air &&
          !m_completion_sent) {
    string response = "id=" + m_session_id + "#status=complete";
    sendMessage(m_peer_node, "RENDEZVOUS_RESPONSE", response);
    m_completion_sent = true;
    transitionTo("COMPLETE", "landed");
  }

  if(m_secondary_recovery_state != "IDLE")
    updateSecondaryRecovery();
}

void Rendezvous::iteratePearl()
{
  reconcilePearlWithUAV();
  double elapsed = m_curr_time - m_state_enter_time;

  if((m_state == "REQUESTING") && !m_pending_request.empty()) {
    string check_reason;
    int check_status = requiredChecksStatus(check_reason);
    if(check_status < 0) {
      string response = "id=" + m_session_id;
      response += "#status=rejected#reason=" + check_reason;
      sendMessage(m_peer_node, "RENDEZVOUS_RESPONSE", response);
      m_pending_request.clear();
      abortMission(check_reason, false);
      return;
    }
    if(check_status > 0) {
      string request = m_pending_request;
      m_pending_request.clear();
      proposeRendezvous(request);
      return;
    }
    if(elapsed > m_request_timeout) {
      string response = "id=" + m_session_id;
      response += "#status=rejected#reason=required_check_timeout";
      sendMessage(m_peer_node, "RENDEZVOUS_RESPONSE", response);
      m_pending_request.clear();
      abortMission("required_check_timeout", false);
    }
    return;
  }

  if(m_pearl_activation_pending &&
     (m_curr_time >= m_pearl_activation_time)) {
    Notify("PEARL_RENDEZVOUS_ACTIVE", "true");
    m_pearl_activation_pending = false;
  }

  if((m_state == "REQUESTING") && (elapsed > m_request_timeout))
    abortMission("response_timeout");
  else if(m_state == "RENDEZVOUS") {
    if(!navIsFresh()) {
      abortMission("pearl_navigation_stale");
      return;
    }
    if(!peerReportIsFresh()) {
      if(elapsed > m_nav_stale_thresh)
        abortMission("uav_report_stale");
      return;
    }
    if(elapsed > m_transit_timeout) {
      abortMission("transit_timeout");
      return;
    }

    checkPearlArrival();
    double uav_dist = hypot(m_peer_report_x - m_target_x,
                            m_peer_report_y - m_target_y);
    m_uav_arrived = (uav_dist <= m_arrival_radius);
    double peer_dist = hypot(m_peer_report_x - m_nav_x,
                             m_peer_report_y - m_nav_y);
    bool gate_ready = m_pearl_arrived && m_uav_arrived &&
                      (peer_dist <= m_max_peer_separation);
    string gate_reason = "ready";
    if(!m_pearl_arrived)
      gate_reason = "pearl_outside_rendezvous";
    else if(!m_uav_arrived)
      gate_reason = "uav_outside_rendezvous";
    else if(peer_dist > m_max_peer_separation)
      gate_reason = "peer_separation_too_large";
    else
      gate_ready = landingPlatformIsReady(gate_reason);
    Notify("PEARL_UAV_SEPARATION", peer_dist);
    Notify("PEARL_LANDING_GATE_READY", gate_ready ? 1.0 : 0.0);
    Notify("PEARL_LANDING_GATE_REASON", gate_reason);

    if(gate_ready) {
      if(m_arrival_start_time <= 0)
        m_arrival_start_time = m_curr_time;
      if((m_curr_time - m_arrival_start_time) >= m_arrival_dwell)
        grantLandingClearance();
    }
    else
      m_arrival_start_time = 0;
  }
  else if(m_state == "ACQUIRING_TARGET") {
    if(!navIsFresh())
      abortMission("pearl_navigation_stale");
    else if(!peerReportIsFresh())
      abortMission("uav_report_stale");
    else if(elapsed > (m_landing_target_acquire_timeout + 5.0))
      abortMission("uav_landing_commit_timeout");
  }
}

bool Rendezvous::beginRequest()
{
  string reason;
  if(!uavIsReady(reason)) {
    m_session_id = "";
    m_target_set = false;
    transitionTo("ABORT", reason);
    reportRunWarning("Rendezvous rejected: " + reason);
    return(true);
  }
  if(!landingPlatformIsReady(reason)) {
    m_session_id = "";
    m_target_set = false;
    transitionTo("ABORT", reason);
    reportRunWarning("Rendezvous rejected: " + reason);
    return(true);
  }

  m_session_id = makeSessionID();
  m_target_set = false;
  m_route_command_time = 0;
  m_route_state_time = 0;
  m_arrival_start_time = 0;
  m_uav_arrived = false;
  m_clearance_received = false;
  m_acquisition_start_time = 0;
  m_target_lock_start_time = 0;
  m_last_reacquire_route_time = 0;
  m_landing_target_gate_ready = false;
  m_completion_sent = false;

  string battery = m_battery_set ? doubleToStringX(m_battery, 1) : "unknown";
  string priority = recoveryPriority();
  string request = "id=" + m_session_id;
  request += "#x=" + doubleToStringX(m_nav_x, 2);
  request += "#y=" + doubleToStringX(m_nav_y, 2);
  request += "#speed=" + doubleToStringX(m_uav_speed, 2);
  request += "#battery=" + battery;
  request += "#priority=" + priority;
  request += "#health=ok";
  Notify("UAV_RECOVERY_PRIORITY", priority);

  if(!sendMessage(m_peer_node, "RENDEZVOUS_REQUEST", request))
    return(false);

  m_requests_sent++;
  transitionTo("REQUESTING", "request_sent");
  return(true);
}

bool Rendezvous::proposeRendezvous(const string& request)
{
  string id = tokStringParse(request, "id", '#', '=');
  string sx = tokStringParse(request, "x", '#', '=');
  string sy = tokStringParse(request, "y", '#', '=');
  string sspeed = tokStringParse(request, "speed", '#', '=');
  string battery = tolower(tokStringParse(request, "battery", '#', '='));
  string priority = tolower(tokStringParse(request, "priority", '#', '='));
  string health = tolower(tokStringParse(request, "health", '#', '='));

  string reject_reason;
  string platform_reason;
  if(!navIsFresh())
    reject_reason = "pearl_navigation_stale";
  else if(!landingPlatformIsReady(platform_reason))
    reject_reason = platform_reason;
  else if(!isNumber(sx) || !isNumber(sy) || !isNumber(sspeed))
    reject_reason = "invalid_request";
  else if(health != "ok")
    reject_reason = "uav_unhealthy";
  else if(m_require_battery && !isNumber(battery))
    reject_reason = "battery_unknown";
  else if(isNumber(battery) &&
          ((atof(battery.c_str()) < 0) || (atof(battery.c_str()) > 100)))
    reject_reason = "battery_invalid";
  else if((priority != "") && (priority != "normal") &&
          (priority != "urgent") && (priority != "emergency") &&
          (priority != "unknown"))
    reject_reason = "invalid_priority";

  if(reject_reason != "") {
    string response = "id=" + id + "#status=rejected#reason=" + reject_reason;
    sendMessage(m_peer_node, "RENDEZVOUS_RESPONSE", response);
    transitionTo("ABORT", reject_reason);
    return(true);
  }

  savePearlControlState();

  double uav_x = atof(sx.c_str());
  double uav_y = atof(sy.c_str());
  double request_speed = atof(sspeed.c_str());
  double speed_sum = m_pearl_speed + request_speed;
  if(speed_sum <= 0) {
    string response = "id=" + id + "#status=rejected#reason=invalid_speed";
    sendMessage(m_peer_node, "RENDEZVOUS_RESPONSE", response);
    transitionTo("ABORT", "invalid_speed");
    return(true);
  }

  // With negligible PEARL turn radius, this fraction equalizes nominal
  // travel time along the straight line between the two vehicles.
  double fraction = m_pearl_speed / speed_sum;
  m_target_x = m_nav_x + fraction * (uav_x - m_nav_x);
  m_target_y = m_nav_y + fraction * (uav_y - m_nav_y);
  m_target_set = true;
  m_session_id = id;
  m_pearl_arrived = false;
  m_uav_arrived = false;
  m_arrival_start_time = 0;

  string proposal = "id=" + id;
  proposal += "#x=" + doubleToStringX(m_target_x, 2);
  proposal += "#y=" + doubleToStringX(m_target_y, 2);
  proposal += "#valid_for=" + doubleToStringX(m_request_timeout, 1);
  if(!sendMessage(m_peer_node, "RENDEZVOUS_PROPOSAL", proposal))
    return(false);

  m_pending_request.clear();
  m_proposals_sent++;
  publishPoint(true);
  transitionTo("REQUESTING", "proposal_sent");
  return(true);
}

bool Rendezvous::acceptProposal(const string& proposal)
{
  string id = tokStringParse(proposal, "id", '#', '=');
  string sx = tokStringParse(proposal, "x", '#', '=');
  string sy = tokStringParse(proposal, "y", '#', '=');

  string reason;
  if((id == "") || (id != m_session_id))
    reason = "stale_session";
  else if(!isNumber(sx) || !isNumber(sy))
    reason = "invalid_proposal";
  else {
    string readiness_reason;
    if(!uavIsReady(readiness_reason))
      reason = readiness_reason;
  }

  if(reason != "") {
    string response = "id=" + ((id == "") ? m_session_id : id);
    response += "#status=rejected#reason=" + reason;
    sendMessage(m_peer_node, "RENDEZVOUS_RESPONSE", response);
    abortMission(reason, false);
    return(true);
  }

  m_target_x = atof(sx.c_str());
  m_target_y = atof(sy.c_str());
  m_target_set = true;
  m_uav_arrived = false;
  m_arrival_start_time = 0;

  string command = "action=deploy # purpose=recovery # route_name=recovery_" + m_session_id;
  command += " # points={";
  command += doubleToStringX(m_target_x, 2) + ",";
  command += doubleToStringX(m_target_y, 2) + "}";
  command += " # speed=" + doubleToStringX(m_recovery_route_speed, 2);
  command += " # capture_radius=" +
             doubleToStringX(m_recovery_capture_radius, 2);
  command += " # slip_radius=" +
             doubleToStringX(m_recovery_capture_radius, 2);
  Notify("ROUTE_BUFFER_COMMAND", command);
  m_route_command_time = m_curr_time;
  m_reason = "staging_route";
  publishState(true);
  return(true);
}

void Rendezvous::activatePearlTransit()
{
  // A templating=spawn behavior only treats an update as a spawn request
  // when it contains name=. The session ID also guarantees a fresh waypoint
  // instance for each rendezvous.
  string update = "name=" + m_session_id;
  update += " # points=" + doubleToStringX(m_target_x, 2) + ",";
  update += doubleToStringX(m_target_y, 2);
  update += " # repeat=0 # order=normal";

  // Stage the waypoint before enabling the initially-empty behavior. This
  // prevents it from completing on activation before it sees its first point.
  Notify("PEARL_RENDEZVOUS_UPDATE", update);
  Notify("PEARL_RENDEZVOUS_ARRIVED", "false");
  Notify("DEPLOY", "true");
  Notify("RETURN", "false");
  Notify("STATION_KEEP", "false");
  Notify("MOOS_MANUAL_OVERRIDE", "false");

  // Give the Helm one iteration to apply the update before activating an
  // initially-empty waypoint behavior.
  m_pearl_activation_pending = true;
  m_pearl_activation_time = m_curr_time + 0.25;

  m_pearl_arrived = false;
  m_arrival_start_time = 0;
}

void Rendezvous::checkPearlArrival()
{
  if(!m_target_set) {
    m_pearl_arrived = false;
    return;
  }
  m_pearl_arrived =
    (hypot(m_nav_x - m_target_x, m_nav_y - m_target_y) <= m_arrival_radius);
}

void Rendezvous::checkUAVArrival()
{
  if(!m_target_set)
    return;

  bool inside = (hypot(m_nav_x - m_target_x, m_nav_y - m_target_y) <=
                 m_arrival_radius);
  if(inside) {
    if(m_arrival_start_time <= 0)
      m_arrival_start_time = m_curr_time;
    m_uav_arrived = ((m_curr_time - m_arrival_start_time) >= m_arrival_dwell);
  }
  else {
    m_arrival_start_time = 0;
    m_uav_arrived = false;
  }
}

void Rendezvous::grantLandingClearance()
{
  Notify("PEARL_RENDEZVOUS_ACTIVE", "false");
  Notify("DEPLOY", "true");
  Notify("RETURN", "false");
  Notify("STATION_KEEP", "true");
  Notify("MOOS_MANUAL_OVERRIDE", "false");

  string clearance = "id=" + m_session_id + "#status=cleared";
  clearance += "#x=" + doubleToStringX(m_nav_x, 2);
  clearance += "#y=" + doubleToStringX(m_nav_y, 2);
  sendMessage(m_peer_node, "LANDING_CLEARANCE", clearance);
  m_clearances_sent++;
  transitionTo("ACQUIRING_TARGET", "clearance_sent");
}

void Rendezvous::beginTargetAcquisition(double pearl_x, double pearl_y)
{
  m_peer_report_x = pearl_x;
  m_peer_report_y = pearl_y;
  m_peer_report_time = m_curr_time;
  m_acquisition_start_time = m_curr_time;
  m_target_lock_start_time = 0;
  m_last_reacquire_route_time = 0;
  m_landing_target_gate_ready = false;
  Notify("UAV_LANDING_GATE_READY", 0.0);
  Notify("UAV_LANDING_GATE_REASON", "acquiring_target");
  routeToPearl(true);
  transitionTo("ACQUIRING_TARGET", "clearance_received");
}

void Rendezvous::updateTargetAcquisition()
{
  string readiness_reason;
  if(!uavIsReady(readiness_reason)) {
    abortMission(readiness_reason);
    return;
  }
  if((m_curr_time - m_acquisition_start_time) >
     m_landing_target_acquire_timeout) {
    abortMission("landing_target_timeout");
    return;
  }
  if(!landingPlatformIsReady(readiness_reason)) {
    m_target_lock_start_time = 0;
    m_landing_target_gate_ready = false;
    Notify("UAV_LANDING_GATE_READY", 0.0);
    Notify("UAV_LANDING_GATE_REASON", readiness_reason);
    Notify("UAV_LANDING_TARGET_LOCK_AGE", 0.0);
    return;
  }
  if(!peerReportIsFresh()) {
    m_target_lock_start_time = 0;
    m_landing_target_gate_ready = false;
    Notify("UAV_LANDING_GATE_READY", 0.0);
    Notify("UAV_LANDING_GATE_REASON", "pearl_report_stale");
    if((m_curr_time - m_peer_report_time) > m_nav_stale_thresh)
      abortMission("pearl_report_stale");
    return;
  }

  routeToPearl(false);
  double separation = hypot(m_nav_x - m_peer_report_x,
                            m_nav_y - m_peer_report_y);
  Notify("UAV_PEER_DISTANCE", separation);

  string gate_reason;
  bool gate_ready = (separation <= m_max_peer_separation);
  if(!gate_ready)
    gate_reason = "peer_separation_too_large";
  else
    gate_ready = landingTargetIsReady(gate_reason);

  if(gate_ready) {
    if(m_target_lock_start_time <= 0)
      m_target_lock_start_time = m_curr_time;
    double lock_age = m_curr_time - m_target_lock_start_time;
    m_landing_target_gate_ready =
      (lock_age >= m_landing_target_lock_dwell);
    Notify("UAV_LANDING_GATE_READY",
           m_landing_target_gate_ready ? 1.0 : 0.0);
    Notify("UAV_LANDING_GATE_REASON",
           m_landing_target_gate_ready ? "ready" : "target_lock_dwell");
    Notify("UAV_LANDING_TARGET_LOCK_AGE", lock_age);
    if(m_landing_target_gate_ready)
      beginPrecisionLanding();
  }
  else {
    m_target_lock_start_time = 0;
    m_landing_target_gate_ready = false;
    Notify("UAV_LANDING_GATE_READY", 0.0);
    Notify("UAV_LANDING_GATE_REASON", gate_reason);
    Notify("UAV_LANDING_TARGET_LOCK_AGE", 0.0);
  }
}

void Rendezvous::routeToPearl(bool force)
{
  if(!peerReportIsFresh())
    return;

  double moved = hypot(m_peer_report_x - m_last_reacquire_x,
                       m_peer_report_y - m_last_reacquire_y);
  bool interval_ok = ((m_curr_time - m_last_reacquire_route_time) >=
                      m_reacquire_update_interval);
  if(!force && (!interval_ok || (moved < m_reacquire_update_distance)))
    return;

  string command = "action=deploy # purpose=recovery # route_name=recovery_" + m_session_id;
  command += " # points={";
  command += doubleToStringX(m_peer_report_x, 2) + ",";
  command += doubleToStringX(m_peer_report_y, 2) + "}";
  command += " # speed=" + doubleToStringX(m_recovery_route_speed, 2);
  command += " # capture_radius=" +
             doubleToStringX(m_recovery_capture_radius, 2);
  command += " # slip_radius=" +
             doubleToStringX(m_recovery_capture_radius, 2);
  Notify("ROUTE_BUFFER_COMMAND", command);
  m_route_command_time = m_curr_time;
  m_last_reacquire_route_time = m_curr_time;
  m_last_reacquire_x = m_peer_report_x;
  m_last_reacquire_y = m_peer_report_y;
  Notify("UAV_LANDING_APPROACH_POINT",
         pointString(m_peer_report_x, m_peer_report_y));
}

void Rendezvous::beginPrecisionLanding()
{
  if((m_state != "ACQUIRING_TARGET") || !m_landing_target_gate_ready)
    return;
  Notify("ROUTE_BUFFER_COMMAND", "action=clear");
  Notify("UAV_PREC_LAND_COMMIT", "true");
  Notify("UAV_PREC_LAND_RESULT", "status=committed#reason=landing_gate_ready");
  string response = "id=" + m_session_id + "#status=landing";
  sendMessage(m_peer_node, "RENDEZVOUS_RESPONSE", response);
  transitionTo("LANDING", "landing_gate_ready");
}

void Rendezvous::savePearlControlState()
{
  if((m_role != "pearl") || m_pearl_control_saved)
    return;

  if(!m_pearl_deploy_set || !m_pearl_return_set ||
     !m_pearl_station_keep_set || !m_pearl_manual_override_set)
    reportRunWarning("Saving incomplete PEARL control state");

  m_saved_pearl_deploy = m_pearl_deploy;
  m_saved_pearl_return = m_pearl_return;
  m_saved_pearl_station_keep = m_pearl_station_keep;
  m_saved_pearl_manual_override = m_pearl_manual_override;
  m_pearl_control_saved = true;

  Notify("PEARL_CONTROL_RESTORED", "false");
  Notify("PEARL_CONTROL_HISTORY",
         "deploy=" + boolToString(m_saved_pearl_deploy) +
         ",return=" + boolToString(m_saved_pearl_return) +
         ",station_keep=" + boolToString(m_saved_pearl_station_keep) +
         ",manual_override=" + boolToString(m_saved_pearl_manual_override));
}

void Rendezvous::restorePearlControlState()
{
  if((m_role != "pearl") || !m_pearl_control_saved)
    return;

  Notify("PEARL_RENDEZVOUS_ACTIVE", "false");
  Notify("DEPLOY", boolToString(m_saved_pearl_deploy));
  Notify("RETURN", boolToString(m_saved_pearl_return));
  Notify("STATION_KEEP", boolToString(m_saved_pearl_station_keep));
  Notify("MOOS_MANUAL_OVERRIDE", boolToString(m_saved_pearl_manual_override));
  Notify("PEARL_CONTROL_RESTORED", "true");
  m_pearl_control_saved = false;
  reportEvent("Restored pre-rendezvous PEARL control state");
}

void Rendezvous::reconcilePearlWithUAV()
{
  if((m_role != "pearl") || (m_session_id == "") ||
     (m_remote_uav_session != m_session_id))
    return;

  if((m_remote_uav_phase == "LANDING") &&
     ((m_state == "RENDEZVOUS") || (m_state == "ACQUIRING_TARGET")))
    transitionTo("LANDING", "reconciled_from_uav_phase");
  else if((m_remote_uav_phase == "COMPLETE") &&
          ((m_state == "REQUESTING") || (m_state == "RENDEZVOUS") ||
           (m_state == "ACQUIRING_TARGET") || (m_state == "LANDING"))) {
    transitionTo("COMPLETE", "reconciled_from_uav_phase");
    publishPoint(false);
  }
  else if((m_remote_uav_phase == "ABORT") &&
          ((m_state == "REQUESTING") || (m_state == "RENDEZVOUS") ||
           (m_state == "ACQUIRING_TARGET")))
    abortMission("reconciled_uav_abort", false);
}

void Rendezvous::beginSecondaryRecovery()
{
  if(!m_secondary_landing_enabled ||
     (m_secondary_recovery_state != "IDLE") ||
     !m_uav_landed_set || !m_uav_in_air)
    return;

  string command = "action=deploy # purpose=recovery # route_name=secondary_" + m_session_id;
  command += " # points={";
  command += doubleToStringX(m_secondary_landing_x, 2) + ",";
  command += doubleToStringX(m_secondary_landing_y, 2) + "}";
  command += " # speed=" + doubleToStringX(m_recovery_route_speed, 2);
  command += " # capture_radius=" +
             doubleToStringX(m_recovery_capture_radius, 2);
  command += " # slip_radius=" +
             doubleToStringX(m_recovery_capture_radius, 2);
  Notify("ROUTE_BUFFER_COMMAND", command);

  string point = "x=" + doubleToStringX(m_secondary_landing_x, 2);
  point += ",y=" + doubleToStringX(m_secondary_landing_y, 2);
  point += ",label=secondary_landing";
  point += ",vertex_color=orange,label_color=orange,vertex_size=10";
  Notify("VIEW_POINT", point);
  Notify("UAV_SECONDARY_RECOVERY_POINT",
         pointString(m_secondary_landing_x, m_secondary_landing_y));

  m_secondary_arrival_start_time = 0.0;
  publishSecondaryRecoveryState("TRANSITING");
}

void Rendezvous::updateSecondaryRecovery()
{
  if(m_secondary_recovery_state == "TRANSITING") {
    if(!navIsFresh()) {
      m_secondary_arrival_start_time = 0.0;
      return;
    }

    double distance = hypot(m_nav_x - m_secondary_landing_x,
                            m_nav_y - m_secondary_landing_y);
    Notify("UAV_SECONDARY_RECOVERY_DISTANCE", distance);
    if(distance <= m_secondary_arrival_radius) {
      if(m_secondary_arrival_start_time <= 0)
        m_secondary_arrival_start_time = m_curr_time;
      if((m_curr_time - m_secondary_arrival_start_time) >=
         m_secondary_arrival_dwell) {
        Notify("ROUTE_BUFFER_COMMAND", "action=clear");
        publishSecondaryRecoveryState("ARRIVED");
      }
    }
    else
      m_secondary_arrival_start_time = 0.0;
  }
  else if((m_secondary_recovery_state == "ARRIVED") &&
          ((m_curr_time - m_secondary_state_enter_time) >=
           m_secondary_landing_delay)) {
    publishSecondaryRecoveryState("LANDING");
    Notify("UAV_PREC_LAND_RESULT",
           "status=committed#reason=secondary_recovery");
    Notify("UAV_PREC_LAND_COMMIT", "true");
  }
  else if((m_secondary_recovery_state == "LANDING") &&
          m_uav_landed_set && !m_uav_in_air &&
          m_uav_armed_set && !m_uav_armed) {
    publishSecondaryRecoveryState("LANDED");
  }
}

void Rendezvous::publishSecondaryRecoveryState(const string& state)
{
  m_secondary_recovery_state = state;
  m_secondary_state_enter_time = m_curr_time;
  Notify("UAV_SECONDARY_RECOVERY_STATE", state);
  reportEvent("Secondary recovery state: " + state);
}

void Rendezvous::abortMission(const string& reason, bool notify_peer)
{
  if(m_state == "ABORT")
    return;

  if(m_role == "uav") {
    if((m_state == "RENDEZVOUS") || (m_state == "ACQUIRING_TARGET") ||
       (m_route_command_time > 0))
      Notify("ROUTE_BUFFER_COMMAND", "action=clear");
    Notify("UAV_LANDING_GATE_READY", 0.0);
    Notify("UAV_LANDING_GATE_REASON", reason);
  }
  else if(m_role == "pearl") {
    m_pearl_activation_pending = false;
    Notify("PEARL_RENDEZVOUS_ACTIVE", "false");
    Notify("DEPLOY", "true");
    Notify("RETURN", "false");
    Notify("STATION_KEEP", "true");
    Notify("MOOS_MANUAL_OVERRIDE", "false");
    Notify("PEARL_LANDING_GATE_READY", 0.0);
    Notify("PEARL_LANDING_GATE_REASON", reason);
    publishPoint(false);
  }

  if(notify_peer && (m_session_id != "")) {
    string response = "id=" + m_session_id;
    response += "#status=abort#reason=" + reason;
    sendMessage(m_peer_node, "RENDEZVOUS_RESPONSE", response);
  }

  m_pending_request.clear();
  m_aborts++;
  transitionTo("ABORT", reason);
  if(m_role == "uav")
    beginSecondaryRecovery();
}

//---------------------------------------------------------
// Publications and utility methods

void Rendezvous::transitionTo(const string& state, const string& reason)
{
  m_state = state;
  m_reason = reason;
  if((m_role == "pearl") && ((state == "COMPLETE") || (state == "ABORT")))
    restorePearlControlState();
  m_state_enter_time = m_curr_time;
  publishState(true);
  reportEvent("Rendezvous state: " + state + " (" + reason + ")");
}

void Rendezvous::publishState(bool force)
{
  if(!force && ((m_curr_time - m_last_state_post_time) < m_state_post_interval))
    return;

  string state = "state=" + m_state + "#reason=" + m_reason;
  if(m_session_id != "")
    state += "#id=" + m_session_id;
  if(m_target_set) {
    state += "#x=" + doubleToStringX(m_target_x, 2);
    state += "#y=" + doubleToStringX(m_target_y, 2);
  }
  string state_var = (m_role == "uav") ? "UAV_RENDEZVOUS_STATE" :
                                          "PEARL_RENDEZVOUS_STATE";
  Notify(state_var, state);
  string phase_var = (m_role == "uav") ? "UAV_RENDEZVOUS_PHASE" :
                                          "PEARL_RENDEZVOUS_PHASE";
  string session_var = (m_role == "uav") ? "UAV_RENDEZVOUS_SESSION" :
                                            "PEARL_RENDEZVOUS_SESSION";
  Notify(phase_var, m_state);
  Notify(session_var, m_session_id);
  if(m_role == "uav") {
    Notify("UAV_RECOVERY_PRIORITY", recoveryPriority());
    Notify("UAV_RENDEZVOUS_ARRIVED", m_uav_arrived ? 1.0 : 0.0);
    Notify("UAV_SECONDARY_RECOVERY_STATE", m_secondary_recovery_state);
    Notify("UAV_SECONDARY_RECOVERY_POINT",
           pointString(m_secondary_landing_x, m_secondary_landing_y));
  }
  m_last_state_post_time = m_curr_time;
}

void Rendezvous::publishPoint(bool active)
{
  if((m_role != "pearl") || !m_target_set)
    return;

  string spec = "x=" + doubleToStringX(m_target_x, 2);
  spec += ",y=" + doubleToStringX(m_target_y, 2);
  spec += ",label=rendezvous_point";
  spec += ",vertex_color=magenta,label_color=magenta,vertex_size=10";
  spec += active ? ",active=true" : ",active=false";
  Notify("VIEW_POINT", spec);
  Notify("RENDEZVOUS_POINT", pointString(m_target_x, m_target_y));
}

bool Rendezvous::sendMessage(const string& destination,
                             const string& variable,
                             const string& value)
{
  NodeMessage message(m_ownship, destination, variable);
  message.setSourceApp(GetAppName());
  message.setStringVal(value);
  if(!message.valid()) {
    reportRunWarning("Unable to form rendezvous node message");
    return(false);
  }
  Notify("NODE_MESSAGE_LOCAL", message.getSpec());
  return(true);
}

bool Rendezvous::navIsFresh() const
{
  if(!m_nav_x_set || !m_nav_y_set)
    return(false);
  double latest = (m_nav_x_time < m_nav_y_time) ? m_nav_x_time : m_nav_y_time;
  return((m_curr_time - latest) <= m_nav_stale_thresh);
}

bool Rendezvous::peerReportIsFresh() const
{
  return((m_peer_report_time > 0) &&
         ((m_curr_time - m_peer_report_time) <= m_nav_stale_thresh));
}

bool Rendezvous::landingTargetIsReady(string& reason) const
{
  if(!m_require_landing_target) {
    reason = "ready";
    return(true);
  }

  double mail_max_age = m_landing_target_max_age + 0.5;
  if((m_landing_target_available_time <= 0) ||
     ((m_curr_time - m_landing_target_available_time) > mail_max_age))
    reason = "landing_target_status_stale";
  else if(!m_landing_target_available)
    reason = "landing_target_unavailable";
  else if((m_landing_target_age_time <= 0) ||
          ((m_curr_time - m_landing_target_age_time) > mail_max_age))
    reason = "landing_target_age_stale";
  else if((m_landing_target_age < 0) ||
          (m_landing_target_age > m_landing_target_max_age))
    reason = "landing_target_too_old";
  else if((m_landing_target_num_time <= 0) ||
          ((m_curr_time - m_landing_target_num_time) > mail_max_age))
    reason = "landing_target_identity_stale";
  else if((m_landing_target_num < 0) ||
          (fabs(m_landing_target_num - m_expected_target_num) > 0.1))
    reason = "unexpected_landing_target";
  else if((m_landing_target_position_valid_time <= 0) ||
          ((m_curr_time - m_landing_target_position_valid_time) > mail_max_age))
    reason = "landing_target_geometry_stale";
  else if(m_landing_target_position_valid > 0.5) {
    if((m_landing_target_position_time <= 0) ||
       ((m_curr_time - m_landing_target_position_time) > mail_max_age))
      reason = "landing_target_position_stale";
    else if(hypot(m_landing_target_x, m_landing_target_y) >
            m_landing_target_max_offset)
      reason = "landing_target_offset_too_large";
    else {
      reason = "ready";
      return(true);
    }
  }
  else if((m_landing_target_angle_time <= 0) ||
          ((m_curr_time - m_landing_target_angle_time) > mail_max_age))
    reason = "landing_target_angle_stale";
  else if(hypot(m_landing_target_angle_x, m_landing_target_angle_y) >
          m_landing_target_max_angle)
    reason = "landing_target_angle_too_large";
  else {
    reason = "ready";
    return(true);
  }
  return(false);
}

bool Rendezvous::landingPlatformIsReady(string& reason) const
{
  if(!m_require_platform_ready) {
    reason = "ready";
    return(true);
  }

  if(m_require_platform_health && !m_platform_health_set)
    reason = "pearl_health_unknown";
  else if(m_require_platform_health && !m_platform_health_ok)
    reason = "pearl_health_not_ready";
  else if(!m_pearl_battery_set || !m_pearl_battery_valid_set)
    reason = "pearl_battery_unknown";
  else if(!m_pearl_battery_valid)
    reason = "pearl_battery_invalid";
  else if((m_curr_time - m_pearl_battery_time) > m_platform_data_max_age ||
          (m_curr_time - m_pearl_battery_valid_time) >
            m_platform_data_max_age)
    reason = "pearl_battery_stale";
  else if((m_pearl_battery < 0) || (m_pearl_battery > 100))
    reason = "pearl_battery_invalid";
  else if(m_pearl_battery < m_min_pearl_battery)
    reason = "pearl_battery_below_reserve";
  else if(!m_pearl_wind_speed_set || !m_pearl_wind_valid_set)
    reason = "pearl_wind_unknown";
  else if(!m_pearl_wind_valid)
    reason = "pearl_wind_invalid";
  else if((m_curr_time - m_pearl_wind_speed_time) >
            m_platform_data_max_age ||
          (m_curr_time - m_pearl_wind_valid_time) >
            m_platform_data_max_age)
    reason = "pearl_wind_stale";
  else if(m_pearl_wind_speed < 0)
    reason = "pearl_wind_invalid";
  else if(m_pearl_wind_speed > m_max_landing_wind_speed)
    reason = "landing_wind_high";
  else {
    reason = "ready";
    return(true);
  }
  return(false);
}

bool Rendezvous::uavIsReady(string& reason) const
{
  if(!navIsFresh())
    reason = "navigation_stale";
  else if(m_require_health && (!m_health_set || !m_health_ok))
    reason = "health_not_ready";
  else if(m_require_flight_state && (!m_uav_armed_set || !m_uav_armed))
    reason = "not_armed";
  else if(m_require_flight_state && (!m_uav_landed_set || !m_uav_in_air))
    reason = "not_airborne";
  else if(m_require_battery && (!m_battery_set || !m_battery_valid_set))
    reason = "battery_unknown";
  else if(m_require_battery && !m_battery_valid)
    reason = "battery_invalid";
  else if(m_require_battery &&
          (((m_curr_time - m_battery_time) > m_battery_max_age) ||
           ((m_curr_time - m_battery_valid_time) > m_battery_max_age)))
    reason = "battery_stale";
  else if(m_require_battery && ((m_battery < 0) || (m_battery > 100)))
    reason = "battery_invalid";
  else {
    reason = "ready";
    return(true);
  }
  return(false);
}

string Rendezvous::recoveryPriority() const
{
  if(!m_battery_set || !m_battery_valid_set || !m_battery_valid ||
     ((m_curr_time - m_battery_time) > m_battery_max_age) ||
     ((m_curr_time - m_battery_valid_time) > m_battery_max_age) ||
     (m_battery < 0) || (m_battery > 100))
    return("UNKNOWN");
  if(m_battery < m_critical_battery)
    return("EMERGENCY");
  if(m_battery < m_min_battery)
    return("URGENT");
  return("NORMAL");
}

bool Rendezvous::mailIsTrue(const CMOOSMsg& msg) const
{
  if(msg.IsDouble()) {
    double value = msg.GetDouble();
    return(std::isfinite(value) && (value != 0));
  }
  string value = tolower(stripBlankEnds(msg.GetString()));
  return((value == "true") || (value == "1") || (value == "on"));
}

string Rendezvous::makeSessionID() const
{
  uint64_t usec = static_cast<uint64_t>(chrono::duration_cast<chrono::microseconds>
    (chrono::system_clock::now().time_since_epoch()).count());
  return(tolower(m_ownship) + "_" + to_string(usec));
}

string Rendezvous::pointString(double x, double y) const
{
  return("{" + doubleToStringX(x, 2) + "," +
         doubleToStringX(y, 2) + "}");
}

//------------------------------------------------------------
// Procedure: buildReport

bool Rendezvous::buildReport()
{
  m_msgs << "Role:               " << m_role << endl;
  m_msgs << "Ownship / peer:     " << m_ownship << " / " << m_peer_node << endl;
  m_msgs << "State:              " << m_state << endl;
  m_msgs << "Reason:             " << m_reason << endl;
  m_msgs << "Session:            " << m_session_id << endl;
  m_msgs << "Navigation fresh:   " << boolToString(navIsFresh()) << endl;
  m_msgs << "NAV X/Y:            " << doubleToStringX(m_nav_x, 2) << " / "
         << doubleToStringX(m_nav_y, 2) << endl;
  m_msgs << "Target set:         " << boolToString(m_target_set) << endl;
  if(m_target_set)
    m_msgs << "Target X/Y:         " << doubleToStringX(m_target_x, 2) << " / "
           << doubleToStringX(m_target_y, 2) << endl;

  if(m_role == "uav") {
    string ready_reason;
    bool ready = uavIsReady(ready_reason);
    m_msgs << "UAV ready:          " << boolToString(ready)
           << " (" << ready_reason << ")" << endl;
    m_msgs << "Battery:            ";
    if(m_battery_set)
      m_msgs << doubleToStringX(m_battery, 1) << "%" << endl;
    else
      m_msgs << "unknown" << endl;
    m_msgs << "Recovery priority:  " << recoveryPriority() << endl;
    m_msgs << "Route state:        " << m_route_state << endl;
    m_msgs << "At rendezvous:      " << boolToString(m_uav_arrived) << endl;
    m_msgs << "Clearance received: " << boolToString(m_clearance_received) << endl;
    m_msgs << "PEARL report fresh: " << boolToString(peerReportIsFresh()) << endl;
    m_msgs << "Landing gate ready: "
           << boolToString(m_landing_target_gate_ready) << endl;
    string target_reason;
    bool target_ready = landingTargetIsReady(target_reason);
    m_msgs << "Landing target:     " << boolToString(target_ready)
           << " (" << target_reason << ")" << endl;
    m_msgs << "Recovery speed:     "
           << doubleToStringX(m_recovery_route_speed, 2) << endl;
    m_msgs << "Recovery capture:   "
           << doubleToStringX(m_recovery_capture_radius, 2) << endl;
    m_msgs << "Secondary recovery: " << m_secondary_recovery_state << endl;
    m_msgs << "Secondary point:    "
           << doubleToStringX(m_secondary_landing_x, 2) << " / "
           << doubleToStringX(m_secondary_landing_y, 2) << endl;
  }
  else {
    m_msgs << "PEARL arrived:      " << boolToString(m_pearl_arrived) << endl;
    m_msgs << "UAV arrived:        " << boolToString(m_uav_arrived) << endl;
    m_msgs << "UAV report age:     ";
    if(m_peer_report_time > 0)
      m_msgs << doubleToStringX(m_curr_time - m_peer_report_time, 2) << endl;
    else
      m_msgs << "n/a" << endl;
  }

  string platform_reason;
  bool platform_ready = landingPlatformIsReady(platform_reason);
  m_msgs << "PEARL platform:     " << boolToString(platform_ready)
         << " (" << platform_reason << ")" << endl;
  m_msgs << "PEARL battery/wind: " << doubleToStringX(m_pearl_battery, 1)
         << "% / " << doubleToStringX(m_pearl_wind_speed, 1) << endl;

  ACTable table(2);
  table << "Event | Count";
  table.addHeaderLines();
  table << "Requests sent" << m_requests_sent;
  table << "Proposals sent" << m_proposals_sent;
  table << "Acceptances sent" << m_acceptances_sent;
  table << "Clearances sent" << m_clearances_sent;
  table << "Aborts" << m_aborts;
  m_msgs << endl << table.getFormattedString();
  return(true);
}
