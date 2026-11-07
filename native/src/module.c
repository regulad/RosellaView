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
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#define NAPI_VERSION 8
#include <node_api.h>

#include <stdlib.h>
#include <string.h>

#include "receiver.h"

#define MAX_STRING_ARG 256

static napi_threadsafe_function g_events; /* JS listener, or NULL */

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

/* setEventListener(fn | null) */
static napi_value js_set_event_listener(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1], name;
    napi_valuetype t = napi_undefined;
    napi_threadsafe_function old = g_events;

    napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
    if (argc >= 1) napi_typeof(env, argv[0], &t);
    g_events = NULL;
    if (old) napi_release_threadsafe_function(old, napi_tsfn_release);
    if (t == napi_function) {
        napi_threadsafe_function tsfn;
        name = make_string(env, "RosellaViewEvents");
        if (napi_create_threadsafe_function(env, argv[0], NULL, name, 0, 1, NULL, NULL, NULL, deliver_event,
                                            &tsfn) == napi_ok) {
            g_events = tsfn;
        }
    }
    receiver_set_event_sink(event_sink);
    return make_bool(env, g_events != NULL);
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
        {"start", NULL, js_start, NULL, NULL, NULL, napi_default, NULL},
        {"stop", NULL, js_stop, NULL, NULL, NULL, napi_default, NULL},
        {"isRunning", NULL, js_is_running, NULL, NULL, NULL, napi_default, NULL},
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
