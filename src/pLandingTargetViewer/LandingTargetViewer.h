#ifndef LANDING_TARGET_VIEWER_HEADER
#define LANDING_TARGET_VIEWER_HEADER

#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "MOOS/libMOOSGeodesy/MOOSGeodesy.h"
#include "AttitudeHistory.h"
#include "DirectPose.h"
#include <map>
#include <string>

// Display received BODY_FRD observations only. Never reads simulator target geometry.
class LandingTargetViewer : public AppCastingMOOSApp
{
public:
  ~LandingTargetViewer() override;

protected:
  bool OnStartUp() override;
  bool OnConnectToServer() override;
  bool OnNewMail(MOOSMSG_LIST &) override;
  bool Iterate() override;
  bool buildReport() override;

private:
  struct Input
  {
    double value = 0;
    double time = -1;
    bool known = false;
  };
  void registerVariables();
  bool read(const std::string &, double &) const;
  bool drawTarget();
  void postRelativeVehicle();
  bool project(double lat, double lon, double north, double east, double &x, double &y);

  CMOOSGeodesy m_geodesy;
  std::map<std::string, Input> m_inputs;
  bodyframe::AttitudeHistory m_attitude_history;
  std::string m_pose_samples_var;
  std::map<uint64_t, mavlink::NativePose> m_observation_poses;
  std::string m_pose_source = "latest native attitude";
  std::string m_target_prefix = "UAV_LANDING_TARGET";
  std::string m_vehicle_prefix = "UAV";
  std::string m_label = "received_landing_target";
  std::string m_color = "magenta";
  std::string m_status = "waiting for observations";
  std::string m_size_source = "configured";
  std::string m_orientation_source = "configured";
  bool m_relative_projection = false;
  double m_relative_anchor_x = 0;
  double m_relative_anchor_y = 0;
  double m_observation_timeout = 0.5; // Real seconds, matching bridge-reported age.
  double m_input_timeout = 2;         // MOOS seconds for relay/pose inputs.
  double m_target_size = 1;           // Metres when angular sizes are absent.
  double m_fallback_heading = 0;      // Local-grid degrees if quaternion is absent.
  double m_age = -1;
  double m_x = 0, m_y = 0, m_width = 1, m_length = 1, m_heading = 0;
  bool m_native_samples_seen = false;
  bool m_use_received_size = true;
  bool m_valid = false;
  bool m_active = false;
};
#endif
