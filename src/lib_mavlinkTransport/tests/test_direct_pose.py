#!/usr/bin/env python3
"""Compile real C++11 direct-MAVLink decoder/pose production headers."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

LIB = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "DirectPose.h"
#include <cassert>
#include <cstring>
using namespace mavlink;
void u32(Payload &p,uint32_t v) { for(unsigned i=0;i<4;++i)p.push_back(v>>(8*i)); }
void real(Payload &p,float v) { uint32_t b;std::memcpy(&b,&v,4);u32(p,b); }
Frame attitude(unsigned stamp,double pitch=10) {
  Frame f;f.id=31;f.system=f.component=1;u32(f.payload,stamp);
  real(f.payload,std::cos(pitch*bodyframe::radians/2));real(f.payload,0);
  real(f.payload,std::sin(pitch*bodyframe::radians/2));real(f.payload,0);return f;
}
Frame position(unsigned stamp) {
  Frame f;f.id=33;f.system=f.component=1;u32(f.payload,stamp);
  u32(f.payload,420000000);u32(f.payload,uint32_t(-710000000));u32(f.payload,8000);return f;
}
Payload packet(const Frame &f,bool v2) {
  Payload p=v2?Payload{0xfd,uint8_t(f.payload.size()),0,0,0,1,1,uint8_t(f.id),0,0}:
                Payload{0xfe,uint8_t(f.payload.size()),0,1,1,uint8_t(f.id)};
  p.insert(p.end(),f.payload.begin(),f.payload.end());uint16_t c=0xffff;
  for(size_t i=1;i<=p.size();++i) {
    uint8_t b=i==p.size()?receiveExtra(f.id):p[i];uint8_t t=b^uint8_t(c);t^=uint8_t(t<<4);
    c=uint16_t((c>>8)^(uint16_t(t)<<8)^(uint16_t(t)<<3)^(t>>4));
  }p.push_back(c);p.push_back(c>>8);return p;
}
int main() {
  Decoder decoder;auto a=packet(attitude(10000),true);auto p=packet(position(10000),false);
  // Arbitrary TCP fragmentation and v1/v2 mixing; no delivery until CRC complete.
  assert(decoder.feed(a.data(),7).empty());auto frames=decoder.feed(a.data()+7,a.size()-7);
  assert(frames.size()==1 && frames[0].id==31);
  auto corrupt=a;corrupt.back()^=1;assert(decoder.feed(corrupt.data(),corrupt.size()).empty());
  frames=decoder.feed(p.data(),p.size());assert(frames.size()==1 && frames[0].id==33);
  DirectPose model;NativePose pose;
  auto foreign=attitude(10000);foreign.system=2;model.receive(foreign,1,1,1,100);
  model.receive(position(10000),1,1,1,100);assert(!model.pose(1,.25,20,pose));
  model.receive(attitude(10000),1,1,1,100);assert(model.pose(1,.25,20,pose));
  assert(std::abs(pose.pitch-10)<1e-5 && pose.lat==42 && pose.lon==-71 && pose.msl==8);
  assert(pose.stamp==10000000 && pose.position_stamp==pose.stamp);
  // Reposting original native packets cannot manufacture freshness.
  model.receive(attitude(10000),1,1,2,101);model.receive(position(10000),1,1,2,101);
  assert(!model.pose(2,.25,20,pose));
  // Reject excessive source-clock skew, then admit a genuinely coherent pair.
  model.clear();model.receive(attitude(11000),1,1,2,101);model.receive(position(11100),1,1,2,101);
  assert(!model.pose(2,.25,20,pose));model.receive(position(11005),1,1,2,101);
  assert(model.pose(2,.25,20,pose));assert(pose.position_stamp-pose.stamp==5000);
  std::map<uint64_t,NativePose> history;
  assert(readPoseWindow(model.window(2),history));assert(history.at(pose.stamp).received_moos==101);
  for(auto bad:{"", "1,0,0,0,42,-71,8,1,1;", "1,0,0,0,42,-71,8,1,1,",
                "1,0,0,0,42,-71,8,1,900000", "1,0,nan,0,42,-71,8,1,1"}) {
    assert(!readPoseWindow(bad,history));assert(history.empty());
  }
  // Native reboot/source rewind removes old generation, not timestamp interpolation.
  model.receive(position(100),1,1,3,102);assert(!model.pose(3,.25,20,pose));
  model.receive(attitude(100),1,1,3,102);assert(model.pose(3,.25,20,pose));
  assert(readPoseWindow(model.window(3),history) && history.size()==1 && history.begin()->first==100000);
  // Logged 01:42:03 case: low/zero translation still gives >1m apparent target
  // motion if a 7.88m-down observation is decoded with the newer pitch.
  const bodyframe::Vector ned{{.4690612709,.8488541753,7.8799979459}};
  const auto body=bodyframe::toBody(bodyframe::bodyToNed(.7949297,10.0120668,-118.7789230),ned);
  const auto wrong=bodyframe::toNed(bodyframe::bodyToNed(.76445,1.88430,241.24854),body);
  assert(std::hypot(wrong[0]-ned[0],wrong[1]-ned[1])>1.10);
  const auto right=bodyframe::toNed(bodyframe::bodyToNed(.7949297,10.0120668,-118.7789230),body);
  assert(std::hypot(right[0]-ned[0],right[1]-ned[1])<1e-10);
}
'''


class DirectPoseTest(unittest.TestCase):
    def test_native_decoder_pairing_and_logged_pitch_regression(self):
        with tempfile.TemporaryDirectory(prefix="direct-pose-") as directory:
            source = Path(directory) / "test.cpp"
            source.write_text(SOURCE)
            binary = Path(directory) / "test"
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra",
                            "-Werror", f"-I{LIB}", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
