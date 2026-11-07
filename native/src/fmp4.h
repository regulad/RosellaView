/*
 * fmp4.h - H.264 Annex-B access units → fragmented MP4 for MSE.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef ROSELLA_FMP4_H
#define ROSELLA_FMP4_H

#include <stddef.h>
#include <stdint.h>

enum fmp4_kind { FMP4_INIT = 0, FMP4_MEDIA = 1 };

/* Receives each finished segment. `data` is malloc'd; the callee owns it. */
typedef void (*fmp4_output)(void *ctx, enum fmp4_kind kind, uint8_t *data, size_t len);

typedef struct fmp4_muxer fmp4_muxer;

fmp4_muxer *fmp4_create(fmp4_output out, void *ctx);
void fmp4_destroy(fmp4_muxer *m);

/* Display size used in the init segment (the decoder itself reads the SPS). */
void fmp4_set_size(fmp4_muxer *m, unsigned width, unsigned height);

/* The sender's SPS as received (before our timing rewrite), for diagnostics. */
size_t fmp4_get_sender_sps(const fmp4_muxer *m, uint8_t *out, size_t cap);

/* Forget SPS/PPS and timing: the next keyframe starts a new init segment. */
void fmp4_reset(fmp4_muxer *m);

/* One Annex-B access unit; `ts_ns` is its sender timestamp in nanoseconds.
 * Emits an init segment first whenever the SPS/PPS change. Frames before the
 * first SPS/PPS are dropped. */
void fmp4_push_h264(fmp4_muxer *m, const uint8_t *au, size_t len, uint64_t ts_ns);

#endif
