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
}
