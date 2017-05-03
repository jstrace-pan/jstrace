// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/p2p/socket_host_throttler.h"

#include <utility>

#include "third_party/webrtc/base/ratelimiter.h"
#include "third_party/webrtc/base/timing.h"

namespace content {

namespace  {

const int kMaxIceMessageBandwidth = 256 * 1024;

}  // namespace


P2PMessageThrottler::P2PMessageThrottler()
    : timing_(new rtc::Timing()),
      rate_limiter_(new rtc::RateLimiter(kMaxIceMessageBandwidth, 1.0)) {
}

P2PMessageThrottler::~P2PMessageThrottler() {
}

void P2PMessageThrottler::SetTiming(std::unique_ptr<rtc::Timing> timing) {
  timing_ = std::move(timing);
}

void P2PMessageThrottler::SetSendIceBandwidth(int bandwidth_kbps) {
  rate_limiter_.reset(new rtc::RateLimiter(bandwidth_kbps, 1.0));
}

bool P2PMessageThrottler::DropNextPacket(size_t packet_len) {
  double now = timing_->TimerNow();
  if (!rate_limiter_->CanUse(packet_len, now)) {
    // Exceeding the send rate, this packet should be dropped.
    return true;
  }

  rate_limiter_->Use(packet_len, now);
  return false;
}

}  // namespace content
