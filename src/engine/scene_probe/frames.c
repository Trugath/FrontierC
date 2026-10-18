/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the axis permutation, 0x135e0..0x14088 the 64-bit frame
 * transforms, and the word products shared by the probe. */
#include "scene_probe_internal.h"

enum {
    NARROW_LIMIT = 0x4000,             /* the reduced words stay below this */
    WIDE_BIAS = 17,                    /* the wide reduction keeps bits 17..32 */
    RESTORE_BIAS = 0x30,               /* products are Q16 of the reduced words */
    AXIS_NEGATE_X = 0x20, AXIS_NEGATE_Y = 0x10, AXIS_NEGATE_Z = 0x08, AXIS_ROW = 7
};

/* Shifts as the original's one-bit loops: 32 or more steps leave the sign
 * (or zero). */
int32_t fe2_probe_sar32(int32_t v, unsigned n)
{
    if (n >= 32) return v < 0 ? -1 : 0;
    return (int32_t)(v < 0 ? ~(~(uint32_t)v >> n) : (uint32_t)v >> n);
}

uint32_t fe2_probe_shl32(uint32_t v, unsigned n) { return n >= 32 ? 0 : v << n; }

/* Twice the 32-bit wrapped dot product. */
static uint32_t dot2(int16_t a0, int16_t a1, int16_t a2, const int16_t v[3])
{
    uint32_t s = (uint32_t)((int32_t)a0 * v[0]) + (uint32_t)((int32_t)a1 * v[1]) + (uint32_t)((int32_t)a2 * v[2]);
    return s << 1;
}

int16_t fe2_probe_row(const int16_t b[9], unsigned k, const int16_t v[3])
{
    return (int16_t)(uint16_t)(dot2(b[3 * k], b[3 * k + 1], b[3 * k + 2], v) >> 16);
}

int16_t fe2_probe_column(const int16_t b[9], unsigned k, const int16_t v[3])
{
    return (int16_t)(uint16_t)(dot2(b[k], b[3 + k], b[6 + k], v) >> 16);
}

void fe2_probe_by_rows(const int16_t b[9], const int16_t v[3], int16_t out[3])
{
    int16_t t[3];
    unsigned k;
    for (k = 0; k < 3; ++k) t[k] = fe2_probe_row(b, k, v);
    for (k = 0; k < 3; ++k) out[k] = t[k];
}

void fe2_probe_by_columns(const int16_t b[9], const int16_t v[3], int16_t out[3])
{
    int16_t t[3];
    unsigned k;
    for (k = 0; k < 3; ++k) t[k] = fe2_probe_column(b, k, v);
    for (k = 0; k < 3; ++k) out[k] = t[k];
}

/*
 * The magnitudes (ones' complement for negatives) are ORed and doubled,
 * losing bit 63. When the doubled envelope's high dword is nonzero, the
 * values are halved until it clears, and each word is bits 17..32 of the
 * halved value; otherwise each is the low dword shifted until the
 * envelope is below 0x4000. The doubled products are then put back as
 * value << (total reduction - 16), each part shifted by the original's
 * masked counts.
 */
static void wide_transform(const int16_t b[9], int columns, const int64_t in[3], int64_t out[3])
{
    uint64_t envelope = 0;
    int64_t v[3];
    int16_t w[3];
    uint16_t total, count, rest;
    unsigned i;
    for (i = 0; i < 3; ++i) {
        v[i] = in[i];
        envelope |= in[i] < 0 ? ~(uint64_t)in[i] : (uint64_t)in[i];
    }
    envelope <<= 1;
    if (envelope >> 32) {
        uint32_t high = (uint32_t)(envelope >> 32);
        uint16_t n = 0;
        do {
            for (i = 0; i < 3; ++i) v[i] = v[i] < 0 ? (int64_t)~(~(uint64_t)v[i] >> 1) : (int64_t)((uint64_t)v[i] >> 1);
            ++n;
            high >>= 1;
        } while (high);
        for (i = 0; i < 3; ++i) w[i] = (int16_t)((int16_t)(uint16_t)((uint64_t)v[i] >> 16) >> 1);
        total = (uint16_t)(n + WIDE_BIAS);
    } else {
        uint32_t e = (uint32_t)envelope;
        uint16_t c = 0;
        while (e >= NARROW_LIMIT) {
            ++c;
            e >>= 1;
        }
        for (i = 0; i < 3; ++i) w[i] = (int16_t)(uint16_t)(uint32_t)fe2_probe_sar32((int32_t)(uint32_t)v[i], c);
        total = c;
    }
    count = (uint16_t)(RESTORE_BIAS - total);
    rest = (uint16_t)(count - 32);
    for (i = 0; i < 3; ++i) {
        int32_t p = (int32_t)(columns ? dot2(b[i], b[3 + i], b[6 + i], w) : dot2(b[3 * i], b[3 * i + 1], b[3 * i + 2], w));
        uint32_t high, low;
        if ((int16_t)rest >= 0) {
            high = p < 0 ? 0xffffffffu : 0;
            low = (uint32_t)fe2_probe_sar32(p, rest & 63u);
        } else {
            high = (uint32_t)fe2_probe_sar32(p, count & 63u);
            low = fe2_probe_shl32((uint32_t)p, (uint16_t)(0u - rest) & 63u);
        }
        out[i] = (int64_t)((uint64_t)high << 32 | low);
    }
}

void fe2_probe_wide_by_rows(const int16_t b[9], const int64_t in[3], int64_t out[3]) { wide_transform(b, 0, in, out); }

void fe2_probe_wide_by_columns(const int16_t b[9], const int64_t in[3], int64_t out[3])
{
    wide_transform(b, 1, in, out);
}

void fe2_probe_permute(int16_t v[3], uint8_t code)
{
    int16_t t[3];
    unsigned k;
    if (code == 0) return;
    for (k = 0; k < 3; ++k) t[k] = v[fe2_probe_axes[code & AXIS_ROW][k]];
    for (k = 0; k < 3; ++k) v[k] = t[k];
    if (code & AXIS_NEGATE_X) v[0] = (int16_t)(uint16_t)(0u - (uint16_t)v[0]);
    if (code & AXIS_NEGATE_Y) v[1] = (int16_t)(uint16_t)(0u - (uint16_t)v[1]);
    if (code & AXIS_NEGATE_Z) v[2] = (int16_t)(uint16_t)(0u - (uint16_t)v[2]);
}
