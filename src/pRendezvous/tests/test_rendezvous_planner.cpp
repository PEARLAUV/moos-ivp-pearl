#include "../RendezvousPlanner.h"

#include <cassert>
#include <cmath>

int main()
{
  RendezvousSpeedEstimator own_speed;
  RendezvousSpeedEstimator peer_speed;
  own_speed.reset(0, 0, 0);
  peer_speed.reset(0, 200, 0);
  own_speed.sample(1, 3, 0, 10);
  peer_speed.sample(1, 199, 0, 10);

  RendezvousPlanner planner;
  planner.reset(0, 0, 0);
  const RendezvousPlanner::Speed no_speed;
  assert(planner.update(0, 0, 0, 200, 0, no_speed, 0));
  assert(std::abs(planner.x() - 100) < 1e-9); // Initial estimate is midpoint.

  // For own speed 3 m/s and peer speed 1 m/s, the equal-arrival point is
  // three quarters of the segment from our current position.
  planner.sample(1, 3, 0, 10);
  assert(planner.update(1, 3, 0, 199, 0, peer_speed.speed(), 0));
  assert(std::abs(planner.x() - 150) < 1e-9);
  assert(std::abs((planner.x() - 3) / own_speed.speed().value -
                  (199 - planner.x()) / peer_speed.speed().value) < 1e-9);

  RendezvousSpeedEstimator stopped_peer;
  stopped_peer.reset(0, 200, 0);
  stopped_peer.sample(1, 200, 0, 10);
  RendezvousPlanner fast_own_planner;
  fast_own_planner.reset(0, 0, 0);
  assert(fast_own_planner.update(0, 0, 0, 200, 0, no_speed, 0));
  fast_own_planner.sample(1, 3, 0, 10);
  assert(fast_own_planner.update(1, 3, 0, 200, 0, stopped_peer.speed(), 0));
  assert(std::abs(fast_own_planner.x() - 200) < 1e-9);

  RendezvousSpeedEstimator stopped_own;
  stopped_own.reset(0, 0, 0);
  stopped_own.sample(1, 0, 0, 10);
  RendezvousPlanner fast_peer_planner;
  fast_peer_planner.reset(0, 0, 0);
  assert(fast_peer_planner.update(0, 0, 0, 200, 0, no_speed, 0));
  fast_peer_planner.sample(1, 0, 0, 10);
  RendezvousSpeedEstimator fast_peer;
  fast_peer.reset(0, 200, 0);
  fast_peer.sample(1, 197, 0, 10);
  assert(fast_peer_planner.update(1, 0, 0, 197, 0, fast_peer.speed(), 0));
  assert(std::abs(fast_peer_planner.x()) < 1e-9);

  // A captured ownship must keep its current rendezvous point even if the
  // peer is stationary and the near-zero rule would otherwise move it.
  RendezvousPlanner captured_planner;
  captured_planner.reset(0, 0, 0);
  assert(captured_planner.update(0, 0, 0, 200, 0, no_speed, 0));
  captured_planner.sample(1, 3, 0, 10);
  assert(!captured_planner.update(1, 3, 0, 200, 0, stopped_peer.speed(), 0, true));
  assert(std::abs(captured_planner.x() - 100) < 1e-9);

  // A peer's earlier freeze overrides an already-frozen local candidate.
  assert(captured_planner.adoptFrozenPoint(105, 12));
  assert(captured_planner.frozen());
  assert(std::abs(captured_planner.x() - 105) < 1e-9);
  assert(std::abs(captured_planner.y() - 12) < 1e-9);
  assert(!captured_planner.update(2, 3, 0, 200, 0, stopped_peer.speed(), 0));
  assert(std::abs(captured_planner.x() - 105) < 1e-9);

  // Near-point startup and capture cannot bypass the history gate.
  RendezvousPlanner warming;
  RendezvousSpeedEstimator moving;
  warming.reset(0, 0, 0);
  moving.reset(0, 10, 0);
  assert(warming.update(0, 0, 0, 10, 0, no_speed, 6, false, false));
  for (int tick = 1; tick <= 12; ++tick) {
    const double t = tick / 4.0;
    warming.sample(t, 0, 0, 10);
    moving.sample(t, 10 - t, 0, 10);
    const bool ready = warming.history(t, 3, 0.5).usable && moving.history(t, 3, 0.5).usable;
    assert(ready == (tick == 12));
    assert(!warming.update(t, 0, 0, 10 - t, 0, moving.speed(), 6, true, ready));
    assert(warming.frozen() == ready);
    assert(std::abs(warming.x() - 5) < 1e-9);
  }
  // Unusable history never undoes a canonical freeze, including an earlier peer's.
  assert(!warming.update(4, 0, 0, 6, 0, moving.speed(), 6, false, false));
  assert(warming.frozen());
  assert(warming.adoptFrozenPoint(4, 1));
  assert(!warming.update(5, 0, 0, 6, 0, moving.speed(), 6, true, false));
  assert(std::abs(warming.x() - 4) < 1e-9);

  RendezvousSpeedEstimator accelerating, boundary, decelerating;
  accelerating.reset(0, 0, 0);
  boundary.reset(0, 0, 0);
  decelerating.reset(0, 0, 0);
  for (int tick = 1; tick <= 12; ++tick) {
    const double t = tick / 4.0;
    accelerating.sample(t, 0.3 * t * t, 0, 10); // +0.6 m/s^2
    boundary.sample(t, 0.25 * t * t, 0, 10);    // +0.5 m/s^2
    decelerating.sample(t, 3 * t - 0.3 * t * t, 0, 10);
  }
  assert(!accelerating.history(3, 3, 0.5).usable);
  assert(std::abs(accelerating.history(3, 3, 0.5).max_positive_accel - 0.6) < 1e-9);
  assert(boundary.history(3, 3, 0.5).usable);
  assert(!boundary.history(3, 3, 0.5, 1).usable); // pre-start records cannot qualify
  assert(decelerating.history(3, 3, 0.5).usable); // negative acceleration allowed
  for (int tick = 13; tick <= 24; ++tick) {
    const double t = tick / 4.0;
    accelerating.sample(t, 2.7 + 1.8 * (t - 3), 0, 10);
  }
  assert(accelerating.history(6, 3, 0.5).usable);  // old acceleration ages out
  assert(!accelerating.history(8, 3, 0.5).usable); // cached good history is stale
  assert(!accelerating.history(5, 3, 0.5).usable); // future timestamps fail closed

  RendezvousSpeedEstimator gap;
  gap.reset(0, 0, 0);
  gap.sample(1, 1, 0, 10);
  gap.sample(3, 3, 0, 10);
  assert(!gap.history(3, 3, 0.5).usable);
  for (int t = 4; t <= 6; ++t)
    gap.sample(t, t, 0, 10);
  assert(gap.history(6, 3, 0.5).usable);
  gap.sample(20, 20, 0, 10); // producer interruption resets history
  assert(!gap.history(20, 3, 0.5).usable);
  gap.sample(20, 20, 0, 10); // duplicate/reordered samples don't earn coverage
  gap.sample(19, 19, 0, 10);
  assert(!gap.history(21, 3, 0.5).usable);

  RendezvousSpeedEstimator empty;
  assert(!empty.history(100, 3, 0.5).usable);
  assert(empty.history(100, 0, 0.5).usable); // opt-in, legacy behavior unchanged
  assert(!empty.history(100, 2, 0.5).usable);
  empty.reset(0, 0, 0);
  assert(!empty.history(3, 3, 0.5).usable); // elapsed time alone is not evidence
}
