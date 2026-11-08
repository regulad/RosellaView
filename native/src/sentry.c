/*
 * sentry.c - native crash reporting through sentry-native.
 *
 * The inproc backend records the crashed thread's stack into the database in
 * /data; sentry-native sends it on the next launch. It has no transport of its
 * own here: every envelope is serialized and handed to JS (module.c), which
 * posts it with the same fetch transport as the JS SDK (app/src/Sentry.ts).
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "sentry.h"

#include <dlfcn.h>
#include <stdio.h>
#include <link.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ucontext.h>

#include <sentry.h>

#define DATABASE "/data/rosellaview/sentry"

static rv_sentry_sink g_sink;
static int g_started;

void rv_sentry_set_sink(rv_sentry_sink sink) { g_sink = sink; }

/* ---------- crash stacks on armv7 ----------
 * sentry-native's unwinder yields a single unusable frame from a signal
 * context on armv7, so the crash hook rebuilds the stack the way Breakpad's
 * stack walker does when it has no unwind info: the crashing pc, the caller
 * in lr, then return addresses found by scanning the stack for pointers into
 * this library's code (marked as scanned, so Sentry shows they're guesses).
 * Our code is Thumb, so return addresses have bit 0 set, which filters out
 * most data that merely looks like an address. */
#define MAX_SCANNED_FRAMES 48
#define MAX_SCAN_BYTES (32 * 1024)

static uintptr_t g_text_start, g_text_end;

static int find_text(struct dl_phdr_info *info, size_t size, void *data) {
    uintptr_t self = (uintptr_t)data;
    (void)size;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        uintptr_t start = info->dlpi_addr + ph->p_vaddr, end = start + ph->p_memsz;
        if (ph->p_type == PT_LOAD && (ph->p_flags & PF_X) && self >= start && self < end) {
            g_text_start = start;
            g_text_end = end;
            return 1;
        }
    }
    return 0;
}

static sentry_value_t frame(uintptr_t addr, const char *trust) {
    char hex[24];
    sentry_value_t f = sentry_value_new_object();
    snprintf(hex, sizeof hex, "0x%lx", (unsigned long)(addr & ~(uintptr_t)1));
    sentry_value_set_by_key(f, "instruction_addr", sentry_value_new_string(hex));
    sentry_value_set_by_key(f, "trust", sentry_value_new_string(trust));
    return f;
}

static sentry_value_t on_crash(const sentry_ucontext_t *uctx, sentry_value_t event, sentry_hint_t *hint, void *data) {
#if defined(__arm__)
    const mcontext_t *mc;
    uintptr_t pc, lr, sp, stack_end = 0, scanned[MAX_SCANNED_FRAMES];
    size_t n = 0;
    pthread_attr_t attr;
    sentry_value_t exc, frames;
    (void)hint;
    (void)data;
    if (!uctx || !uctx->user_context) return event;
    mc = &uctx->user_context->uc_mcontext;
    pc = mc->arm_pc;
    lr = mc->arm_lr;
    sp = mc->arm_sp;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        void *addr;
        size_t size;
        if (pthread_attr_getstack(&attr, &addr, &size) == 0) stack_end = (uintptr_t)addr + size;
        pthread_attr_destroy(&attr);
    }
    if (stack_end > sp && stack_end - sp > MAX_SCAN_BYTES) stack_end = sp + MAX_SCAN_BYTES;
    for (uintptr_t p = sp & ~(uintptr_t)3; g_text_end && p + 4 <= stack_end && n < MAX_SCANNED_FRAMES; p += 4) {
        uintptr_t w = *(const uintptr_t *)p;
        if ((w & 1) && w >= g_text_start && w < g_text_end && w != lr) scanned[n++] = w;
    }
    /* Sentry lists frames oldest first: scanned callers, then lr, then pc. */
    frames = sentry_value_new_list();
    for (size_t i = n; i > 0; i--) sentry_value_append(frames, frame(scanned[i - 1], "scan"));
    sentry_value_append(frames, frame(lr, "fp"));
    sentry_value_append(frames, frame(pc, "context"));
    exc = sentry_value_get_by_index(sentry_value_get_by_key(sentry_value_get_by_key(event, "exception"), "values"), 0);
    if (!sentry_value_is_null(exc)) {
        sentry_value_t st = sentry_value_get_by_key(exc, "stacktrace");
        if (!sentry_value_is_null(st))
            sentry_value_set_by_key(st, "frames", frames);
        else
            sentry_value_decref(frames);
    } else {
        sentry_value_decref(frames);
    }
#else
    (void)uctx;
    (void)hint;
    (void)data;
#endif
    return event;
}

static void send_envelope(sentry_envelope_t *envelope, void *state) {
    size_t len = 0;
    char *s;
    (void)state;
    s = sentry_envelope_serialize(envelope, &len);
    if (s && len && g_sink) {
        uint8_t *copy = malloc(len);
        if (copy) {
            memcpy(copy, s, len);
            g_sink(copy, len);
        }
    }
    sentry_free(s);
    sentry_envelope_free(envelope);
}

int rv_sentry_init(const char *dsn, const char *release, const char *dist, const char *environment) {
    sentry_options_t *opts;
    if (g_started) return 0;
    mkdir("/data/rosellaview", 0700);
    opts = sentry_options_new();
    sentry_options_set_dsn(opts, dsn);
    sentry_options_set_release(opts, release);
    if (dist && dist[0]) sentry_options_set_dist(opts, dist);
    sentry_options_set_environment(opts, environment);
    sentry_options_set_database_path(opts, DATABASE);
    sentry_options_set_transport(opts, sentry_transport_new(send_envelope));
    /* Release health comes from the JS SDK's sessions. */
    sentry_options_set_auto_session_tracking(opts, 0);
    sentry_options_set_max_breadcrumbs(opts, 100);
    dl_iterate_phdr(find_text, (void *)(uintptr_t)&rv_sentry_init);
    sentry_options_set_on_crash(opts, on_crash, NULL);
    if (sentry_init(opts) != 0) return -1;
    g_started = 1;
    return 0;
}

void rv_sentry_breadcrumb(const char *category, const char *message) {
    sentry_value_t crumb;
    if (!g_started) return;
    crumb = sentry_value_new_breadcrumb("default", message);
    sentry_value_set_by_key(crumb, "category", sentry_value_new_string(category));
    sentry_add_breadcrumb(crumb);
}

void rv_sentry_set_tag(const char *key, const char *value) {
    if (g_started && value) sentry_set_tag(key, value);
}

void rv_sentry_set_context(const char *name, const char *const *pairs, size_t n) {
    sentry_value_t ctx;
    if (!g_started) return;
    ctx = sentry_value_new_object();
    for (size_t i = 0; i + 1 < n * 2; i += 2) {
        if (pairs[i + 1]) sentry_value_set_by_key(ctx, pairs[i], sentry_value_new_string(pairs[i + 1]));
    }
    sentry_set_context(name, ctx);
}
