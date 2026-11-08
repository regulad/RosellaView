/*
 * AudioOut: mirrored audio straight to the OS's PCM output
 * (IAudioStreamPlayback, /com.amazon.kepler.audiostream), with a small buffer
 * instead of the MSE player's ~0.5 s. Spike: plays PCM as it arrives and
 * reports what the platform gives us (minimum buffer, latency, underruns).
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rosella {

class AudioOut {
 public:
  using Log = std::function<void(std::string const&)>;
  explicit AudioOut(Log log);
  ~AudioOut();

  // Opens focus and the stream on its own thread, then writes what push() queues.
  void start();
  void stop();

  // Interleaved s16 stereo at 44.1 kHz; any thread.
  void push(int16_t const* pcm, size_t frames, uint64_t senderNs);

 private:
  void run();
  Log log_;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<int16_t> queue_;  // interleaved samples waiting to be written
  bool stop_ = false;
  std::atomic<uint64_t> pushedFrames_{0};
};

}  // namespace rosella
