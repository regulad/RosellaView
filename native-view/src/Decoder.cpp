/*
 * Decoder: the OS's hardware H.264 decoder through media_transform. See
 * Decoder.h.
 *
 * Findings this relies on (tested on a Fire TV Stick 4K Select, 2026-11-07):
 *   - mime video/h264 works; video/avc fails to configure.
 *   - Input buffers: ARGB8888 graphics buffers with BufferMime::H264, one
 *     access unit each, described by MediaFrameMetadata.
 *   - Output: MM21 tiles, stride = width, planes padded to planeHeight rows.
 *     The two IBuffers are one allocation: UV starts stride * planeHeight bytes
 *     in, which can be inside buffer 0, continuing into buffer 1.
 *   - mapForRead works; queue to output is ~4 ms; 1080p60 keeps up.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "Decoder.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <thread>
#include <vector>

#include <apmf/apmf_base.h>
#include <apmf/array.h>
#include <apmf/data_object.h>
#include <apmf/native_pointer.h>
#include <apmf/process.h>
#include <apmf/ptr.h>
#include <apmf/string.h>

#include <apmf/iface/com/amazon/apmf/IByteBufferMapping.h>
#include <apmf/iface/com/amazon/apmf/IMutableByteBufferMapping.h>
#include <apmf/iface/com/amazon/apmf/IStandard.h>
#include <apmf/iface/com/amazon/apmf/Uuid.h>
#include <apmf/iface/com/amazon/kepler/buffer/IBuffer.h>
#include <apmf/iface/com/amazon/kepler/buffer/IBufferGroup.h>
#include <apmf/iface/com/amazon/kepler/buffer_queue/DequeueStatus.h>
#include <apmf/iface/com/amazon/kepler/buffer_queue/Frame.h>
#include <apmf/iface/com/amazon/kepler/buffer_queue/IBufferQueue.h>
#include <apmf/iface/com/amazon/kepler/buffer_queue/IBufferQueueModuleFactory.h>
#include <apmf/iface/com/amazon/kepler/buffer_queue/IBufferQueueReader.h>
#include <apmf/iface/com/amazon/kepler/buffer_queue/IBufferQueueWriter.h>
#include <apmf/iface/com/amazon/kepler/graphics/BufferMime.h>
#include <apmf/iface/com/amazon/kepler/graphics/BufferPixelFormat.h>
#include <apmf/iface/com/amazon/kepler/graphics/BufferUsage.h>
#include <apmf/iface/com/amazon/kepler/graphics/GraphicsBufferData.h>
#include <apmf/iface/com/amazon/kepler/graphics/GraphicsBufferGroupData.h>
#include <apmf/iface/com/amazon/kepler/graphics/IGraphicsModule.h>
#include <apmf/iface/com/amazon/kepler/media_descriptor/CodecMimeType.h>
#include <apmf/iface/com/amazon/kepler/media_descriptor/IMediaConfig.h>
#include <apmf/iface/com/amazon/kepler/media_descriptor/IMediaDescriptorFactory.h>
#include <apmf/iface/com/amazon/kepler/media_descriptor/IMediaFormat.h>
#include <apmf/iface/com/amazon/kepler/media_descriptor/IVideoFormat.h>
#include <apmf/iface/com/amazon/kepler/media_descriptor/Resolution.h>
#include <apmf/iface/com/amazon/kepler/media_transform/IMediaFactory.h>
#include <apmf/iface/com/amazon/kepler/media_transform/IMediaTransform.h>
#include <apmf/iface/com/amazon/kepler/media_transform/IMediaTransformListener.h>
#include <apmf/iface/com/amazon/kepler/media_transform/MediaFrameError.h>
#include <apmf/iface/com/amazon/kepler/media_transform/MediaFrameMetadata.h>

namespace rosella {

using apmf::ApmfBase;
using apmf::Ptr;
namespace ka = apmf::iface::com::amazon::apmf;
namespace kb = apmf::iface::com::amazon::kepler::buffer;
namespace kq = apmf::iface::com::amazon::kepler::buffer_queue;
namespace kg = apmf::iface::com::amazon::kepler::graphics;
namespace md = apmf::iface::com::amazon::kepler::media_descriptor;
namespace mt = apmf::iface::com::amazon::kepler::media_transform;

namespace {

// Input buffers: 512 ARGB8888 pixels (2 KiB) a row; a 1.5 MiB access unit
// fits, well above a 1080p IDR at mirroring bit rates.
constexpr int kInputBuffers = 4;
constexpr int kInputWidth = 512;
constexpr int kInputRows = 768;

std::string str(apmf::String const& s) { return std::string(s.data(), s.size()); }

struct Listener : ApmfBase<Listener, mt::IMediaTransformListener> {
  void onStart() { started = true; }
  void onStop() {}
  void onFlush() {}
  std::atomic<bool> started{false};
};

Ptr<kg::BufferUsage> inputUsage() {
  return apmf::MakeBuilderFor<kg::BufferUsage>()
      ->scanOut(false)
      ->gpuWrite(false)
      ->gpuRead(false)
      ->cameraWrite(false)
      ->cameraRead(false)
      ->secured(false)
      ->softwareReadOften(false)
      ->softwareReadRarely(true)
      ->softwareWriteOften(true)
      ->softwareWriteRarely(false)
      ->hardwareVideoDecoder(false)
      ->hardwareVideoEncoder(false)
      ->decrypt(false)
      ->build()
      .QueryInterface<kg::BufferUsage>();
}

// The output buffers' bytes as one address range (see the header comment).
struct Spans {
  uint8_t const* p[2] = {nullptr, nullptr};
  size_t n[2] = {0, 0};
  int count = 0;
  size_t total() const { return n[0] + n[1]; }
  // Copies `len` bytes from offset `off`; false past the end.
  bool copy(size_t off, uint8_t* dst, size_t len) const {
    if (off + len <= n[0]) {
      std::memcpy(dst, p[0] + off, len);
      return true;
    }
    if (off >= n[0] && count > 1 && off - n[0] + len <= n[1]) {
      std::memcpy(dst, p[1] + (off - n[0]), len);
      return true;
    }
    for (size_t i = 0; i < len; i++) {  // straddles the boundary
      size_t const o = off + i;
      if (o < n[0]) {
        dst[i] = p[0][o];
      } else if (count > 1 && o - n[0] < n[1]) {
        dst[i] = p[1][o - n[0]];
      } else {
        return false;
      }
    }
    return true;
  }
};

// MM21 tiles (tw x th bytes, row-major, each stored contiguously) from
// `base` into `rows` linear rows of `stride` bytes.
void detilePlane(Spans const& s, size_t base, uint8_t* dst, int stride, int rows, int tw, int th) {
  int const tilesX = stride / tw;
  for (int y = 0; y < rows; y++) {
    size_t const rowBase = base + ((size_t)(y / th) * tilesX * th + y % th) * tw;
    uint8_t* d = dst + (size_t)y * stride;
    for (int t = 0; t < tilesX; t++) {
      if (!s.copy(rowBase + (size_t)t * tw * th, d + t * tw, tw)) return;
    }
  }
}

}  // namespace

struct Decoder::Impl {
  Ptr<kq::IBufferQueue> inputQueue;
  Ptr<kq::IBufferQueueWriter> writer;
  Ptr<mt::IMediaTransform> transform;
  Ptr<kq::IBufferQueueReader> reader;
  Ptr<Listener> listener;
};

Decoder::Decoder(Log log) : log_(std::move(log)) {}

Decoder::~Decoder() { close(); }

bool Decoder::isOpen() const { return impl_ && impl_->transform; }

bool Decoder::open(int width, int height) {
  close();
  auto const t0 = std::chrono::steady_clock::now();
  try {
    auto process = apmf::GetProcessObject();
    auto descriptors =
        process->getComponent("/com.amazon.kepler.media_descriptor").TryQueryInterface<md::IMediaDescriptorFactory>();
    auto queues = process->getComponent("/com.amazon.kepler.buffer_queue").TryQueryInterface<kq::IBufferQueueModuleFactory>();
    auto graphics = process->getComponent("/com.amazon.kepler.graphics").TryQueryInterface<kg::IGraphicsModule>();
    auto factory = process->getComponent("/com.amazon.kepler.media_transform").TryQueryInterface<mt::IMediaFactory>();
    if (!descriptors || !queues || !graphics || !factory) {
      log_("decoder: a media component is missing");
      return false;
    }
    auto config = descriptors->createMediaConfigForVideoDecoder();
    config.QueryInterface<md::IMediaFormat>()->setMime(md::CodecMimeType::MIME_H264);
    config.QueryInterface<md::IVideoFormat>()->setResolution(md::Resolution{width, height});

    auto impl = std::make_unique<Impl>();
    ka::Uuid password{};
    impl->inputQueue = queues->createBufferQueue(&password);
    impl->writer = impl->inputQueue->attachAsWriter(password, nullptr);
    std::vector<Ptr<kb::IBufferGroup>> inputs;
    for (int i = 0; i < kInputBuffers; i++) {
      inputs.push_back(graphics->createGraphicsBuffer(kInputWidth, kInputRows, kg::BufferPixelFormat::ARGB8888,
                                                      kg::BufferMime::H264, inputUsage()));
    }
    impl->writer->addBuffers(inputs);
    impl->listener = Ptr<Listener>::Make();
    impl->transform = factory->createTransform(config, impl->listener, impl->inputQueue);
    impl->reader = impl->transform->getOutputBufferQueue()->attachAsReader(nullptr);
    impl->transform->startAsync();
    for (int i = 0; i < 100 && !impl->listener->started; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    impl_ = std::move(impl);
    width_ = width;
    height_ = height;
    auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    log_("decoder: opened for " + std::to_string(width) + "x" + std::to_string(height) + " in " + std::to_string(ms) +
         " ms" + (impl_->listener->started ? "" : " (no onStart yet)"));
    return true;
  } catch (std::exception const& e) {
    log_(std::string("decoder: open failed: ") + e.what());
    impl_.reset();
    return false;
  }
}

void Decoder::close() {
  if (!impl_) return;
  try {
    if (impl_->transform) impl_->transform->stopAsync();
  } catch (std::exception const& e) {
    log_(std::string("decoder: stop failed: ") + e.what());
  }
  impl_.reset();
  width_ = height_ = 0;
}

Decoder::Feed Decoder::feed(uint8_t const* data, size_t len, uint64_t pts, bool idr, bool config) {
  if (!isOpen()) return Feed::Failed;
  try {
    kq::Frame in{};
    auto const status = impl_->writer->tryDequeueFrame(&in);
    if (static_cast<int32_t>(status) != 0 || !in.bufferGroup) return Feed::Busy;
    auto buffer = in.bufferGroup->getBuffers()[0];
    if (len > (size_t)buffer->getSize()) {
      // Hand the buffer back empty, so it isn't lost.
      in.metaData = apmf::MakeBuilderFor<mt::MediaFrameMetadata>()
                        ->presentationTimeUs(pts)
                        ->length(0)
                        ->hasFormatChanged(false)
                        ->isSyncFrame(false)
                        ->isLastFrame(false)
                        ->hasCodecConfig(false)
                        ->build();
      impl_->writer->enqueueFrame(in);
      return Feed::TooBig;
    }
    auto map = buffer->mapForWriteOnly();
    std::memcpy(apmf::FromNativePointer<uint8_t*>(map->getMapPointer()), data, len);
    map->close();
    in.metaData = apmf::MakeBuilderFor<mt::MediaFrameMetadata>()
                      ->presentationTimeUs(pts)
                      ->length((int32_t)len)
                      ->hasFormatChanged(false)
                      ->isSyncFrame(idr)
                      ->isLastFrame(false)
                      ->hasCodecConfig(config)
                      ->build();
    impl_->writer->enqueueFrame(in);
    return Feed::Queued;
  } catch (std::exception const& e) {
    log_(std::string("decoder: feed failed: ") + e.what());
    return Feed::Failed;
  }
}

Decoder::Take Decoder::take(Picture& out, std::string& error) {
  if (!isOpen()) return Take::None;
  auto frame = std::make_shared<kq::Frame>();
  try {
    auto const status = impl_->reader->tryDequeueFrame(frame.get());
    if (static_cast<int32_t>(status) == 3) {
      error = "output queue closed";
      return Take::Error;
    }
    if (static_cast<int32_t>(status) != 0) return Take::None;
    out = Picture();
    if (frame->metaData) {
      if (auto e = frame->metaData.TryQueryInterface<mt::MediaFrameError>()) {
        auto err = e->error();
        error = err ? str(err->getMessage()) : "decode error";
        impl_->reader->enqueueFrame(*frame);
        return Take::Error;
      }
      if (auto m = frame->metaData.TryQueryInterface<mt::MediaFrameMetadata>()) {
        out.pts = m->presentationTimeUs();
      }
    }
    if (!frame->bufferGroup) {
      impl_->reader->enqueueFrame(*frame);
      return Take::None;
    }
    if (auto data = frame->bufferGroup->getMetadata()) {
      if (auto gd = data.TryQueryInterface<kg::GraphicsBufferGroupData>()) {
        out.width = gd->width();
        out.height = gd->height();
        auto planes = gd->bufferData();
        if (!planes.empty()) {
          out.stride = planes[0]->stride();
          out.planeHeight = planes[0]->planeHeight();
        }
      }
    }
    if (out.stride <= 0) out.stride = (out.width + 15) & ~15;
    if (out.planeHeight <= 0) out.planeHeight = (out.height + 31) & ~31;
    out.frame = frame;
    return Take::Picture;
  } catch (std::exception const& e) {
    error = e.what();
    return Take::Error;
  }
}

void Decoder::giveBack(Picture& picture) {
  if (!picture.frame) return;
  auto frame = std::static_pointer_cast<kq::Frame>(picture.frame);
  picture.frame.reset();
  if (!isOpen()) return;
  try {
    impl_->reader->enqueueFrame(*frame);
  } catch (std::exception const& e) {
    log_(std::string("decoder: returning a picture failed: ") + e.what());
  }
}

bool Decoder::detile(Picture const& picture, uint8_t* y, uint8_t* uv) {
  auto frame = std::static_pointer_cast<kq::Frame>(picture.frame);
  if (!frame || !frame->bufferGroup) return false;
  try {
    auto buffers = frame->bufferGroup->getBuffers();
    std::vector<Ptr<ka::IByteBufferMapping>> maps;
    Spans spans;
    for (auto const& b : buffers) {
      if (spans.count == 2) break;
      maps.push_back(b->mapForRead());
      spans.p[spans.count] = apmf::FromNativePointer<uint8_t const*>(maps.back()->getMapPointer());
      spans.n[spans.count] = (size_t)b->getSize();
      spans.count++;
    }
    int const stride = picture.stride;
    detilePlane(spans, 0, y, stride, picture.height, 16, 32);
    detilePlane(spans, (size_t)stride * picture.planeHeight, uv, stride, (picture.height + 1) / 2, 16, 16);
    for (auto& m : maps) m->close();
    return true;
  } catch (std::exception const& e) {
    log_(std::string("decoder: map failed: ") + e.what());
    return false;
  }
}

}  // namespace rosella
