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

/* One decrypted AirPlay audio packet. */
void audio_push(audio_pipeline *p, const uint8_t *data, size_t len);

/* Drops buffered samples (on flush, teardown or a new connection). */
void audio_reset(audio_pipeline *p);

#endif
