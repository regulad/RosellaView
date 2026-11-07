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
#include <sys/stat.h>
#include <time.h>

#include "audio.h"
#include "dnssd.h"
#include "fmp4.h"
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

/* Development aid: every event is also appended to the app's scratch dir,
 * because the system logger rate-limits the app once the media player gets
 * chatty. Read it from the component shell: cat /scratch/rosellaview-events.log */
#define JOURNAL "/scratch/rosellaview-events.log"
#define JOURNAL_MAX (1024 * 1024)
static void journal(const char *json) {
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    FILE *f;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    pthread_mutex_lock(&lock);
    if ((f = fopen(JOURNAL, "a")) != NULL) {
        long size;
        fprintf(f, "%ld.%03ld %s\n", (long)ts.tv_sec, ts.tv_nsec / 1000000, json);
        size = ftell(f);
        fclose(f);
        if (size > JOURNAL_MAX) rename(JOURNAL, JOURNAL ".1"); /* /scratch is RAM: keep ~2 MB at most */
    }
    pthread_mutex_unlock(&lock);
}

static void emit(const char *fmt, ...) {
    va_list ap;
    char *json = NULL;
    va_start(ap, fmt);
    if (vasprintf(&json, fmt, ap) < 0) json = NULL;
    va_end(ap);
    if (!json) return;
    journal(json);
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

static void cb_log(void *cls, int level, const char *msg) {
    char *m;
    (void)cls;
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
    emit("{\"type\":\"client_request\",\"deviceid\":\"%s\",\"model\":\"%s\",\"name\":\"%s\"}", d, m, n);
    free(d);
    free(m);
    free(n);
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
    if (g_audio) audio_push(g_audio, data->data, (size_t)data->data_len);
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
    if (g_audio) rc = audio_set_format(g_audio, *ct);
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
    if (!g_audio) g_audio = audio_create(audio_output, NULL);
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

char *receiver_stats_json(void) {
    char *json = NULL;
    uint64_t keyframes = 0;
    pthread_mutex_lock(&g_mux_lock);
    if (g_mux) keyframes = fmp4_keyframes(g_mux);
    pthread_mutex_unlock(&g_mux_lock);
    if (asprintf(&json,
                 "{\"running\":%s,\"connections\":%d,\"video_frames\":%" PRIuFAST64 ",\"video_bytes\":%" PRIuFAST64
                 ",\"audio_packets\":%" PRIuFAST64 ",\"audio_bytes\":%" PRIuFAST64 ",\"audio_ct\":%d,\"keyframes\":%" PRIu64 "}",
                 g_raop ? "true" : "false", atomic_load(&g_connections), atomic_load(&g_video_frames),
                 atomic_load(&g_video_bytes), atomic_load(&g_audio_packets), atomic_load(&g_audio_bytes),
                 atomic_load(&g_audio_ct), keyframes) < 0)
        json = NULL;
    if (json && atomic_load(&g_connections) > 0) journal(json);
    return json;
}
