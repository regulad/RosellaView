/*
 * fmp4.c - H.264 Annex-B access units → fragmented MP4 (ISO/IEC 14496-12/-15)
 * for Media Source Extensions: one init segment (ftyp+moov) per SPS/PPS set,
 * then one moof+mdat per access unit.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "fmp4.h"

#include "sps.h"

#include <stdlib.h>
#include <string.h>

#define TIMESCALE 90000u
#define DEFAULT_DURATION (TIMESCALE / 60u)
/* The frame rate declared in the SPS. Vega's player declares a discontinuity
 * (and drops everything until the next keyframe) at any frame lasting over
 * twice the declared frame duration, so declare a low rate and real
 * durations up to MAX_DURATION play as given (docs/DECISIONS.md D4). */
#define VUI_FPS 10u
/* Senders may send a single keyframe per session, and MSE only evicts
 * buffered video in whole runs from one sync sample to the next, so with real
 * keyframes alone the SourceBuffer fills (QuotaExceededError after ~130 s of a
 * Mac's 8 Mbit/s) and playback freezes. So a frame is flagged as a sync
 * sample at least this often. The flag only tells the player where it may
 * cut; the decoder, which has already decoded everything before it, carries
 * on unaffected. */
#define RAP_INTERVAL TIMESCALE
#define MIN_DURATION (TIMESCALE / 1000u)
#define MAX_DURATION (2u * TIMESCALE / VUI_FPS - TIMESCALE / 100u) /* 190 ms */
#define MAX_PS 256
#define GOP_CACHE_MAX (16u * 1024u * 1024u) /* frames kept since the last keyframe, for resync */
#define REPLAY_DURATION (TIMESCALE / 1000u) /* 1 ms: replayed frames decode, then live resumes */

struct fmp4_muxer {
    fmp4_output out;
    void *ctx;
    unsigned width, height;
    uint8_t sps[MAX_PS], pps[MAX_PS];
    size_t sps_len, pps_len;
    int have_init;
    uint32_t sequence;
    uint64_t decode_time; /* in TIMESCALE units */
    uint64_t last_rap_time; /* decode_time of the last fragment flagged as a sync sample */
    uint64_t last_ts_ns;
    int have_last_ts;
    uint64_t keyframes;
    /* Samples since the last keyframe, as [u32 length][u8 sync][bytes]...;
     * gop_valid is 0 until a keyframe arrives or after the cache overflows. */
    uint8_t *gop;
    size_t gop_len, gop_cap;
    int gop_valid;
};

/* ---------- growable byte buffer ---------- */

typedef struct {
    uint8_t *b;
    size_t n, cap;
} buf_t;

static void put(buf_t *w, const void *d, size_t len) {
    if (w->n + len > w->cap) {
        size_t cap = w->cap ? w->cap : 1024;
        while (cap < w->n + len) cap *= 2;
        w->b = realloc(w->b, cap);
        w->cap = cap;
    }
    memcpy(w->b + w->n, d, len);
    w->n += len;
}
static void u8(buf_t *w, uint8_t v) { put(w, &v, 1); }
static void u16(buf_t *w, uint16_t v) {
    uint8_t x[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    put(w, x, 2);
}
static void u32(buf_t *w, uint32_t v) {
    uint8_t x[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    put(w, x, 4);
}
static void u64(buf_t *w, uint64_t v) {
    u32(w, (uint32_t)(v >> 32));
    u32(w, (uint32_t)v);
}
static void zeros(buf_t *w, size_t n) {
    while (n--) u8(w, 0);
}
static void patch32(buf_t *w, size_t at, uint32_t v) {
    w->b[at] = (uint8_t)(v >> 24);
    w->b[at + 1] = (uint8_t)(v >> 16);
    w->b[at + 2] = (uint8_t)(v >> 8);
    w->b[at + 3] = (uint8_t)v;
}

/* Boxes nest by recording the size field's offset and patching it on close. */
static size_t box(buf_t *w, const char *type) {
    size_t at = w->n;
    u32(w, 0);
    put(w, type, 4);
    return at;
}
static size_t fullbox(buf_t *w, const char *type, uint8_t version, uint32_t flags) {
    size_t at = box(w, type);
    u32(w, ((uint32_t)version << 24) | (flags & 0xffffff));
    return at;
}
static void end(buf_t *w, size_t at) { patch32(w, at, (uint32_t)(w->n - at)); }

/* ---------- Annex-B parsing ---------- */

/* Finds the next NAL unit at or after *pos; returns its start and sets *len. */
static const uint8_t *next_nal(const uint8_t *b, size_t n, size_t *pos, size_t *len) {
    size_t i = *pos, start;
    while (i + 3 <= n && !(b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1)) i++;
    if (i + 3 > n) return NULL;
    start = i + 3;
    i = start;
    while (i + 3 <= n && !(b[i] == 0 && b[i + 1] == 0 && (b[i + 2] == 1 || (b[i + 2] == 0 && i + 3 < n && b[i + 3] == 1))))
        i++;
    if (i + 3 > n) i = n;
    *len = i - start;
    *pos = i;
    return b + start;
}

/* ---------- segments ---------- */

static void emit_init(fmp4_muxer *m) {
    buf_t w = {0};
    uint8_t sps[MAX_PS + 32];
    /* Vega's MSE pipeline assumes 1 ms per frame without VUI timing; see VUI_FPS. */
    size_t sps_len = sps_set_timing(m->sps, m->sps_len, VUI_FPS, sps, sizeof sps);
    size_t moov, trak, mdia, minf, dinf, dref, stbl, stsd, avc1, avcc, mvex, a;
    unsigned wpx = m->width ? m->width : 1920, hpx = m->height ? m->height : 1080;
    unsigned sps_w, sps_h;
    /* The SPS is authoritative: a rotated phone changes it in-band, without a
     * new video_set_codec_and_size from libairplay. */
    if (sps_get_size(m->sps, m->sps_len, &sps_w, &sps_h) == 0 && sps_w && sps_h) {
        wpx = sps_w;
        hpx = sps_h;
    }

    a = box(&w, "ftyp");
    put(&w, "isom", 4);
    u32(&w, 0x200);
    put(&w, "isomiso5avc1mp41", 16);
    end(&w, a);

    moov = box(&w, "moov");
    a = fullbox(&w, "mvhd", 0, 0);
    u32(&w, 0);           /* creation time */
    u32(&w, 0);           /* modification time */
    u32(&w, 1000);        /* timescale */
    u32(&w, 0);           /* duration: fragmented */
    u32(&w, 0x00010000);  /* rate 1.0 */
    u16(&w, 0x0100);      /* volume 1.0 */
    zeros(&w, 10);
    u32(&w, 0x00010000); u32(&w, 0); u32(&w, 0); /* unity matrix */
    u32(&w, 0); u32(&w, 0x00010000); u32(&w, 0);
    u32(&w, 0); u32(&w, 0); u32(&w, 0x40000000);
    zeros(&w, 24);        /* pre_defined */
    u32(&w, 2);           /* next track id */
    end(&w, a);

    trak = box(&w, "trak");
    a = fullbox(&w, "tkhd", 0, 3); /* enabled | in movie */
    u32(&w, 0);
    u32(&w, 0);
    u32(&w, 1);          /* track id */
    u32(&w, 0);
    u32(&w, 0);          /* duration */
    zeros(&w, 8);
    u16(&w, 0);          /* layer */
    u16(&w, 0);          /* alternate group */
    u16(&w, 0);          /* volume: video */
    u16(&w, 0);
    u32(&w, 0x00010000); u32(&w, 0); u32(&w, 0);
    u32(&w, 0); u32(&w, 0x00010000); u32(&w, 0);
    u32(&w, 0); u32(&w, 0); u32(&w, 0x40000000);
    u32(&w, wpx << 16);
    u32(&w, hpx << 16);
    end(&w, a);

    mdia = box(&w, "mdia");
    a = fullbox(&w, "mdhd", 0, 0);
    u32(&w, 0);
    u32(&w, 0);
    u32(&w, TIMESCALE);
    u32(&w, 0);
    u16(&w, 0x55c4);     /* language "und" */
    u16(&w, 0);
    end(&w, a);
    a = fullbox(&w, "hdlr", 0, 0);
    u32(&w, 0);
    put(&w, "vide", 4);
    zeros(&w, 12);
    put(&w, "RosellaView", 12); /* name, NUL-terminated */
    end(&w, a);

    minf = box(&w, "minf");
    a = fullbox(&w, "vmhd", 0, 1);
    zeros(&w, 8);
    end(&w, a);
    dinf = box(&w, "dinf");
    dref = fullbox(&w, "dref", 0, 0);
    u32(&w, 1);
    a = fullbox(&w, "url ", 0, 1); /* self-contained */
    end(&w, a);
    end(&w, dref);
    end(&w, dinf);

    stbl = box(&w, "stbl");
    stsd = fullbox(&w, "stsd", 0, 0);
    u32(&w, 1);
    avc1 = box(&w, "avc1");
    zeros(&w, 6);
    u16(&w, 1);          /* data reference index */
    zeros(&w, 16);
    u16(&w, (uint16_t)wpx);
    u16(&w, (uint16_t)hpx);
    u32(&w, 0x00480000); /* 72 dpi */
    u32(&w, 0x00480000);
    u32(&w, 0);
    u16(&w, 1);          /* frame count */
    zeros(&w, 32);       /* compressor name */
    u16(&w, 0x0018);     /* depth */
    u16(&w, 0xffff);
    avcc = box(&w, "avcC");
    u8(&w, 1);
    u8(&w, m->sps[1]);   /* profile */
    u8(&w, m->sps[2]);   /* compatibility */
    u8(&w, m->sps[3]);   /* level */
    u8(&w, 0xff);        /* 4-byte NAL lengths */
    u8(&w, 0xe1);        /* one SPS */
    if (sps_len == 0) { /* unparseable: fall back to the sender's SPS */
        memcpy(sps, m->sps, m->sps_len);
        sps_len = m->sps_len;
    }
    u16(&w, (uint16_t)sps_len);
    put(&w, sps, sps_len);
    u8(&w, 1);           /* one PPS */
    u16(&w, (uint16_t)m->pps_len);
    put(&w, m->pps, m->pps_len);
    end(&w, avcc);
    end(&w, avc1);
    end(&w, stsd);
    a = fullbox(&w, "stts", 0, 0); u32(&w, 0); end(&w, a);
    a = fullbox(&w, "stsc", 0, 0); u32(&w, 0); end(&w, a);
    a = fullbox(&w, "stsz", 0, 0); u32(&w, 0); u32(&w, 0); end(&w, a);
    a = fullbox(&w, "stco", 0, 0); u32(&w, 0); end(&w, a);
    end(&w, stbl);
    end(&w, minf);
    end(&w, mdia);
    end(&w, trak);

    mvex = box(&w, "mvex");
    a = fullbox(&w, "trex", 0, 0);
    u32(&w, 1);          /* track id */
    u32(&w, 1);          /* default sample description */
    u32(&w, 0);
    u32(&w, 0);
    u32(&w, 0);
    end(&w, a);
    end(&w, mvex);
    end(&w, moov);

    m->out(m->ctx, FMP4_INIT, w.b, w.n);
}

/* One moof+mdat holding a single sample. Duration, size and flags go in tfhd
 * as defaults, the way ffmpeg writes fragments: Vega's MSE demuxer ignores
 * per-sample trun durations and falls back to 1 ms, which then trips its
 * discontinuity check. */
static void write_fragment(buf_t *w, uint32_t sequence, uint64_t decode_time, const uint8_t *sample, size_t len,
                           uint32_t duration, int sync) {
    size_t moof, traf, trun, a, data_offset_at, mdat;

    moof = box(w, "moof");
    a = fullbox(w, "mfhd", 0, 0);
    u32(w, sequence);
    end(w, a);
    traf = box(w, "traf");
    /* default-base-is-moof | default-sample-duration | -size | -flags */
    a = fullbox(w, "tfhd", 0, 0x020000 | 0x000008 | 0x000010 | 0x000020);
    u32(w, 1);
    u32(w, duration);
    u32(w, (uint32_t)len);
    u32(w, 0x01010000u); /* default: non-sync, depends on others */
    end(w, a);
    a = fullbox(w, "tfdt", 1, 0);
    u64(w, decode_time);
    end(w, a);
    /* data-offset, plus first-sample-flags on sync samples */
    trun = fullbox(w, "trun", 0, sync ? (0x000001 | 0x000004) : 0x000001);
    u32(w, 1);
    data_offset_at = w->n;
    u32(w, 0);
    if (sync) u32(w, 0x02000000u); /* depends on nothing */
    end(w, trun);
    end(w, traf);
    end(w, moof);

    mdat = box(w, "mdat");
    put(w, sample, len);
    end(w, mdat);
    patch32(w, data_offset_at, (uint32_t)(mdat + 8 - moof));
}

static void emit_media(fmp4_muxer *m, const buf_t *sample, int sync, uint32_t duration) {
    buf_t w = {0};
    write_fragment(&w, ++m->sequence, m->decode_time, sample->b, sample->n, duration, sync);
    m->decode_time += duration;
    m->out(m->ctx, FMP4_MEDIA, w.b, w.n);
}

/* ---------- public API ---------- */

fmp4_muxer *fmp4_create(fmp4_output out, void *ctx) {
    fmp4_muxer *m = calloc(1, sizeof *m);
    m->out = out;
    m->ctx = ctx;
    return m;
}

void fmp4_destroy(fmp4_muxer *m) {
    if (!m) return;
    free(m->gop);
    free(m);
}

static void gop_add(fmp4_muxer *m, const buf_t *sample, int sync) {
    size_t need;
    if (sync) {
        m->gop_len = 0;
        m->gop_valid = 1;
    }
    if (!m->gop_valid) return;
    need = m->gop_len + 5 + sample->n;
    if (need > GOP_CACHE_MAX) {
        m->gop_valid = 0; /* too long a GOP to replay; resync waits for a keyframe */
        m->gop_len = 0;
        return;
    }
    if (need > m->gop_cap) {
        size_t cap = m->gop_cap ? m->gop_cap : 256 * 1024;
        while (cap < need) cap *= 2;
        m->gop = realloc(m->gop, cap);
        m->gop_cap = cap;
    }
    m->gop[m->gop_len++] = (uint8_t)(sample->n >> 24);
    m->gop[m->gop_len++] = (uint8_t)(sample->n >> 16);
    m->gop[m->gop_len++] = (uint8_t)(sample->n >> 8);
    m->gop[m->gop_len++] = (uint8_t)sample->n;
    m->gop[m->gop_len++] = (uint8_t)sync;
    memcpy(m->gop + m->gop_len, sample->b, sample->n);
    m->gop_len += sample->n;
}

uint64_t fmp4_keyframes(const fmp4_muxer *m) { return m->keyframes; }

int fmp4_can_resync(const fmp4_muxer *m) { return m->have_init && m->gop_valid && m->gop_len > 0; }

int fmp4_resync(fmp4_muxer *m) {
    size_t pos = 0;
    if (!fmp4_can_resync(m)) return -1;
    emit_init(m);
    while (pos + 5 <= m->gop_len) {
        size_t len = (size_t)m->gop[pos] << 24 | (size_t)m->gop[pos + 1] << 16 | (size_t)m->gop[pos + 2] << 8 |
                     m->gop[pos + 3];
        buf_t sample = {m->gop + pos + 5, len, len};
        emit_media(m, &sample, m->gop[pos + 4], REPLAY_DURATION);
        pos += 5 + len;
    }
    return 0;
}

size_t fmp4_get_sender_sps(const fmp4_muxer *m, uint8_t *out, size_t cap) {
    if (m->sps_len > cap) return 0;
    memcpy(out, m->sps, m->sps_len);
    return m->sps_len;
}

void fmp4_set_size(fmp4_muxer *m, unsigned width, unsigned height) {
    if (m->width != width || m->height != height) {
        m->width = width;
        m->height = height;
        m->have_init = 0; /* re-emit the init segment with the new size */
    }
}

void fmp4_reset(fmp4_muxer *m) {
    m->sps_len = m->pps_len = 0;
    m->have_init = 0;
    m->have_last_ts = 0;
    m->gop_valid = 0;
    m->gop_len = 0;
}

void fmp4_push_h264(fmp4_muxer *m, const uint8_t *au, size_t len, uint64_t ts_ns) {
    buf_t sample = {0};
    size_t pos = 0, nal_len;
    const uint8_t *nal;
    int sync = 0, ps_changed = 0;
    uint32_t duration = DEFAULT_DURATION;

    while ((nal = next_nal(au, len, &pos, &nal_len)) != NULL) {
        int type;
        if (nal_len == 0) continue;
        type = nal[0] & 0x1f;
        if (type == 7 || type == 8) {
            uint8_t *dst = type == 7 ? m->sps : m->pps;
            size_t *dst_len = type == 7 ? &m->sps_len : &m->pps_len;
            if (nal_len <= MAX_PS && (nal_len != *dst_len || memcmp(dst, nal, nal_len) != 0)) {
                memcpy(dst, nal, nal_len);
                *dst_len = nal_len;
                ps_changed = 1;
            }
            continue;
        }
        if (type == 9) continue; /* access unit delimiter */
        if (type == 5) sync = 1;
        u32(&sample, (uint32_t)nal_len);
        put(&sample, nal, nal_len);
    }

    if (m->sps_len < 4 || m->pps_len == 0 || sample.n == 0) {
        free(sample.b);
        return;
    }
    if (ps_changed) m->have_init = 0;
    if (!m->have_init) {
        if (!sync) { /* a new init segment must be followed by a keyframe */
            free(sample.b);
            return;
        }
        emit_init(m);
        m->have_init = 1;
    }

    /* Senders run at the content's rate (a Mac playing a film sends ~24 fps,
     * an iPhone up to 60), so each frame lasts as long as the interval before
     * it: the next frame's time isn't known yet, and waiting for it would add
     * a frame of latency. Over time the media clock then tracks the sender's.
     * Pauses longer than MAX_DURATION (a still screen sends nothing) are cut
     * short; the player just waits for the next frame. */
    if (m->have_last_ts && ts_ns > m->last_ts_ns) {
        uint64_t ticks = (ts_ns - m->last_ts_ns) * TIMESCALE / 1000000000u;
        duration = ticks < MIN_DURATION ? MIN_DURATION : ticks > MAX_DURATION ? MAX_DURATION : (uint32_t)ticks;
    }
    m->last_ts_ns = ts_ns;
    m->have_last_ts = 1;

    if (sync) m->keyframes++;
    gop_add(m, &sample, sync);
    if (sync || m->decode_time - m->last_rap_time >= RAP_INTERVAL) {
        m->last_rap_time = m->decode_time;
        emit_media(m, &sample, 1, duration);
    } else {
        emit_media(m, &sample, 0, duration);
    }
    free(sample.b);
}

/* ---------- audio track ---------- */

#define MAX_AUDIO_CONFIG 64

struct fmp4_audio {
    fmp4_output out;
    void *ctx;
    enum fmp4_audio_codec codec;
    unsigned rate, channels;
    uint8_t config[MAX_AUDIO_CONFIG];
    size_t config_len;
    int configured, have_init;
    uint32_t sequence;
    uint64_t decode_time; /* in samples */
};

/* MPEG-4 descriptor (ISO/IEC 14496-1 8.3.3) with a one-byte length. */
static size_t descriptor(buf_t *w, uint8_t tag) {
    size_t at;
    u8(w, tag);
    at = w->n;
    u8(w, 0);
    return at;
}
static void end_descriptor(buf_t *w, size_t at) { w->b[at] = (uint8_t)(w->n - at - 1); }

static void emit_audio_init(fmp4_audio *a) {
    buf_t w = {0};
    size_t moov, trak, mdia, minf, dinf, dref, stbl, stsd, entry, b;

    b = box(&w, "ftyp");
    put(&w, "isom", 4);
    u32(&w, 0x200);
    put(&w, "isomiso5mp41", 12);
    end(&w, b);

    moov = box(&w, "moov");
    b = fullbox(&w, "mvhd", 0, 0);
    u32(&w, 0);
    u32(&w, 0);
    u32(&w, 1000);
    u32(&w, 0);
    u32(&w, 0x00010000);
    u16(&w, 0x0100);
    zeros(&w, 10);
    u32(&w, 0x00010000); u32(&w, 0); u32(&w, 0);
    u32(&w, 0); u32(&w, 0x00010000); u32(&w, 0);
    u32(&w, 0); u32(&w, 0); u32(&w, 0x40000000);
    zeros(&w, 24);
    u32(&w, 2);
    end(&w, b);

    trak = box(&w, "trak");
    b = fullbox(&w, "tkhd", 0, 3);
    u32(&w, 0);
    u32(&w, 0);
    u32(&w, 1);
    u32(&w, 0);
    u32(&w, 0);
    zeros(&w, 8);
    u16(&w, 0);
    u16(&w, 1);      /* alternate group */
    u16(&w, 0x0100); /* volume 1.0 */
    u16(&w, 0);
    u32(&w, 0x00010000); u32(&w, 0); u32(&w, 0);
    u32(&w, 0); u32(&w, 0x00010000); u32(&w, 0);
    u32(&w, 0); u32(&w, 0); u32(&w, 0x40000000);
    u32(&w, 0);
    u32(&w, 0);
    end(&w, b);

    mdia = box(&w, "mdia");
    b = fullbox(&w, "mdhd", 0, 0);
    u32(&w, 0);
    u32(&w, 0);
    u32(&w, a->rate); /* timescale = sample rate */
    u32(&w, 0);
    u16(&w, 0x55c4);
    u16(&w, 0);
    end(&w, b);
    b = fullbox(&w, "hdlr", 0, 0);
    u32(&w, 0);
    put(&w, "soun", 4);
    zeros(&w, 12);
    put(&w, "RosellaView", 12);
    end(&w, b);

    minf = box(&w, "minf");
    b = fullbox(&w, "smhd", 0, 0);
    u32(&w, 0); /* balance, reserved */
    end(&w, b);
    dinf = box(&w, "dinf");
    dref = fullbox(&w, "dref", 0, 0);
    u32(&w, 1);
    b = fullbox(&w, "url ", 0, 1);
    end(&w, b);
    end(&w, dref);
    end(&w, dinf);

    stbl = box(&w, "stbl");
    stsd = fullbox(&w, "stsd", 0, 0);
    u32(&w, 1);
    entry = box(&w, a->codec == FMP4_AUDIO_FLAC ? "fLaC" : "mp4a");
    zeros(&w, 6);
    u16(&w, 1); /* data reference index */
    zeros(&w, 8);
    u16(&w, (uint16_t)a->channels);
    u16(&w, 16); /* sample size */
    u16(&w, 0);
    u16(&w, 0);
    u32(&w, a->rate << 16);
    if (a->codec == FMP4_AUDIO_FLAC) {
        b = fullbox(&w, "dfLa", 0, 0);
        u8(&w, 0x80); /* last metadata block, type 0 = STREAMINFO */
        u8(&w, 0);
        u16(&w, (uint16_t)a->config_len);
        put(&w, a->config, a->config_len);
        end(&w, b);
    } else {
        size_t es, dc, dsi, sl;
        b = fullbox(&w, "esds", 0, 0);
        es = descriptor(&w, 0x03); /* ES_Descriptor */
        u16(&w, 1);                /* ES_ID */
        u8(&w, 0);
        dc = descriptor(&w, 0x04); /* DecoderConfigDescriptor */
        u8(&w, 0x40);              /* MPEG-4 Audio */
        u8(&w, 0x15);              /* audio stream */
        u8(&w, 0);
        u16(&w, 0);                /* buffer size */
        u32(&w, 0);                /* max bitrate */
        u32(&w, 0);                /* avg bitrate */
        dsi = descriptor(&w, 0x05); /* DecoderSpecificInfo: AudioSpecificConfig */
        put(&w, a->config, a->config_len);
        end_descriptor(&w, dsi);
        end_descriptor(&w, dc);
        sl = descriptor(&w, 0x06); /* SLConfigDescriptor */
        u8(&w, 0x02);
        end_descriptor(&w, sl);
        end_descriptor(&w, es);
        end(&w, b);
    }
    end(&w, entry);
    end(&w, stsd);
    b = fullbox(&w, "stts", 0, 0); u32(&w, 0); end(&w, b);
    b = fullbox(&w, "stsc", 0, 0); u32(&w, 0); end(&w, b);
    b = fullbox(&w, "stsz", 0, 0); u32(&w, 0); u32(&w, 0); end(&w, b);
    b = fullbox(&w, "stco", 0, 0); u32(&w, 0); end(&w, b);
    end(&w, stbl);
    end(&w, minf);
    end(&w, mdia);
    end(&w, trak);

    b = box(&w, "mvex");
    {
        size_t t = fullbox(&w, "trex", 0, 0);
        u32(&w, 1);
        u32(&w, 1);
        u32(&w, 0);
        u32(&w, 0);
        u32(&w, 0);
        end(&w, t);
    }
    end(&w, b);
    end(&w, moov);

    a->out(a->ctx, FMP4_INIT, w.b, w.n);
}

fmp4_audio *fmp4_audio_create(fmp4_output out, void *ctx) {
    fmp4_audio *a = calloc(1, sizeof *a);
    a->out = out;
    a->ctx = ctx;
    return a;
}

void fmp4_audio_destroy(fmp4_audio *a) { free(a); }

void fmp4_audio_configure(fmp4_audio *a, enum fmp4_audio_codec codec, unsigned sample_rate, unsigned channels,
                          const uint8_t *config, size_t config_len) {
    if (config_len > MAX_AUDIO_CONFIG) config_len = MAX_AUDIO_CONFIG;
    a->codec = codec;
    a->rate = sample_rate;
    a->channels = channels;
    memcpy(a->config, config, config_len);
    a->config_len = config_len;
    a->configured = 1;
    a->have_init = 0;
    a->decode_time = 0;
}

void fmp4_audio_push(fmp4_audio *a, const uint8_t *sample, size_t len, uint32_t duration) {
    buf_t w = {0};
    if (!a->configured || len == 0) return;
    if (!a->have_init) {
        emit_audio_init(a);
        a->have_init = 1;
    }
    write_fragment(&w, ++a->sequence, a->decode_time, sample, len, duration, 1);
    a->decode_time += duration;
    a->out(a->ctx, FMP4_MEDIA, w.b, w.n);
}
