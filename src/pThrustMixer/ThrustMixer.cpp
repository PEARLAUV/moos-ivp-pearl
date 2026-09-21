/************************************************************/
/*    NAME: PEARL Project                                   */
/*    FILE: ThrustMixer.cpp                                 */
/************************************************************/

#include <algorithm>
#include <cstdlib>
#include "MBUtils.h"
#include "ThrustMixer.h"

using namespace std;

ThrustMixer::ThrustMixer()
{
  m_input_thrust_var = "DESIRED_THRUST";
  m_input_rudder_var = "DESIRED_RUDDER";
  m_output_left_var  = "DESIRED_THRUST_L";
  m_output_right_var = "DESIRED_THRUST_R";
  m_state_var        = "THRUST_MIXER_STATE";
  m_max_thrust       = 100;
  m_max_rudder       = 50;
  m_stale_timeout    = 1;

  m_desired_thrust  = 0;
  m_desired_rudder  = 0;
  m_left_thrust     = 0;
  m_right_thrust    = 0;
  m_last_thrust_time = 0;
  m_last_rudder_time = 0;
  m_have_thrust      = false;
  m_have_rudder      = false;
  m_state            = "WAITING_FOR_INPUT";
}

bool ThrustMixer::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  for(const CMOOSMsg& msg : NewMail) {
    const string key = msg.GetKey();

    if(key == m_input_thrust_var) {
      if(msg.IsDouble()) {
        m_desired_thrust = msg.GetDouble();
        m_last_thrust_time = msg.GetTime();
        m_have_thrust = true;
      }
      else
        reportRunWarning("Non-numeric mail on " + m_input_thrust_var);
    }
    else if(key == m_input_rudder_var) {
      if(msg.IsDouble()) {
        m_desired_rudder = msg.GetDouble();
        m_last_rudder_time = msg.GetTime();
        m_have_rudder = true;
      }
      else
        reportRunWarning("Non-numeric mail on " + m_input_rudder_var);
    }
    else if(key != "APPCAST_REQ")
      reportRunWarning("Unhandled Mail: " + key);
  }

  return(true);
}

bool ThrustMixer::Iterate()
{
  AppCastingMOOSApp::Iterate();

  const bool have_inputs = m_have_thrust && m_have_rudder;
  const bool fresh_thrust = have_inputs &&
    ((m_curr_time - m_last_thrust_time) <= m_stale_timeout);
  const bool fresh_rudder = have_inputs &&
    ((m_curr_time - m_last_rudder_time) <= m_stale_timeout);

  if(fresh_thrust && fresh_rudder) {
    mix(m_desired_thrust, m_desired_rudder,
        m_left_thrust, m_right_thrust);
    m_state = "ACTIVE";
  }
  else {
    m_left_thrust = 0;
    m_right_thrust = 0;
    m_state = have_inputs ? "STALE_INPUT" : "WAITING_FOR_INPUT";
  }

  Notify(m_output_left_var, m_left_thrust);
  Notify(m_output_right_var, m_right_thrust);
  Notify(m_state_var, m_state);

  AppCastingMOOSApp::PostReport();
  return(true);
}

bool ThrustMixer::OnConnectToServer()
{
  registerVariables();
  return(true);
}

bool ThrustMixer::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();

  STRING_LIST config_lines;
  if(!m_MissionReader.GetConfiguration(GetAppName(), config_lines))
    reportConfigWarning("No config block found for " + GetAppName());

  for(string line : config_lines) {
    string original = line;
    string param = tolower(biteStringX(line, '='));
    string value = stripBlankEnds(line);
    bool handled = false;

    if(param == "input_thrust_var")
      handled = setNonWhiteVarOnString(m_input_thrust_var, value);
    else if(param == "input_rudder_var")
      handled = setNonWhiteVarOnString(m_input_rudder_var, value);
    else if(param == "output_left_var")
      handled = setNonWhiteVarOnString(m_output_left_var, value);
    else if(param == "output_right_var")
      handled = setNonWhiteVarOnString(m_output_right_var, value);
    else if(param == "state_var")
      handled = setNonWhiteVarOnString(m_state_var, value);
    else if((param == "max_thrust") && isNumber(value)) {
      m_max_thrust = atof(value.c_str());
      handled = (m_max_thrust > 0);
    }
    else if((param == "max_rudder") && isNumber(value)) {
      m_max_rudder = atof(value.c_str());
      handled = (m_max_rudder > 0);
    }
    else if((param == "stale_timeout") && isNumber(value)) {
      m_stale_timeout = atof(value.c_str());
      handled = (m_stale_timeout > 0);
    }

    if(!handled)
      reportUnhandledConfigWarning(original);
  }

  registerVariables();
  return(true);
}

void ThrustMixer::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  Register(m_input_thrust_var, 0);
  Register(m_input_rudder_var, 0);
}

void ThrustMixer::mix(double thrust, double rudder,
                      double& left, double& right) const
{
  thrust = std::max(-m_max_thrust, std::min(thrust, m_max_thrust));
  rudder = std::max(-m_max_rudder, std::min(rudder, m_max_rudder));

  left  = thrust + rudder;
  right = thrust - rudder;

  // Preserve differential authority when either side exceeds the motor
  // range. This is the same overage handling used by iPEARL.
  if(left > m_max_thrust)
    right -= left - m_max_thrust;
  if(left < -m_max_thrust)
    right -= left + m_max_thrust;
  if(right > m_max_thrust)
    left -= right - m_max_thrust;
  if(right < -m_max_thrust)
    left -= right + m_max_thrust;

  left  = std::max(-m_max_thrust, std::min(left, m_max_thrust));
  right = std::max(-m_max_thrust, std::min(right, m_max_thrust));
}

bool ThrustMixer::buildReport()
{
  m_msgs << "Input thrust:  " << m_input_thrust_var
         << " = " << m_desired_thrust << endl;
  m_msgs << "Input rudder:  " << m_input_rudder_var
         << " = " << m_desired_rudder << endl;
  m_msgs << "Output left:   " << m_output_left_var
         << " = " << m_left_thrust << endl;
  m_msgs << "Output right:  " << m_output_right_var
         << " = " << m_right_thrust << endl;
  m_msgs << "Limits:        thrust=" << m_max_thrust
         << ", rudder=" << m_max_rudder << endl;
  m_msgs << "Stale timeout: " << m_stale_timeout << endl;
  m_msgs << "State:         " << m_state << endl;
  return(true);
}
