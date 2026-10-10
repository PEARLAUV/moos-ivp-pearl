// Test-only atomic numeric pose publisher. No FC/socket/flight commands.
#include "MOOS/libMOOS/Comms/MOOSCommClient.h"
#include "MOOS/libMOOS/Utils/MOOSUtilityFunctions.h"
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>

int main(int argc, char **argv)
{
  if (argc < 3)
    return 2;
  CMOOSCommClient client;
  client.Run("127.0.0.1", std::atoi(argv[1]), "native_pose_fixture");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!client.IsConnected() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  if (!client.IsConnected())
    return 3;
  for (int i = 2; i < argc; ++i) {
    const std::string item = argv[i];
    const auto equals = item.find('=');
    if (equals == std::string::npos)
      return 4;
    std::string value = item.substr(equals + 1);
    const bool string_value = equals > 0 && item[equals - 1] == ':';
    const std::string key = item.substr(0, equals - (string_value ? 1 : 0));
    if (string_value || key == "UAV_ATTITUDE_SAMPLES") {
      std::ostringstream time;
      time << std::setprecision(17) << MOOSTime();
      size_t found;
      while ((found = value.find("@MOOSTIME")) != std::string::npos)
        value.replace(found, 9, time.str());
      client.Notify(key, value);
    } else
      client.Notify(item.substr(0, equals), value == "@MOOSTIME" ? MOOSTime() : std::stod(value));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  client.Close();
  return 0;
}
