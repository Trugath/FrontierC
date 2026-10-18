/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* 0x82: replace an object's motion sample and move it by the change. */
#include "navigation_internal.h"

enum { PREVIOUS_PARAMETER = 0, CHANGE_PARAMETER = 2 };

static void sample_args(fe2_call_args *args, unsigned n, uint64_t value)
{
    fe2_args_dword(args, n, (uint32_t)value);
    fe2_args_dword(args, n + 1, (uint32_t)(value >> 32));
}

/* Fixed objects (behaviour 0x84) take the new sample without moving. The
 * change wraps modulo 2^64. */
void fe2_navigation_update_motion(const fe2_navigation_services *game, fe2_object *o,
                                  uint64_t sample, fe2_motion_samples *published)
{
    fe2_call_args args;
    uint64_t previous = (uint64_t)o->orbit[1] << 32 | o->orbit[0];
    o->orbit[0] = (uint32_t)sample;
    o->orbit[1] = (uint32_t)(sample >> 32);
    published->previous = previous;
    published->change = sample;
    if (o->behaviour == BEHAVIOUR_FIXED) return;
    published->change = sample - previous;
    o->update_period = o->update_countdown = 0;
    fe2_args_clear(&args);
    sample_args(&args, PREVIOUS_PARAMETER, previous);
    sample_args(&args, CHANGE_PARAMETER, published->change);
    game->motion_update(game->opaque, o, &args);
    game->reference_update(game->opaque, o);
    fe2_args_clear(&args);
    sample_args(&args, CHANGE_PARAMETER, published->change);
    game->transform_vector(game->opaque, o, &args);
}
