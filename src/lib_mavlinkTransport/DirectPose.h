#ifndef MAVLINK_DIRECT_POSE_HEADER
#define MAVLINK_DIRECT_POSE_HEADER
#include "BodyFrame.h"
#include "MavlinkReceive.h"
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>

namespace mavlink
{
inline uint32_t readU32(const Payload &p, size_t at)
{
  uint32_t v = 0;
  for (unsigned i = 0; i < 4; ++i)
    if (at + i < p.size())
      v |= uint32_t(p[at + i]) << (8 * i);
  return v;
}
inline float readFloat(const Payload &p, size_t at)
{
  const uint32_t bits = readU32(p, at);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}
inline int32_t readI32(const Payload &p, size_t at)
{
  const uint32_t bits = readU32(p, at);
  int32_t v;
  std::memcpy(&v, &bits, 4);
  return v;
}
struct NativePose
{
  uint64_t stamp = 0, position_stamp = 0;
  double roll = 0, pitch = 0, yaw = 0, lat = 0, lon = 0, msl = 0;
  double received = -1, received_moos = -1;
};
// Native samples only. No extrapolation, guessed clock offset, position borrowing,
// or stale MOOS fallback. Pair actual FC boot timestamps within a bounded skew.
class DirectPose
{
  struct Att
  {
    double roll = 0, pitch = 0, yaw = 0, received = -1, moos = -1;
  };
  struct Pos
  {
    double lat = 0, lon = 0, msl = 0, received = -1;
  };

public:
  void clear()
  {
    m_att.clear();
    m_pos.clear();
    m_clock = 0;
    m_quaternion = false;
    m_window.clear();
  }
  void receive(const Frame &f, unsigned system, unsigned component, double now, double moos)
  {
    if (f.system != system || f.component != component || (f.id != 30 && f.id != 31 && f.id != 33))
      return;
    if (f.payload.size() < 4)
      return;
    const uint32_t stamp = readU32(f.payload, 0);
    if (!stamp)
      return;
    if (m_clock > 1000 && stamp < m_clock - 1000)
      clear();
    m_clock = std::max(m_clock, stamp);
    if (f.id == 33) {
      if (f.payload.size() < 16)
        return;
      Pos p;
      p.lat = readI32(f.payload, 4) * 1e-7;
      p.lon = readI32(f.payload, 8) * 1e-7;
      p.msl = readI32(f.payload, 12) * 0.001;
      p.received = now;
      if (std::abs(p.lat) >= 90 || std::abs(p.lon) > 180 || p.msl < -1000 || p.msl > 10000)
        return;
      m_pos.emplace(stamp, p);
    } else {
      Att a;
      a.received = now;
      a.moos = moos;
      if (f.id == 31) {
        // MAVLink2 trims trailing zero bytes; omitted payload bytes decode as zero.
        bodyframe::Quaternion q{{readFloat(f.payload, 4), readFloat(f.payload, 8),
                                 readFloat(f.payload, 12), readFloat(f.payload, 16)}};
        double norm = 0;
        for (double v : q) {
          if (!std::isfinite(v))
            return;
          norm += v * v;
        }
        if (norm < 0.25 || norm > 2.25)
          return;
        for (auto &v : q)
          v /= std::sqrt(norm);
        a.roll = std::atan2(2 * (q[0] * q[1] + q[2] * q[3]), 1 - 2 * (q[1] * q[1] + q[2] * q[2])) /
                 bodyframe::radians;
        a.pitch = std::asin(std::max(-1.0, std::min(1.0, 2 * (q[0] * q[2] - q[3] * q[1])))) /
                  bodyframe::radians;
        a.yaw = std::atan2(2 * (q[0] * q[3] + q[1] * q[2]), 1 - 2 * (q[2] * q[2] + q[3] * q[3])) /
                bodyframe::radians;
        if (!m_quaternion) {
          m_att.clear();
          m_window.clear();
          m_quaternion = true;
        }
      } else {
        if (m_quaternion || f.payload.size() < 16)
          return;
        a.roll = readFloat(f.payload, 4) / bodyframe::radians;
        a.pitch = readFloat(f.payload, 8) / bodyframe::radians;
        a.yaw = readFloat(f.payload, 12) / bodyframe::radians;
        if (!std::isfinite(a.roll) || !std::isfinite(a.pitch) || !std::isfinite(a.yaw))
          return;
      }
      m_att.emplace(stamp, a); // Replays cannot renew receipt time.
    }
    while (m_att.size() > 128)
      m_att.erase(m_att.begin());
    while (m_pos.size() > 128)
      m_pos.erase(m_pos.begin());
  }
  bool pose(double now, double timeout, unsigned max_skew_ms, NativePose &out)
  {
    if (m_att.empty() || m_pos.empty()) {
      m_reason = "missing_native_attitude_or_position";
      return false;
    }
    // Prefer the newest complete native pair. Never reuse a stale pair just
    // because packets keep arriving with different/unmatched timestamps.
    for (auto p = m_pos.rbegin(); p != m_pos.rend(); ++p) {
      auto a = m_att.lower_bound(p->first);
      auto best = a;
      if (a != m_att.begin()) {
        auto previous = a;
        --previous;
        if (a == m_att.end() || p->first - previous->first <= a->first - p->first)
          best = previous;
      }
      if (best == m_att.end())
        continue;
      const uint32_t skew =
          best->first > p->first ? best->first - p->first : p->first - best->first;
      if (skew > max_skew_ms)
        continue;
      const double received = std::min(best->second.received, p->second.received);
      if (now < received || now - received > timeout ||
          (m_clock - std::min(best->first, p->first)) * 0.001 > timeout)
        continue;
      out.stamp = uint64_t(best->first) * 1000;
      out.position_stamp = uint64_t(p->first) * 1000;
      out.roll = best->second.roll;
      out.pitch = best->second.pitch;
      out.yaw = best->second.yaw;
      out.lat = p->second.lat;
      out.lon = p->second.lon;
      out.msl = p->second.msl;
      out.received = received;
      out.received_moos = best->second.moos;
      m_window.emplace(out.stamp, out);
      out = m_window.at(out.stamp); // One immutable pose per observation clock ID.
      if (now - out.received > timeout)
        continue;
      while (m_window.size() > 128)
        m_window.erase(m_window.begin());
      m_reason = "ready";
      return true;
    }
    m_reason = "native_pose_stale_or_timestamp_skew";
    return false;
  }
  std::string window(double now) const
  {
    std::ostringstream s;
    s << std::setprecision(17);
    bool first = true;
    for (const auto &item : m_window) {
      const auto &p = item.second;
      if (now - p.received > 1.0 || now < p.received)
        continue;
      if (!first)
        s << ';';
      first = false;
      s << p.stamp << ',' << p.roll << ',' << p.pitch << ',' << p.yaw << ',' << p.lat << ','
        << p.lon << ',' << p.msl << ',' << p.received_moos << ',' << p.position_stamp;
    }
    return s.str();
  }
  const std::string &reason() const
  {
    return m_reason;
  }

private:
  std::map<uint32_t, Att> m_att;
  std::map<uint32_t, Pos> m_pos;
  std::map<uint64_t, NativePose> m_window;
  uint32_t m_clock = 0;
  bool m_quaternion = false;
  std::string m_reason = "not_received";
};
// Public, independently received native pose window, shared by viewer/coordinator.
inline bool readPoseWindow(const std::string &wire, std::map<uint64_t, NativePose> &poses)
{
  poses.clear();
  if (wire.empty() || wire.back() == ';')
    return false;
  std::istringstream input(wire);
  std::string row;
  uint64_t previous = 0;
  while (std::getline(input, row, ';')) {
    if (row.empty() || row.back() == ',') {
      poses.clear();
      return false;
    }
    std::istringstream fields(row);
    double v[9];
    std::string field;
    for (unsigned i = 0; i < 9; ++i) {
      if (!std::getline(fields, field, ',') || field.empty()) {
        poses.clear();
        return false;
      }
      try {
        size_t end = 0;
        v[i] = std::stod(field, &end);
        if (end != field.size() || !std::isfinite(v[i]))
          throw std::invalid_argument("pose");
      } catch (const std::exception &) {
        poses.clear();
        return false;
      }
    }
    if (std::getline(fields, field, ',') || v[0] <= previous || v[0] >= 9007199254740992.0 ||
        std::floor(v[0]) != v[0] || std::abs(v[4]) >= 90 || std::abs(v[5]) > 180 || v[6] < -1000 ||
        v[6] > 10000 || v[7] < 0 || v[8] <= 0 || v[8] >= 9007199254740992.0 ||
        std::floor(v[8]) != v[8] || std::abs(v[8] - v[0]) > 50000 || poses.size() >= 128) {
      poses.clear();
      return false;
    }
    NativePose p;
    p.stamp = uint64_t(v[0]);
    p.roll = v[1];
    p.pitch = v[2];
    p.yaw = v[3];
    p.lat = v[4];
    p.lon = v[5];
    p.msl = v[6];
    p.received_moos = v[7];
    p.position_stamp = uint64_t(v[8]);
    poses.emplace(p.stamp, p);
    previous = p.stamp;
  }
  return !poses.empty();
}
} // namespace mavlink
#endif
