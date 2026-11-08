/*
 * AudioOut: see AudioOut.h. Follows the SDK's AudioStream and AudioFocus
 * guides (KeplerAudioInterface: audio-stream.md, audio-focus.md).
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "AudioOut.h"

#include <algorithm>
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

AudioOut::AudioOut(Log log) : log_(std::move(log)) {}

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

void AudioOut::push(int16_t const* pcm, size_t frames, uint64_t) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.insert(queue_.end(), pcm, pcm + frames * kChannels);
    // A spike: never hold more than a second.
    while (queue_.size() > (size_t)kRate * kChannels) queue_.pop_front();
  }
  pushedFrames_ += frames;
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
    std::vector<uint8_t> chunk((size_t)perBuffer * kBytesPerFrame, 0);
    for (int i = 0; i < playback->getBufferCount(); i++) {
      try {
        playback->write(apmf::ArrayView<uint8_t>(chunk.data(), chunk.size()));
      } catch (std::exception const&) {
        break;  // full
      }
    }
    playback->start();
    log_("audio: started");

    auto lastReport = std::chrono::steady_clock::now();
    uint64_t written = 0, blocked = 0;
    for (;;) {
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(5),
                       [&] { return stop_ || queue_.size() >= (size_t)perBuffer * kChannels; });
        if (stop_) break;
        if (queue_.size() >= (size_t)perBuffer * kChannels) {
          auto* out = reinterpret_cast<int16_t*>(chunk.data());
          std::copy_n(queue_.begin(), (size_t)perBuffer * kChannels, out);
          queue_.erase(queue_.begin(), queue_.begin() + (size_t)perBuffer * kChannels);
        } else {
          chunk.clear();
        }
      }
      if (!chunk.empty()) {
        try {
          playback->write(apmf::ArrayView<uint8_t>(chunk.data(), chunk.size()));  // blocks while full
          written += perBuffer;
        } catch (std::exception const& e) {
          blocked++;
        }
      }
      chunk.resize((size_t)perBuffer * kBytesPerFrame);

      auto const now = std::chrono::steady_clock::now();
      if (now - lastReport > std::chrono::seconds(5)) {
        lastReport = now;
        size_t queued;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          queued = queue_.size() / kChannels;
        }
        log_("audio: latency " + std::to_string(playback->getLatencyInMs()) + " ms, queued here " + ms(queued) +
             ", underruns " + std::to_string(playback->getUnderrunCount()) + " (" +
             std::to_string(streamListener->underruns.load()) + " events, " +
             ms(streamListener->underrunFrames.load()) + "), written " + ms(written) + " of " +
             ms(pushedFrames_.load()) + " received, write errors " + std::to_string(blocked) +
             (streamListener->died ? ", stream died" : ""));
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
