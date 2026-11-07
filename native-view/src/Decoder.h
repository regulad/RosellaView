/*
 * Decoder: the OS's hardware H.264 decoder, through media_transform
 * (IMediaFactory::createTransform). Off the OS compatibility contract, but it
 * works from an app and leaves the samples alone: full range survives
 * bit-exactly (docs/DECISIONS.md D12; ProjectRosella/VEGA.md).
 *
 * Access units go in as Annex-B; decoded pictures come out in MediaTek's MM21
 * block layout (Y in 16x32 tiles, interleaved UV in 16x16), which detile()
 * turns into linear NV12.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace rosella {

using Log = std::function<void(std::string const&)>;

// A decoded picture, still owned by the decoder until given back.
struct Picture {
  uint64_t pts = 0;  // as fed
  int width = 0, height = 0;
  int stride = 0, planeHeight = 0;  // of the tiled Y plane
  std::shared_ptr<void> frame;      // the decoder's output frame
};

class Decoder {
 public:
  explicit Decoder(Log log);
  ~Decoder();

  // Creates and starts a decoder for pictures of this size.
  bool open(int width, int height);
  void close();
  bool isOpen() const;
  int width() const { return width_; }
  int height() const { return height_; }

  enum class Feed { Queued, Busy, TooBig, Failed };
  Feed feed(uint8_t const* data, size_t len, uint64_t pts, bool idr, bool config);

  enum class Take { Picture, None, Error };
  // The next decoded picture, if there is one. On Error, `error` says why.
  Take take(Picture& out, std::string& error);
  void giveBack(Picture& picture);

  // Copies a picture out as linear NV12 rows of `stride` bytes: `rows` rows
  // of Y, then (rows + 1) / 2 rows of UV.
  bool detile(Picture const& picture, uint8_t* y, uint8_t* uv);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  Log log_;
  int width_ = 0, height_ = 0;
};

}  // namespace rosella
