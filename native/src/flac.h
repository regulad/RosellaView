/*
 * flac.h - minimal FLAC framing: VERBATIM subframes only, i.e. PCM wrapped in
 * FLAC headers and CRCs. No prediction or entropy coding, so it costs about
 * as much as a copy; it exists because Vega's MSE plays FLAC but not PCM.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef ROSELLA_FLAC_H
#define ROSELLA_FLAC_H

#include <stddef.h>
#include <stdint.h>

/* Samples per channel in every frame (a FLAC fixed-blocksize stream). */
#define FLAC_BLOCK 2048

/* The 34-byte STREAMINFO metadata block body for 16-bit PCM. */
void flac_streaminfo(uint8_t out[34], unsigned sample_rate, unsigned channels, size_t max_frame_bytes);

/* Upper bound on one frame's size for `channels` channels of FLAC_BLOCK samples. */
size_t flac_max_frame_bytes(unsigned channels);

/* Writes frame number `frame` holding FLAC_BLOCK interleaved s16 samples per
 * channel. Returns the frame's length, or 0 if `sample_rate` has no FLAC code. */
size_t flac_write_frame(uint8_t *out, uint64_t frame, unsigned sample_rate, unsigned channels, const int16_t *pcm);

#endif
