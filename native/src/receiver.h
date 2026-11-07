/*
 * receiver.h - RosellaView's AirPlay receiver, built on UxPlay's libairplay.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef ROSELLA_RECEIVER_H
#define ROSELLA_RECEIVER_H

#include <stddef.h>
#include <stdint.h>

/* Events go out as one-line JSON strings; the sink takes ownership of `json`
 * (free() it). Called from libairplay's own threads. */
typedef void (*receiver_event_sink)(char *json);

void receiver_set_event_sink(receiver_event_sink sink);

/* Mirrored video as fragmented MP4: kind 0 = init segment, 1 = media segment.
 * The sink takes ownership of `data` (free() it). Called from libairplay's
 * mirroring thread. */
typedef void (*receiver_video_sink)(int kind, uint8_t *data, size_t len);

void receiver_set_video_sink(receiver_video_sink sink);

/* Mirrored/streamed audio as fragmented MP4, same contract as the video sink.
 * A new init segment starts each audio stream. */
void receiver_set_audio_sink(receiver_video_sink sink);

/* Starts mDNS advertisement and the AirPlay server as "<base_name> <TAG>",
 * where TAG is four characters derived from this device's persistent ID
 * (the full name is reported in the "started" event).
 * Returns 0 on success, or a negative error code (see receiver.c). */
int receiver_start(const char *base_name);
void receiver_stop(void);
int receiver_is_running(void);

/* While on (the default), audio carries a spoken "Preview." every 5 s of
 * playback (mirroring and audio-only alike); the app turns it off once the
 * unlock is purchased. */
void receiver_set_watermark(int on);

/* Asks the video muxer to replay the current GOP for a rebuilt player
 * (see fmp4_resync). Returns 0 if a replay was sent. */
int receiver_video_resync(void);
/* 1 if receiver_video_resync() would replay a GOP. */
int receiver_video_can_resync(void);

/* Development hooks: removes /scratch/dev-<name> and returns 1 if it existed
 * (create it from the component shell); and a deliberate native crash, to
 * check that crash reports arrive. */
int receiver_take_dev_flag(const char *name);
void receiver_crash_for_test(void);

/* Running counters since receiver_start(), as a JSON string (caller frees). */
char *receiver_stats_json(void);

#endif
