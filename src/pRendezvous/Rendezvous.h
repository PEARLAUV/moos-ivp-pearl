#ifndef Rendezvous_HEADER
#define Rendezvous_HEADER

#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "RendezvousPlanner.h"
#include <string>

class Rendezvous : public AppCastingMOOSApp
{
public:
  Rendezvous();
  ~Rendezvous() override;

protected:
  bool OnNewMail(MOOSMSG_LIST &mail) override;
  bool Iterate() override;
  bool OnConnectToServer() override;
  bool OnStartUp() override;
  bool buildReport() override;

private:
  void registerVariables();
  bool handleStart(const CMOOSMsg &msg);
  bool handleAbort(const CMOOSMsg &msg);
  bool handleNodeReport(const CMOOSMsg &msg);
  bool handleMotion(const CMOOSMsg &msg);
  bool handleDouble(const CMOOSMsg &msg, double &value, double &stamp);
  bool handleBool(const CMOOSMsg &msg, bool &value);
  void updatePlanner();
  void adoptPeerFreeze(double x, double y, double freeze_time, const std::string &freeze_node);
  void publishMotion(bool force = false);
  void publishRoute(bool initial);
  void activateRoute();
  void complete(const std::string &reason);
  void abort(const std::string &reason);
  void publishState(bool force = false);
  void publishPoint(bool active);
  bool sendMessage(const std::string &destination, const std::string &variable,
                   const std::string &value);
  bool navFresh() const;
  bool peerFresh() const;
  bool isTrue(const CMOOSMsg &msg) const;
  std::string pointString() const;
  std::string ownVar(const std::string &suffix) const;
  std::string peerVar(const std::string &suffix) const;
  std::string makeSession() const;

private:
  std::string m_ownship;
  std::string m_peer_node;
  std::string m_state;
  std::string m_reason;
  std::string m_session;
  double m_nav_x = 0;
  double m_nav_y = 0;
  double m_nav_x_time = 0;
  double m_nav_y_time = 0;
  double m_peer_x = 0;
  double m_peer_y = 0;
  double m_peer_time = 0;
  double m_peer_phase_time = 0;
  double m_target_x = 0;
  double m_target_y = 0;
  double m_freeze_x = 0;
  double m_freeze_y = 0;
  double m_freeze_time = 0;
  std::string m_freeze_node;
  double m_nav_stale_thresh = 10;
  double m_transit_timeout = 300;
  double m_point_freeze_radius = 3;
  double m_planning_started_time = 0;
  double m_planning_history_window = 0; // opt-in; legacy behavior elsewhere
  double m_planning_max_positive_accel = 0.5;
  bool m_history_usable = true;
  RendezvousSpeedEstimator::History m_own_history, m_peer_history;
  double m_arrival_radius = 1;
  double m_arrival_dwell = 0;
  double m_state_post_interval = 1;
  double m_last_motion_time = 0;
  double m_last_state_time = 0;
  double m_state_enter_time = 0;
  double m_arrival_start = 0;
  double m_point_radius = 1;
  double m_point_transparency = 0.3;
  std::string m_point_color = "magenta";
  std::string m_point_shape = "point";
  bool m_point_label = false;
  bool m_nav_x_set = false;
  bool m_nav_y_set = false;
  bool m_peer_set = false;
  bool m_waypoint_reached = false;
  bool m_route_active = false;
  bool m_config_valid = true;
  unsigned int m_sessions = 0;
  unsigned int m_aborts = 0;
  RendezvousPlanner m_planner;
  RendezvousSpeedEstimator m_peer_speed_estimator;
};

#endif
