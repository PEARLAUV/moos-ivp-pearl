/************************************************************/
/*    NAME: PEARL Project                                   */
/*    FILE: ThrustMixer.h                                   */
/************************************************************/

#ifndef ThrustMixer_HEADER
#define ThrustMixer_HEADER

#include <string>
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"

class ThrustMixer : public AppCastingMOOSApp
{
 public:
   ThrustMixer();
   ~ThrustMixer() = default;

 protected:
   bool OnNewMail(MOOSMSG_LIST &NewMail) override;
   bool Iterate() override;
   bool OnConnectToServer() override;
   bool OnStartUp() override;
   bool buildReport() override;

 private:
   void registerVariables();
   void mix(double thrust, double rudder, double& left, double& right) const;

 private: // Configuration
   std::string m_input_thrust_var;
   std::string m_input_rudder_var;
   std::string m_output_left_var;
   std::string m_output_right_var;
   std::string m_state_var;
   double      m_max_thrust;
   double      m_max_rudder;
   double      m_stale_timeout;

 private: // State
   double      m_desired_thrust;
   double      m_desired_rudder;
   double      m_left_thrust;
   double      m_right_thrust;
   double      m_last_thrust_time;
   double      m_last_rudder_time;
   bool        m_have_thrust;
   bool        m_have_rudder;
   std::string m_state;
};

#endif
