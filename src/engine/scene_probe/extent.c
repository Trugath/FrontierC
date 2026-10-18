/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* a candidate as a sphere of its extent (+0x10a, 48 bits). */
#include "scene_probe_internal.h"

enum {
    SMALLEST_EXTENT = 0x3e8,           /* smaller extents (with zero high words) are not tested */
    WORD_LIMIT = 0x4000,
    CLASS_EXTENT = 0x1c, CLASS_TOUCHING = 0x1e, CLASS_DISTANCE = 0x1f
};

static uint32_t magnitude32(int32_t v) { return v < 0 ? 0u - (uint32_t)v : (uint32_t)v; }

/* An approximate square of a 32-bit magnitude, scaled down by 2^16: the
 * high word squared plus the high word of twice the cross product (each
 * 32-bit, wrapping). */
static uint32_t square(uint32_t v)
{
    uint32_t low = v & 0xffffu, high = v >> 16;
    return high * high + ((low * high) << 1 >> 16);
}

/* The square root of a positive value by averaging an estimate with a
 * restoring quotient of value * 2^32 (whose dividend loses its top bit at
 * each step), until the quotient is at least the estimate - 0x8000. */
static uint32_t square_root(uint32_t value)
{
    uint32_t estimate = value >> 1;
    estimate = (estimate & 0x00ffffffu) | (((estimate >> 24) + 0x40u) & 0xffu) << 24;
    for (;;) {
        uint32_t rest = value, quotient = 1, top;
        do {
            uint32_t bit;
            rest <<= 1;
            if (rest >= estimate) {
                rest -= estimate;
                bit = 0;
            } else {
                bit = 1;
            }
            top = quotient >> 31;
            quotient = quotient << 1 | bit;
        } while (!top);
        quotient = ~quotient >> 1;
        estimate = (estimate + quotient) >> 1;
        if ((int32_t)(quotient - estimate) >= -0x8000) return estimate;
    }
}

/* Words below 0x4000: the exponent of a doubled 32-bit value. */
static void normalize(uint32_t value, uint16_t exponent, uint16_t *mantissa, uint16_t *out)
{
    value <<= 1;
    do {
        ++exponent;
        value >>= 1;
    } while (value >= WORD_LIMIT);
    --exponent;
    if ((int16_t)exponent < 0) {
        do {
            value >>= 1;
            ++exponent;
        } while (exponent != 0);
    }
    *mantissa = (uint16_t)value;
    *out = exponent;
}

/*
 * The origin must lie within the extent plus the best distance (64-bit
 * magnitudes). It is reduced to a common scale with the extent: shifted up
 * when its high words, the extent's high word and (the low part) are small,
 * else shifted down 48-bit by the bit length of the high words. The ray
 * must approach (the direction's products with the origin sum negative);
 * the distance along it to the sphere is then the projection less the
 * root of the projection squared plus the extent squared less the
 * origin's squares, all on squares scaled down by 2^16.
 */
void fe2_probe_extent(fe2_scene_probe *p, const fe2_probe_candidate *c)
{
    const fe2_object *o = c->record;
    const int16_t *d = c->direction;
    uint64_t magnitude[3], limit;
    uint32_t reference, low_extent = (uint32_t)o->model_scale_a[0] | (uint32_t)o->model_scale_a[1] << 16;
    uint32_t offset[3], approach, factor, root, value, distance_low = 0, distance_high = 0;
    int32_t gap, product = 0, remainder;
    uint16_t scale, shift, mantissa, exponent;
    uint8_t class_a, class_b;
    int i;
    for (i = 0; i < 3; ++i) p->saved_offset[i] = c->origin[i];
    if (o->model_scale_a[2] == 0 && o->model_scale_a[1] == 0 && o->model_scale_a[0] < SMALLEST_EXTENT) return;
    limit = ((uint64_t)o->model_scale_a[2] << 32 | low_extent) + p->best;
    for (i = 2; i >= 0; --i) {
        magnitude[i] = c->origin[i] < 0 ? ~(uint64_t)c->origin[i] : (uint64_t)c->origin[i];
        if (magnitude[i] >= limit) return;
    }

    /* The common scale. */
    scale = (uint16_t)(magnitude[0] >> 32) | (uint16_t)(magnitude[1] >> 32) | (uint16_t)(magnitude[2] >> 32) |
            o->model_scale_a[2];
    if (scale == 0) {
        uint32_t envelope = low_extent | (uint32_t)magnitude[0] | (uint32_t)magnitude[1] | (uint32_t)magnitude[2];
        reference = low_extent;
        scale = 1;
        if ((int32_t)envelope > 0) {
            envelope <<= 1;
            if (!(envelope & 0x80000000u)) {
                uint16_t countdown = 0xffff;
                for (;;) {
                    envelope <<= 1;
                    if (envelope & 0x80000000u) break;
                    if (--countdown == 0) break;
                }
                shift = (uint16_t)(countdown + 1u);
                scale = (uint16_t)(0u - shift);
                for (i = 0; i < 3; ++i) offset[i] = fe2_probe_shl32((uint32_t)p->saved_offset[i], scale & 63u);
                reference = fe2_probe_shl32(reference, scale & 63u);
                goto scaled;
            }
            goto shift_down;
        }
    } else {
        scale = (uint16_t)(scale << 1);
    }
    scale = (uint16_t)(scale << 1);
shift_down:
    {
        int64_t v[3];
        uint64_t wide_extent = (uint64_t)o->model_scale_a[2] << 32 | low_extent;
        shift = 0;
        for (i = 0; i < 3; ++i) v[i] = (int64_t)((uint64_t)p->saved_offset[i] << 16) >> 16;
        do {
            for (i = 0; i < 3; ++i) v[i] >>= 1;
            ++shift;
            scale >>= 1;
        } while (scale != 0);
        for (i = 0; i < 3; ++i) offset[i] = (uint32_t)(uint64_t)v[i];
        reference = (uint32_t)(wide_extent >> shift);
    }
scaled:
    for (i = 0; i < 3; ++i) p->work.offset[i] = (int32_t)offset[i];
    p->work.reference = (int32_t)reference;
    p->work.shift = shift;

    /* The approach: the origin against the direction. */
    approach = 0;
    for (i = 0; i < 3; ++i) {
        uint32_t a = magnitude32((int32_t)offset[i]);
        uint16_t m = (uint16_t)(d[i] < 0 ? 0u - (uint16_t)d[i] : (uint16_t)d[i]);
        uint32_t t = (a >> 16) * m + (((a & 0xffffu) * m) >> 16);
        if ((d[i] < 0) != ((int32_t)offset[i] < 0)) t = 0u - t;
        product = (int32_t)t;
        approach += t;
    }
    if ((int32_t)approach >= 0) return;
    approach = 0u - approach;

    /* The distance. */
    factor = (uint32_t)product;
    {
        uint32_t room = square(magnitude32((int32_t)reference)), last = square(magnitude32((int32_t)offset[2]));
        room -= square(magnitude32((int32_t)offset[0]));
        room -= square(magnitude32((int32_t)offset[1]));
        if ((int32_t)room > (int32_t)last) goto touching;
        gap = (int32_t)(room - last);
    }
    factor = square(magnitude32((int32_t)approach)) << 1;
    value = (uint32_t)(gap >> 1) + factor;
    if ((int32_t)value <= 0) return;
    root = square_root(value);
    remainder = (int32_t)(approach - root);
    if (remainder < 0) goto touching;
    factor = (uint32_t)remainder << 1;
    if ((int16_t)shift > 0) {
        uint64_t wide = shift >= 64 ? 0 : (uint64_t)factor << shift;
        distance_low = (uint32_t)wide;
        distance_high = (uint32_t)(wide >> 32);
    } else {
        uint16_t n = (uint16_t)(0u - shift);
        distance_low = n >= 32 ? 0 : factor >> n;
    }
    goto compare;
touching:
    distance_low = distance_high = 0;
compare:
    if (((uint64_t)distance_high << 32 | distance_low) >= p->best) return;
    if (distance_low == 0 && distance_high == 0) {
        mantissa = 0;
        exponent = 0;
        class_a = CLASS_EXTENT;
        class_b = CLASS_TOUCHING;
    } else {
        normalize(factor, shift, &mantissa, &exponent);
        class_a = CLASS_EXTENT;
        class_b = CLASS_DISTANCE;
    }
    p->hit_frame = p->candidate_frame;
    p->best = (uint64_t)distance_high << 32 | distance_low;
    p->mantissa = mantissa;
    p->exponent = exponent;
    p->hit_class[0] = class_a;
    p->hit_class[1] = class_b;
    p->hit_work = p->work;
    p->hit_factor = factor;
    p->hit_record = c->record;
    p->hit_kind = 0xff;
    p->status |= 0x80;
}
