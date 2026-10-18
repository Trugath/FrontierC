/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Countdown and phase motion handlers. */
#include "people_internal.h"

/* The 32-bit countdown at +0x7a (high word) and +0x7c (low word). */
static uint32_t people_countdown(const fe2_object *object)
{
    return (uint32_t)object->size_mantissa << 16 | object->size_exponent;
}

static void people_set_countdown(fe2_object *object, uint32_t value)
{
    object->size_mantissa = (uint16_t)(value >> 16);
    object->size_exponent = (uint16_t)value;
}

/* Subtracts the frame step << `shift` (32-bit); nonzero on a borrow. */
static int people_count_down(people_context *c, fe2_object *object, unsigned shift)
{
    uint32_t step = c->s->frame_step << shift;
    uint32_t left = people_countdown(object);

    people_set_countdown(object, left - step);
    return left < step;
}

/* a burning wreck: released when its countdown runs out, smoking
 * (effect kind 2) while it is between 0xbc00 and 0xc7ff. */
void people_motion_burning_wreck(people_context *c, fe2_object *object, uint64_t change)
{
    uint16_t high;

    fe2_args_dword(c->args, P2, (uint32_t)change);
    fe2_args_dword(c->args, P3, (uint32_t)(change >> 32));
    if (people_count_down(c, object, 14)) {
        people_release(c, object);
        return;
    }
    high = object->size_mantissa;
    if (high < 0xc800 && high >= 0xbc00) {
        fe2_call_arg change_low = c->args->arg[P2];
        fe2_call_arg change_high = c->args->arg[P3];

        people_position_args(c, object);
        fe2_args_word(c->args, P_MODE, 2);
        /* is the effect's source. The position words do not name it. */
        people_set_object(c, P_CURRENT, object);
        c->g->spawn_effect(c->g->opaque, c->args);
        c->args->arg[P3] = change_high;
        c->args->arg[P2] = change_low;
    }
    c->g->transform_vector(c->g->opaque, object, c->args);
}

/* released when the countdown runs out or above level 0xd. */
void people_motion_expire_countdown_or_distance(people_context *c, fe2_object *object, uint64_t change)
{
    (void)change;
    if (people_count_down(c, object, 15) || (int8_t)object->level > 0x0d)
        people_release(c, object);
}

/* the basis turns with the time of day at rates from the seed. */
void people_motion_rotate_from_seed(people_context *c, fe2_object *object, uint64_t change)
{
    uint16_t low = (uint16_t)object->target_seed;
    uint16_t high = (uint16_t)(object->target_seed >> 16);
    uint32_t a = c->s->time_of_day << ((low & 7) + 6);
    uint32_t b = c->s->time_of_day << ((high & 7) + 6);

    (void)change;
    c->s->basis_angle[0] = (uint16_t)((a >> 16) + high);
    c->s->basis_angle[1] = (uint16_t)((b >> 16) + low);
    c->g->basis(c->g->opaque, object);
}
