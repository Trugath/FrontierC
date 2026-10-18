/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* steering feedback, 0x10fc8..0x1129c with the scene probe,
 * 0x10c51..0x10e44 axis feedback. */
#include "motion_internal.h"

enum {
    MOTION_LIMIT = 0x4000, MOTION_KEEP = 0x2100, MOTION_LEFT_LIMIT = 0x2000,
    MIN_MOTION_SHIFT = 2, TRANSITION_RANGE = 8, TRANSITION_MARGIN = 2,
    EXPONENT_BIAS = 8, MAX_EXPAND = 12, FRAME_EXPAND = 2,
    FOLLOW_PARENT = 0x20, FOLLOW_TARGET = 0x10,         /* target flags (+0x118) */
    UNIT_SHIFT = 14, PROBE_COUNT_SHIFT = 4, PROBE_EXTENT_BIAS = 11, MIN_PROBE_EXTENT = 15,
    PROBE_NARROW = 0x1880, PROBE_WIDE = 0x5880, PROBE_LENGTH_BIAS = 15,
    AXIS_COLUMN = 2, AXIS_MAX_EXPAND = 4
};

static int16_t sar16(int16_t v, unsigned n) { return (int16_t)(v >> ((n & 31u) > 15u ? 15u : (n & 31u))); }

/* An arithmetic shift by a count the original loops over (any size). */
static int32_t sar32(int32_t v, unsigned long n) { return (int32_t)(v >> (n > 31ul ? 31ul : n)); }

static uint32_t magnitude32(int32_t v) { return v < 0 ? 0u - (uint32_t)v : (uint32_t)v; }

/* The relative velocity as words and their shift. Within 0x4000 it is
 * doubled up to 0x2000; at 0x2100 and above, or zero, the original keeps
 * the registers of its measure: the envelope, then the third magnitude's
 * low and high words. */
static int16_t motion_words(const int32_t v[3], int16_t w[3])
{
    uint32_t a[3], envelope;
    int16_t shift = 0;
    unsigned i;
    for (i = 0; i < 3; ++i) a[i] = magnitude32(v[i]);
    envelope = a[0] | a[1] | a[2];
    if (envelope >= MOTION_LIMIT) {
        do {
            ++shift;
            envelope >>= 1;
        } while (envelope >= MOTION_LIMIT);
        for (i = 0; i < 3; ++i) w[i] = (int16_t)(uint16_t)(uint32_t)(v[i] >> shift);
        return shift;
    }
    if (envelope == 0 || envelope >= MOTION_KEEP) {
        w[0] = (int16_t)(uint16_t)envelope;
        w[1] = (int16_t)(uint16_t)a[2];
        w[2] = (int16_t)(uint16_t)(a[2] >> 16);
        return 0;
    }
    do {
        --shift;
        envelope = (envelope << 1) & 0xffffu;
    } while (envelope < MOTION_LEFT_LIMIT);
    for (i = 0; i < 3; ++i) w[i] = (int16_t)(uint16_t)((uint16_t)v[i] << -shift);
    return shift;
}

/* (v * 16384) / d, as IDIV; false when the quotient overflows a word. */
static int divide(int16_t v, uint16_t d, int16_t *q)
{
    int32_t quotient = ((int32_t)v * (1 << UNIT_SHIFT)) / (int16_t)d;
    if (quotient > INT16_MAX || quotient < INT16_MIN) return 0;
    *q = (int16_t)quotient;
    return 1;
}

static int16_t dividend_low(int16_t v) { return (int16_t)(uint16_t)((uint16_t)v << UNIT_SHIFT); }

/* The steering words over half the square root of their length. The
 * original swaps its registers between the divides; a divide fault (the
 * resident handler resumes after the divides with AX unchanged) leaves
 * them part way: the faulting component's dividend low word first, then
 * the components as they stood. */
static void scale_by_length(int16_t *a, int16_t *b, int16_t *c)
{
    uint16_t length = fe2_math_length3(*a, *b, *c);
    uint16_t d = (uint16_t)(fe2_math_square_root32((uint32_t)length << 16) >> 1);
    int16_t q, x;
    if (d == 0) return;
    if (!divide(*a, d, &q)) {
        *a = dividend_low(*a);
        return;
    }
    x = q;                                   /* AX = y, BX = x' */
    *a = *b;
    *b = x;
    if (!divide(*a, d, &q)) {
        *a = dividend_low(*a);
        return;
    }
    *b = q;                                  /* BX = y', CX = x', AX = z */
    *a = *c;
    *c = x;
    if (!divide(*a, d, &q)) {
        *a = dividend_low(*a);
        return;
    }
    *a = x;
    *c = q;
}

/* Into the target's frame: the frame is selected, then the record follows
 * the target's parent (flags 0x20) or the target itself (0x10), adds the
 * relative velocity and position and stops (its feedback, fractions and
 * controls cleared). */
static fe2_feedback transition(fe2_motion *m, const fe2_motion_services *game, fe2_object *o, fe2_object *target)
{
    fe2_feedback r;
    int32_t v[3];
    int64_t d[3];
    unsigned i;
    fe2_motion_select_frame(m, game, o);
    if (target->flags & FOLLOW_PARENT) {
        o->parent_handle = target->parent_handle;
        o->station_slot = target->station_slot;
    } else if (target->flags & FOLLOW_TARGET) {
        o->parent_handle = target->handle;
        o->station_slot = 0;
    }
    fe2_motion_relative_velocity(game, o, target, v);
    for (i = 0; i < 3; ++i) o->velocity[i] = (int32_t)((uint32_t)o->velocity[i] + (uint32_t)v[i]);
    fe2_motion_offset_position(game, o, target, d);
    for (i = 0; i < 3; ++i) {
        uint64_t p = ((uint64_t)o->position_high[i] << 32 | o->position_low[i]) + (uint64_t)d[i];
        o->position_low[i] = (uint32_t)p;
        o->position_high[i] = (uint32_t)(p >> 32);
    }
    o->render_state = 0;
    target->update_period = 0;
    target->update_countdown = 0;
    for (i = 0; i < 3; ++i) {
        r.v[i] = 0;
        o->feedback[i] = 0;
        o->velocity_fraction[i] = 0;
        o->controls[i] = 0;
    }
    r.shift = 0;
    return r;
}

fe2_feedback fe2_motion_steering_feedback(fe2_motion *m, const fe2_motion_services *game, fe2_object *o,
                                          fe2_object *target, uint16_t exponent, const int16_t motion[3],
                                          int16_t motion_shift)
{
    fe2_feedback r;
    int16_t a = o->steering[0], b = o->steering[1], c = o->steering[2], mw[3], out[3], words[3], s, delta, t;
    uint16_t shift = o->steering_shift;
    int32_t wide[3];
    unsigned i;
    for (i = 0; i < 3; ++i) mw[i] = motion[i];
    if (m->time_exponent != 0) {
        int16_t bias = motion_shift < MIN_MOTION_SHIFT ? MIN_MOTION_SHIFT : motion_shift;
        int16_t te = (int16_t)m->time_exponent;
        int16_t d = (int16_t)(uint16_t)(shift - (uint16_t)bias - (uint16_t)te - (uint16_t)(int16_t)(te >> 2));
        if (d <= TRANSITION_RANGE) {
            o->flags &= 0x00ffu;
            target->update_countdown = 0;
            if ((int16_t)(d - TRANSITION_MARGIN) < 0 &&
                (o->source_offset[0] | o->source_offset[1] | o->source_offset[2]) != 0)
                return transition(m, game, o, target);
        }
    }
    s = (int16_t)(uint16_t)(shift + exponent - EXPONENT_BIAS);
    if (s >= 0) {
        if (s & 1) {
            s = (int16_t)((s >> 1) + 1);
            a = sar16(a, 1), b = sar16(b, 1), c = sar16(c, 1);
        } else {
            s = (int16_t)(s >> 1);
        }
        scale_by_length(&a, &b, &c);
    }
    s = (int16_t)(s - 2);
    delta = (int16_t)(motion_shift - s);
    if (delta >= 0) {
        a = sar16(a, (uint8_t)delta), b = sar16(b, (uint8_t)delta), c = sar16(c, (uint8_t)delta);
        s = (int16_t)(s + delta);
    } else {
        uint8_t n = (uint8_t)-delta;
        for (i = 0; i < 3; ++i) mw[i] = sar16(mw[i], n);
    }
    words[0] = (int16_t)(a + mw[0]);
    words[1] = (int16_t)(b + mw[1]);
    words[2] = (int16_t)(c + mw[2]);
    for (i = 0; i < 3; ++i) wide[i] = words[i];
    if (s > 0) {
        if (s >= MAX_EXPAND) s = MAX_EXPAND;
        for (i = 0; i < 3; ++i) wide[i] = (int32_t)((uint32_t)wide[i] << s);
    }
    for (i = 0; i < 3; ++i) o->feedback[i] = wide[i];
    fe2_motion_by_columns(o->orientation, words, out);
    for (i = 0; i < 3; ++i) r.v[i] = out[i];
    t = (int16_t)(s + FRAME_EXPAND);
    if (t < 0) {
        for (i = 0; i < 3; ++i) r.v[i] = sar32(r.v[i], (unsigned long)-(long)t);
    } else if (t > 0) {
        if (t > FRAME_EXPAND) t = FRAME_EXPAND;
        for (i = 0; i < 3; ++i) r.v[i] = (int32_t)((uint32_t)r.v[i] << t);
    }
    r.shift = shift;
    return r;
}

/* The steering (offset relative position) words are kept on the record. */
static int16_t steering(fe2_motion *m, const fe2_motion_services *game, fe2_object *o, fe2_object *target,
                        int16_t mw[3])
{
    int32_t v[3];
    int16_t motion_shift;
    fe2_scaled p;
    unsigned i;
    fe2_motion_relative_velocity(game, o, target, v);
    motion_shift = motion_words(v, mw);
    p = fe2_motion_offset_difference(m, game, o, target);
    for (i = 0; i < 3; ++i) o->steering[i] = p.v[i];
    o->steering_shift = p.shift;
    return motion_shift;
}

fe2_feedback fe2_motion_steer(fe2_motion *m, const fe2_motion_services *game, fe2_object *o, fe2_object *target,
                              uint16_t exponent)
{
    int16_t mw[3], motion_shift = steering(m, game, o, target, mw);
    return fe2_motion_steering_feedback(m, game, o, target, exponent, mw, motion_shift);
}

/* The probe's extent: the largest velocity magnitude, halved, shifted up
 * by the time exponent + 11 (at least 15). When bits pass the top, the
 * upper part goes in the second dword (computed as the original does,
 * which drops it beyond 32 bits). */
static void probe_extent(const fe2_motion *m, const fe2_object *o, fe2_motion_probe *request)
{
    uint32_t largest = magnitude32(o->velocity[0]), low;
    uint16_t count = (uint16_t)(m->game_time_exponent + PROBE_EXTENT_BIAS), upper;
    unsigned i;
    int carried = 0;
    for (i = 1; i < 3; ++i)
        if (magnitude32(o->velocity[i]) >= largest) largest = magnitude32(o->velocity[i]);
    largest >>= 1;
    if ((int16_t)count < MIN_PROBE_EXTENT) count = MIN_PROBE_EXTENT;
    low = largest;
    for (i = 0; i < count; ++i) {
        if (low & 0x80000000u) carried = 1;
        low <<= 1;
    }
    request->extent = low;
    if (!carried) {
        request->extent_high = 0;
        request->mode = PROBE_NARROW;
        return;
    }
    upper = (uint16_t)(32u - count);
    request->extent_high = upper == 0 ? largest : upper >= 32 ? 0 : largest >> upper;
    request->mode = PROBE_WIDE;
}

fe2_feedback fe2_motion_steer_probed(fe2_motion *m, const fe2_motion_services *game, fe2_object *o,
                                     fe2_object *target, uint16_t exponent)
{
    int16_t mw[3], motion_shift = steering(m, game, o, target, mw), unit[3];
    uint8_t step = (uint8_t)(m->game_time_exponent + 1u), countdown = o->probe_countdown;
    fe2_motion_probe request;
    fe2_motion_hit hit;
    unsigned i;
    o->probe_countdown = (uint8_t)(countdown - step);
    if (countdown < step) {
        o->probe_countdown = (uint8_t)((uint16_t)(m->game_time_exponent + 1u) << PROBE_COUNT_SHIFT);
        for (i = 0; i < 3; ++i) unit[i] = o->steering[i];
        fe2_motion_normalize(unit);
        probe_extent(m, o, &request);
        request.handle = o->handle;
        for (i = 0; i < 3; ++i) request.direction[i] = unit[i];
        game->probe(game->opaque, o, target, &request, &hit);
        if (hit.hit) {
            fe2_scalar length;
            length.mantissa = fe2_math_length3(o->steering[0], o->steering[1], o->steering[2]);
            length.exponent = (uint16_t)(o->steering_shift + PROBE_LENGTH_BIAS);
            if ((int64_t)((uint64_t)hit.distance - (uint64_t)fe2_math_to_signed64(length)) < 0) {
                for (i = 0; i < 3; ++i)
                    o->steering[i] = (int16_t)(sar16(hit.direction[i], 1) + sar16(unit[i], 2));
                o->probe_countdown = 0;
            }
        }
    }
    return fe2_motion_steering_feedback(m, game, o, target, exponent, mw, motion_shift);
}

/* Each row's third-column word times |speed| (the product's bits 15..46),
 * with both signs, less the velocity: the feedback limits, transformed
 * into the record's frame and doubled up to four times. */
fe2_feedback fe2_motion_axis_feedback(fe2_object *o, int32_t speed)
{
    fe2_feedback r;
    uint32_t magnitude = magnitude32(speed);
    int32_t limits[3];
    unsigned i, n;
    for (i = 0; i < 3; ++i) {
        int16_t w = o->orientation[3 * i + AXIS_COLUMN];
        uint32_t a = w < 0 ? (uint16_t)(0u - (uint16_t)w) : (uint16_t)w;
        uint32_t p = (uint32_t)(((uint64_t)a * magnitude) >> 16) << 1;
        if (w < 0) p = 0u - p;
        if (speed < 0) p = 0u - p;
        limits[i] = (int32_t)(p - (uint32_t)o->velocity[i]);
        o->feedback[i] = limits[i];
    }
    r = fe2_motion_transform_feedback(o, limits);
    n = r.shift + 2u;
    if (n > AXIS_MAX_EXPAND) n = AXIS_MAX_EXPAND;
    for (i = 0; i < 3; ++i) r.v[i] = (int32_t)((uint32_t)r.v[i] << n);
    return r;
}
