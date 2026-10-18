/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the hyperspace jump, its outcomes and the object helpers. */
#include "hyperspace_internal.h"

enum {
    JUMP_FROM_SPACE = 0, JUMP_FROM_48 = 0x48, IN_HYPERSPACE = 8,
    JUMP_CLOCK_READY = 0xb71b00,
    EVENT_JUMP = 1, EVENT_ARRIVED = 2, EVENT_ARRIVING = 0x13, EVENT_MISJUMP = 0x14,
    HYPERSPACE_FLAGS = 0x4f, ENTRY_SPREAD = 0x2000, ENTRY_SCALE = 0x4000, ENTRY_EXPONENT = 0x33,
    JUMP_EFFECT = 0x21, MISJUMP_SAMPLE = 0xfe00,
    MISJUMP_TEXT = 0x98ae, FORCED_MISJUMP_TEXT = 0x98af, DRIVE_DESTROYED_TEXT = 0x98b0,
    ARRIVAL_SCALE = 0x5600, ARRIVAL_EXPONENT = 0x39, DISTANCE_SCALE = 0x151,
    DISTANCE_EXPONENT = 0x3f,
    MAP_SAVED_SCALE = 0x337fff, MAP_SELECTION = 0x72,
    CLOCK_5709 = 0xf880000, FIELD_570D = 0x12,
    DESTROYED_DRIVE_CLASS = 1, DESTROYED_DRIVE_KEEP = 4
};

/* Misjump outcomes (the table's selectors). */
enum {
    OUTCOME_REPREPARE = 0, OUTCOME_DESTROY_REPREPARE = 1, OUTCOME_DESTROY = 3,
    OUTCOME_MESSAGE = 5, OUTCOME_ELSEWHERE = 6
};

/* the drive's mass less 4, if not negative, becomes rubbish (and
 * cargo space); the ship keeps class 1 and no range. */
uint16_t fe2_hyperspace_destroy_drive(fe2_hyperspace_state *s, fe2_object *object)
{
    uint16_t salvage = (uint16_t)(s->drive_classes[object->as.craft.drive].mass - DESTROYED_DRIVE_KEEP);
    if (!(salvage & 0x8000u)) {
        s->rubbish = (uint16_t)(s->rubbish + salvage);
        s->cargo_used = (uint16_t)(s->cargo_used + salvage);
        s->cargo_capacity = (uint16_t)(s->cargo_capacity + salvage);
    }
    object->as.craft.drive = DESTROYED_DRIVE_CLASS;
    s->jump_range = 0;
    return DRIVE_DESTROYED_TEXT;
}

/* random motion through the interpolation service at `scale` and
 * `exponent`, then clear the object's motion fields. */
void fe2_hyperspace_start_motion(fe2_hyperspace_state *s, const fe2_hyperspace_services *game,
                          fe2_object *object, uint16_t scale, uint16_t exponent)
{
    fe2_call_args args;
    uint32_t high, low = game->random(game->opaque, &high);
    unsigned i;
    fe2_args_clear(&args);
    fe2_args_word(&args, WORD_PARAMETER, (uint16_t)low);
    fe2_args_dword(&args, DWORD_PARAMETER, high);
    fe2_args_word(&args, SCALE_PARAMETER, scale);
    fe2_args_word(&args, EXPONENT_PARAMETER, exponent);
    game->interpolate(game->opaque, object, &args);
    object->render_state = 0;
    for (i = 0; i < 3; ++i) {
        object->vector_delta[i] = 0;
        object->velocity[i] = 0;
        object->hyperspace[i] = 0;
    }
    s->throttle = 0;
}

static void publish_arrival(fe2_hyperspace_state *s)
{
    s->published_jump_time = s->jump_time;
    s->simulation_time_step = CLOCK_5709;
    s->game_time_exponent = FIELD_570D;
}

static void message(const fe2_hyperspace_services *game, uint16_t text)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_word(&args, MESSAGE_PARAMETER, 0xffff);
    game->show_message(game->opaque, text, &args);
}

/* After the message, arrive in the destination anyway: prepare it and
 * start at a scale set by the distance (by its low word only, though the
 * test is on the whole dword). */
static void reprepare(fe2_hyperspace_state *s, const fe2_hyperspace_services *game, uint16_t text)
{
    uint16_t scale = ARRIVAL_SCALE, exponent = ARRIVAL_EXPONENT;
    message(game, text);
    fe2_hyperspace_prepare(s, game);
    game->post_event(game->opaque, EVENT_ARRIVED);
    if (s->jump_distance != 0) {
        scale = (uint16_t)((uint16_t)s->jump_distance * DISTANCE_SCALE);
        exponent = DISTANCE_EXPONENT;
    }
    fe2_hyperspace_start_motion(s, game, s->ship, scale, exponent);
    publish_arrival(s);
}

/* The table selects the outcome by a sample's low nibble. Selectors 0 and
 * 1 are not in the shipped table. */
static void misjump(fe2_hyperspace_state *s, const fe2_hyperspace_services *game, uint16_t text)
{
    uint8_t outcome = s->outcomes[game->random(game->opaque, 0) & 0x0fu];
    game->post_event(game->opaque, EVENT_MISJUMP);
    switch (outcome) {
    case OUTCOME_REPREPARE:
        reprepare(s, game, text);
        return;
    case OUTCOME_DESTROY_REPREPARE:
        reprepare(s, game, fe2_hyperspace_destroy_drive(s, s->ship));
        return;
    case OUTCOME_DESTROY:
        text = fe2_hyperspace_destroy_drive(s, s->ship);
        break;
    case OUTCOME_ELSEWHERE:
        game->misjump(game->opaque, s->ship, s->player_handle);
        return;
    default:
        break;
    }
    message(game, text);
    publish_arrival(s);
}

static void arrive(fe2_hyperspace_state *s, const fe2_hyperspace_services *game)
{
    game->post_event(game->opaque, EVENT_ARRIVING);
    fe2_hyperspace_prepare(s, game);
    game->post_event(game->opaque, EVENT_ARRIVED);
    s->map_saved_scale = MAP_SAVED_SCALE;
    s->map_default_selection = MAP_SELECTION;
    publish_arrival(s);
    s->local_record = s->criminal_record[s->legal_authority];
}

/* Leave the ship's position and links behind and enter hyperspace. */
static void enter_hyperspace(fe2_hyperspace_state *s, const fe2_hyperspace_services *game)
{
    fe2_object *ship = s->ship;
    fe2_call_args args;
    ship->vector_delta[1] = 0;            /* the dword at +0x8c */
    ship->vector_delta[2] = 0;
    ship->as.craft.camera_flags = 0;
    s->flight_state = IN_HYPERSPACE;
    game->post_event(game->opaque, EVENT_JUMP);
    s->pool_flag_count = 0;
    fe2_args_clear(&args);
    fe2_args_objects(&args, OBJECTS_PARAMETER, s->objects);
    game->clear_objects(game->opaque, &args);
    game->set_object_flags(game->opaque, ship, HYPERSPACE_FLAGS);
    ship->parent_handle = 0;
    ship->station_slot = 0;
    s->navigation_target = 0;
    s->fined_handle = 0;
    ship->flags &= 0x00ffu;
    fe2_hyperspace_start_motion(s, game, ship,
                         (uint16_t)(game->random_below(game->opaque, ENTRY_SPREAD) | ENTRY_SCALE),
                         ENTRY_EXPONENT);
    game->play_effect(game->opaque, JUMP_EFFECT);
}

int fe2_hyperspace_jump(fe2_hyperspace_state *s, const fe2_hyperspace_services *game,
                        fe2_drive_query *query)
{
    uint32_t rotated, sample;
    int status;
    if (s->flight_state != JUMP_FROM_SPACE && s->flight_state != JUMP_FROM_48) return FE2_HYPERSPACE_OK;
    if (!s->jump_enabled || s->jump_clock < JUMP_CLOCK_READY) return FE2_HYPERSPACE_OK;
    s->criminal_record[s->legal_authority] = s->local_record;
    s->jump_distance_word = (uint16_t)s->jump_distance;
    s->jump_value_copy = s->jump_value;
    query->mode = FE2_DRIVE_CONSUME;
    query->request = s->jump_distance;
    status = fe2_hyperspace_query(s, game, s->ship, query);
    if (status != FE2_HYPERSPACE_OK || (query->mode & 0x8000u)) return status;
    s->jump_time = query->request;
    s->jump_range = query->range_left;
    /* Arrival: the clock plus the jump time with its words swapped, the
     * high dword the sign of its original high word. */
    rotated = query->request << 16 | query->request >> 16;
    s->arrival = fe2_hyperspace_clock(s) +
                 ((uint64_t)(uint32_t)(int32_t)(int16_t)(uint16_t)rotated << 32 | rotated);
    enter_hyperspace(s, game);
    sample = game->random(game->opaque, 0);
    if ((uint16_t)sample > MISJUMP_SAMPLE) misjump(s, game, MISJUMP_TEXT);
    else if (s->misjump_guard == 0) misjump(s, game, FORCED_MISJUMP_TEXT);
    else arrive(s, game);
    return FE2_HYPERSPACE_OK;
}
