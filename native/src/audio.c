/*
 * audio.c - AirPlay audio → fragmented MP4 that Vega's MSE plays.
 *
 * Vega's MSE plays FLAC and AAC-LC but not ALAC, AAC-ELD or raw PCM
 * (docs/DECISIONS.md D3), so:
 *   ct 4 (AAC-LC)            passes through untouched;
 *   ct 2 (ALAC), 8 (AAC-ELD) are decoded with libavcodec, then
 *   ct 1 (PCM) and decoded audio get the spoken watermark mixed in (while
 *   enabled) and are wrapped in FLAC VERBATIM frames
 *   (no compression, so the CPU cost is about that of a copy).
 * All AirPlay formats here are 44.1 kHz stereo.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "audio.h"

#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>

#include "flac.h"

#define RATE 44100
#define CHANNELS 2

/* Decoder configuration for each AirPlay compression type, as in UxPlay's
 * renderers/audio_renderer.c. */
static const uint8_t ALAC_COOKIE[36] = {
    0x00, 0x00, 0x00, 0x24, 0x61, 0x6c, 0x61, 0x63, 0x00, 0x00, 0x00, 0x00, /* size, 'alac', version */
    0x00, 0x00, 0x01, 0x60, 0x00, 0x10, 0x28, 0x0a, 0x0e, 0x02, 0x00, 0xff, /* 352 frames, 16-bit, 2 ch */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xac, 0x44, /* 44100 Hz */
};
static const uint8_t AAC_LC_ASC[2] = {0x12, 0x10};
static const uint8_t AAC_ELD_ASC[4] = {0xf8, 0xe8, 0x50, 0x00};
#define AAC_LC_FRAME 1024

#define PREVIEW_INTERVAL (5 * RATE) /* samples between spoken watermarks */
#define PREVIEW_FIRST (2 * RATE)     /* the first comes this far into a stream */
#define DUCK_FULL 1024               /* programme gain, Q10 */
#define DUCK_LOW 307                 /* ~30% while the voice speaks */
#define DUCK_STEP 2                  /* per sample: ~8 ms ramps, so ducking doesn't click */
#define LIMIT_KNEE 24576             /* soft limiter knee (~-2.5 dBFS) */

#define MAX_CONCEAL RATE /* gaps longer than this are a new timeline, not loss */

struct audio_pipeline {
    fmp4_audio *mux;
    unsigned ct;
    AVCodecContext *dec;
    AVPacket *pkt;
    AVFrame *frame;
    int16_t pcm[FLAC_BLOCK * CHANNELS]; /* samples waiting for a full FLAC block */
    size_t pcm_frames;
    uint64_t flac_frame;
    uint8_t *flac_buf;
    int16_t *preview;          /* spoken watermark, mono */
    size_t preview_len;
    int preview_enabled;
    size_t preview_pos;        /* < preview_len while the clip is playing */
    uint64_t preview_wait;     /* samples until the next clip starts */
    int duck;                  /* current programme gain, Q10 */
    int have_rtp;
    uint32_t next_rtp;         /* RTP timestamp expected for the next packet */
    uint64_t concealed;
    uint64_t in_ts_ns;  /* sender time of the first sample of the current input, 0 if unknown */
    size_t in_pos;      /* samples of the current input queued so far */
    uint64_t block_ts_ns; /* sender time of the current FLAC block's first sample */
    size_t drop_frames;    /* upcoming samples to drop (audio_shift: play sooner) */
    size_t insert_frames;  /* silence to insert before the next packet (play later) */
};

static void close_decoder(audio_pipeline *p) {
    avcodec_free_context(&p->dec);
}

static int open_decoder(audio_pipeline *p, enum AVCodecID id, const uint8_t *extradata, size_t len) {
    const AVCodec *codec = avcodec_find_decoder(id);
    if (!codec) return -1;
    p->dec = avcodec_alloc_context3(codec);
    if (!p->dec) return -1;
    p->dec->sample_rate = RATE;
    av_channel_layout_default(&p->dec->ch_layout, CHANNELS);
    p->dec->extradata = av_mallocz(len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!p->dec->extradata) return -1;
    memcpy(p->dec->extradata, extradata, len);
    p->dec->extradata_size = (int)len;
    if (avcodec_open2(p->dec, codec, NULL) < 0) {
        close_decoder(p);
        return -1;
    }
    return 0;
}

audio_pipeline *audio_create(fmp4_output out, void *ctx) {
    audio_pipeline *p = calloc(1, sizeof *p);
    p->mux = fmp4_audio_create(out, ctx);
    p->pkt = av_packet_alloc();
    p->frame = av_frame_alloc();
    p->flac_buf = malloc(flac_max_frame_bytes(CHANNELS));
    p->duck = DUCK_FULL;
    return p;
}

void audio_destroy(audio_pipeline *p) {
    if (!p) return;
    free(p->preview);
    close_decoder(p);
    av_packet_free(&p->pkt);
    av_frame_free(&p->frame);
    fmp4_audio_destroy(p->mux);
    free(p->flac_buf);
    free(p);
}

void audio_set_preview_clip(audio_pipeline *p, const int16_t *clip, size_t samples) {
    free(p->preview);
    p->preview = NULL;
    p->preview_len = 0;
    if (!clip || !samples) return;
    p->preview = malloc(samples * sizeof *clip);
    if (!p->preview) return;
    memcpy(p->preview, clip, samples * sizeof *clip);
    p->preview_len = samples;
    p->preview_pos = samples;
}

void audio_set_preview_enabled(audio_pipeline *p, int enabled) {
    if (enabled && !p->preview_enabled) {
        p->preview_wait = PREVIEW_FIRST;
        p->preview_pos = p->preview_len;
    }
    p->preview_enabled = enabled;
}

/* Above the knee, compresses smoothly towards full scale instead of clipping
 * (hard clipping the boosted voice crackled). */
static int16_t soft_limit(int m) {
    int a = m < 0 ? -m : m;
    if (a > LIMIT_KNEE) {
        const int room = 32767 - LIMIT_KNEE;
        a = LIMIT_KNEE + (int)((int64_t)(a - LIMIT_KNEE) * room / (a - LIMIT_KNEE + room));
    }
    return (int16_t)(m < 0 ? -a : a);
}

/* Mixes the spoken watermark into interleaved s16 frames in place: the voice
 * boosted ~3 dB (x1.4) over the programme, which ramps down to 30% while it
 * speaks, through a soft limiter. */
static void mix_preview(audio_pipeline *p, int16_t *pcm, size_t frames) {
    if (!p->preview_len || (!p->preview_enabled && p->duck == DUCK_FULL)) return;
    for (size_t i = 0; i < frames; i++) {
        int v = 0, speaking = 0;
        if (p->preview_enabled) {
            if (p->preview_pos >= p->preview_len) {
                if (p->preview_wait > 0) {
                    p->preview_wait--;
                } else {
                    p->preview_pos = 0;
                    p->preview_wait = PREVIEW_INTERVAL - p->preview_len;
                }
            }
            if (p->preview_pos < p->preview_len) {
                v = p->preview[p->preview_pos++] * 7 / 5;
                speaking = 1;
            }
        }
        if (speaking && p->duck > DUCK_LOW) {
            p->duck = p->duck - DUCK_STEP < DUCK_LOW ? DUCK_LOW : p->duck - DUCK_STEP;
        } else if (!speaking && p->duck < DUCK_FULL) {
            p->duck = p->duck + DUCK_STEP > DUCK_FULL ? DUCK_FULL : p->duck + DUCK_STEP;
        }
        if (v == 0 && p->duck == DUCK_FULL) continue;
        for (int c = 0; c < CHANNELS; c++) {
            pcm[i * CHANNELS + c] = soft_limit(pcm[i * CHANNELS + c] * p->duck / DUCK_FULL + v);
        }
    }
}

void audio_reset(audio_pipeline *p) {
    p->pcm_frames = 0;
    p->drop_frames = p->insert_frames = 0;
    p->have_rtp = 0;
    if (p->dec) avcodec_flush_buffers(p->dec);
}

int audio_set_format(audio_pipeline *p, unsigned ct) {
    close_decoder(p);
    p->ct = 0;
    p->pcm_frames = 0;
    p->flac_frame = 0;
    p->have_rtp = 0;
    switch (ct) {
    case 4:
        fmp4_audio_configure(p->mux, FMP4_AUDIO_AAC, RATE, CHANNELS, AAC_LC_ASC, sizeof AAC_LC_ASC);
        break;
    case 1:
    case 2:
    case 8: {
        uint8_t streaminfo[34];
        if (ct == 2 && open_decoder(p, AV_CODEC_ID_ALAC, ALAC_COOKIE, sizeof ALAC_COOKIE) < 0) return -1;
        if (ct == 8 && open_decoder(p, AV_CODEC_ID_AAC, AAC_ELD_ASC, sizeof AAC_ELD_ASC) < 0) return -1;
        flac_streaminfo(streaminfo, RATE, CHANNELS, flac_max_frame_bytes(CHANNELS));
        fmp4_audio_configure(p->mux, FMP4_AUDIO_FLAC, RATE, CHANNELS, streaminfo, sizeof streaminfo);
        break;
    }
    default:
        return -1;
    }
    p->ct = ct;
    return 0;
}

/* Appends interleaved s16 frames, emitting a FLAC frame per full block. */
/* Sets the sender time of the input about to be queued. */
static void set_input_time(audio_pipeline *p, uint64_t ts_ns) {
    p->in_ts_ns = ts_ns;
    p->in_pos = 0;
}

static void queue_pcm(audio_pipeline *p, const int16_t *pcm, size_t frames) {
    if (p->drop_frames > 0) {
        size_t skip = p->drop_frames < frames ? p->drop_frames : frames;
        p->drop_frames -= skip;
        p->in_pos += skip; /* the sender time moves on past them */
        pcm += skip * CHANNELS;
        frames -= skip;
    }
    while (frames > 0) {
        size_t take = FLAC_BLOCK - p->pcm_frames;
        if (take > frames) take = frames;
        if (p->pcm_frames == 0) {
            p->block_ts_ns = p->in_ts_ns ? p->in_ts_ns + p->in_pos * 1000000000ull / RATE : 0;
        }
        p->in_pos += take;
        memcpy(p->pcm + p->pcm_frames * CHANNELS, pcm, take * CHANNELS * sizeof *pcm);
        mix_preview(p, p->pcm + p->pcm_frames * CHANNELS, take);
        p->pcm_frames += take;
        pcm += take * CHANNELS;
        frames -= take;
        if (p->pcm_frames == FLAC_BLOCK) {
            size_t len = flac_write_frame(p->flac_buf, p->flac_frame++, RATE, CHANNELS, p->pcm);
            fmp4_audio_push(p->mux, p->flac_buf, len, FLAC_BLOCK, p->block_ts_ns);
            p->pcm_frames = 0;
        }
    }
}

static int16_t clip16(float v) {
    if (v >= 1.0f) return 32767;
    if (v <= -1.0f) return -32768;
    return (int16_t)(v * 32767.0f);
}

/* Converts a decoded frame (planar or packed, float/s16/s32) to s16 interleaved. */
static void queue_frame(audio_pipeline *p, const AVFrame *f) {
    int16_t tmp[512 * CHANNELS];
    int ch = f->ch_layout.nb_channels;
    for (int done = 0; done < f->nb_samples;) {
        int n = f->nb_samples - done;
        if (n > 512) n = 512;
        for (int i = 0; i < n; i++) {
            for (int c = 0; c < CHANNELS; c++) {
                int sc = c < ch ? c : ch - 1; /* mono → both channels */
                int16_t v = 0;
                switch (f->format) {
                case AV_SAMPLE_FMT_FLTP: v = clip16(((const float *)f->extended_data[sc])[done + i]); break;
                case AV_SAMPLE_FMT_FLT: v = clip16(((const float *)f->data[0])[(done + i) * ch + sc]); break;
                case AV_SAMPLE_FMT_S16P: v = ((const int16_t *)f->extended_data[sc])[done + i]; break;
                case AV_SAMPLE_FMT_S16: v = ((const int16_t *)f->data[0])[(done + i) * ch + sc]; break;
                case AV_SAMPLE_FMT_S32P: v = (int16_t)(((const int32_t *)f->extended_data[sc])[done + i] >> 16); break;
                case AV_SAMPLE_FMT_S32: v = (int16_t)(((const int32_t *)f->data[0])[(done + i) * ch + sc] >> 16); break;
                default: break;
                }
                tmp[i * CHANNELS + c] = v;
            }
        }
        queue_pcm(p, tmp, (size_t)n);
        done += n;
    }
}

uint64_t audio_concealed(const audio_pipeline *p) { return p->concealed; }

/* Queues `frames` of silence. */
static void queue_silence(audio_pipeline *p, size_t frames) {
    static const int16_t zeros[512 * CHANNELS];
    while (frames > 0) {
        size_t n = frames > 512 ? 512 : frames;
        queue_pcm(p, zeros, n);
        frames -= n;
    }
}

/* Fills a gap of `frames` lost samples with silence. */
static void conceal(audio_pipeline *p, size_t frames) {
    p->concealed += frames;
    queue_silence(p, frames);
}

void audio_shift(audio_pipeline *p, int ms) {
    size_t frames = (size_t)(ms < 0 ? -ms : ms) * RATE / 1000;
    if (ms > 0) {
        p->drop_frames += frames;
    } else {
        p->insert_frames += frames;
    }
}

void audio_push(audio_pipeline *p, const uint8_t *data, size_t len, uint32_t rtp_time, uint64_t ts_ns) {
    size_t frames = 0;
    if (!p->ct || len == 0) return;
    if (p->ct == 4) {
        fmp4_audio_push(p->mux, data, len, AAC_LC_FRAME, ts_ns);
        return;
    }
    if (p->have_rtp) {
        int32_t gap = (int32_t)(rtp_time - p->next_rtp);
        if (gap < 0 && gap > -MAX_CONCEAL) return; /* late or duplicate: already covered */
        if (gap > 0 && gap <= MAX_CONCEAL) {
            uint64_t gap_ns = (uint64_t)gap * 1000000000ull / RATE;
            set_input_time(p, ts_ns > gap_ns ? ts_ns - gap_ns : 0);
            conceal(p, (size_t)gap);
        }
    }
    if (p->insert_frames > 0) {
        /* audio_shift asked to play later: silence that sits before this packet */
        uint64_t ins_ns = (uint64_t)p->insert_frames * 1000000000ull / RATE;
        set_input_time(p, ts_ns > ins_ns ? ts_ns - ins_ns : 0);
        queue_silence(p, p->insert_frames);
        p->insert_frames = 0;
    }
    set_input_time(p, ts_ns);
    if (p->ct == 1) {
        frames = len / (CHANNELS * sizeof(int16_t));
        queue_pcm(p, (const int16_t *)data, frames);
    } else if (p->dec) {
        av_packet_unref(p->pkt);
        if (av_new_packet(p->pkt, (int)len) < 0) return;
        memcpy(p->pkt->data, data, len);
        if (avcodec_send_packet(p->dec, p->pkt) < 0) return;
        while (avcodec_receive_frame(p->dec, p->frame) == 0) {
            frames += (size_t)p->frame->nb_samples;
            queue_frame(p, p->frame);
            av_frame_unref(p->frame);
        }
    }
    if (frames > 0) {
        p->have_rtp = 1;
        p->next_rtp = rtp_time + (uint32_t)frames;
    }
}
