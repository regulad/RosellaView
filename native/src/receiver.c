/*
 * receiver.c - RosellaView's AirPlay receiver: owns libairplay's raop_t and
 * dnssd_t (the roles uxplay.cpp plays upstream) and reports what happens as
 * JSON events. Phase 1: advertise, accept connections, count media.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "receiver.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <unistd.h>

#include "audio.h"
#include "dnssd.h"
#include "fmp4.h"
#include "sentry.h"
#include "sps.h"
#include "logger.h"
#include "raop.h"
#include "stream.h"

static receiver_event_sink g_sink;
static receiver_video_sink g_video_sink;
static pthread_mutex_t g_mux_lock = PTHREAD_MUTEX_INITIALIZER;
static fmp4_muxer *g_mux;
static receiver_video_sink g_audio_sink;
static pthread_mutex_t g_audio_lock = PTHREAD_MUTEX_INITIALIZER;
static audio_pipeline *g_audio;
static int g_watermark = 1;   /* until the unlock is purchased (JS decides) */

/* Spoken watermark, shipped as an app asset (scripts/make-preview-clip.sh). */
#define PREVIEW_CLIP "/pkg/assets/raw/preview.pcm"

static void emit(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static raop_t *g_raop;
static dnssd_t *g_dnssd;

static atomic_uint_fast64_t g_video_frames, g_video_bytes, g_audio_packets, g_audio_bytes;
static atomic_int g_audio_ct;
static atomic_int g_connections;

void receiver_set_event_sink(receiver_event_sink sink) { g_sink = sink; }
void receiver_set_video_sink(receiver_video_sink sink) { g_video_sink = sink; }
void receiver_set_audio_sink(receiver_video_sink sink) { g_audio_sink = sink; }

static void audio_output(void *ctx, enum fmp4_kind kind, uint8_t *data, size_t len) {
    (void)ctx;
    if (kind == FMP4_INIT) emit("{\"type\":\"audio_init\",\"bytes\":%zu}", len);
    if (g_audio_sink) {
        g_audio_sink(kind == FMP4_INIT ? 0 : 1, data, len);
    } else {
        free(data);
    }
}

/* Under g_audio_lock. The spoken watermark is mixed into every audio stream,
 * mirroring included; it is only heard while the sender is actually sending
 * audio (iOS stops while nothing plays), and its 5 s count only advances
 * with incoming samples. */
static void update_preview_locked(void) {
    if (g_audio) audio_set_preview_enabled(g_audio, g_watermark);
}

static void load_preview_clip_locked(void) {
    FILE *f = fopen(PREVIEW_CLIP, "rb");
    int16_t *clip;
    long n;
    if (!f) {
        emit("{\"type\":\"error\",\"where\":\"preview_clip\",\"path\":\"%s\"}", PREVIEW_CLIP);
        return;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    clip = n > 0 ? malloc((size_t)n) : NULL;
    if (clip && fread(clip, 1, (size_t)n, f) == (size_t)n) {
        audio_set_preview_clip(g_audio, clip, (size_t)n / sizeof *clip);
    }
    free(clip);
    fclose(f);
}

void receiver_set_watermark(int on) {
    pthread_mutex_lock(&g_audio_lock);
    g_watermark = on != 0;
    update_preview_locked();
    pthread_mutex_unlock(&g_audio_lock);
}

static void audio_reset_locked(void) {
    pthread_mutex_lock(&g_audio_lock);
    if (g_audio) audio_reset(g_audio);
    pthread_mutex_unlock(&g_audio_lock);
}

static void mux_output(void *ctx, enum fmp4_kind kind, uint8_t *data, size_t len) {
    (void)ctx;
    if (kind == FMP4_INIT && g_mux) { /* called under g_mux_lock from fmp4_push_h264 */
        uint8_t sps[256];
        char hex[2 * sizeof sps + 1];
        size_t n = fmp4_get_sender_sps(g_mux, sps, sizeof sps);
        unsigned w = 0, h = 0;
        for (size_t i = 0; i < n; i++) sprintf(hex + 2 * i, "%02x", sps[i]);
        hex[2 * n] = 0;
        sps_get_size(sps, n, &w, &h);
        emit("{\"type\":\"video_init\",\"bytes\":%zu,\"width\":%u,\"height\":%u,\"sender_sps\":\"%s\"}", len, w, h,
             hex);
    }
    if (g_video_sink) {
        g_video_sink(kind == FMP4_INIT ? 0 : 1, data, len);
    } else {
        free(data);
    }
}

static void mux_reset(void) {
    pthread_mutex_lock(&g_mux_lock);
    if (g_mux) fmp4_reset(g_mux);
    pthread_mutex_unlock(&g_mux_lock);
}

/* Escapes `s` into a JSON string body (no quotes). Result is malloc'd. */
static char *json_escape(const char *s) {
    size_t n = s ? strlen(s) : 0, o = 0;
    char *out = malloc(n * 6 + 1);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c < 0x20) {
            o += (size_t)sprintf(out + o, "\\u%04x", c);
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = 0;
    return out;
}

static void emit(const char *fmt, ...) {
    va_list ap;
    char *json = NULL;
    va_start(ap, fmt);
    if (vasprintf(&json, fmt, ap) < 0) json = NULL;
    va_end(ap);
    if (!json) return;
    rv_sentry_breadcrumb("receiver", json);
    if (g_sink) {
        g_sink(json);
    } else {
        free(json);
    }
}

static void emit_str(const char *type, const char *key, const char *value) {
    char *v = json_escape(value);
    emit("{\"type\":\"%s\",\"%s\":\"%s\"}", type, key, v);
    free(v);
}

/* ---------- libairplay callbacks ---------- */

#define USER_AGENT_PREFIX "Client identified as User-Agent: "

static void cb_log(void *cls, int level, const char *msg) {
    char *m;
    (void)cls;
    if (strncmp(msg, USER_AGENT_PREFIX, strlen(USER_AGENT_PREFIX)) == 0) {
        /* The sender's AirPlay version ("AirPlay/980.77.1") isn't reported any other way. */
        const char *ua = msg + strlen(USER_AGENT_PREFIX);
        rv_sentry_set_tag("sender.user_agent", ua);
        emit_str("client_user_agent", "user_agent", ua);
    }
    m = json_escape(msg);
    emit("{\"type\":\"log\",\"level\":%d,\"msg\":\"%s\"}", level, m);
    free(m);
}

static void cb_conn_init(void *cls) {
    (void)cls;
    emit("{\"type\":\"conn_init\",\"connections\":%d}", atomic_fetch_add(&g_connections, 1) + 1);
}

static void cb_conn_destroy(void *cls) {
    (void)cls;
    mux_reset();
    audio_reset_locked();
    emit("{\"type\":\"conn_destroy\",\"connections\":%d}", atomic_fetch_sub(&g_connections, 1) - 1);
}

static void cb_conn_reset(void *cls, int reason) {
    (void)cls;
    emit("{\"type\":\"conn_reset\",\"reason\":%d}", reason);
}

static void cb_conn_feedback(void *cls) { (void)cls; }

static void cb_report_client_request(void *cls, char *deviceid, char *model, char *name, bool *admit) {
    char *d = json_escape(deviceid), *m = json_escape(model), *n = json_escape(name);
    (void)cls;
    *admit = true;
    {
        const char *pairs[] = {"device_id", deviceid, "model", model, "name", name};
        rv_sentry_set_context("sender", pairs, 3);
        rv_sentry_set_tag("sender.model", model);
    }
    emit("{\"type\":\"client_request\",\"deviceid\":\"%s\",\"model\":\"%s\",\"name\":\"%s\"}", d, m, n);
    free(d);
    free(m);
    free(n);
}

/* From our UxPlay fork (regulad/UxPlay, branch rosella). */
static void cb_report_client_details(void *cls, const char *os_name, const char *os_version,
                                     const char *os_build_version, const char *source_version,
                                     const char *mac_address) {
    char *on = json_escape(os_name ? os_name : ""), *ov = json_escape(os_version ? os_version : ""),
         *ob = json_escape(os_build_version ? os_build_version : ""),
         *sv = json_escape(source_version ? source_version : ""), *mac = json_escape(mac_address ? mac_address : "");
    const char *os_pairs[] = {"name", os_name, "version", os_version, "build", os_build_version};
    (void)cls;
    rv_sentry_set_context("sender_os", os_pairs, 3);
    rv_sentry_set_tag("sender.os", os_name);
    rv_sentry_set_tag("sender.os_version", os_version);
    rv_sentry_set_tag("sender.airplay_version", source_version);
    emit("{\"type\":\"client_details\",\"os_name\":\"%s\",\"os_version\":\"%s\",\"os_build_version\":\"%s\","
         "\"source_version\":\"%s\",\"mac_address\":\"%s\"}",
         on, ov, ob, sv, mac);
    free(on);
    free(ov);
    free(ob);
    free(sv);
    free(mac);
}

static void cb_video_process(void *cls, raop_ntp_t *ntp, video_decode_struct *data) {
    (void)cls;
    (void)ntp;
    if (atomic_fetch_add(&g_video_frames, 1) == 0) {
        emit("{\"type\":\"first_video_frame\",\"h265\":%s,\"nal_count\":%d,\"bytes\":%d}",
             data->is_h265 ? "true" : "false", data->nal_count, data->data_len);
    }
    atomic_fetch_add(&g_video_bytes, (uint_fast64_t)data->data_len);
    if (data->is_h265) return; /* H.265 isn't advertised; ignore it if it arrives */
    pthread_mutex_lock(&g_mux_lock);
    if (g_mux) fmp4_push_h264(g_mux, data->data, (size_t)data->data_len, data->ntp_time_remote);
    pthread_mutex_unlock(&g_mux_lock);
}

static void cb_audio_process(void *cls, raop_ntp_t *ntp, audio_decode_struct *data) {
    (void)cls;
    (void)ntp;
    atomic_fetch_add(&g_audio_packets, 1);
    atomic_fetch_add(&g_audio_bytes, (uint_fast64_t)data->data_len);
    pthread_mutex_lock(&g_audio_lock);
    if (g_audio) audio_push(g_audio, data->data, (size_t)data->data_len, data->rtp_time);
    pthread_mutex_unlock(&g_audio_lock);
}

static void cb_video_pause(void *cls) { (void)cls; emit("{\"type\":\"video_pause\"}"); }
static void cb_video_resume(void *cls) { (void)cls; emit("{\"type\":\"video_resume\"}"); }
static void cb_video_flush(void *cls) {
    (void)cls;
    mux_reset();
    emit("{\"type\":\"video_flush\"}");
}
static void cb_audio_flush(void *cls) {
    (void)cls;
    audio_reset_locked();
}

static void cb_video_reset(void *cls, reset_type_t reset_type) {
    (void)cls;
    mux_reset();
    emit("{\"type\":\"video_reset\",\"reset_type\":%d}", (int)reset_type);
}

static int cb_video_set_codec_and_size(void *cls, video_codec_t codec, unsigned short width_source,
                                       unsigned short height_source, unsigned short x_offset,
                                       unsigned short y_offset, unsigned short width, unsigned short height) {
    (void)cls;
    pthread_mutex_lock(&g_mux_lock);
    if (g_mux) fmp4_set_size(g_mux, width_source, height_source);
    pthread_mutex_unlock(&g_mux_lock);
    emit("{\"type\":\"video_format\",\"codec\":\"%s\",\"source\":[%u,%u],\"offset\":[%u,%u],\"size\":[%u,%u]}",
         codec == VIDEO_CODEC_H264 ? "h264" : codec == VIDEO_CODEC_H265 ? "h265" : "unknown",
         width_source, height_source, x_offset, y_offset, width, height);
    return 0;
}

static void cb_mirror_video_running(void *cls, bool is_running) {
    (void)cls;
    emit("{\"type\":\"mirror_video_running\",\"running\":%s}", is_running ? "true" : "false");
}

static double cb_audio_set_client_volume(void *cls) {
    (void)cls;
    return 0.0;
}

static void cb_audio_set_volume(void *cls, float volume) {
    (void)cls;
    emit("{\"type\":\"volume\",\"db\":%.2f}", volume);
}

static void cb_audio_set_metadata(void *cls, const void *buffer, int buflen) {
    (void)cls;
    (void)buffer;
    (void)buflen;
}

static void cb_audio_set_coverart(void *cls, const void *buffer, int buflen) {
    (void)cls;
    (void)buffer;
    (void)buflen;
}

static void cb_audio_stop_coverart_rendering(void *cls) { (void)cls; }

static void cb_audio_remote_control_id(void *cls, const char *dacp_id, const char *active_remote_header) {
    (void)cls;
    (void)dacp_id;
    (void)active_remote_header;
}

static void cb_audio_set_progress(void *cls, uint32_t *start, uint32_t *curr, uint32_t *end) {
    (void)cls;
    (void)start;
    (void)curr;
    (void)end;
}

static void cb_audio_get_format(void *cls, unsigned char *ct, unsigned short *spf, bool *usingScreen,
                                bool *isMedia, uint64_t *audioFormat) {
    int rc = -1;
    (void)cls;
    atomic_store(&g_audio_ct, *ct);
    pthread_mutex_lock(&g_audio_lock);
    if (g_audio) {
        rc = audio_set_format(g_audio, *ct);
        audio_set_preview_enabled(g_audio, 0); /* restart the countdown for the new stream */
        update_preview_locked();
    }
    pthread_mutex_unlock(&g_audio_lock);
    if (rc != 0) emit("{\"type\":\"error\",\"where\":\"audio_set_format\",\"ct\":%u}", *ct);
    emit("{\"type\":\"audio_format\",\"ct\":%u,\"spf\":%u,\"using_screen\":%s,\"is_media\":%s,\"format\":\"0x%" PRIx64 "\"}",
         *ct, *spf, *usingScreen ? "true" : "false", *isMedia ? "true" : "false", *audioFormat);
}

static void cb_display_pin(void *cls, char *pin) {
    (void)cls;
    emit_str("display_pin", "pin", pin);
}

static void cb_register_client(void *cls, const char *device_id, const char *pk_str, const char *name) {
    (void)cls;
    (void)pk_str;
    (void)name;
    emit_str("register_client", "deviceid", device_id);
}

static bool cb_check_register(void *cls, const char *pk_str) {
    (void)cls;
    (void)pk_str;
    return true; /* no client registration list (yet) */
}

static const char *cb_passwd(void *cls, int *len) {
    (void)cls;
    *len = 0; /* no password */
    return NULL;
}

static void cb_export_dacp(void *cls, const char *active_remote, const char *dacp_id) {
    (void)cls;
    (void)active_remote;
    (void)dacp_id;
}

/* HLS "AirPlay video" is advertised as unsupported, so these stay inert. */
static void cb_on_video_play(void *cls, const char *location, const float start_position) {
    (void)cls;
    (void)start_position;
    emit_str("hls_play", "location", location);
}
static void cb_on_video_scrub(void *cls, const float position) { (void)cls; (void)position; }
static void cb_on_video_rate(void *cls, const float rate) { (void)cls; (void)rate; }
static void cb_on_video_stop(void *cls) { (void)cls; }
static void cb_on_video_acquire_playback_info(void *cls, playback_info_t *playback_video) {
    (void)cls;
    memset(playback_video, 0, sizeof *playback_video);
}
static float cb_on_video_playlist_remove(void *cls) {
    (void)cls;
    return 0.0f;
}
static void cb_get_custom_profile(void *cls, const char **custom_profile) {
    (void)cls;
    *custom_profile = NULL;
}

/* ---------- start / stop ---------- */

/* Persistent state lives in the app's data directory (/data, per-app ext4). */
#define STATE_DIR "/data/rosellaview"
#define IDENTITY_FILE STATE_DIR "/identity"
#define PAIRING_KEY_FILE STATE_DIR "/pairing.key"

/* The receiver's device ID: 6 random bytes (a locally administered MAC),
 * created on first run and kept in IDENTITY_FILE so senders see the same
 * receiver across launches. Falls back to a per-launch ID if /data isn't
 * writable. */
static void get_hw_addr(unsigned char out[6]) {
    FILE *f = fopen(IDENTITY_FILE, "rb");
    if (f) {
        size_t n = fread(out, 1, 6, f);
        fclose(f);
        if (n == 6) return;
    }
    f = fopen("/dev/urandom", "rb");
    if (!f || fread(out, 1, 6, f) != 6) {
        srand((unsigned)time(NULL) ^ (unsigned)(uintptr_t)out);
        for (int i = 0; i < 6; i++) out[i] = (unsigned char)(rand() & 0xff);
    }
    if (f) fclose(f);
    out[0] = (unsigned char)((out[0] & 0xfe) | 0x02); /* unicast, locally administered */
    mkdir(STATE_DIR, 0700);
    if ((f = fopen(IDENTITY_FILE, "wb")) != NULL) {
        fwrite(out, 1, 6, f);
        fclose(f);
    }
}

/* Four characters [A-Z0-9] derived deterministically from the device ID
 * (FNV-1a, then base 36), so each receiver gets a stable, distinct name. */
static void device_tag(const unsigned char id[6], char tag[5]) {
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) h = (h ^ id[i]) * 16777619u;
    for (int i = 0; i < 4; i++) {
        tag[i] = digits[h % 36];
        h /= 36;
    }
    tag[4] = 0;
}

/* Same advertised feature set as uxplay.cpp's start_dnssd(), with HLS off. */
static void set_features(dnssd_t *d) {
    static const signed char bits[32] = {0, 1, 1, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
                                         1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0};
    for (int i = 0; i < 32; i++) dnssd_set_airplay_features(d, i, bits[i]);
    for (int i = 32; i < 62; i++) dnssd_set_airplay_features(d, i, 0);
}

int receiver_start(const char *base_name) {
    raop_callbacks_t cbs;
    unsigned char hw[6];
    char device_id[18], tag[5], name[128];
    unsigned short tcp[3] = {0, 0, 0}, udp[3] = {0, 0, 0}, port;
    int err = 0;

    pthread_mutex_lock(&g_lock);
    if (g_raop) {
        pthread_mutex_unlock(&g_lock);
        return 0;
    }
    atomic_store(&g_video_frames, 0);
    atomic_store(&g_video_bytes, 0);
    atomic_store(&g_audio_packets, 0);
    atomic_store(&g_audio_bytes, 0);
    atomic_store(&g_connections, 0);

    pthread_mutex_lock(&g_mux_lock);
    if (!g_mux) g_mux = fmp4_create(mux_output, NULL);
    fmp4_reset(g_mux);
    pthread_mutex_unlock(&g_mux_lock);
    pthread_mutex_lock(&g_audio_lock);
    if (!g_audio) {
        g_audio = audio_create(audio_output, NULL);
        load_preview_clip_locked();
    }
    pthread_mutex_unlock(&g_audio_lock);

    get_hw_addr(hw);
    snprintf(device_id, sizeof device_id, "%02x:%02x:%02x:%02x:%02x:%02x", hw[0], hw[1], hw[2], hw[3], hw[4], hw[5]);
    device_tag(hw, tag);
    snprintf(name, sizeof name, "%s %s", base_name, tag);

    g_dnssd = dnssd_init(name, (int)strlen(name), (const char *)hw, sizeof hw, 0, &err);
    if (!g_dnssd || err) {
        emit("{\"type\":\"error\",\"where\":\"dnssd_init\",\"code\":%d}", err);
        g_dnssd = NULL;
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    set_features(g_dnssd);

    memset(&cbs, 0, sizeof cbs);
    cbs.audio_process = cb_audio_process;
    cbs.video_process = cb_video_process;
    cbs.video_pause = cb_video_pause;
    cbs.video_resume = cb_video_resume;
    cbs.conn_feedback = cb_conn_feedback;
    cbs.conn_reset = cb_conn_reset;
    cbs.video_reset = cb_video_reset;
    cbs.video_set_codec_and_size = cb_video_set_codec_and_size;
    cbs.conn_init = cb_conn_init;
    cbs.conn_destroy = cb_conn_destroy;
    cbs.audio_flush = cb_audio_flush;
    cbs.video_flush = cb_video_flush;
    cbs.audio_set_client_volume = cb_audio_set_client_volume;
    cbs.audio_set_volume = cb_audio_set_volume;
    cbs.audio_set_metadata = cb_audio_set_metadata;
    cbs.audio_set_coverart = cb_audio_set_coverart;
    cbs.audio_stop_coverart_rendering = cb_audio_stop_coverart_rendering;
    cbs.audio_remote_control_id = cb_audio_remote_control_id;
    cbs.audio_set_progress = cb_audio_set_progress;
    cbs.audio_get_format = cb_audio_get_format;
    cbs.mirror_video_running = cb_mirror_video_running;
    cbs.report_client_request = cb_report_client_request;
    cbs.report_client_details = cb_report_client_details;
    cbs.display_pin = cb_display_pin;
    cbs.register_client = cb_register_client;
    cbs.check_register = cb_check_register;
    cbs.passwd = cb_passwd;
    cbs.export_dacp = cb_export_dacp;
    cbs.on_video_play = cb_on_video_play;
    cbs.on_video_scrub = cb_on_video_scrub;
    cbs.on_video_rate = cb_on_video_rate;
    cbs.on_video_stop = cb_on_video_stop;
    cbs.on_video_acquire_playback_info = cb_on_video_acquire_playback_info;
    cbs.on_video_playlist_remove = cb_on_video_playlist_remove;
    cbs.get_custom_profile = cb_get_custom_profile;

    g_raop = raop_init(&cbs);
    if (!g_raop) {
        emit("{\"type\":\"error\",\"where\":\"raop_init\"}");
        goto fail;
    }
    raop_set_log_callback(g_raop, cb_log, NULL);
    raop_set_log_level(g_raop, LOGGER_INFO);
    /* nohold = 1: a new client may take over from the current one. The pairing
     * key persists in /data so senders keep recognising this receiver. */
    mkdir(STATE_DIR, 0700);
    if (raop_init2(g_raop, 1, device_id, PAIRING_KEY_FILE)) {
        emit("{\"type\":\"error\",\"where\":\"raop_init2\"}");
        goto fail;
    }
    /* Tell senders to target 1080p60, the probe-verified decode path. */
    raop_set_plist(g_raop, "width", 1920);
    raop_set_plist(g_raop, "height", 1080);
    raop_set_plist(g_raop, "refreshRate", 60);
    raop_set_plist(g_raop, "maxFPS", 60);

    raop_set_tcp_ports(g_raop, tcp);
    raop_set_udp_ports(g_raop, udp);
    port = raop_get_port(g_raop);
    if (raop_start_httpd(g_raop, &port) < 0) {
        emit("{\"type\":\"error\",\"where\":\"raop_start_httpd\"}");
        goto fail;
    }
    raop_set_port(g_raop, port);
    raop_set_dnssd(g_raop, g_dnssd);

    if ((err = dnssd_register_raop(g_dnssd, port)) != 0) {
        emit("{\"type\":\"error\",\"where\":\"dnssd_register_raop\",\"code\":%d}", err);
        goto fail;
    }
    if ((err = dnssd_register_airplay(g_dnssd, port)) != 0) {
        emit("{\"type\":\"error\",\"where\":\"dnssd_register_airplay\",\"code\":%d}", err);
        goto fail;
    }
    {
        char *n = json_escape(name);
        emit("{\"type\":\"started\",\"name\":\"%s\",\"port\":%u,\"deviceid\":\"%s\",\"features\":\"0x%" PRIx64 "\"}",
             n, port, device_id, dnssd_get_airplay_features(g_dnssd));
        free(n);
    }
    pthread_mutex_unlock(&g_lock);
    return 0;

fail:
    if (g_raop) {
        raop_destroy(g_raop);
        g_raop = NULL;
    }
    if (g_dnssd) {
        dnssd_unregister_raop(g_dnssd);
        dnssd_unregister_airplay(g_dnssd);
        dnssd_destroy(g_dnssd);
        g_dnssd = NULL;
    }
    pthread_mutex_unlock(&g_lock);
    return -2;
}

void receiver_stop(void) {
    pthread_mutex_lock(&g_lock);
    if (g_raop) {
        raop_destroy(g_raop);
        g_raop = NULL;
    }
    pthread_mutex_lock(&g_mux_lock);
    if (g_mux) {
        fmp4_destroy(g_mux);
        g_mux = NULL;
    }
    pthread_mutex_unlock(&g_mux_lock);
    pthread_mutex_lock(&g_audio_lock);
    audio_destroy(g_audio);
    g_audio = NULL;
    pthread_mutex_unlock(&g_audio_lock);
    if (g_dnssd) {
        dnssd_unregister_raop(g_dnssd);
        dnssd_unregister_airplay(g_dnssd);
        dnssd_destroy(g_dnssd);
        g_dnssd = NULL;
    }
    pthread_mutex_unlock(&g_lock);
    emit("{\"type\":\"stopped\"}");
}

int receiver_is_running(void) { return g_raop != NULL; }

int receiver_video_resync(void) {
    int rc = -1;
    pthread_mutex_lock(&g_mux_lock);
    if (g_mux) rc = fmp4_resync(g_mux);
    pthread_mutex_unlock(&g_mux_lock);
    emit("{\"type\":\"video_resync\",\"replayed\":%s}", rc == 0 ? "true" : "false");
    return rc;
}

int receiver_request_keyframe(void) {
    int rc = -1;
    pthread_mutex_lock(&g_lock);
    if (g_raop) rc = raop_request_keyframe(g_raop);
    pthread_mutex_unlock(&g_lock);
    return rc;
}

int receiver_video_can_resync(void) {
    int ok = 0;
    pthread_mutex_lock(&g_mux_lock);
    if (g_mux) ok = fmp4_can_resync(g_mux);
    pthread_mutex_unlock(&g_mux_lock);
    return ok;
}

int receiver_take_dev_flag(const char *name) {
    char path[128];
    snprintf(path, sizeof path, "/scratch/dev-%s", name);
    return unlink(path) == 0;
}

void receiver_crash_for_test(void) {
    volatile int *p = NULL;
    *p = 1;
}

/* Value of "key:" (in kB) in a /proc file such as /proc/self/status, or -1. */
static long proc_kb(const char *path, const char *key) {
    char line[128];
    long kb = -1;
    size_t n = strlen(key);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, key, n) == 0 && line[n] == ':') {
            kb = strtol(line + n + 1, NULL, 10);
            break;
        }
    }
    fclose(f);
    return kb;
}

/* First line of `path` into buf (empty on failure). */
static void proc_line(const char *path, char *buf, size_t len) {
    FILE *f = fopen(path, "r");
    buf[0] = '\0';
    if (!f) return;
    if (!fgets(buf, (int)len, f)) buf[0] = '\0';
    fclose(f);
}

/* Appends `"key":value` (or `"key":null` when value < 0) to out. */
static void put_num(char **out, size_t *left, const char *key, double value) {
    int n = value < 0 ? snprintf(*out, *left, ",\"%s\":null", key) : snprintf(*out, *left, ",\"%s\":%.2f", key, value);
    if (n > 0 && (size_t)n < *left) {
        *out += n;
        *left -= (size_t)n;
    }
}

/* System and process counters. The app's /proc shows only per-process
 * entries (no /proc/meminfo, /proc/stat, /proc/loadavg, /proc/pressure), so
 * device-wide figures come from sysinfo() and process CPU from getrusage().
 * Anything unavailable is null, never 0. */
static void system_stats(char *out, size_t len) {
    char line[128], *p = out;
    size_t left = len;
    struct sysinfo si;
    struct rusage ru;
    double unit;
    long freq = -1, freq_max = -1, fds = -1;
    DIR *d;

    out[0] = '\0';
    if (sysinfo(&si) == 0) {
        unit = (double)si.mem_unit / 1024.0;
        put_num(&p, &left, "mem_free_kb", (double)(si.freeram + si.bufferram) * unit);
        put_num(&p, &left, "mem_total_kb", (double)si.totalram * unit);
        put_num(&p, &left, "swap_total_kb", (double)si.totalswap * unit);
        put_num(&p, &left, "swap_free_kb", (double)si.freeswap * unit);
        put_num(&p, &left, "load1", si.loads[0] / 65536.0);
    }
    put_num(&p, &left, "mem_available_kb", (double)proc_kb("/proc/meminfo", "MemAvailable"));
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        put_num(&p, &left, "proc_cpu_ms", ru.ru_utime.tv_sec * 1e3 + ru.ru_utime.tv_usec / 1e3 + ru.ru_stime.tv_sec * 1e3 +
                                              ru.ru_stime.tv_usec / 1e3);
    }
    put_num(&p, &left, "ncpu", (double)sysconf(_SC_NPROCESSORS_ONLN));
    proc_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", line, sizeof line);
    if (sscanf(line, "%ld", &freq) != 1) freq = -1;
    proc_line("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", line, sizeof line);
    if (sscanf(line, "%ld", &freq_max) != 1) freq_max = -1;
    put_num(&p, &left, "cpu_freq_khz", (double)freq);
    put_num(&p, &left, "cpu_freq_max_khz", (double)freq_max);
    put_num(&p, &left, "threads", (double)proc_kb("/proc/self/status", "Threads"));
    if ((d = opendir("/proc/self/fd")) != NULL) {
        fds = -2; /* . and .. */
        while (readdir(d)) fds++;
        closedir(d);
    }
    put_num(&p, &left, "fds", (double)fds);
}

char *receiver_stats_json(void) {
    char *json = NULL;
    uint64_t keyframes = 0, concealed = 0;
    char sys[768];
    pthread_mutex_lock(&g_mux_lock);
    if (g_mux) keyframes = fmp4_keyframes(g_mux);
    pthread_mutex_unlock(&g_mux_lock);
    pthread_mutex_lock(&g_audio_lock);
    if (g_audio) concealed = audio_concealed(g_audio);
    pthread_mutex_unlock(&g_audio_lock);
    system_stats(sys, sizeof sys);
    if (asprintf(&json,
                 "{\"running\":%s,\"connections\":%d,\"video_frames\":%" PRIuFAST64 ",\"video_bytes\":%" PRIuFAST64
                 ",\"audio_packets\":%" PRIuFAST64 ",\"audio_bytes\":%" PRIuFAST64 ",\"audio_ct\":%d,\"audio_concealed_ms\":%" PRIu64 ",\"keyframes\":%" PRIu64
                 ",\"rss_kb\":%ld,\"rss_peak_kb\":%ld%s}",
                 g_raop ? "true" : "false", atomic_load(&g_connections), atomic_load(&g_video_frames),
                 atomic_load(&g_video_bytes), atomic_load(&g_audio_packets), atomic_load(&g_audio_bytes),
                 atomic_load(&g_audio_ct), concealed * 1000 / 44100, keyframes,
                 proc_kb("/proc/self/status", "VmRSS"), proc_kb("/proc/self/status", "VmHWM"), sys) < 0)
        json = NULL;
    return json;
}
