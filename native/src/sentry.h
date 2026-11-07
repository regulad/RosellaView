/*
 * sentry.h - native crash reporting through sentry-native.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef ROSELLA_SENTRY_H
#define ROSELLA_SENTRY_H

#include <stddef.h>
#include <stdint.h>

/* Receives each serialized envelope; `data` is malloc'd and the callee owns it. */
typedef void (*rv_sentry_sink)(uint8_t *data, size_t len);

/* Set before rv_sentry_init(): crashes from the previous run are sent during
 * init. */
void rv_sentry_set_sink(rv_sentry_sink sink);

/* Starts crash handling (inproc backend) with its database in /data. Idempotent.
 * Returns 0 on success. */
int rv_sentry_init(const char *dsn, const char *release, const char *dist, const char *environment);

/* Scope for native crash reports; JS events get the same from the JS SDK. */
void rv_sentry_breadcrumb(const char *category, const char *message);
void rv_sentry_set_tag(const char *key, const char *value);
/* Sets context `name` from up to `n` key/value string pairs (NULL values skipped). */
void rv_sentry_set_context(const char *name, const char *const *pairs, size_t n);

#endif
