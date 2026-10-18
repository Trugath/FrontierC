/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* turning steps. */
#include "motion.h"

enum {
    DEAD_ZONE = 15, LIMIT = 0x7fff, FULL_BUDGET = 60, MAX_PRESHRINK_SHIFT = 5,
    HALVING_MODEL = 0x3a, QUARTERING_MODEL = 0x46
};

static const struct { int16_t threshold; uint16_t shift; } steps[] = {
    {0x9fb, 2}, {0x511, 3}, {0x28b, 4}, {0x146, 5}, {0xa3, 6}, {0x52, 7}, {0x29, 8}
};

/* The largest power-of-two-ish step not above the remainder, and its
 * shift (20 at shift 9 below the last threshold). */
int16_t fe2_motion_turn_step(int16_t remainder, uint16_t *shift)
{
    unsigned i;
    for (i = 0; i < sizeof steps / sizeof steps[0]; ++i)
        if (remainder >= steps[i].threshold) {
            *shift = steps[i].shift;
            return steps[i].threshold;
        }
    *shift = 9;
    return 0x14;
}

/* Turn column p toward column q (positive) or away (negative) by one step
 * of `shift`: for small shifts both columns first shrink by
 * 2^-(2 * shift + 1), then q += p >> shift and p -= q >> shift (from the
 * values before the turn). */
static void turn_columns(fe2_object *o, unsigned p, unsigned q, uint16_t shift, int negative)
{
    unsigned row, n = shift & 31u;
    if (shift <= MAX_PRESHRINK_SHIFT) {
        unsigned pre = (2u * shift + 1u) & 31u;
        for (row = 0; row < 3; ++row) {
            int16_t *a = &o->orientation[3 * row + p], *b = &o->orientation[3 * row + q];
            int16_t da = (int16_t)(*a >> (pre > 15 ? 15 : pre)), db = (int16_t)(*b >> (pre > 15 ? 15 : pre));
            *a = (int16_t)(uint16_t)((uint16_t)*a - (uint16_t)da);
            *b = (int16_t)(uint16_t)((uint16_t)*b - (uint16_t)db);
        }
    }
    for (row = 0; row < 3; ++row) {
        int16_t *a = &o->orientation[3 * row + p], *b = &o->orientation[3 * row + q];
        int16_t pa = (int16_t)(*a >> (n > 15 ? 15 : n)), qb = (int16_t)(*b >> (n > 15 ? 15 : n));
        if (negative) {
            *b = (int16_t)(uint16_t)((uint16_t)*b - (uint16_t)pa);
            *a = (int16_t)(uint16_t)((uint16_t)*a + (uint16_t)qb);
        } else {
            *b = (int16_t)(uint16_t)((uint16_t)*b + (uint16_t)pa);
            *a = (int16_t)(uint16_t)((uint16_t)*a - (uint16_t)qb);
        }
    }
}

/* One axis: a remainder beyond the dead zone (capped at the limit) is
 * turned by one step, and the step's cost comes out of the rebuild
 * budget; an exhausted budget renormalizes the basis. */
static void turn_axis(fe2_object *o, fe2_turn *t, unsigned axis, unsigned p, unsigned q)
{
    int16_t remainder = o->spin[axis], magnitude, step;
    int negative = remainder < 0;
    uint16_t shift;
    uint8_t cost;
    magnitude = negative ? (int16_t)(uint16_t)-(uint16_t)remainder : remainder;
    if (magnitude > (int16_t)(uint16_t)t->step_limit) magnitude = (int16_t)(uint16_t)t->step_limit;
    if (magnitude < DEAD_ZONE) return;
    step = fe2_motion_turn_step(magnitude, &shift);
    o->spin[axis] = (int16_t)(uint16_t)(negative ? (uint16_t)remainder + (uint16_t)step
                                                 : (uint16_t)remainder - (uint16_t)step);
    t->last_shift = shift;
    turn_columns(o, p, q, shift, negative);
    cost = fe2_turn_costs[shift - 2];
    if (o->rebuild_budget < cost) {
        o->rebuild_budget = (uint8_t)(o->rebuild_budget - cost);
        fe2_motion_rebuild_basis(o);
        o->rebuild_budget = FULL_BUDGET;
    } else {
        o->rebuild_budget = (uint8_t)(o->rebuild_budget - cost);
    }
}

void fe2_motion_turn(fe2_object *o, fe2_turn *t)
{
    unsigned axis;
    if (o->model >= HALVING_MODEL) {
        t->step_limit >>= 1;
        if (o->model >= QUARTERING_MODEL) t->step_limit >>= 1;
    }
    if (t->integrate != 0) {
        if (t->rate_scale >= LIMIT) t->rate_scale = (t->rate_scale & 0xffff0000u) | LIMIT;
        for (axis = 0; axis < 3; ++axis)
            o->spin[axis] = (int16_t)(uint16_t)((uint16_t)o->spin[axis] +
                (uint16_t)(((uint32_t)((int32_t)o->rotation_rates[axis] * (int16_t)(uint16_t)t->rate_scale)) >> 16));
    }
    if (t->step_limit >= LIMIT) t->step_limit = (t->step_limit & 0xffff0000u) | LIMIT;
    /* Axis 0 turns column 2 toward 1, axis 1 column 1 toward 0, axis 2
     * column 0 toward 2. */
    turn_axis(o, t, 0, 2, 1);
    turn_axis(o, t, 1, 1, 0);
    turn_axis(o, t, 2, 0, 2);
}
