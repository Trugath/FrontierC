/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* occasionally create a child object (for example a cargo canister)
 * beside `source`, with the source's position, orientation and drift. */
#include "status_internal.h"

enum {
    MAX_SOURCE_LEVEL = 0x11,
    CHILD_CHANCE = 0x1000,        /* random word below this: 1 in 16 */
    CHILD_OPTIONS = 4,
    MARKET_CHILD_SERVICE = 9,     /* also the people creation model the original passes */
    MARKET_CHILD_REQUEST = 4,
    NAMED_SOURCE_MODE = 7, OTHER_SOURCE_MODE = 8,
    CHILD_BEHAVIOUR = 4,
    DRIFT_SHIFT = 5
};

/* Drift from two orientation entries, each scaled down by 32. */
static int32_t drift(int16_t a, int16_t b)
{
    return (int16_t)(fe2_status_sar16(a, DRIFT_SHIFT) + fe2_status_sar16(b, DRIFT_SHIFT));
}

void fe2_status_spawn_child(const fe2_game_services *game, fe2_object *source)
{
    fe2_call_args args;
    fe2_object *child;
    uint16_t option;
    unsigned axis, i;

    if (source->level >= MAX_SOURCE_LEVEL) return;
    fe2_args_clear(&args);
    fe2_args_object(&args, PARAM_RESULT, source);
    if ((uint16_t)game->random(game->opaque) >= CHILD_CHANCE) return;
    option = game->random_below(game->opaque, CHILD_OPTIONS);
    fe2_args_word(&args, PARAM_STYLE, option);
    game->market_service(game->opaque, MARKET_CHILD_SERVICE, MARKET_CHILD_REQUEST, &args);
    fe2_args_word(&args, PARAM_STYLE, source->name[0] == '*' ? NAMED_SOURCE_MODE
                                                             : OTHER_SOURCE_MODE);
    child = game->create_object(game->opaque, MARKET_CHILD_SERVICE, &args);
    if (child == 0) return;

    child->behaviour = CHILD_BEHAVIOUR;
    for (axis = 0; axis < 3; ++axis) {
        uint32_t low = child->position_low[axis] + source->position_low[axis];
        child->position_high[axis] += source->position_high[axis] +
                                      (low < source->position_low[axis]);
        child->position_low[axis] = low;
    }
    child->parent_handle = source->parent_handle;
    child->station_slot = source->station_slot;
    for (i = 0; i < 9; ++i) child->orientation[i] = source->orientation[i];
    for (axis = 0; axis < 3; ++axis)
        child->velocity[axis] = drift(child->orientation[3 * axis + 2],
                                      child->orientation[3 * axis + 1]);
}
