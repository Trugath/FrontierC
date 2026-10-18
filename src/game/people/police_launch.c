/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* police launches; 0xafd: launching one ship; 0xcb2: the launch
 * progress. */
#include "offence_internal.h"

enum {
    KIND_PARAMETER = 0, ATTEMPT_PARAMETER = 7, OBJECT_PARAMETER = 9, CREATE_MODE = 5,
    BATCH_KIND = 0x7c, TRICKLE_KIND = 4, BATCH_ATTEMPTS = 3, TRICKLE_CHANCE = 0x4000,
    TRICKLE_ATTEMPTS = 4,
    DOORS_OPENING = 1, DOORS_CLOSING = 0xff, UNDOCKED = 0xff
};

/* A linked source launches from its own position and basis; otherwise the
 * ship is docked to the source. The launch velocity is the basis' third
 * column / 32 (its first component repeated where the third belongs), plus
 * the second column / 64 for a linked trickling source; a linked batch
 * source uses only the second column / 64. Word sums wrap. */
static void place(fe2_object *source, fe2_object *ship)
{
    int16_t velocity[3] = {0, 0, 0};
    int linked = (source->flags & SOURCE_LINKED) != 0, trickle = (source->flags & SOURCE_TRICKLE) != 0;
    unsigned i;
    if (linked) {
        for (i = 0; i < 3; ++i) {
            uint64_t sum = ((uint64_t)ship->position_high[i] << 32 | ship->position_low[i]) +
                           ((uint64_t)source->position_high[i] << 32 | source->position_low[i]);
            ship->position_low[i] = (uint32_t)sum;
            ship->position_high[i] = (uint32_t)(sum >> 32);
        }
        ship->parent_handle = source->parent_handle;
        ship->station_slot = source->station_slot;
        for (i = 0; i < 9; ++i) ship->orientation[i] = source->orientation[i];
    } else {
        ship->parent_handle = source->handle;
        ship->station_slot = UNDOCKED;
    }
    if (!linked || trickle) {
        velocity[0] = (int16_t)(ship->orientation[2] >> 5);
        velocity[1] = (int16_t)(ship->orientation[5] >> 5);
        velocity[2] = (int16_t)(ship->orientation[2] >> 5);
    }
    if (linked) {
        velocity[0] = (int16_t)(velocity[0] + (ship->orientation[1] >> 6));
        velocity[1] = (int16_t)(velocity[1] + (ship->orientation[4] >> 6));
        velocity[2] = (int16_t)(velocity[2] + (ship->orientation[7] >> 6));
    }
    for (i = 0; i < 3; ++i) ship->velocity[i] = velocity[i];
}

/* prepare (market) and create (people) a ship of `kind` (its behaviour);
 * a failed creation gives the source its ship back. */
static void launch_ship(fe2_offence_state *s, const fe2_offence_services *game, fe2_object *source,
                        uint8_t kind, uint16_t attempt)
{
    fe2_call_args args;
    fe2_object *ship;
    s->creation_objects = s->objects;
    fe2_args_clear(&args);
    fe2_args_byte(&args, KIND_PARAMETER, kind);
    fe2_args_word(&args, ATTEMPT_PARAMETER, attempt);
    fe2_args_object(&args, OBJECT_PARAMETER, source);
    game->prepare_child(game->opaque, &args);
    fe2_args_word(&args, ATTEMPT_PARAMETER, CREATE_MODE);
    ship = game->create_object(game->opaque, &args);
    if (!ship) {
        fe2_offences_set_ships_left(source, (int16_t)(fe2_offences_ships_left(source) + 1));
        return;
    }
    ship->behaviour = kind;
    place(source, ship);
}

static void stand_down(fe2_offence_state *s, fe2_object *source)
{
    s->police_flags &= (uint8_t)~ALERTED;
    fe2_offences_set_ships_left(source, 0);
}

/* A trickling source (flag 0x40) launches one ship a quarter of the time
 * once its progress has started; when it runs out the progress falls. */
static void trickle(fe2_offence_state *s, const fe2_offence_services *game, fe2_object *source)
{
    if (source->as.body.progress == 0) {
        s->launch_phase = FE2_LAUNCH_RISING;
        return;
    }
    if ((uint16_t)game->random(game->opaque) >= TRICKLE_CHANCE) return;
    fe2_offences_set_ships_left(source, (int16_t)(fe2_offences_ships_left(source) - 1));
    if (fe2_offences_ships_left(source) < 0) {
        s->launch_phase = FE2_LAUNCH_FALLING;
        stand_down(s, source);
        return;
    }
    launch_ship(s, game, source, TRICKLE_KIND, game->random_below(game->opaque, TRICKLE_ATTEMPTS));
}

/* A batch source opens its doors (docking state), clearing any claim that
 * is free or the player's, then launches up to four ships and starts its
 * progress rising. */
void fe2_offence_launch(fe2_offence_state *s, const fe2_offence_services *game)
{
    fe2_object *source = s->police_source;
    int16_t attempt;
    /* The entry uses the recorded source whatever its role is. */
    if (!source) return;
    if (s->launch_phase != FE2_LAUNCH_READY) return;
    if (source->flags & SOURCE_TRICKLE) {
        trickle(s, game, source);
        return;
    }
    if (fe2_offences_ships_left(source) <= 0) {
        source->as.body.docking_state = DOORS_OPENING;
        stand_down(s, source);
        return;
    }
    if (source->as.body.docking_state != 0) {
        uint8_t claim = source->as.body.docking_claim;
        if (claim == 0 || claim == s->player_handle) {
            source->as.body.docking_state = DOORS_OPENING;
            source->as.body.docking_claim = 0;
            source->as.body.docking_timer = 0;
        }
        return;
    }
    for (attempt = BATCH_ATTEMPTS; attempt >= 0; --attempt) {
        launch_ship(s, game, source, BATCH_KIND, (uint16_t)attempt);
        fe2_offences_set_ships_left(source, (int16_t)(fe2_offences_ships_left(source) - 1));
        if (fe2_offences_ships_left(source) == 0) break;
    }
    source->as.body.docking_state = DOORS_CLOSING;
    s->launch_phase = FE2_LAUNCH_RISING;
    source->as.body.progress = 0;
}

/* the progress moves by a quarter of the frame step: rising, it
 * stops at 0xffff; falling, at 0. Either end makes the source ready. */
void fe2_offence_progress(fe2_offence_state *s)
{
    fe2_object *source = s->police_source;
    uint32_t step = s->frame_step >> 2;
    if (s->launch_phase & 0x80u) {
        if (step > source->as.body.progress) {
            source->as.body.progress = 0;
            s->launch_phase = FE2_LAUNCH_READY;
        } else {
            source->as.body.progress = (uint16_t)(source->as.body.progress - step);
        }
        return;
    }
    if (step >= 0xffffu - source->as.body.progress) {
        source->as.body.progress = 0xffff;
        s->launch_phase = FE2_LAUNCH_READY;
    } else {
        source->as.body.progress = (uint16_t)(source->as.body.progress + step);
    }
}
