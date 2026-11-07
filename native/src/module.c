/*
 * module.c - Node-API bridge for RosellaView's native receiver.
 *
 * A plain Node-API addon: no Amazon SDK code is compiled in. The napi_*
 * symbols resolve at load time from the OS's libkeplerscript.so, and the Vega
 * runtime registers the module by calling autoLinkKeplerTurboModulesV1().
 *
 * Runtime quirks (each crashed the app on Vega OS 2.0):
 *   - napi_*_async_work aborts: use a pthread + napi_threadsafe_function.
 *   - napi_get_value_string_utf8 with a NULL buffer (length query) aborts:
 *     always pass a real buffer.
 *   - napi_create_arraybuffer is "not implemented" (logged, no buffer):
 *     use napi_create_external_arraybuffer.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#define NAPI_VERSION 8
#include <node_api.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "receiver.h"
#include "sentry.h"

#define MAX_STRING_ARG 256

static napi_threadsafe_function g_events; /* JS listener, or NULL */
static napi_threadsafe_function g_video;  /* JS video listener, or NULL */
static napi_threadsafe_function g_audio;  /* JS audio listener, or NULL */
static napi_threadsafe_function g_sentry; /* JS envelope poster, or NULL */

typedef struct {
    int kind;
    uint8_t *data;
    size_t len;
    double sender_ms;
} video_segment;

static napi_value make_string(napi_env env, const char *s) {
    napi_value v;
    napi_create_string_utf8(env, s, strlen(s), &v);
    return v;
}

static napi_value make_bool(napi_env env, int b) {
    napi_value v;
    napi_get_boolean(env, b != 0, &v);
    return v;
}

/* Copies string argument `index` into `buf`; returns 0 if absent or not a string. */
static int string_arg_at(napi_env env, napi_callback_info info, size_t index, char *buf, size_t len) {
    size_t argc = 8, copied = 0;
    napi_value argv[8];
    napi_valuetype t;
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc <= index) return 0;
    if (napi_typeof(env, argv[index], &t) != napi_ok || t != napi_string) return 0;
    if (napi_get_value_string_utf8(env, argv[index], buf, len, &copied) != napi_ok) return 0;
    buf[copied < len ? copied : len - 1] = '\0'; /* Vega's copy isn't NUL-terminated */
    return 1;
}

static int string_arg(napi_env env, napi_callback_info info, char *buf, size_t len) {
    return string_arg_at(env, info, 0, buf, len);
}

/* ---------- events: libairplay threads → JS ---------- */

static void deliver_event(napi_env env, napi_value js_cb, void *context, void *data) {
    char *json = data;
    (void)context;
    if (env != NULL && js_cb != NULL) {
        napi_value undefined, arg = make_string(env, json);
        napi_get_undefined(env, &undefined);
        napi_call_function(env, undefined, js_cb, 1, &arg, NULL);
    }
    free(json);
}

static void event_sink(char *json) {
    napi_threadsafe_function tsfn = g_events;
    if (!tsfn || napi_call_threadsafe_function(tsfn, json, napi_tsfn_nonblocking) != napi_ok) free(json);
}

/* ---------- video: mirroring thread → JS ---------- */

static void free_segment_data(napi_env env, void *data, void *hint) {
    (void)env;
    (void)hint;
    free(data);
}

static void deliver_video(napi_env env, napi_value js_cb, void *context, void *data) {
    video_segment *seg = data;
    uint8_t *bytes = seg->data;
    (void)context;
    if (env != NULL && js_cb != NULL) {
        napi_value undefined, argv[3];
        napi_get_undefined(env, &undefined);
        napi_create_int32(env, seg->kind, &argv[0]);
        napi_create_double(env, seg->sender_ms, &argv[2]);
        /* napi_create_arraybuffer is unimplemented on Vega; the external variant
         * works and hands the segment to JS without a copy. */
        if (napi_create_external_arraybuffer(env, bytes, seg->len, free_segment_data, NULL, &argv[1]) == napi_ok) {
            bytes = NULL; /* now owned by the ArrayBuffer's finalizer */
            napi_call_function(env, undefined, js_cb, 3, argv, NULL);
        }
    }
    free(bytes);
    free(seg);
}

static void send_segment(napi_threadsafe_function tsfn, int kind, uint8_t *data, size_t len, double sender_ms) {
    video_segment *seg;
    if (!tsfn) {
        free(data);
        return;
    }
    seg = malloc(sizeof *seg);
    seg->kind = kind;
    seg->data = data;
    seg->len = len;
    seg->sender_ms = sender_ms;
    if (napi_call_threadsafe_function(tsfn, seg, napi_tsfn_nonblocking) != napi_ok) {
        free(data);
        free(seg);
    }
}

static void video_sink(int kind, uint8_t *data, size_t len, double sender_ms) {
    send_segment(g_video, kind, data, len, sender_ms);
}
static void audio_sink(int kind, uint8_t *data, size_t len, double sender_ms) {
    send_segment(g_audio, kind, data, len, sender_ms);
}
static void sentry_sink(uint8_t *data, size_t len) { send_segment(g_sentry, 0, data, len, 0); }

/* Replaces *slot with a threadsafe function for argv[0] if it's a function. */
static int replace_listener(napi_env env, napi_callback_info info, napi_threadsafe_function *slot,
                            const char *name, napi_threadsafe_function_call_js call_js) {
    size_t argc = 1;
    napi_value argv[1];
    napi_valuetype t = napi_undefined;
    napi_threadsafe_function old = *slot;

    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc >= 1) napi_typeof(env, argv[0], &t);
    *slot = NULL;
    if (old) napi_release_threadsafe_function(old, napi_tsfn_release);
    if (t == napi_function) {
        napi_threadsafe_function tsfn;
        if (napi_create_threadsafe_function(env, argv[0], NULL, make_string(env, name), 0, 1, NULL, NULL, NULL,
                                            call_js, &tsfn) == napi_ok) {
            *slot = tsfn;
        }
    }
    return *slot != NULL;
}

/* setVideoListener(fn(kind, buffer) | null) */
static napi_value js_set_video_listener(napi_env env, napi_callback_info info) {
    int ok = replace_listener(env, info, &g_video, "RosellaViewVideo", deliver_video);
    receiver_set_video_sink(video_sink);
    return make_bool(env, ok);
}

/* setAudioListener(fn(kind, buffer) | null) */
static napi_value js_set_audio_listener(napi_env env, napi_callback_info info) {
    int ok = replace_listener(env, info, &g_audio, "RosellaViewAudio", deliver_video);
    receiver_set_audio_sink(audio_sink);
    return make_bool(env, ok);
}

/* setEventListener(fn | null) */
static napi_value js_set_event_listener(napi_env env, napi_callback_info info) {
    int ok = replace_listener(env, info, &g_events, "RosellaViewEvents", deliver_event);
    receiver_set_event_sink(event_sink);
    return make_bool(env, ok);
}

/* start(name) → true on success (details arrive as events) */
static napi_value js_start(napi_env env, napi_callback_info info) {
    char name[MAX_STRING_ARG];
    if (!string_arg(env, info, name, sizeof name) || !name[0]) strcpy(name, "RosellaView");
    return make_bool(env, receiver_start(name) == 0);
}

static napi_value js_stop(napi_env env, napi_callback_info info) {
    (void)info;
    receiver_stop();
    return make_bool(env, 1);
}

/* setWatermark(on) */
static napi_value js_set_watermark(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    bool on = true;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc >= 1) napi_get_value_bool(env, argv[0], &on);
    receiver_set_watermark(on);
    return make_bool(env, on);
}

/* shiftAudio(ms): ms > 0 drops that much upcoming audio (sooner), < 0 inserts silence */
static napi_value js_shift_audio(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    int32_t ms = 0;
    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc >= 1) napi_get_value_int32(env, argv[0], &ms);
    receiver_shift_audio(ms);
    return make_bool(env, 1);
}

/* restartVideo(): after rebuilding a stalled player; the next keyframe (requested now) starts it */
static napi_value js_restart_video(napi_env env, napi_callback_info info) {
    (void)info;
    receiver_video_restart();
    return make_bool(env, 1);
}

static napi_value js_request_keyframe(napi_env env, napi_callback_info info) {
    (void)info;
    return make_bool(env, receiver_request_keyframe() == 0);
}

static napi_value js_set_sentry_listener(napi_env env, napi_callback_info info) {
    int ok = replace_listener(env, info, &g_sentry, "RosellaViewSentry", deliver_video);
    rv_sentry_set_sink(sentry_sink);
    return make_bool(env, ok);
}

/* sentryInit(dsn, release, dist, environment) → true once native crash handling runs */
static napi_value js_sentry_init(napi_env env, napi_callback_info info) {
    char dsn[MAX_STRING_ARG], release[MAX_STRING_ARG], dist[MAX_STRING_ARG], environment[MAX_STRING_ARG];
    if (!string_arg_at(env, info, 0, dsn, sizeof dsn) || !string_arg_at(env, info, 1, release, sizeof release) ||
        !string_arg_at(env, info, 2, dist, sizeof dist) ||
        !string_arg_at(env, info, 3, environment, sizeof environment))
        return make_bool(env, 0);
    return make_bool(env, rv_sentry_init(dsn, release, dist, environment) == 0);
}

/* takeDevFlag(name) → true if /scratch/dev-<name> existed (it's removed) */
static napi_value js_take_dev_flag(napi_env env, napi_callback_info info) {
    char name[64];
    return make_bool(env, string_arg(env, info, name, sizeof name) && receiver_take_dev_flag(name));
}

/* crashForTest(): a deliberate native crash */
static napi_value js_crash_for_test(napi_env env, napi_callback_info info) {
    (void)info;
    receiver_crash_for_test();
    return make_bool(env, 0);
}

static napi_value js_is_running(napi_env env, napi_callback_info info) {
    (void)info;
    return make_bool(env, receiver_is_running());
}

/* getStats() → JSON string */
static napi_value js_get_stats(napi_env env, napi_callback_info info) {
    char *json = receiver_stats_json();
    napi_value v = make_string(env, json ? json : "{}");
    (void)info;
    free(json);
    return v;
}

static napi_value module_init(napi_env env, napi_value exports) {
    napi_property_descriptor props[] = {
        {"setEventListener", NULL, js_set_event_listener, NULL, NULL, NULL, napi_default, NULL},
        {"setVideoListener", NULL, js_set_video_listener, NULL, NULL, NULL, napi_default, NULL},
        {"setAudioListener", NULL, js_set_audio_listener, NULL, NULL, NULL, napi_default, NULL},
        {"start", NULL, js_start, NULL, NULL, NULL, napi_default, NULL},
        {"stop", NULL, js_stop, NULL, NULL, NULL, napi_default, NULL},
        {"isRunning", NULL, js_is_running, NULL, NULL, NULL, napi_default, NULL},
        {"restartVideo", NULL, js_restart_video, NULL, NULL, NULL, napi_default, NULL},
        {"shiftAudio", NULL, js_shift_audio, NULL, NULL, NULL, napi_default, NULL},
        {"requestKeyframe", NULL, js_request_keyframe, NULL, NULL, NULL, napi_default, NULL},
        {"setSentryEnvelopeListener", NULL, js_set_sentry_listener, NULL, NULL, NULL, napi_default, NULL},
        {"sentryInit", NULL, js_sentry_init, NULL, NULL, NULL, napi_default, NULL},
        {"takeDevFlag", NULL, js_take_dev_flag, NULL, NULL, NULL, napi_default, NULL},
        {"crashForTest", NULL, js_crash_for_test, NULL, NULL, NULL, napi_default, NULL},
        {"setWatermark", NULL, js_set_watermark, NULL, NULL, NULL, napi_default, NULL},
        {"getStats", NULL, js_get_stats, NULL, NULL, NULL, napi_default, NULL},
    };
    napi_define_properties(env, exports, sizeof props / sizeof props[0], props);
    return exports;
}

static napi_module g_module = {
    NAPI_MODULE_VERSION, 0, __FILE__, module_init, "RosellaView", NULL, {0},
};

/* Entry point the Vega runtime calls for app-provided ("linkDynamic") modules. */
__attribute__((visibility("default"))) void autoLinkKeplerTurboModulesV1(void) {
    napi_module_register(&g_module);
}
