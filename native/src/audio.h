/*
 * audio.h - AirPlay audio → fragmented MP4 that Vega's MSE plays.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef ROSELLA_AUDIO_H
#define ROSELLA_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include "fmp4.h"

typedef struct audio_pipeline audio_pipeline;

/* Segments come out through `out` (init first, then media), like the video muxer. */
audio_pipeline *audio_create(fmp4_output out, void *ctx);
void audio_destroy(audio_pipeline *p);

/* AirPlay compression type from audio_get_format: 1 = PCM, 2 = ALAC,
 * 4 = AAC-LC, 8 = AAC-ELD. Starts a new stream (and init segment).
 * Returns 0, or -1 if the type isn't supported. */
int audio_set_format(audio_pipeline *p, unsigned ct);

/* One decrypted AirPlay audio packet with its RTP timestamp (in samples) and
 * the sender's clock time of its first sample (ns; segments carry it on, for
 * A/V sync).
 * Packets lost on the network leave a gap in the timestamps; it is filled with
 * silence so the player's timeline (sequence mode) keeps pace with the sender
 * and its buffer doesn't run dry. Late duplicates are dropped. */
void audio_push(audio_pipeline *p, const uint8_t *data, size_t len, uint32_t rtp_time, uint64_t ts_ns);

/* Samples of silence inserted for lost packets so far. */
uint64_t audio_concealed(const audio_pipeline *p);

/* The spoken watermark: mono s16 44.1 kHz PCM (copied), mixed into decoded
 * audio every PREVIEW_INTERVAL while enabled. AAC-LC passthrough (ct 4)
 * can't carry it, since that path isn't decoded. */
void audio_set_preview_clip(audio_pipeline *p, const int16_t *clip, size_t samples);
void audio_set_preview_enabled(audio_pipeline *p, int enabled);

/* Moves audio against the sender's timeline, for A/V sync: ms > 0 drops that
 * much of the upcoming audio (it plays sooner), ms < 0 inserts silence (it
 * plays later). Seeking the platform's live audio player doesn't work. Not
 * for AAC-LC passthrough (ct 4). */
void audio_shift(audio_pipeline *p, int ms);

/* Drops buffered samples (on flush, teardown or a new connection). */
void audio_reset(audio_pipeline *p);

#endif
