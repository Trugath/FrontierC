/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the wide reduction; 0x1011d and 0x10e45..0x10f99 feedback
 * reduction and the basis transforms. */
#include "motion_internal.h"

enum { NARROW_LIMIT = 0x4000, WIDE_EXTRA_SHIFT = 17, FEEDBACK_LIMIT = 0x3f00 };

/* The values' common envelope (each negative one complemented, ORed),
 * doubled. Above 32 bits every value is shifted down until it fits and
 * the words are bits 17..32 (bits 16..31 shifted once more); otherwise
 * the low dwords are shifted until the envelope is below 0x4000. */
fe2_scaled fe2_motion_scale_wide(const int64_t v[3])
{
    fe2_scaled r;
    uint64_t envelope = 0;
    unsigned i, count = 0;
    for (i = 0; i < 3; ++i) envelope |= v[i] < 0 ? ~(uint64_t)v[i] : (uint64_t)v[i];
    envelope <<= 1;
    if (envelope >> 32) {
        uint32_t high = (uint32_t)(envelope >> 32);
        int64_t w[3];
        for (i = 0; i < 3; ++i) w[i] = v[i];
        do {
            for (i = 0; i < 3; ++i) w[i] >>= 1;
            ++count;
            high >>= 1;
        } while (high != 0);
        for (i = 0; i < 3; ++i) r.v[i] = (int16_t)((int16_t)(uint16_t)((uint64_t)w[i] >> 16) >> 1);
        r.shift = (uint16_t)(count + WIDE_EXTRA_SHIFT);
        return r;
    }
    {
        uint32_t low = (uint32_t)envelope;
        while (low >= NARROW_LIMIT) {
            ++count;
            low >>= 1;
        }
    }
    for (i = 0; i < 3; ++i) r.v[i] = (int16_t)(uint16_t)((int32_t)(uint32_t)(uint64_t)v[i] >> count);
    r.shift = (uint16_t)count;
    return r;
}

static uint32_t magnitude32(int32_t v) { return v < 0 ? 0u - (uint32_t)v : (uint32_t)v; }

/* The shift bringing every magnitude below 0x3f00. (The original measures
 * the third, then the first and second after the shift so far: the same
 * smallest shift for the largest.) */
unsigned fe2_motion_feedback_shift(const int32_t v[3])
{
    uint32_t largest = 0;
    unsigned i, shift = 0;
    for (i = 0; i < 3; ++i)
        if (magnitude32(v[i]) > largest) largest = magnitude32(v[i]);
    while ((largest >> shift) >= FEEDBACK_LIMIT) ++shift;
    return shift;
}

int16_t fe2_motion_dot_high(const int16_t a[3], const int16_t b[3])
{
    return (int16_t)(uint16_t)(fe2_motion_dot2(a, b) >> 16);
}

uint32_t fe2_motion_dot2(const int16_t a[3], const int16_t b[3])
{
    uint32_t sum = (uint32_t)((int32_t)a[0] * b[0]) + (uint32_t)((int32_t)a[1] * b[1]) +
                   (uint32_t)((int32_t)a[2] * b[2]);
    return sum << 1;
}

void fe2_motion_column(const int16_t basis[9], unsigned k, int16_t out[3])
{
    out[0] = basis[k];
    out[1] = basis[3 + k];
    out[2] = basis[6 + k];
}

void fe2_motion_row(const int16_t basis[9], unsigned k, int16_t out[3])
{
    out[0] = basis[3 * k];
    out[1] = basis[3 * k + 1];
    out[2] = basis[3 * k + 2];
}

/* Each output: the high word of twice the 32-bit wrapped dot product with
 * the basis's column (or row) k. */
void fe2_motion_by_columns(const int16_t basis[9], const int16_t w[3], int16_t out[3])
{
    int16_t column[3], in[3];
    unsigned k;
    in[0] = w[0], in[1] = w[1], in[2] = w[2];
    for (k = 0; k < 3; ++k) {
        fe2_motion_column(basis, k, column);
        out[k] = fe2_motion_dot_high(in, column);
    }
}

void fe2_motion_by_rows(const int16_t basis[9], const int16_t w[3], int16_t out[3])
{
    int16_t row[3], in[3];
    unsigned k;
    in[0] = w[0], in[1] = w[1], in[2] = w[2];
    for (k = 0; k < 3; ++k) {
        fe2_motion_row(basis, k, row);
        out[k] = fe2_motion_dot_high(in, row);
    }
}

fe2_feedback fe2_motion_transform_feedback(const fe2_object *o, const int32_t v[3])
{
    fe2_feedback r;
    int16_t w[3], out[3];
    unsigned i, shift = fe2_motion_feedback_shift(v);
    for (i = 0; i < 3; ++i) w[i] = (int16_t)(uint16_t)(uint32_t)(v[i] >> shift);
    fe2_motion_by_columns(o->orientation, w, out);
    for (i = 0; i < 3; ++i) r.v[i] = out[i];
    r.shift = (uint16_t)shift;
    return r;
}
