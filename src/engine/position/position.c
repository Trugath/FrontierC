/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* absolute and relative positions, the alternate basis. */
#include "position_internal.h"

enum {
    WIDE_SHIFT = 17,                   /* the wide reduction's words are bits 17.. */
    NARROW_LIMIT = 0x4000,
    BASIS_SHIFT = 0x10,                /* a basis product is the value * 2^16 */
    COUNT_MASK = 0x3f                  /* the shift loops count CX & 0x3f */
};

int16_t fe2_position_dot(int16_t a0, int16_t a1, int16_t a2, int16_t b0, int16_t b1, int16_t b2)
{
    uint32_t sum = (uint32_t)((int32_t)a0 * b0) + (uint32_t)((int32_t)a1 * b1) + (uint32_t)((int32_t)a2 * b2);
    return (int16_t)(uint16_t)((sum << 1) >> 16);
}

/* Bit by bit, as the loops do: past 31 places only the sign is left. */
static uint32_t sar32(uint32_t v, unsigned count)
{
    if (count > 31) count = 31;
    return (v & 0x80000000u) ? ~(~v >> count) : v >> count;
}

static uint32_t shl32(uint32_t v, unsigned count) { return count > 31 ? 0 : v << count; }

unsigned fe2_position_reduce(const int64_t v[3], int16_t w[3])
{
    uint64_t envelope = 0;
    unsigned i, shift = 0;
    for (i = 0; i < 3; ++i) envelope |= (uint64_t)(v[i] < 0 ? ~v[i] : v[i]);
    envelope <<= 1;
    if (envelope >> 32) {
        uint32_t high = (uint32_t)(envelope >> 32);
        do {
            high >>= 1;
            ++shift;
        } while (high);
        for (i = 0; i < 3; ++i) {
            /* The second word of the shifted value, halved as a word. */
            int16_t second = (int16_t)(uint16_t)((uint64_t)(v[i] >> shift) >> 16);
            w[i] = (int16_t)(second >> 1);
        }
        return WIDE_SHIFT + shift;
    }
    for (uint32_t low = (uint32_t)envelope; low >= NARROW_LIMIT; low >>= 1) ++shift;
    for (i = 0; i < 3; ++i) w[i] = (int16_t)(uint16_t)sar32((uint32_t)(uint64_t)v[i], shift);
    return shift;
}

/* A rotated word vector (each the dot * 2^16, wrapped) back at `shift`:
 * the 64-bit value * 2^(shift - 16), its two dwords shifted separately by
 * counts taken modulo 64 (past 48 the high dword is only the sign). */
static int64_t widen(uint32_t r, unsigned shift)
{
    uint16_t up = (uint16_t)(BASIS_SHIFT - shift);
    uint32_t high, low;
    if ((int16_t)up >= 0) {
        low = sar32(r, up & COUNT_MASK);
        high = (r & 0x80000000u) ? 0xffffffffu : 0;
    } else {
        high = sar32(r, (uint16_t)(3 * BASIS_SHIFT - shift) & COUNT_MASK);
        low = shl32(r, (uint16_t)(0u - up) & COUNT_MASK);
    }
    return (int64_t)((uint64_t)high << 32 | low);
}

static void raw_position(const fe2_object *o, int64_t p[3])
{
    unsigned i;
    for (i = 0; i < 3; ++i) p[i] = (int64_t)((uint64_t)o->position_high[i] << 32 | o->position_low[i]);
}

static void cache(const fe2_position *s, fe2_object *o, const int64_t p[3])
{
    unsigned i;
    for (i = 0; i < 3; ++i) {
        o->camera_offset[i] = (uint32_t)(uint64_t)p[i];
        o->camera_target[i] = (uint32_t)((uint64_t)p[i] >> 32);
    }
    o->render_state = s->generation;
}

static void cached(const fe2_object *o, int64_t p[3])
{
    unsigned i;
    for (i = 0; i < 3; ++i) p[i] = (int64_t)((uint64_t)o->camera_target[i] << 32 | o->camera_offset[i]);
}

/* The parent's absolute position plus the record's own, which in a basis
 * frame is first reduced to words, turned by the parent's rows and widened
 * back. No check for link cycles. */
static void absolute(fe2_position *s, fe2_object *o, int64_t p[3])
{
    const fe2_object *parent;
    int64_t own[3];
    unsigned i;
    if (o->parent_handle == 0) {
        raw_position(o, p);
        cache(s, o, p);
        return;
    }
    if (o->render_state == s->generation) {
        cached(o, p);
        return;
    }
    parent = fe2_pool_record(s->saved, o->parent_handle);
    absolute(s, (fe2_object *)parent, p);
    raw_position(o, own);
    if (o->station_slot != 0) {
        int16_t w[3];
        unsigned shift = fe2_position_reduce(own, w);
        const int16_t *b = parent->orientation;
        for (i = 0; i < 3; ++i) {
            uint32_t sum = (uint32_t)((int32_t)b[3 * i] * w[0]) + (uint32_t)((int32_t)b[3 * i + 1] * w[1]) +
                           (uint32_t)((int32_t)b[3 * i + 2] * w[2]);
            own[i] = widen(sum << 1, shift);
        }
    }
    for (i = 0; i < 3; ++i) p[i] = (int64_t)((uint64_t)p[i] + (uint64_t)own[i]);
    cache(s, o, p);
}

void fe2_position_absolute(void *position, fe2_object *o, int64_t p[3])
{
    absolute((fe2_position *)position, o, p);
}

void fe2_position_relative(void *position, fe2_object *source, fe2_object *reference, int64_t d[3])
{
    int64_t a[3], b[3];
    unsigned i;
    if (reference->parent_handle == source->parent_handle && source->station_slot == reference->station_slot) {
        raw_position(source, a);
        raw_position(reference, b);
    } else if (reference->parent_handle != source->parent_handle && reference->parent_handle == source->handle) {
        raw_position(reference, b);
        for (i = 0; i < 3; ++i) a[i] = 0;
    } else {
        fe2_position_absolute(position, source, a);
        cached(reference, b);
    }
    for (i = 0; i < 3; ++i) d[i] = (int64_t)((uint64_t)a[i] - (uint64_t)b[i]);
}

void fe2_position_secondary_basis(void *position, fe2_object *o)
{
    fe2_position *s = (fe2_position *)position;
    const int16_t *p, *r = o->orientation;
    unsigned k, j;
    if (o->basis_generation == s->generation) return;
    p = fe2_pool_record(s->saved, o->parent_handle)->orientation;
    for (k = 0; k < 3; ++k)
        for (j = 0; j < 3; ++j)
            o->alternate_orientation[3 * k + j] =
                fe2_position_dot(p[3 * k], p[3 * k + 1], p[3 * k + 2], r[j], r[3 + j], r[6 + j]);
    o->basis_generation = s->generation;
}
