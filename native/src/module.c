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

#define MAX_STRING_ARG 256

static napi_threadsafe_function g_events; /* JS listener, or NULL */
static napi_threadsafe_function g_video;  /* JS video listener, or NULL */
static napi_threadsafe_function g_audio;  /* JS audio listener, or NULL */

typedef struct {
    int kind;
    uint8_t *data;
    size_t len;
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

/* Copies the first argument into `buf` if it is a string; returns 0 if absent. */
static int string_arg(napi_env env, napi_callback_info info, char *buf, size_t len) {
    size_t argc = 1, copied = 0;
    napi_value argv[1];
    napi_valuetype t;
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok || argc < 1) return 0;
    if (napi_typeof(env, argv[0], &t) != napi_ok || t != napi_string) return 0;
    if (napi_get_value_string_utf8(env, argv[0], buf, len, &copied) != napi_ok) return 0;
    return 1;
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
        napi_value undefined, argv[2];
        napi_get_undefined(env, &undefined);
        napi_create_int32(env, seg->kind, &argv[0]);
        /* napi_create_arraybuffer is unimplemented on Vega; the external variant
         * works and hands the segment to JS without a copy. */
        if (napi_create_external_arraybuffer(env, bytes, seg->len, free_segment_data, NULL, &argv[1]) == napi_ok) {
            bytes = NULL; /* now owned by the ArrayBuffer's finalizer */
            napi_call_function(env, undefined, js_cb, 2, argv, NULL);
        }
    }
    free(bytes);
    free(seg);
}

static void send_segment(napi_threadsafe_function tsfn, int kind, uint8_t *data, size_t len) {
    video_segment *seg;
    if (!tsfn) {
        free(data);
        return;
    }
    seg = malloc(sizeof *seg);
    seg->kind = kind;
    seg->data = data;
    seg->len = len;
    if (napi_call_threadsafe_function(tsfn, seg, napi_tsfn_nonblocking) != napi_ok) {
        free(data);
        free(seg);
    }
}

static void video_sink(int kind, uint8_t *data, size_t len) { send_segment(g_video, kind, data, len); }
static void audio_sink(int kind, uint8_t *data, size_t len) { send_segment(g_audio, kind, data, len); }

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

/* resyncVideo() → true if the current GOP was replayed */
static napi_value js_resync_video(napi_env env, napi_callback_info info) {
    (void)info;
    return make_bool(env, receiver_video_resync() == 0);
}

/* canResyncVideo() → true if resyncVideo() would replay a GOP */
static napi_value js_can_resync_video(napi_env env, napi_callback_info info) {
    (void)info;
    return make_bool(env, receiver_video_can_resync());
}

/* note(msg): appends an app-side line to the development journal */
static napi_value js_note(napi_env env, napi_callback_info info) {
    char msg[MAX_STRING_ARG];
    if (string_arg(env, info, msg, sizeof msg)) receiver_note(msg);
    return make_bool(env, 1);
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
        {"resyncVideo", NULL, js_resync_video, NULL, NULL, NULL, napi_default, NULL},
        {"canResyncVideo", NULL, js_can_resync_video, NULL, NULL, NULL, napi_default, NULL},
        {"note", NULL, js_note, NULL, NULL, NULL, napi_default, NULL},
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
