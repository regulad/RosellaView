/*
 * receiver.h - RosellaView's AirPlay receiver, built on UxPlay's libairplay.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef ROSELLA_RECEIVER_H
#define ROSELLA_RECEIVER_H

#include <stdint.h>

/* Events go out as one-line JSON strings; the sink takes ownership of `json`
 * (free() it). Called from libairplay's own threads. */
typedef void (*receiver_event_sink)(char *json);

void receiver_set_event_sink(receiver_event_sink sink);

/* Starts mDNS advertisement and the AirPlay server under `name`.
 * Returns 0 on success, or a negative error code (see receiver.c). */
int receiver_start(const char *name);
void receiver_stop(void);
int receiver_is_running(void);

/* Running counters since receiver_start(), as a JSON string (caller frees). */
char *receiver_stats_json(void);

#endif
