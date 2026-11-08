/*
 * sps.c - H.264 SPS rewriting (ITU-T H.264 7.3.2.1.1 and E.1.1): parse up to
 * the VUI's timing_info_present_flag, then emit the same bits with our timing
 * in place of whatever was there.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "sps.h"

#include <string.h>

#define MAX_RBSP 512

typedef struct {
    const uint8_t *b;
    size_t nbits, pos;
    int err;
} reader;

static unsigned bit(reader *r) {
    unsigned v;
    if (r->pos >= r->nbits) {
        r->err = 1;
        return 0;
    }
    v = (r->b[r->pos >> 3] >> (7 - (r->pos & 7))) & 1u;
    r->pos++;
    return v;
}

static uint32_t bits(reader *r, unsigned n) {
    uint32_t v = 0;
    while (n--) v = (v << 1) | bit(r);
    return v;
}

static uint32_t ue(reader *r) {
    unsigned zeros = 0;
    while (!bit(r)) {
        if (r->err || ++zeros > 31) {
            r->err = 1;
            return 0;
        }
    }
    return zeros ? ((1u << zeros) - 1u) + bits(r, zeros) : 0;
}

static void se(reader *r) { (void)ue(r); }

static void scaling_list(reader *r, int size) {
    int last = 8, next = 8;
    for (int j = 0; j < size && !r->err; j++) {
        if (next != 0) {
            int delta = (int)ue(r);
            delta = (delta & 1) ? (delta + 1) / 2 : -(delta / 2);
            next = (last + delta + 256) % 256;
        }
        last = next == 0 ? last : next;
    }
}

typedef struct {
    uint8_t *b;
    size_t cap, pos; /* in bits */
    int err;
} writer;

static void put_bit(writer *w, unsigned v) {
    if ((w->pos >> 3) >= w->cap) {
        w->err = 1;
        return;
    }
    if (v) w->b[w->pos >> 3] |= (uint8_t)(0x80u >> (w->pos & 7));
    w->pos++;
}

static void put_bits(writer *w, uint32_t v, unsigned n) {
    while (n--) put_bit(w, (v >> n) & 1u);
}

static void copy_bits(writer *w, reader *r, size_t from, size_t to) {
    size_t save = r->pos;
    r->pos = from;
    while (r->pos < to && !r->err) put_bit(w, bit(r));
    r->pos = save;
}

/* Strips emulation prevention bytes (00 00 03 → 00 00). */
static size_t unescape(const uint8_t *in, size_t len, uint8_t *out) {
    size_t o = 0, zeros = 0;
    for (size_t i = 0; i < len; i++) {
        if (zeros >= 2 && in[i] == 3) {
            zeros = 0;
            continue;
        }
        zeros = in[i] == 0 ? zeros + 1 : 0;
        out[o++] = in[i];
    }
    return o;
}

/* Re-applies emulation prevention. Returns 0 if `cap` is too small. */
static size_t escape(const uint8_t *in, size_t len, uint8_t *out, size_t cap) {
    size_t o = 0, zeros = 0;
    for (size_t i = 0; i < len; i++) {
        if (zeros >= 2 && in[i] <= 3) {
            if (o >= cap) return 0;
            out[o++] = 3;
            zeros = 0;
        }
        if (o >= cap) return 0;
        out[o++] = in[i];
        zeros = in[i] == 0 ? zeros + 1 : 0;
    }
    return o;
}

/* Finds the RBSP's last 1 bit (rbsp_stop_one_bit); returns its bit index. */
static size_t stop_bit(const uint8_t *b, size_t nbytes) {
    for (size_t i = nbytes; i-- > 0;) {
        if (b[i]) {
            unsigned k = 0;
            while (!((b[i] >> k) & 1u)) k++;
            return i * 8 + (7 - k);
        }
    }
    return 0;
}

typedef struct {
    uint8_t rbsp[MAX_RBSP];
    size_t n, end; /* RBSP bytes; bit index of the stop bit */
    size_t vui_flag_at, timing_at, after_timing;
    unsigned vui_present, width, height;
    int has_colour; /* video_signal_type_present_flag */
    unsigned full_range, primaries, transfer, matrix;
} sps_info;

/* Parses an SPS up to its VUI timing fields; returns 0 on success. */
static int parse(const uint8_t *sps, size_t len, sps_info *si, reader *r) {
    unsigned profile, chroma = 1, frame_mbs_only;
    uint32_t w_mbs, h_map, crop[4] = {0, 0, 0, 0};

    if (len < 4 || len > MAX_RBSP || (sps[0] & 0x1f) != 7) return -1;
    si->n = unescape(sps, len, si->rbsp);
    si->end = stop_bit(si->rbsp, si->n);
    *r = (reader){si->rbsp, si->n * 8, 8, 0}; /* skip the NAL header byte */

    profile = bits(r, 8);
    bits(r, 16); /* constraint flags, level_idc */
    ue(r);       /* seq_parameter_set_id */
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 || profile == 83 ||
        profile == 86 || profile == 118 || profile == 128 || profile == 138 || profile == 139 || profile == 134 ||
        profile == 135) {
        chroma = ue(r);
        if (chroma == 3) bit(r); /* separate_colour_plane_flag */
        ue(r);                   /* bit_depth_luma_minus8 */
        ue(r);                   /* bit_depth_chroma_minus8 */
        bit(r);                  /* qpprime_y_zero_transform_bypass_flag */
        if (bit(r)) {            /* seq_scaling_matrix_present_flag */
            for (int i = 0; i < (chroma == 3 ? 12 : 8); i++)
                if (bit(r)) scaling_list(r, i < 6 ? 16 : 64);
        }
    }
    ue(r); /* log2_max_frame_num_minus4 */
    {
        uint32_t poc_type = ue(r);
        if (poc_type == 0) {
            ue(r);
        } else if (poc_type == 1) {
            uint32_t cycle;
            bit(r);
            se(r);
            se(r);
            cycle = ue(r);
            for (uint32_t i = 0; i < cycle && !r->err; i++) se(r);
        }
    }
    ue(r);  /* max_num_ref_frames */
    bit(r); /* gaps_in_frame_num_value_allowed_flag */
    w_mbs = ue(r) + 1;
    h_map = ue(r) + 1;
    frame_mbs_only = bit(r);
    if (!frame_mbs_only) bit(r); /* mb_adaptive_frame_field_flag */
    bit(r);                      /* direct_8x8_inference_flag */
    if (bit(r)) {                /* frame_cropping_flag: left, right, top, bottom */
        for (int i = 0; i < 4; i++) crop[i] = ue(r);
    }
    {
        /* Crop units per 7.4.2.1.1 (CropUnitX/Y). */
        unsigned ux = (chroma == 1 || chroma == 2) ? 2 : 1;
        unsigned uy = (chroma == 1 ? 2 : 1) * (2 - frame_mbs_only);
        si->width = w_mbs * 16 - ux * (crop[0] + crop[1]);
        si->height = (2 - frame_mbs_only) * h_map * 16 - uy * (crop[2] + crop[3]);
    }
    si->vui_flag_at = r->pos;
    si->vui_present = bit(r);
    si->timing_at = si->after_timing = 0;
    si->has_colour = 0;
    si->full_range = 0;
    si->primaries = si->transfer = si->matrix = 2; /* unspecified */
    if (si->vui_present) {
        if (bit(r)) {              /* aspect_ratio_info_present_flag */
            if (bits(r, 8) == 255) /* Extended_SAR */
                bits(r, 32);
        }
        if (bit(r)) bit(r); /* overscan_info_present_flag, overscan_appropriate_flag */
        if (bit(r)) {       /* video_signal_type_present_flag */
            si->has_colour = 1;
            bits(r, 3); /* video_format */
            si->full_range = bit(r);
            if (bit(r)) { /* colour_description_present_flag */
                si->primaries = bits(r, 8);
                si->transfer = bits(r, 8);
                si->matrix = bits(r, 8);
            }
        }
        if (bit(r)) { /* chroma_loc_info_present_flag */
            ue(r);
            ue(r);
        }
        si->timing_at = r->pos;
        if (bit(r)) bits(r, 65); /* num_units_in_tick, time_scale, fixed_frame_rate_flag */
        si->after_timing = r->pos;
    }
    return r->err ? -1 : 0;
}

int sps_get_size(const uint8_t *sps, size_t len, unsigned *width, unsigned *height) {
    sps_info si;
    reader r;
    if (parse(sps, len, &si, &r) != 0) return -1;
    *width = si.width;
    *height = si.height;
    return 0;
}

int sps_get_colour(const uint8_t *sps, size_t len, unsigned *primaries, unsigned *transfer, unsigned *matrix,
                   unsigned *full_range) {
    sps_info si;
    reader r;
    if (parse(sps, len, &si, &r) != 0 || !si.has_colour) return -1;
    *primaries = si.primaries;
    *transfer = si.transfer;
    *matrix = si.matrix;
    *full_range = si.full_range;
    return 0;
}

size_t sps_set_timing(const uint8_t *sps, size_t len, unsigned fps, uint8_t *out, size_t out_cap) {
    sps_info si;
    uint8_t neu[MAX_RBSP + 32];
    reader r;
    writer w;

    if (parse(sps, len, &si, &r) != 0) return 0;

    memset(neu, 0, sizeof neu);
    w = (writer){neu, sizeof neu, 0, 0};
    if (si.vui_present) {
        copy_bits(&w, &r, 0, si.timing_at);
        put_bit(&w, 1);
        put_bits(&w, 1, 32);                            /* num_units_in_tick */
        put_bits(&w, fps * 2u, 32);                     /* time_scale: two ticks per frame */
        put_bit(&w, 0);                                 /* fixed_frame_rate_flag: iOS is variable-rate */
        copy_bits(&w, &r, si.after_timing, si.end + 1); /* rest of the VUI and the stop bit */
    } else {
        copy_bits(&w, &r, 0, si.vui_flag_at);
        put_bit(&w, 1);     /* vui_parameters_present_flag */
        put_bits(&w, 0, 4); /* aspect, overscan, signal type, chroma loc: absent */
        put_bit(&w, 1);     /* timing_info_present_flag */
        put_bits(&w, 1, 32);
        put_bits(&w, fps * 2u, 32);
        put_bit(&w, 0);
        put_bits(&w, 0, 5); /* nal/vcl HRD, pic_struct, bitstream_restriction: absent */
        put_bit(&w, 1);     /* rbsp_stop_one_bit */
    }
    if (w.err) return 0;
    return escape(neu, (w.pos + 7) / 8, out, out_cap);
}
