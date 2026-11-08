/*
 * flac.c - minimal FLAC framing (https://www.rfc-editor.org/rfc/rfc9639):
 * fixed 2048-sample blocks, independent channels, 16-bit VERBATIM subframes.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "flac.h"

#include <string.h>

static uint8_t crc8(const uint8_t *b, size_t n) {
    uint8_t c = 0;
    while (n--) {
        c ^= *b++;
        for (int i = 0; i < 8; i++) c = (uint8_t)((c & 0x80) ? (c << 1) ^ 0x07 : c << 1);
    }
    return c;
}

static uint16_t crc16(const uint8_t *b, size_t n) {
    uint16_t c = 0;
    while (n--) {
        c ^= (uint16_t)(*b++ << 8);
        for (int i = 0; i < 8; i++) c = (uint16_t)((c & 0x8000) ? (c << 1) ^ 0x8005 : c << 1);
    }
    return c;
}

/* 4-bit sample rate codes (RFC 9639 9.1.3); 0 means "not encodable here". */
static int rate_code(unsigned rate) {
    switch (rate) {
    case 8000:
        return 4;
    case 16000:
        return 5;
    case 22050:
        return 6;
    case 24000:
        return 7;
    case 32000:
        return 8;
    case 44100:
        return 9;
    case 48000:
        return 10;
    case 96000:
        return 11;
    default:
        return 0;
    }
}

void flac_streaminfo(uint8_t out[34], unsigned sample_rate, unsigned channels, size_t max_frame_bytes) {
    memset(out, 0, 34);
    out[0] = FLAC_BLOCK >> 8; /* min block size */
    out[1] = FLAC_BLOCK & 0xff;
    out[2] = FLAC_BLOCK >> 8; /* max block size */
    out[3] = FLAC_BLOCK & 0xff;
    /* min frame size: 0 (unknown); max frame size (24 bits) */
    out[7] = (uint8_t)(max_frame_bytes >> 16);
    out[8] = (uint8_t)(max_frame_bytes >> 8);
    out[9] = (uint8_t)max_frame_bytes;
    /* 20 bits sample rate, 3 bits channels-1, 5 bits bps-1, 36 bits total samples (0 = unknown) */
    out[10] = (uint8_t)(sample_rate >> 12);
    out[11] = (uint8_t)(sample_rate >> 4);
    out[12] = (uint8_t)(((sample_rate & 0xf) << 4) | (((channels - 1) & 7) << 1) | ((16 - 1) >> 4));
    out[13] = (uint8_t)(((16 - 1) & 0xf) << 4);
    /* bytes 14..17: rest of total samples (0); 18..33: MD5 (0 = unknown) */
}

size_t flac_max_frame_bytes(unsigned channels) {
    /* header (<= 16) + per channel (1 subframe header + 2 bytes/sample) + CRC-16 */
    return 16 + channels * (1 + 2 * FLAC_BLOCK) + 2;
}

/* UTF-8-style coding of the frame number (RFC 9639 9.1.5). */
static size_t put_coded_number(uint8_t *out, uint64_t v) {
    if (v < 0x80) {
        out[0] = (uint8_t)v;
        return 1;
    }
    {
        int len = v < 0x800 ? 2 : v < 0x10000 ? 3 : v < 0x200000 ? 4 : v < 0x4000000 ? 5 : 6;
        out[0] = (uint8_t)((0xff00 >> len) | (v >> (6 * (len - 1))));
        for (int i = 1; i < len; i++) out[i] = (uint8_t)(0x80 | ((v >> (6 * (len - 1 - i))) & 0x3f));
        return (size_t)len;
    }
}

size_t flac_write_frame(uint8_t *out, uint64_t frame, unsigned sample_rate, unsigned channels, const int16_t *pcm) {
    size_t n = 0;
    int rc = rate_code(sample_rate);
    if (!rc || channels < 1 || channels > 8) return 0;

    out[n++] = 0xff;                                        /* sync code 0b11111111111110 ... */
    out[n++] = 0xf8;                                        /* ... reserved 0, fixed-blocksize strategy */
    out[n++] = (uint8_t)(0xb0 | rc);                        /* block size code 1011 = 2048, sample rate */
    out[n++] = (uint8_t)(((channels - 1) << 4) | (4 << 1)); /* independent channels, 16 bits/sample */
    n += put_coded_number(out + n, frame);
    out[n] = crc8(out, n);
    n++;

    for (unsigned c = 0; c < channels; c++) {
        out[n++] = 0x02; /* padding 0, type 000001 VERBATIM, no wasted bits */
        for (unsigned i = 0; i < FLAC_BLOCK; i++) {
            uint16_t s = (uint16_t)pcm[i * channels + c];
            out[n++] = (uint8_t)(s >> 8);
            out[n++] = (uint8_t)s;
        }
    }
    {
        uint16_t crc = crc16(out, n);
        out[n++] = (uint8_t)(crc >> 8);
        out[n++] = (uint8_t)crc;
    }
    return n;
}
