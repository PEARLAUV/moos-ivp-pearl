#ifndef VIDEO_RECORDER_HEADER
#define VIDEO_RECORDER_HEADER

#include <map>
#include <string>
#include <vector>

#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"

class VideoRecorder : public AppCastingMOOSApp
{
public:
  VideoRecorder();

protected:
  bool OnStartUp() override;
  bool OnConnectToServer() override;
  bool OnNewMail(MOOSMSG_LIST &mail) override;
  bool Iterate() override;
  bool buildReport() override;

private:
  struct PathState
  {
    std::string name;
    std::string api_name;
    std::string proxy_api_name;
    std::string status;
    std::string activation_command;
    int http_status = 0;
  };

  struct HttpResponse
  {
    int status = 0;
    std::string body;
    std::string error;
  };

  void registerVariables();
  void handleCommand(const std::string &command);
  void beginStart();
  void beginStop();
  void processOperation();
  void finishOperation();
  void setState(const std::string &state);
  void publishConfiguration();
  void publishResult(const std::string &action, const std::string &result,
                     const std::string &detail = "");
  bool prepareRecordingDirectory(std::string &error);
  bool patchPath(const PathState &path, bool record, HttpResponse &response);
  bool loadPassword(std::string &error);
  bool parseApiUrl(std::string &error);

  static std::string directoryBasename(const std::string &path);
  static bool validDirectoryName(const std::string &name);
  static std::string trimTrailingSlashes(const std::string &path);
  static std::string urlEncode(const std::string &value);
  static std::string jsonEscape(const std::string &value);
  static std::string base64Encode(const std::string &value);

  HttpResponse request(const std::string &method, const std::string &target,
                       const std::string &body) const;

  std::string m_api_url;
  std::string m_api_host;
  std::string m_api_port;
  std::string m_api_base_path;
  std::string m_api_user;
  std::string m_api_password;
  std::string m_api_password_file;
  std::string m_record_root;
  std::string m_record_host;
  std::string m_segment_duration;
  std::string m_proxy_rtsp_address;
  std::string m_command_var;
  std::string m_logger_directory_var;
  std::string m_recording_id_var;
  std::string m_recording_id_prefix;
  std::string m_prefix;
  std::string m_logger_directory;
  std::string m_recording_id;
  std::string m_log_directory_name;
  std::string m_recording_id_latched;
  std::string m_record_directory;
  std::string m_state;
  std::string m_operation;
  std::string m_last_error;
  std::string m_terminal_error;
  std::vector<PathState> m_paths;
  std::map<std::string, std::string> m_activation_commands;
  std::map<std::string, std::string> m_record_proxies;
  std::size_t m_path_index;
  double m_http_timeout;
  bool m_valid;
  bool m_logger_received;
  bool m_recording_id_received;
  bool m_cleanup_after_start_failure;
  bool m_api_reachable;
  int m_last_http_status;
  unsigned int m_start_count;
  unsigned int m_stop_count;
  unsigned int m_recording_sequence;
};

#endif
