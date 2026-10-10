#ifndef RENDEZVOUS_PLANNER_HEADER
#define RENDEZVOUS_PLANNER_HEADER

#include <algorithm>
#include <cmath>
#include <deque>

// Estimate speed from the distance covered in recent position reports.
class RendezvousSpeedEstimator
{
public:
  struct Speed
  {
    bool valid = false;
    double value = 0;
    double window = 0;
  };

  struct History
  {
    bool usable = false;
    double window = 0;
    double max_positive_accel = 0;
    const char *reason = "missing_history";
  };

  // This is a qualification check, not a modification of measured speeds.
  // One-second path-speed averages avoid differentiating individual GPS steps.
  History history(double now, double window, double max_positive_accel,
                  double not_before = -1) const
  {
    History result;
    if (window == 0) {
      result.usable = true;
      result.reason = "disabled";
      return result;
    }
    if (!std::isfinite(now) || !std::isfinite(window) || window < 3 || window > 20 ||
        !std::isfinite(max_positive_accel) || max_positive_accel < 0)
      return result;
    if (m_samples.empty())
      return result;
    const double end = m_samples.back().time;
    result.window =
        std::max(0.0, std::min(window, end - std::max(m_samples.front().time, not_before)));
    constexpr double max_sample_gap = 1.5; // supports nominal 1 Hz peer motion
    if (end > now || now - end > max_sample_gap) {
      result.reason = "stale_history";
      return result;
    }
    const double start = end - window;
    if (m_samples.front().time > start + 1e-6 || start < not_before - 1e-6) {
      result.reason = "short_history";
      return result;
    }
    for (size_t i = 1; i < m_samples.size(); ++i) {
      if (m_samples[i].time > start && m_samples[i - 1].time < end &&
          m_samples[i].time - m_samples[i - 1].time > max_sample_gap) {
        result.reason = "history_gap";
        return result;
      }
    }
    double previous_speed = 0;
    bool have_speed = false;
    for (int seconds = static_cast<int>(window); seconds > 0; --seconds) {
      const double t = end - seconds;
      const double speed = distanceAt(t + 1) - distanceAt(t);
      if (!std::isfinite(speed) || speed < 0) {
        result.reason = "invalid_history";
        return result;
      }
      if (have_speed)
        result.max_positive_accel = std::max(result.max_positive_accel, speed - previous_speed);
      previous_speed = speed;
      have_speed = true;
    }
    result.usable = result.max_positive_accel <= max_positive_accel + 1e-9;
    result.reason = result.usable ? "usable" : "positive_acceleration";
    return result;
  }

  void reset(double time, double x, double y)
  {
    m_samples.clear();
    if (std::isfinite(time) && std::isfinite(x) && std::isfinite(y))
      m_samples.push_back({time, x, y, 0});
  }

  void sample(double time, double x, double y, double max_gap)
  {
    if (!std::isfinite(time) || !std::isfinite(x) || !std::isfinite(y) || m_samples.empty() ||
        time <= m_samples.back().time)
      return;
    const Sample previous = m_samples.back();
    if (time - previous.time > max_gap) {
      // Unobserved travel is not a usable speed measurement.
      reset(time, x, y);
      return;
    }
    m_samples.push_back(
        {time, x, y, previous.distance + std::hypot(x - previous.x, y - previous.y)});
    const double cutoff = time - 20;
    // Keep one sample preceding the window boundary for interpolation.
    while (m_samples.size() > 2 && m_samples[1].time <= cutoff)
      m_samples.pop_front();
  }

  Speed speed() const
  {
    Speed result;
    if (m_samples.size() < 2)
      return result;
    const Sample &first = m_samples.front();
    const Sample &last = m_samples.back();
    const double start = std::max(first.time, last.time - 20);
    double start_distance = first.distance;
    if (start > first.time) {
      const Sample &next = m_samples[1];
      start_distance +=
          (next.distance - first.distance) * (start - first.time) / (next.time - first.time);
    }
    result.window = last.time - start;
    if (result.window <= 0)
      return result;
    result.valid = true;
    result.value = (last.distance - start_distance) / result.window;
    return result;
  }

private:
  struct Sample
  {
    double time;
    double x;
    double y;
    double distance;
  };
  double distanceAt(double time) const
  {
    for (size_t i = 1; i < m_samples.size(); ++i) {
      if (time <= m_samples[i].time) {
        const Sample &before = m_samples[i - 1], &after = m_samples[i];
        const double fraction = (time - before.time) / (after.time - before.time);
        return before.distance + fraction * (after.distance - before.distance);
      }
    }
    return m_samples.back().distance;
  }

  std::deque<Sample> m_samples;
};

// Geometry and measured motion only: no vehicle roles or nominal speeds.
class RendezvousPlanner
{
public:
  using Speed = RendezvousSpeedEstimator::Speed;

  void reset(double time, double x, double y)
  {
    m_speed_estimator.reset(time, x, y);
    m_has_point = false;
    m_frozen = false;
    m_last_update = time;
  }

  void sample(double time, double x, double y, double max_gap)
  {
    m_speed_estimator.sample(time, x, y, max_gap);
  }

  Speed speed() const
  {
    return m_speed_estimator.speed();
  }

  RendezvousSpeedEstimator::History history(double now, double window, double max_accel,
                                            double not_before = -1) const
  {
    return m_speed_estimator.history(now, window, max_accel, not_before);
  }

  // Adopt a peer's earlier freeze as the canonical point, even if this
  // planner had already frozen its own candidate.
  bool adoptFrozenPoint(double x, double y)
  {
    if (!std::isfinite(x) || !std::isfinite(y))
      return false;
    m_x = x;
    m_y = y;
    m_has_point = true;
    m_frozen = true;
    return true;
  }

  // Return true only when a new point must be published to the local Helm.
  bool update(double time, double x, double y, double peer_x, double peer_y,
              const Speed &peer_speed, double freeze_radius, bool own_captured = false,
              bool history_usable = true)
  {
    if (!m_has_point) {
      m_x = (x + peer_x) / 2;
      m_y = (y + peer_y) / 2;
      m_has_point = true;
      m_last_update = time;
      return true;
    }
    // Bootstrap and unstable history cannot turn cached zero speed or capture
    // into a local freeze. A canonical peer freeze is never undone.
    if (m_frozen || !history_usable)
      return false;
    // Once ownship captures the point, keep it fixed through arrival dwell.
    // Also freeze slightly early within the configured capture buffer.
    if (own_captured || std::hypot(x - m_x, y - m_y) <= freeze_radius)
      m_frozen = true;
    if (m_frozen || time - m_last_update < 1)
      return false;
    m_last_update = time;
    const Speed own_speed = speed();
    constexpr double near_zero_speed = 0.1; // m/s
    double fraction = 0.5;
    if (own_speed.valid && peer_speed.valid && std::isfinite(own_speed.value) &&
        own_speed.value >= 0 && std::isfinite(peer_speed.value) && peer_speed.value >= 0) {
      if (peer_speed.value <= near_zero_speed && own_speed.value > near_zero_speed)
        fraction = 1.0; // Rendezvous at the nearly stationary peer.
      else if (own_speed.value <= near_zero_speed && peer_speed.value > near_zero_speed)
        fraction = 0.0; // Symmetric case: rendezvous at own position.
      else if (own_speed.value + peer_speed.value > 0) {
        // Equal travel times require own_distance/own_speed ==
        // peer_distance/peer_speed, so the own-side segment fraction is
        // own_speed/(own_speed+peer_speed).
        fraction = own_speed.value / (own_speed.value + peer_speed.value);
      }
    }
    const double next_x = x + fraction * (peer_x - x);
    const double next_y = y + fraction * (peer_y - y);
    if (std::hypot(next_x - m_x, next_y - m_y) < 0.001)
      return false;
    m_x = next_x;
    m_y = next_y;
    return true;
  }

  double x() const
  {
    return m_x;
  }
  double y() const
  {
    return m_y;
  }
  bool frozen() const
  {
    return m_frozen;
  }

private:
  RendezvousSpeedEstimator m_speed_estimator;
  double m_x = 0;
  double m_y = 0;
  double m_last_update = 0;
  bool m_has_point = false;
  bool m_frozen = false;
};

#endif
