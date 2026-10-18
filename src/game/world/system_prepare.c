/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* generate and instantiate the destination system. */
#include "hyperspace_internal.h"

enum { NAMED_BODY_LIMIT = 12 };

/* The destination becomes the current system; the last three systems shift
 * down the history. */
static void enter_system(fe2_hyperspace_state *s, uint32_t system)
{
    s->previous_systems[0] = s->previous_systems[1];
    s->previous_systems[1] = s->previous_systems[2];
    s->previous_systems[2] = s->current_system;
    s->current_system = system;
}

/* Byte 0x518e: zero with 12 or more named bodies; otherwise
 * 8 * (12 - named) scaled twice by the generation random word (high
 * words of 16-bit products). */
static uint8_t body_shortfall(const fe2_hyperspace_state *s)
{
    uint16_t value;
    if (s->named_bodies >= NAMED_BODY_LIMIT) return 0;
    value = (uint16_t)((NAMED_BODY_LIMIT - s->named_bodies) << 3);
    value = (uint16_t)(((uint32_t)value * s->generation_random) >> 16);
    value = (uint16_t)(((uint32_t)value * s->generation_random) >> 16);
    return (uint8_t)value;
}

void fe2_hyperspace_prepare(fe2_hyperspace_state *s, const fe2_hyperspace_services *game)
{
    uint8_t records[FE2_HYPERSPACE_RECORD_BYTES];
    fe2_jump_target target = game->target_system(game->opaque);
    fe2_call_args args;
    enter_system(s, target.system);
    fe2_args_clear(&args);
    fe2_args_template(&args, RECORDS_PARAMETER, records);
    fe2_args_word(&args, MODE_PARAMETER, 0);
    fe2_args_string(&args, NAME_PARAMETER, target.name);
    game->system_action(game->opaque, &target, &args);
    s->station_count_copy = s->sol_station_count;
    s->emitted_limit = body_shortfall(s);
    s->reference_index = 0;
    fe2_args_clear(&args);
    fe2_args_template(&args, RECORDS_PARAMETER, records);
    fe2_args_dword(&args, TIME_PARAMETER, s->time_of_day);
    fe2_args_dword(&args, DAY_PARAMETER, s->day);
    fe2_args_word(&args, MODE_PARAMETER, 1);
    game->system_action(game->opaque, 0, &args);
}
