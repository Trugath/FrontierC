/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the docking timer countdown. */
#include "orbits.h"

enum {
    CLAIM_LAPSED_TEXT = 0x98a6, SEED_PARAMETER = 3, MESSAGE_PARAMETER = 7, MESSAGE_STYLE = 0x18,
    OBJECT_PARAMETER = 9, RELEASE_REQUEST = 2,
    FOLLOWING_STATE = 4, BEHAVIOUR_FOLLOW = 0x20
};

/* The player whose claim lapsed is told (with the target seed's words
 * swapped); then the claim is released through market. */
static void expire(fe2_orbit_state *s, const fe2_orbit_services *game, fe2_object *o)
{
    fe2_call_args args;
    if (o->as.body.docking_claim == s->player_handle) {
        if (s->flight_state == FOLLOWING_STATE) o->behaviour = BEHAVIOUR_FOLLOW;
        fe2_args_clear(&args);
        fe2_args_dword(&args, SEED_PARAMETER, o->target_seed << 16 | o->target_seed >> 16);
        fe2_args_word(&args, MESSAGE_PARAMETER, MESSAGE_STYLE);
        game->show_message(game->opaque, CLAIM_LAPSED_TEXT, &args);
    }
    fe2_args_clear(&args);
    fe2_args_object(&args, OBJECT_PARAMETER, o);
    game->reference_service(game->opaque, RELEASE_REQUEST, o->as.body.docking_claim, &args);
}

/* A fixed body becomes the reference when the current reference orders
 * after it (signed level, +0x5f). */
static void check_reference(fe2_orbit_state *s, const fe2_orbit_services *game, fe2_object *o)
{
    const fe2_object *reference;
    if (!(o->flags & FE2_ORBIT_FIXED)) return;
    reference = game->lookup_object(game->opaque, s->reference_index);
    if ((int8_t)reference->level > (int8_t)o->level) s->reference_index = o->handle;
}

/* A running timer expires when the countdown reaches or passes zero
 * (it is not clamped); a stopped (zero) timer does not. */
void fe2_orbit_count_down(fe2_orbit_state *s, const fe2_orbit_services *game, fe2_object *o,
                          uint16_t elapsed)
{
    uint16_t timer = o->as.body.docking_timer;
    if (timer != 0) {
        o->as.body.docking_timer = (uint16_t)(timer - elapsed);
        if (elapsed >= timer) expire(s, game, o);
    }
    check_reference(s, game, o);
}
