#!/usr/bin/env python3
"""C++11 production frame/window regressions; no mission, FC or simulator."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

LIB = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "AttitudeHistory.h"
#include <cassert>
#include <cmath>
#include <sstream>
using namespace bodyframe;
int main() {
  // Independent tilted-below example: nose down 10 degrees, 8 m NED below.
  const auto body = toBody(bodyToNed(0,-10,0), {{0,0,8}});
  assert(std::abs(body[0]-8*std::sin(10*radians))<1e-10);
  assert(std::abs(body[2]-8*std::cos(10*radians))<1e-10);
  for (double roll : {-35.,0.,25.}) for (double pitch : {-20.,0.,15.})
    for (double yaw : {0.,87.,359.}) {
      const auto rotation = bodyToNed(roll,pitch,yaw);
      const Vector ned{{2,-3,8}};
      const auto recovered = toNed(rotation,toBody(rotation,ned));
      for (unsigned i=0;i<3;++i) assert(std::abs(ned[i]-recovered[i])<1e-10);
      const auto forward=toNed(rotation,rotate(targetToBody(roll,pitch,yaw,213),{{1,0,0}}));
      assert(std::abs(forward[0]-std::cos(213*radians))<1e-10);
      assert(std::abs(forward[1]-std::sin(213*radians))<1e-10);
      assert(std::abs(forward[2])<1e-10);
    }
  AttitudeHistory history;
  // One 4Hz delivery contains all 5Hz native samples, not only the last pose.
  assert(history.receive("100,1,2,3,1000;200,4,5,6,1000.2;300,7,8,9,1000.4"));
  Pose pose;
  assert(history.find(200,pose));
  assert(pose.yaw==6 && pose.received_moos_time==1000.2);
  assert(!history.find(201,pose)); // no interpolation or timestamp guessing
  assert(history.receive("200,4,5,6,1000.2;300,7,8,9,1000.4"));
  assert(history.find(200,pose) && pose.received_moos_time==1000.2); // no renewal
  assert(history.receive("10,0,0,0,1001")); // FC clock generation replacement
  assert(!history.find(200,pose));
  for (const auto bad : {"", "10,0,0", "nan,0,0,0,1", "10,0,inf,0,1",
                          "10.5,0,0,0,1", "10,0,0,0,-1", "10,0,0,0,1,2",
                          "20,0,0,0,1;10,0,0,0,1", "10,0,0,0,1;10,0,0,0,1"}) {
    assert(history.receive("10,0,0,0,1001"));
    assert(!history.receive(bad));
    assert(!history.find(10,pose)); // malformed windows cannot retain old authority
  }
  std::ostringstream overflow;
  for (unsigned i=1;i<=129;++i) overflow<<(i==1?"":";")<<i<<",0,0,0,1001";
  assert(!history.receive(overflow.str()));
}
'''


class FrameWindowTest(unittest.TestCase):
    def test_production_full_rotation_and_atomic_native_window(self):
        with tempfile.TemporaryDirectory(prefix="body-frame-") as directory:
            source = Path(directory) / "test.cpp"
            source.write_text(SOURCE)
            binary = source.with_suffix("")
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra",
                            "-Werror", "-I" + str(LIB), str(source), "-o", str(binary)],
                           check=True, timeout=30)
            subprocess.run([str(binary)], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
