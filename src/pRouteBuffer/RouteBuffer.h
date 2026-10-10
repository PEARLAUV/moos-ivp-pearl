/************************************************************/
/*    NAME: Charles Benjamin                                 */
/*    ORGN: MIT, Cambridge MA                                */
/*    FILE: RouteBuffer.h                                    */
/*    DATE: July 25th, 2026                                  */
/************************************************************/

#ifndef RouteBuffer_HEADER
#define RouteBuffer_HEADER

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "XYPoint.h"

class RouteBuffer : public AppCastingMOOSApp
{
 public:
   RouteBuffer();
   ~RouteBuffer();

 protected: // Standard MOOSApp functions to overload
   bool OnNewMail(MOOSMSG_LIST &NewMail);
   bool Iterate();
   bool OnConnectToServer();
   bool OnStartUp();

 protected: // Standard AppCastingMOOSApp function to overload
   bool buildReport();

 protected:
   void registerVariables();

 private:
   bool handleMailPoint(const CMOOSMsg&);
   bool handleMailTask(const CMOOSMsg&);
   void handleMailReturnPoint(const CMOOSMsg&);
   void postReturnPoint();
   bool taskPermitted() const;
   void cancelPendingTask(const std::string&);
   void clearStagedDrawing();
   bool handleMailTrigger(const CMOOSMsg&, const std::string&);
   bool handleMailGoto(const CMOOSMsg&);
   bool handleMailNodeReport(const CMOOSMsg&);
   bool handleMailCommand(const CMOOSMsg&);
   bool handleConfigRole(const std::string&);

   bool processPendingAction();
   bool processVehicleCommand(const std::string&, const std::string& source);
   bool sendMediatedCommand(const std::string&, const std::string& destination="");
   bool postRouteSnapshot(const std::string&, const std::string& speed="",
                          const std::string& capture_radius="",
                          const std::string& slip_radius="",
                          const std::string& route_name="",
                          const std::string& purpose="operator");

   void postPoint(const XYPoint&, unsigned int, bool);
   void postSegList(bool);
   void clearVisualization();
   void postState(const std::string&);

   std::string getPointsSpec() const;
   bool        mailIsTrue(const CMOOSMsg&) const;

 private: // Configuration variables
   std::string  m_role;
   std::string  m_destination_node;
   std::string  m_source_node;
   std::string  m_point_var;
   std::string  m_deploy_request_var;
   std::string  m_clear_request_var;
   std::string  m_goto_request_var;
   std::string  m_node_report_var;
   std::string  m_command_var;
   std::string  m_route_update_var;
   std::string  m_route_deploy_var;
   std::string  m_route_clear_var;
   std::string  m_route_ready_var;
   std::string  m_route_name;
   std::string  m_route_purpose_var;
   std::string  m_route_identity_var;
   std::string  m_operation_state_var;
   std::string  m_helm_state_var;
   std::string  m_recovery_source;
   std::set<std::string> m_operator_states;
   std::set<std::string> m_recovery_states;
   std::string  m_manual_override_var;
   unsigned int m_max_points;
   double       m_contact_max_age;
   double       m_deploy_delay;
   double       m_route_replay_delay;
   bool         m_live_append;

   // Optional shared-drawing/task policy; legacy deploy remains unchanged.
   std::string m_task_request_var = "ROUTE_BUFFER_TASK";
   std::string m_stage_clear_request_var;
   std::string m_return_point_var, m_return_update_var, m_task_return_destination;
   XYPoint m_return_point;
   bool m_have_return_point = false;
   double m_last_return_edit_time = -1, m_last_return_visual_time = -1;
   std::string m_deferred_return_update, m_committed_return_update;
   std::set<std::string> m_task_destinations;
   std::map<std::string, std::vector<std::pair<std::string, std::string>>> m_task_submit_flags;
   std::map<std::string, std::set<std::string>> m_task_guards;
   std::string m_task_execute_var, m_task_started_var, m_task_started_value;
   std::string m_ready_name_var, m_ready_name, m_ready_name_prefix;
   double m_ready_name_time = -1;
   struct TaskInput {
     TaskInput(const std::string& v="", double t=-1) : value(v), time(t) {}
     std::string value;
     double time;
   };
   std::map<std::string, TaskInput> m_task_inputs;
   std::set<std::string> m_seen_tasks;
   double m_started = 0, m_route_ready_time = -1, m_task_requested_time = -1;
   bool m_task_start_pending = false, m_task_started = false;

 private: // State variables
   std::vector<XYPoint> m_points;
   std::map<std::string, XYPoint> m_contact_points;
   std::map<std::string, double>  m_contact_times;
   std::deque<std::string> m_pending_actions;
   std::deque<std::pair<std::string, std::string>> m_pending_commands;
   std::string          m_state;
   std::string          m_last_submitted_points;
   unsigned int         m_max_visualized_points;
   unsigned int         m_commands_sent;
   unsigned int         m_commands_received;
   unsigned int         m_commands_rejected;
   bool                 m_config_valid;
   bool                 m_sync_sent;
   bool                 m_route_ready;
   bool                 m_route_deployed;
   bool                 m_vehicle_deploy_pending;
   double               m_vehicle_deploy_pending_since;
   bool                 m_vehicle_route_replay_pending;
   double               m_vehicle_route_replay_pending_since;
   std::string          m_vehicle_pending_route_update;
   std::string          m_vehicle_active_route_name;
   std::string          m_vehicle_route_purpose;
   std::string          m_operation_state;
   std::string          m_helm_state;
   std::string          m_deferred_helm_update;
};

#endif
