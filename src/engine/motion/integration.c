/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* control limits and integration, 0x1068a..0x109b2 the
 * linked force. */
#include "motion_internal.h"

enum {
    STEP_SHIFT = 3, SCALE_LIMIT = 0x7fff, MAX_STEP_SHIFT = 15,
    FRACTION_LIMIT = 0x4014,           /* scales above it, small steps, add within the limits */
    WORD_SHIFT = 16
};

void fe2_motion_set_controls(fe2_object *o, int32_t controls[3])
{
    unsigned k;
    for (k = 0; k < 3; ++k) {
        int32_t positive = o->as.flight.control_limits[k][0], negative = o->as.flight.control_limits[k][1];
        if (controls[k] >= 0) {
            if (controls[k] > positive) controls[k] = positive;
        } else if (controls[k] < negative) {
            controls[k] = negative;
        }
        o->controls[k] = (int16_t)(uint16_t)(uint32_t)controls[k];
    }
}

void fe2_motion_add_fractional(fe2_object *o, const int32_t v[3])
{
    unsigned i;
    for (i = 0; i < 3; ++i) {
        uint32_t fraction = (uint32_t)o->velocity_fraction[i] + (uint16_t)(uint32_t)v[i];
        int32_t high = (int16_t)(uint16_t)((uint32_t)v[i] >> WORD_SHIFT);
        o->velocity_fraction[i] = (uint16_t)fraction;
        o->velocity[i] = (int32_t)((uint32_t)o->velocity[i] + (uint32_t)high + (fraction >> WORD_SHIFT));
    }
}

/* The step over 8 as a word scale; a larger one is halved until it fits,
 * and the shift is 16 less the halvings (-1 when none were needed). */
static int16_t step_scale(uint32_t step, int16_t *shift)
{
    step >>= STEP_SHIFT;
    *shift = -1;
    if (step > SCALE_LIMIT) {
        *shift = MAX_STEP_SHIFT;
        for (;;) {
            step >>= 1;
            if (step <= SCALE_LIMIT) break;
            if (--*shift < 0) break;
        }
    }
    return (int16_t)(uint16_t)step;
}

/* Twice the sum over the controls of each row's column word (scaled:
 * the high word of twice its product) times the control. */
static void accumulate(const fe2_object *o, int16_t scale, int32_t acc[3])
{
    unsigned k, r;
    for (r = 0; r < 3; ++r) acc[r] = 0;
    for (k = 0; k < 3; ++k) {
        int16_t control = o->controls[k];
        if (control == 0) continue;
        for (r = 0; r < 3; ++r) {
            uint32_t product = (uint32_t)((int32_t)o->orientation[3 * r + k] * scale) << 1;
            int16_t scaled = (int16_t)(uint16_t)(product >> WORD_SHIFT);
            acc[r] = (int32_t)((uint32_t)acc[r] + (uint32_t)((int32_t)scaled * control));
        }
    }
    for (r = 0; r < 3; ++r) acc[r] = (int32_t)((uint32_t)acc[r] << 1);
}

/* A signed 32-bit add, false on overflow. */
static int add_checked(int32_t *to, int32_t v, uint32_t carry)
{
    int64_t sum = (int64_t)*to + v + (int64_t)carry;
    *to = (int32_t)(uint32_t)(uint64_t)sum;
    return sum >= INT32_MIN && sum <= INT32_MAX;
}

/* Each increment is limited by the feedback (its unsigned comparison:
 * toward zero for a limit of the other sign); an overflowing velocity
 * restores all three (the fractions keep their additions). */
void fe2_motion_integrate_limited(fe2_object *o, uint32_t step)
{
    int16_t shift, scale = step_scale(step, &shift);
    int32_t acc[3], saved[3];
    unsigned i;
    accumulate(o, scale, acc);
    for (i = 0; i < 3; ++i) saved[i] = o->velocity[i];
    if (shift < 0) {
        if (scale <= FRACTION_LIMIT) {
            fe2_motion_add_fractional(o, acc);
            return;
        }
        for (i = 0; i < 3; ++i) {
            int32_t v = (int16_t)(uint16_t)((uint32_t)acc[i] >> WORD_SHIFT), limit = o->feedback[i];
            uint32_t fraction = (uint32_t)o->velocity_fraction[i] + (uint16_t)(uint32_t)acc[i];
            if (v >= 0 ? (uint32_t)v >= (uint32_t)limit : (uint32_t)v <= (uint32_t)limit) v = limit;
            o->velocity_fraction[i] = (uint16_t)fraction;
            if (!add_checked(&o->velocity[i], v, fraction >> WORD_SHIFT)) {
                for (i = 0; i < 3; ++i) o->velocity[i] = saved[i];
                return;
            }
        }
        return;
    }
    for (i = 0; i < 3; ++i) {
        int32_t v = acc[i] >> shift, limit = o->feedback[i];
        if (limit >= 0 ? (uint32_t)v >= (uint32_t)limit : (uint32_t)v <= (uint32_t)limit)
            v = (v ^ limit) >= 0 ? limit : 0;
        if (!add_checked(&o->velocity[i], v, 0)) {
            for (i = 0; i < 3; ++i) o->velocity[i] = saved[i];
            return;
        }
    }
}

void fe2_motion_integrate(fe2_object *o, uint32_t step)
{
    int16_t shift, scale = step_scale(step, &shift);
    int32_t acc[3];
    unsigned i;
    accumulate(o, scale, acc);
    if (shift < 0) {
        fe2_motion_add_fractional(o, acc);
        return;
    }
    for (i = 0; i < 3; ++i) o->velocity[i] = (int32_t)((uint32_t)o->velocity[i] + (uint32_t)(acc[i] >> shift));
}

/* The position's direction words and the parent's mass over the cube of
 * their length, times `scale` (reduced to a word, counting the halvings),
 * negated: added to the velocity with its fraction, or (for a large
 * exponent) whole. */
void fe2_motion_linked_force(fe2_motion *m, fe2_object *o, int32_t scale)
{
    const fe2_object *parent;
    int64_t position[3];
    fe2_scaled w;
    fe2_scalar n, cube, mass, q;
    fe2_scalar32 square;
    int32_t pull[3];
    int16_t factor, exponent;
    uint16_t count;
    unsigned i;
    if (o->parent_handle == 0) return;
    parent = fe2_pool_record(m->pool, o->parent_handle);
    for (i = 0; i < 3; ++i) position[i] = (int64_t)((uint64_t)o->position_high[i] << 32 | o->position_low[i]);
    w = fe2_motion_scale_wide(position);
    if (parent->size_mantissa == 0) return;
    for (i = 0; i < 3; ++i) m->radial[i] = w.v[i];
    n.mantissa = fe2_math_length3(w.v[0], w.v[1], w.v[2]);
    n.exponent = w.shift;
    n = fe2_math_normalize_unsigned(n);
    if (n.mantissa == 0) return;
    square = fe2_math_multiply(n, n);
    cube.mantissa = (uint16_t)((uint32_t)((int32_t)(int16_t)n.mantissa * (int16_t)(uint16_t)(square.mantissa >> 16))
                               << 1 >> 16);
    cube.exponent = square.exponent;
    cube = fe2_math_normalize_unsigned(cube);
    mass.mantissa = parent->size_mantissa;
    mass.exponent = parent->size_exponent;
    q = fe2_math_divide(mass, cube);
    count = q.exponent;
    if (scale < 0) {
        do {
            ++count;
            scale >>= 1;
        } while (scale < -0x7fff);
    } else if (scale > 0x7fff) {
        do {
            ++count;
            scale >>= 1;
        } while (scale > 0x7fff);
    }
    factor = (int16_t)(uint16_t)-(uint16_t)(uint32_t)((uint32_t)((int32_t)(int16_t)q.mantissa * (int16_t)scale) << 1 >>
                                                    WORD_SHIFT);
    for (i = 0; i < 3; ++i) pull[i] = (int32_t)w.v[i] * factor;
    exponent = (int16_t)(uint16_t)-count;
    if (exponent < 0) {
        exponent = (int16_t)(exponent + WORD_SHIFT);
        if (exponent > 0)
            for (i = 0; i < 3; ++i) pull[i] >>= exponent;
        for (i = 0; i < 3; ++i) o->velocity[i] = (int32_t)((uint32_t)o->velocity[i] + (uint32_t)pull[i]);
        return;
    }
    for (i = 0; i < 3; ++i) pull[i] = (int32_t)(pull[i] >> (exponent > 31 ? 31 : exponent));
    fe2_motion_add_fractional(o, pull);
}
