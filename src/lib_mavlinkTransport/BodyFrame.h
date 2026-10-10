#ifndef MAVLINK_BODY_FRAME_HEADER
#define MAVLINK_BODY_FRAME_HEADER

#include <array>
#include <cmath>

// NED and aircraft FRD, Euler angles in degrees, R = Rz(yaw) Ry(pitch) Rx(roll).
// Pure geometry: callers must supply fresh, finite native attitude, not COG.
namespace bodyframe
{
using Vector = std::array<double, 3>;
using Quaternion = std::array<double, 4>; // w, x, y, z; target-local -> body
using Matrix = std::array<Vector, 3>;
constexpr double radians = 3.14159265358979323846 / 180.0;
inline Matrix bodyToNed(double roll, double pitch, double yaw)
{
  const double cr = std::cos(roll * radians), sr = std::sin(roll * radians);
  const double cp = std::cos(pitch * radians), sp = std::sin(pitch * radians);
  const double cy = std::cos(yaw * radians), sy = std::sin(yaw * radians);
  return {{{{cp * cy, sr * sp * cy - cr * sy, cr * sp * cy + sr * sy}},
           {{cp * sy, sr * sp * sy + cr * cy, cr * sp * sy - sr * cy}},
           {{-sp, sr * cp, cr * cp}}}};
}
inline Vector toNed(const Matrix &rotation, const Vector &body)
{
  Vector result{};
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = 0; j < 3; ++j)
      result[i] += rotation[i][j] * body[j];
  return result;
}
inline Vector toBody(const Matrix &rotation, const Vector &ned)
{
  Vector result{};
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = 0; j < 3; ++j)
      result[i] += rotation[j][i] * ned[j];
  return result;
}
inline Quaternion targetToBody(double roll, double pitch, double yaw, double target_yaw)
{
  // q_body_to_ned^-1 * q_target_to_ned (a level platform with true-north yaw).
  const double cr = std::cos(roll * radians / 2), sr = std::sin(roll * radians / 2);
  const double cp = std::cos(pitch * radians / 2), sp = std::sin(pitch * radians / 2);
  const double cy = std::cos(yaw * radians / 2), sy = std::sin(yaw * radians / 2);
  const double w = cr * cp * cy + sr * sp * sy;
  const double x = -(sr * cp * cy - cr * sp * sy);
  const double y = -(cr * sp * cy + sr * cp * sy);
  const double z = -(cr * cp * sy - sr * sp * cy);
  const double tw = std::cos(target_yaw * radians / 2), tz = std::sin(target_yaw * radians / 2);
  return {{w * tw - z * tz, x * tw + y * tz, y * tw - x * tz, w * tz + z * tw}};
}
inline Vector rotate(const Quaternion &q, const Vector &v)
{
  const double w = q[0], x = q[1], y = q[2], z = q[3];
  return {
      {(1 - 2 * (y * y + z * z)) * v[0] + 2 * (x * y - w * z) * v[1] + 2 * (x * z + w * y) * v[2],
       2 * (x * y + w * z) * v[0] + (1 - 2 * (x * x + z * z)) * v[1] + 2 * (y * z - w * x) * v[2],
       2 * (x * z - w * y) * v[0] + 2 * (y * z + w * x) * v[1] + (1 - 2 * (x * x + y * y)) * v[2]}};
}
} // namespace bodyframe
#endif
