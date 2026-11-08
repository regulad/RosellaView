/*
 * AudioOut: see AudioOut.h. Follows the SDK's AudioStream and AudioFocus
 * guides (KeplerAudioInterface: audio-stream.md, audio-focus.md).
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "AudioOut.h"

#include <time.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>

#include <apmf/apmf_base.h>
#include <apmf/array.h>
#include <apmf/array_view.h>
#include <apmf/process.h>
#include <apmf/ptr.h>

#include <apmf/iface/com/amazon/apmf/ISubscription.h>
#include <apmf/iface/com/amazon/kepler/audio/AudioAttributes.h>
#include <apmf/iface/com/amazon/kepler/audio/AudioConfig.h>
#include <apmf/iface/com/amazon/kepler/audio/IAudioFocus.h>
#include <apmf/iface/com/amazon/kepler/audio/IAudioFocusAttributes.h>
#include <apmf/iface/com/amazon/kepler/audio/IAudioFocusAttributesBuilder.h>
#include <apmf/iface/com/amazon/kepler/audio/IAudioFocusSession.h>
#include <apmf/iface/com/amazon/kepler/audio/IAudioStream.h>
#include <apmf/iface/com/amazon/kepler/audio/IAudioStreamEventListener.h>
#include <apmf/iface/com/amazon/kepler/audio/IAudioStreamPlayback.h>
#include <apmf/iface/com/amazon/kepler/audio/IAudioStreamPlaybackBuilder.h>
#include <apmf/iface/com/amazon/kepler/audio/IFocusChangedListener.h>

namespace rosella {

using apmf::ApmfBase;
using apmf::Ptr;
namespace ka = apmf::iface::com::amazon::kepler::audio;

namespace {

// The generated audio constants (AudioUsage::MEDIA, …) are declared but not
// defined in the IDL import library; their values are just these strings.
template <typename T>
T named(char const* value) {
  return T{::apmf::detail::String(value)};
}

constexpr int kRate = 44100;
constexpr int kChannels = 2;
constexpr int kBytesPerFrame = kChannels * 2;
// Audio heard within this of its due time plays as it is; beyond it, it waits
// (silence is written) or its late samples are dropped. A little over one
// buffer (705 frames, 16 ms), so steady playback never trips it.
constexpr int64_t kSlackNs = 40'000'000;
// More queued audio than this is stale (a stalled stream): it's dropped.
constexpr size_t kMaxQueuedFrames = (size_t)kRate * 3;

uint64_t realtimeNs() {
  timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return (uint64_t)t.tv_sec * 1'000'000'000ull + (uint64_t)t.tv_nsec;
}

struct FocusListener : ApmfBase<FocusListener, ka::IFocusChangedListener> {
  explicit FocusListener(AudioOut::Log log) : log_(std::move(log)) {}
  void onFocusGranted() { log_("audio: focus granted"); }
  void onFocusDucked() { log_("audio: focus ducked"); }
  void onFocusPaused() { log_("audio: focus paused"); }
  void onFocusStopped() { log_("audio: focus stopped"); }
  void onFocusReleased() { log_("audio: focus released"); }
  void onFocusMuted() { log_("audio: focus muted"); }
  AudioOut::Log log_;
};

struct StreamListener : ApmfBase<StreamListener, ka::IAudioStreamEventListener> {
  void onDrainFinished() {}
  void onBufferUnderrun(int32_t frames) {
    underruns++;
    underrunFrames += frames;
  }
  void onStreamDied() { died = true; }
  void onStreamRecovered() { died = false; }
  void onMuteStateChanged(bool) {}
  std::atomic<int> underruns{0};
  std::atomic<int64_t> underrunFrames{0};
  std::atomic<bool> died{false};
};

}  // namespace

AudioOut::AudioOut(Log log, std::function<int()> latencyMs) : log_(std::move(log)), latencyMs_(std::move(latencyMs)) {}

AudioOut::~AudioOut() { stop(); }

void AudioOut::start() {
  if (thread_.joinable()) return;
  stop_ = false;
  thread_ = std::thread([this] { run(); });
}

void AudioOut::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void AudioOut::push(int16_t const* pcm, size_t frames, uint64_t localNs) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Chunk c;
    c.pcm.assign(pcm, pcm + frames * kChannels);
    c.localNs = localNs;
    if (localNs) {
      int64_t const lead = ((int64_t)localNs + (int64_t)latencyMs_() * 1'000'000 -
                            ((int64_t)realtimeNs() + streamLatencyMs_ * 1'000'000)) /
                           1'000'000;
      leadMinMs_ = std::min(leadMinMs_, lead);
      leadMaxMs_ = std::max(leadMaxMs_, lead);
      leadSumMs_ += lead;
      leadCount_++;
    }
    queue_.push_back(std::move(c));
    queuedFrames_ += frames;
    while (queuedFrames_ > kMaxQueuedFrames) {
      queuedFrames_ -= queue_.front().pcm.size() / kChannels - queue_.front().pos;
      queue_.pop_front();
    }
  }
  wake_.notify_all();
}

void AudioOut::run() {
  try {
    auto process = apmf::GetProcessObject();
    auto focusModule = process->getComponent("/com.amazon.kepler.audiofocus").TryQueryInterface<ka::IAudioFocus>();
    auto streamModule = process->getComponent("/com.amazon.kepler.audiostream").TryQueryInterface<ka::IAudioStream>();
    if (!focusModule || !streamModule) {
      log_(std::string("audio: component missing:") + (focusModule ? "" : " audiofocus") +
           (streamModule ? "" : " audiostream"));
      return;
    }

    // Focus, as the guides require, with the same usage as the stream.
    auto focusAttributes =
        focusModule->makeAudioFocusAttributesBuilder()->usage(named<ka::AudioUsage>("MEDIA"))->build();
    auto session = focusModule->makeAudioFocusSession();
    auto focusListener = Ptr<FocusListener>::Make(log_);
    auto focusSubscription = session->addFocusChangedListener(focusListener);
    session->requestAudioFocus(focusAttributes);

    // Positional: the generated member types have explicit default constructors.
    ka::AudioConfig config{kRate, named<ka::SampleFormat>("PCM_16_BIT"), named<ka::ChannelMask>("MASK_STEREO"),
                           ka::SampleLayout::INTERLEAVED};
    ka::AudioFlags noFlags{false, false, false, false, false, false, false};
    ka::AudioAttributes attributes{named<ka::AudioContentType>("MOVIE"), named<ka::AudioUsage>("MEDIA"), noFlags,
                                   ka::AudioStreamTag{::apmf::detail::String("")}};

    // What the platform asks for, with and without the low-latency flag.
    int32_t frames = 0, count = 0, framesLow = 0, countLow = 0;
    streamModule->queryMinimumBufferInfo(config, attributes, &frames, &count);
    attributes.flags.lowLatency = true;
    streamModule->queryMinimumBufferInfo(config, attributes, &framesLow, &countLow);
    auto ms = [](int64_t f) { return std::to_string(f * 1000 / kRate) + " ms"; };
    log_("audio: minimum buffer " + std::to_string(frames) + " x " + std::to_string(count) + " frames (" +
         ms((int64_t)frames * count) + "); with lowLatency " + std::to_string(framesLow) + " x " +
         std::to_string(countLow) + " (" + ms((int64_t)framesLow * countLow) + ")");
    if (framesLow <= 0 || countLow <= 0) {
      attributes.flags.lowLatency = false;
      framesLow = frames;
      countLow = count;
    }

    auto playback = streamModule->makeAudioPlaybackBuilder()
                        ->audioConfig(config)
                        ->audioAttributes(attributes)
                        ->framesPerBuffer(framesLow)
                        ->bufferCount(countLow)
                        ->audioFocusSessionId(session->getSessionId())
                        ->build();
    if (!playback->initCheck()) {
      log_("audio: playback stream failed initCheck");
      return;
    }
    auto streamListener = Ptr<StreamListener>::Make();
    auto streamSubscription = playback->addAudioStreamEventListener(streamListener);
    int32_t const perBuffer = playback->getFramesPerBuffer();
    log_("audio: stream built: " + std::to_string(perBuffer) + " x " + std::to_string(playback->getBufferCount()) +
         " frames, " + std::to_string(playback->getNumBytesOfNativeBuffer()) + " bytes, latency " +
         std::to_string(playback->getLatencyInMs()) + " ms" + (attributes.flags.lowLatency ? " (lowLatency)" : ""));

    // Prime with silence, then start (the guide's advice against underruns).
    std::vector<int16_t> out((size_t)perBuffer * kChannels, 0);
    auto view = [&] { return apmf::ArrayView<uint8_t>(reinterpret_cast<uint8_t*>(out.data()), out.size() * 2); };
    for (int i = 0; i < playback->getBufferCount(); i++) {
      try {
        playback->write(view());
      } catch (std::exception const&) {
        break;  // full
      }
    }
    playback->start();
    log_("audio: started");

    // Each buffer: the queued audio due when it will be heard (now + the
    // stream's latency), or silence.
    uint64_t const nsPerFrame = 1'000'000'000ull / kRate;
    int64_t streamLatencyNs = (int64_t)playback->getLatencyInMs() * 1'000'000;
    auto lastReport = std::chrono::steady_clock::now();
    uint64_t played = 0, silence = 0, dropped = 0, waited = 0, errors = 0;
    for (;;) {
      bool audible = false;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stop_) break;
        int64_t const heardAt = (int64_t)realtimeNs() + streamLatencyNs;
        int64_t const latencyNs = (int64_t)latencyMs_() * 1'000'000;
        auto dueOf = [&](Chunk const& c) {
          return c.localNs ? (int64_t)(c.localNs + c.pos * nsPerFrame) + latencyNs : heardAt;
        };
        // Late: drop what should already have been heard.
        while (!queue_.empty() && dueOf(queue_.front()) < heardAt - kSlackNs) {
          Chunk& c = queue_.front();
          size_t const frames = c.pcm.size() / kChannels;
          size_t const late = (size_t)((heardAt - dueOf(c)) / (int64_t)nsPerFrame);
          size_t const skip = std::min(late, frames - c.pos);
          c.pos += skip;
          queuedFrames_ -= skip;
          dropped += skip;
          if (c.pos >= frames) queue_.pop_front();
        }
        if (!queue_.empty() && dueOf(queue_.front()) <= heardAt + kSlackNs) {
          audible = true;
          size_t filled = 0;
          while (filled < (size_t)perBuffer && !queue_.empty()) {
            Chunk& c = queue_.front();
            size_t const frames = c.pcm.size() / kChannels;
            size_t const take = std::min((size_t)perBuffer - filled, frames - c.pos);
            std::copy_n(c.pcm.begin() + c.pos * kChannels, take * kChannels, out.begin() + filled * kChannels);
            c.pos += take;
            filled += take;
            queuedFrames_ -= take;
            if (c.pos >= frames) queue_.pop_front();
          }
          std::fill(out.begin() + filled * kChannels, out.end(), 0);
        } else if (!queue_.empty()) {
          waited++;
        }
      }
      if (!audible) std::fill(out.begin(), out.end(), 0);
      try {
        playback->write(view());  // blocks while the stream's buffers are full
        (audible ? played : silence) += perBuffer;
      } catch (std::exception const&) {
        errors++;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }

      auto const now = std::chrono::steady_clock::now();
      if (now - lastReport > std::chrono::seconds(30)) {
        lastReport = now;
        streamLatencyNs = (int64_t)playback->getLatencyInMs() * 1'000'000;
        size_t queued;
        std::string lead = "n/a";
        {
          std::lock_guard<std::mutex> lock(mutex_);
          queued = queuedFrames_;
          streamLatencyMs_ = streamLatencyNs / 1'000'000;
          if (leadCount_) {
            lead = std::to_string(leadMinMs_) + "/" + std::to_string(leadSumMs_ / leadCount_) + "/" +
                   std::to_string(leadMaxMs_) + " ms";
          }
          leadMinMs_ = INT64_MAX;
          leadMaxMs_ = INT64_MIN;
          leadSumMs_ = leadCount_ = 0;
        }
        log_("audio: stream latency " + std::to_string(streamLatencyNs / 1'000'000) + " ms, target " +
             std::to_string(latencyMs_()) + " ms, queued " + ms(queued) + ", arrival lead min/avg/max " + lead +
             "; played " + ms(played) + ", silence " + ms(silence) + ", dropped late " + ms(dropped) + ", waits " +
             std::to_string(waited) + ", underruns " + std::to_string(playback->getUnderrunCount()) +
             ", write errors " + std::to_string(errors) + (streamListener->died ? ", stream died" : ""));
      }
    }
    try {
      playback->pause();
      playback->flush();
      playback->stop();
    } catch (std::exception const&) {
    }
    streamSubscription->unsubscribe();
    session->releaseAudioFocus();
    focusSubscription->unsubscribe();
    log_("audio: stopped");
  } catch (std::exception const& e) {
    log_(std::string("audio: error: ") + e.what());
  }
}

}  // namespace rosella
