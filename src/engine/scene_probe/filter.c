/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* a candidate's first tests and the nearest hit. */
#include "scene_probe_internal.h"

enum {
    FLAGS_GEOMETRY = 0x80,             /* the record's flag bit 7: tested by its geometry */
    WORD_LIMIT = 0x4000,
    CLASS_LENGTH = 0x1d,
    UNIT_SHIFT = 15
};

/* The candidate's extent (+0x10a, +0x10c) as a dword. */
static uint32_t extent32(const fe2_object *o) { return (uint32_t)o->model_scale_a[0] | (uint32_t)o->model_scale_a[1] << 16; }

static uint32_t magnitude32(int32_t v) { return v < 0 ? 0u - (uint32_t)v : (uint32_t)v; }

/* x86 shift-left-with-overflow: the doubling stops when bits 31 and 30
 * differ (the original's JO after RCL). */
static int doubled(uint32_t *v, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; ++i) {
        uint32_t d = *v;
        *v = d << 1;
        if (((d >> 31) ^ (d >> 30)) & 1u) return 0;
    }
    return 1;
}

void fe2_probe_keep_nearer(fe2_scene_probe *p, fe2_object *record, uint32_t distance, uint16_t mantissa,
                           uint16_t exponent, uint8_t class_a, uint8_t class_b, const int16_t normal[3])
{
    unsigned i;
    if (distance >= (uint32_t)p->best) return;
    p->hit_frame = p->candidate_frame;
    p->best = (p->best & 0xffffffff00000000u) | distance;
    p->mantissa = mantissa;
    p->exponent = exponent;
    p->hit_class[0] = class_a;
    p->hit_class[1] = class_b;
    for (i = 0; i < 3; ++i) p->model_normal[i] = normal[i];
    p->hit_record = record;
    p->hit_kind = 0;
    p->status |= 0x80;
}

/*
 * A geometry candidate's origin must lie within its extent plus the best
 * distance on every axis (magnitudes in ones' complement). It is reduced
 * to words below 0x4000 (losing bit 31 of the envelope), and the ray must
 * pass within the extent at that scale: each cross product of origin and
 * direction is bounded (skipped when the scaled extent overflows). Then
 * the candidate is a sphere of the origin's length, or its geometry.
 */
void fe2_probe_candidate_test(fe2_scene_probe *p, const fe2_scene_probe_services *game, const fe2_probe_candidate *c)
{
    fe2_object *o = c->record;
    uint32_t radius, envelope, extent;
    uint16_t countdown = 0xffff, shift;
    int16_t w[3], normal[3];
    int i;
    if (!(o->flags & FLAGS_GEOMETRY) || (p->flags & FE2_PROBE_EXTENTS_ONLY)) {
        fe2_probe_extent(p, c);
        return;
    }
    radius = extent32(o) + (uint32_t)p->best;
    envelope = 0;
    for (i = 2; i >= 0; --i) {
        uint32_t low = (uint32_t)c->origin[i], high = (uint32_t)((uint64_t)c->origin[i] >> 32);
        if (high & 0x80000000u) {
            low = ~low;
            high = ~high;
        }
        if (high != 0 || low >= radius) return;
        envelope |= low;
    }
    envelope <<= 1;
    for (;;) {
        envelope >>= 1;
        if (envelope < WORD_LIMIT) break;
        if (--countdown == 0) break;
    }
    shift = (uint16_t)~countdown;
    p->offset_shift = shift;
    for (i = 0; i < 3; ++i) p->offset[i] = w[i] = (int16_t)(uint16_t)(uint32_t)fe2_probe_sar32((int32_t)(uint32_t)c->origin[i], shift);
    extent = extent32(o);
    if (doubled(&extent, (uint16_t)(UNIT_SHIFT - shift) & 63u)) {
        const int16_t *d = c->direction;
        int32_t cross[3];
        cross[0] = (int32_t)((uint32_t)((int32_t)w[1] * d[2]) - (uint32_t)((int32_t)w[2] * d[1]));
        cross[1] = (int32_t)((uint32_t)((int32_t)w[1] * d[0]) - (uint32_t)((int32_t)w[0] * d[1]));
        cross[2] = (int32_t)((uint32_t)((int32_t)w[0] * d[2]) - (uint32_t)((int32_t)w[2] * d[0]));
        for (i = 0; i < 3; ++i)
            if (magnitude32(cross[i]) >= extent) return;
        for (i = 0; i < 3; ++i) normal[i] = d[i];
    } else {
        /* The original leaves its scratch here: the origin's (zero) high
         * word and the first two scaled words. */
        normal[0] = 0;
        normal[1] = w[0];
        normal[2] = w[1];
    }
    if (p->flags & FE2_PROBE_LENGTH) {
        uint16_t length = fe2_math_length3(w[0], w[1], w[2]);
        uint32_t distance = (uint32_t)(int32_t)(int16_t)length;
        if (!doubled(&distance, shift & 63u)) return;
        fe2_probe_keep_nearer(p, o, distance, length, shift, CLASS_LENGTH, CLASS_LENGTH, normal);
        return;
    }
    fe2_probe_geometry(p, game, c);
}
