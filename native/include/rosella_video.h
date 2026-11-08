/*
 * rosella_video.h - the native video bridge between the receiver
 * (libRosellaView.so, the Node-API module) and the native surface component
 * (xyz.regulad.rosellaview.surface.so), which decodes and renders the
 * mirrored video itself (.claude/docs/DECISIONS.md D12).
 *
 * Both libraries are loaded into the app's process. The component looks the
 * functions below up in libRosellaView.so at run time (dlopen RTLD_NOLOAD,
 * dlsym) and attaches itself as the video consumer; while one is attached,
 * mirrored video goes to it instead of to the fMP4 muxer and MSE.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef ROSELLA_VIDEO_H
#define ROSELLA_VIDEO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROSELLA_VIDEO_ABI 1
#define ROSELLA_VIDEO_LIBRARY "libRosellaView.so"

/* One H.264 access unit, Annex-B (start codes), as the sender sent it. Only
 * valid during the call. */
typedef struct {
    const uint8_t *data;
    size_t len;
    uint64_t sender_ns; /* capture time on the sender's clock (as audio's) */
    uint64_t local_ns;  /* the same instant on this device's CLOCK_REALTIME */
    int idr;            /* holds an IDR slice */
    /* From the SPS this access unit carries (width 0 when it carries none). */
    unsigned width, height;
    int full_range;  /* 1 full, 0 limited (also when the SPS doesn't say) */
    unsigned matrix; /* ITU-T H.273 matrix coefficients; 2 = unspecified */
} rosella_video_frame;

/* Called on libairplay's mirroring thread; must return quickly. */
typedef struct {
    void *ctx;
    void (*frame)(void *ctx, const rosella_video_frame *frame);
    /* The sender flushed or reset its stream: drop what's queued. */
    void (*reset)(void *ctx);
} rosella_video_consumer;

/* What the consumer reports back, for A/V sync and telemetry. */
typedef struct {
    uint64_t shown_sender_ns; /* sender time of the frame on screen (0: none yet) */
    uint64_t shown_local_ns;  /* CLOCK_REALTIME when it was presented */
    uint64_t decoded, shown, skipped, late, errors, starts;
    unsigned width, height; /* decoded picture */
    int queued;             /* access units waiting to be decoded */
    double upload_ms;       /* mean map + de-tile + upload per shown frame */
} rosella_video_stats;

/* Attaches (or with NULL detaches) the consumer; returns ROSELLA_VIDEO_ABI.
 * Detaching waits for a frame call in progress to finish. */
int rosella_video_attach(const rosella_video_consumer *consumer);
typedef int (*rosella_video_attach_fn)(const rosella_video_consumer *);

/* Asks the sender for an IDR frame; 0 if the request was sent. */
int rosella_video_request_keyframe(void);
typedef int (*rosella_video_request_keyframe_fn)(void);

/* How long after capture (local_ns) frames should be shown, in ms. A/V sync
 * sets it from JS (Receiver.setVideoLatency). */
int rosella_video_latency_ms(void);
typedef int (*rosella_video_latency_ms_fn)(void);

void rosella_video_report(const rosella_video_stats *stats);
typedef void (*rosella_video_report_fn)(const rosella_video_stats *);

/* Native audio output: mixed PCM (interleaved s16) with the capture time of its
 * first frame, on the sender's clock and on this device's CLOCK_REALTIME (as
 * video's local_ns; 0 if unknown). While attached, mirrored audio goes here
 * instead of to MSE. Called on libairplay's audio thread; must return quickly. */
typedef struct {
    void *ctx;
    void (*pcm)(void *ctx, const int16_t *pcm, size_t frames, unsigned rate, unsigned channels, uint64_t sender_ns,
                uint64_t local_ns);
} rosella_audio_consumer;

/* Attaches (or with NULL detaches) the audio consumer; returns ROSELLA_VIDEO_ABI.
 * Detaching waits for a call in progress to finish. */
int rosella_audio_attach(const rosella_audio_consumer *consumer);
typedef int (*rosella_audio_attach_fn)(const rosella_audio_consumer *);

/* How long after capture (local_ns) audio should be heard, in ms: the video
 * latency plus the viewer's audio delay. Set from JS (Receiver.setAudioLatency). */
int rosella_audio_latency_ms(void);
typedef int (*rosella_audio_latency_ms_fn)(void);

/* A status line (decoder starts, restarts, errors), sent to JS as a
 * "native_status" receiver event, and so to Sentry. */
void rosella_video_status(const char *message);
typedef void (*rosella_video_status_fn)(const char *);

#ifdef __cplusplus
}
#endif

#endif
