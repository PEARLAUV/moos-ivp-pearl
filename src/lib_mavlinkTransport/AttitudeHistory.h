#ifndef MAVLINK_ATTITUDE_HISTORY_HEADER
#define MAVLINK_ATTITUDE_HISTORY_HEADER
#include "BodyFrame.h"
#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

// Exact native attitude-sample matching for emulated camera exposure timestamps.
// No interpolation, clock guessing, extrapolation, or manufactured attitude.
namespace bodyframe
{
struct Pose
{
  Matrix rotation{};
  double received_moos_time = -1;
  double roll = 0, pitch = 0, yaw = 0;
};
class AttitudeHistory
{
public:
  void remember(uint64_t stamp, double roll, double pitch, double yaw, double received)
  {
    if (!stamp)
      return;
    if (!m_samples.empty() && stamp < m_samples.rbegin()->first)
      m_samples.clear(); // FC reboot / source-clock rewind: discard old generations.
    Pose pose;
    pose.rotation = bodyToNed(roll, pitch, yaw);
    pose.received_moos_time = received;
    pose.roll = roll;
    pose.pitch = pitch;
    pose.yaw = yaw;
    m_samples.emplace(stamp, pose);
    while (m_samples.size() > 128)
      m_samples.erase(m_samples.begin());
  }
  bool find(uint64_t stamp, Pose &pose) const
  {
    const auto it = m_samples.find(stamp);
    if (it == m_samples.end())
      return false;
    pose = it->second;
    return true;
  }
  // Atomic native window: stamp,roll,pitch,yaw,original_receipt;... .
  // Import every original sample, not just the final scalar values of a mail
  // batch. Replacing the window also discards prior FC clock generations.
  bool receive(const std::string &wire)
  {
    std::map<uint64_t, Pose> parsed;
    std::istringstream stream(wire);
    std::string record;
    uint64_t previous = 0;
    while (std::getline(stream, record, ';')) {
      std::istringstream fields(record);
      double values[5];
      for (unsigned i = 0; i < 5; ++i) {
        std::string field;
        if (!std::getline(fields, field, ',') || field.empty()) {
          m_samples.clear();
          return false;
        }
        try {
          size_t end = 0;
          values[i] = std::stod(field, &end);
          if (end != field.size() || !std::isfinite(values[i]))
            throw std::invalid_argument("invalid native pose");
        } catch (const std::exception &) {
          m_samples.clear();
          return false;
        }
      }
      std::string extra;
      if (std::getline(fields, extra, ',') || values[0] <= previous ||
          values[0] >= 9007199254740992.0 || std::floor(values[0]) != values[0] || values[4] < 0 ||
          parsed.size() >= 128) {
        m_samples.clear();
        return false;
      }
      const auto stamp = static_cast<uint64_t>(values[0]);
      Pose pose;
      pose.roll = values[1];
      pose.pitch = values[2];
      pose.yaw = values[3];
      pose.rotation = bodyToNed(pose.roll, pose.pitch, pose.yaw);
      pose.received_moos_time = values[4];
      parsed.emplace(stamp, pose);
      previous = stamp;
    }
    m_samples.swap(parsed);
    return !m_samples.empty();
  }
  bool latest(uint64_t &stamp, Pose &pose) const
  {
    if (m_samples.empty())
      return false;
    stamp = m_samples.rbegin()->first;
    pose = m_samples.rbegin()->second;
    return true;
  }

private:
  std::map<uint64_t, Pose> m_samples;
};
} // namespace bodyframe
#endif
