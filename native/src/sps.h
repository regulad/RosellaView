/*
 * sps.h - H.264 SPS rewriting.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef ROSELLA_SPS_H
#define ROSELLA_SPS_H

#include <stddef.h>
#include <stdint.h>

/* Rewrites an SPS NAL unit (header byte included, emulation prevention
 * applied) so its VUI declares timing for `fps` frames per second, adding a
 * minimal VUI if the SPS has none. Vega's MSE pipeline derives frame
 * durations from this, and falls back to 1 ms per frame without it.
 * Returns the new length written to `out`, or 0 if the SPS couldn't be parsed
 * (the caller should then use the original). */
size_t sps_set_timing(const uint8_t *sps, size_t len, unsigned fps, uint8_t *out, size_t out_cap);

/* Decoded picture size (after cropping) from an SPS NAL unit. Returns 0 on
 * success, -1 if it couldn't be parsed. */
int sps_get_size(const uint8_t *sps, size_t len, unsigned *width, unsigned *height);

/* The VUI's colour description (ITU-T H.273 codes; 2 = unspecified) and
 * full-range flag. Returns 0, or -1 if the SPS has no video signal type. */
int sps_get_colour(const uint8_t *sps, size_t len, unsigned *primaries, unsigned *transfer, unsigned *matrix,
                   unsigned *full_range);

#endif
