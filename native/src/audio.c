/*
 * audio.c - AirPlay audio → fragmented MP4 that Vega's MSE plays.
 *
 * Vega's MSE plays FLAC and AAC-LC but not ALAC, AAC-ELD or raw PCM
 * (docs/DECISIONS.md D3), so:
 *   ct 4 (AAC-LC)            passes through untouched;
 *   ct 2 (ALAC), 8 (AAC-ELD) are decoded with libavcodec, then
 *   ct 1 (PCM) and decoded audio are wrapped in FLAC VERBATIM frames
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
    return p;
}

void audio_destroy(audio_pipeline *p) {
    if (!p) return;
    close_decoder(p);
    av_packet_free(&p->pkt);
    av_frame_free(&p->frame);
    fmp4_audio_destroy(p->mux);
    free(p->flac_buf);
    free(p);
}

void audio_reset(audio_pipeline *p) {
    p->pcm_frames = 0;
    if (p->dec) avcodec_flush_buffers(p->dec);
}

int audio_set_format(audio_pipeline *p, unsigned ct) {
    close_decoder(p);
    p->ct = 0;
    p->pcm_frames = 0;
    p->flac_frame = 0;
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
static void queue_pcm(audio_pipeline *p, const int16_t *pcm, size_t frames) {
    while (frames > 0) {
        size_t take = FLAC_BLOCK - p->pcm_frames;
        if (take > frames) take = frames;
        memcpy(p->pcm + p->pcm_frames * CHANNELS, pcm, take * CHANNELS * sizeof *pcm);
        p->pcm_frames += take;
        pcm += take * CHANNELS;
        frames -= take;
        if (p->pcm_frames == FLAC_BLOCK) {
            size_t len = flac_write_frame(p->flac_buf, p->flac_frame++, RATE, CHANNELS, p->pcm);
            fmp4_audio_push(p->mux, p->flac_buf, len, FLAC_BLOCK);
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

void audio_push(audio_pipeline *p, const uint8_t *data, size_t len) {
    if (!p->ct || len == 0) return;
    if (p->ct == 4) {
        fmp4_audio_push(p->mux, data, len, AAC_LC_FRAME);
        return;
    }
    if (p->ct == 1) {
        queue_pcm(p, (const int16_t *)data, len / (CHANNELS * sizeof(int16_t)));
        return;
    }
    if (!p->dec) return;
    av_packet_unref(p->pkt);
    if (av_new_packet(p->pkt, (int)len) < 0) return;
    memcpy(p->pkt->data, data, len);
    if (avcodec_send_packet(p->dec, p->pkt) < 0) return;
    while (avcodec_receive_frame(p->dec, p->frame) == 0) {
        queue_frame(p, p->frame);
        av_frame_unref(p->frame);
    }
}
