/*
 * AudioOut: mirrored audio straight to the OS's PCM output
 * (IAudioStreamPlayback, /com.amazon.kepler.audiostream): a 31 ms buffer
 * (2 x 705 frames) and ~73 ms of output latency, instead of the MSE player's
 * ~0.5 s and coarse clock.
 *
 * Each buffer is heard at capture time (local_ns) + the audio latency, the
 * same clock video is shown by, so lip sync holds by construction. Audio that
 * would be heard more than kSlackMs early waits (silence is written instead);
 * more than kSlackMs late, its late samples are dropped. Nothing finer: no
 * rate correction (.claude/docs/DECISIONS.md D13).
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

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
  // latencyMs: how long after capture audio should be heard (read per buffer).
  AudioOut(Log log, std::function<int()> latencyMs);
  ~AudioOut();

  // Opens focus and the stream on its own thread, then plays what push() queues.
  void start();
  void stop();

  // Interleaved s16 stereo at 44.1 kHz, captured at localNs (CLOCK_REALTIME;
  // 0: play as soon as possible). Any thread.
  void push(int16_t const* pcm, size_t frames, uint64_t localNs);

 private:
  struct Chunk {
    std::vector<int16_t> pcm;
    uint64_t localNs = 0;
    size_t pos = 0;  // frames already played or dropped
  };
  void run();
  Log log_;
  std::function<int()> latencyMs_;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Chunk> queue_;
  size_t queuedFrames_ = 0;
  bool stop_ = false;
};

}  // namespace rosella
