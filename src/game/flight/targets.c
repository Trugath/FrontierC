/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Target selection, the mode 0x58 sequence, the annotation callback and
 * the ship events. */
#include "flight_internal.h"

enum { MODEL_REPLACEMENT = 0xbc, SEQUENCE_TIME = 0x3cae6 };

/* each coefficient * 127/128, rounded toward minus infinity. */
void flight_scale_basis(int16_t basis[9])
{
    unsigned i;
    for (i = 0; i < 9; ++i) basis[i] = (int16_t)sar32((int32_t)basis[i] * 0xfe00, 16);
}

/* the replacement ship (an effect object, marker 0xff, model 0xbc)
 * becomes the player's. */
void fe2_flight_adopt_object(void *pass, fe2_object *object)
{
    fe2_flight_pass *p = pass;
    if (object->role != ROLE_EFFECT || object->map_marker != 0xff || object->model != MODEL_REPLACEMENT) return;
    p->state->ship = object;
}

/*
 * 0x39b4: selecting the player's own handle (outside mode 0x58) enters the
 * cockpit if needed and, unless the ship  is replaced, starts the
 * mode 0x58 sequence: the replacement ship adopted, its basis scaled by
 * 127/128, the external view at 0xff900000... (0xf9000000), commander told, the
 * screen cleared and presented, the countdown 0x3cae6, the cockpit's high
 * byte cleared, the pointer regions cleared and game flags 0x21 set.
 */
static void select_player(flight_context *c, uint8_t handle)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_flight_pass pass;
    if (handle != s->player_handle || s->mode == FE2_FLIGHT_SEQUENCE) return;
    if (!cockpit_shown(s)) flight_enter_cockpit(c);
    flight_effect(c, 0x1f);
    if (flight_replace_ship(c, c->subject, c->subject)) return;
    s->mode = FE2_FLIGHT_SEQUENCE;
    pass.state = s;
    pass.game = g;
    g->visit_objects(g->opaque, s->objects, fe2_flight_adopt_object, &pass);
    flight_scale_basis(s->ship->orientation);
    s->view_index = 4;
    s->external_distance = 0xf9000000u;
    g->commander_reset(g->opaque);
    g->clear_drawing(g->opaque);
    g->present(g->opaque);
    g->clear_drawing(g->opaque);
    s->shared_countdown = SEQUENCE_TIME;
    s->view_mode &= 0x00ff;
    g->clear_regions(g->opaque);
    s->game_flags |= 0x21;
    flight_effect(c, 0x1f);
}

/* clear the target (the player's handle selects the player). In
 * the autopilot, manual flight returns. */
void flight_clear_target(flight_context *c, uint8_t handle)
{
    fe2_flight_state *s = c->s;
    s->target = 0;
    if (handle == s->player_handle) {
        select_player(c, handle);
        return;
    }
    if (s->mode == FE2_FLIGHT_AUTOPILOT) {
        s->mode = FE2_FLIGHT_MANUAL;
        flight_refresh_mode_display(c, 0);
    }
    flight_message(c, TEXT_TARGET_CLEARED, 0);
}

/* 0x9a. A nonzero form only sets the delay for the player's handle;
 * otherwise re-selecting the target clears it, and the player's handle
 * selects the player. */
void fe2_flight_select_target(fe2_flight_state *state, const fe2_flight_services *game, uint8_t handle,
                              uint16_t form, fe2_object *record)
{
    flight_context c;
    c.s = state;
    c.g = game;
    c.subject = record;
    if (form != 0) {
        if (handle == state->player_handle) state->delay_ticks = 0x1e;
        return;
    }
    if (handle == state->target) flight_clear_target(&c, handle);
    else select_player(&c, handle);
}

/* (mode 0x58): the countdown runs out into the session restart
 * (vector 0x40, AX = 1); otherwise the external view turns by step / 4 and
 * the scene is tried at +0, +0x2000 and +0x8000, then left at +0x9000. */
void flight_view_sequence(flight_context *c)
{
    fe2_flight_state *s = c->s;
    if (s->shared_countdown < s->frame_step) {
        c->g->restart_session(c->g->opaque, 1);
        return;
    }
    s->shared_countdown -= s->frame_step;
    s->external_angle[0] = (uint16_t)(s->external_angle[0] + (s->frame_step >> 2));
    if (flight_build_scene(c)) return;
    s->external_angle[0] = (uint16_t)(s->external_angle[0] + 0x2000);
    if (flight_build_scene(c)) return;
    s->external_angle[0] = (uint16_t)(s->external_angle[0] + 0x6000);
    if (flight_build_scene(c)) return;
    s->external_angle[0] = (uint16_t)(s->external_angle[0] + 0x1000);
}

/* label objects in state 0: bodies unless within 10 (|dx| + |dy|,
 * 16-bit) of their parent's screen position; others of level 0x11 or
 * below except clouds (model 0xba). */
void fe2_flight_annotate_object(void *pass, fe2_object *object, uint16_t x, uint16_t y)
{
    fe2_flight_pass *p = pass;
    flight_context c;
    fe2_object *parent;
    uint16_t dx, dy;
    c.s = p->state;
    c.g = p->game;
    c.subject = 0;
    if (object->update_period != 0) return;
    if (object->role == 1) {
        if (object->parent_handle != 0) {
            parent = flight_lookup(&c, object->parent_handle);
            dx = (uint16_t)(parent->screen_x - x);
            dy = (uint16_t)(parent->screen_y - y);
            if ((int16_t)dx < 0) dx = (uint16_t)-dx;
            if ((int16_t)dy < 0) dy = (uint16_t)-dy;
            if ((uint16_t)(dx + dy) < 0xa) return;
        }
    } else if (object->level > 0x11 || object->model == 0xba) {
        return;
    }
    p->game->draw_name(p->game->opaque, object, COLOUR_WHITE, (uint16_t)(x + 1), y);
}

/* The drive range from drives (query 1). */
static void recalculate(flight_context *c)
{
    fe2_flight_drive_reply reply;
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_object(&args, P_SUBJECT, c->s->ship);
    fe2_args_dword(&args, P1, 1);
    c->g->hyperspace_query(c->g->opaque, 1, &args, &reply);
    c->s->jump_range = reply.range_left;
    flight_widgets_if_enabled(c);
}

/*
 * 0x76: event 3 relinks the player and reference from their handles;
 * 0x11 (for the player), 0x12, 0x16 and 0x17 recalculate the drive range;
 * 0x15 leaves the timed mode; 5 replaces a model 0x12 ship by a model 0x20
 * one (released, recreated from it with a random seed) and sets the
 * shared countdown, then (when not negative) queues key 0xf4.
 */
void fe2_flight_event(fe2_flight_state *state, const fe2_flight_services *game, uint16_t event,
                      uint8_t selected)
{
    flight_context c;
    fe2_object *ship;
    fe2_call_args args;
    uint32_t edx;
    c.s = state;
    c.g = game;
    c.subject = 0;
    switch (event) {
    case 3:
        state->current_objects = state->objects;
        state->ship = flight_lookup(&c, state->player_handle);
        state->reference = flight_lookup(&c, state->reference_handle);
        return;
    case 0x11:
        if (selected != state->player_handle) return;
        /* fall through */
    case 0x12: case 0x16: case 0x17:
        recalculate(&c);
        return;
    case 0x15:
        if (state->mode == FE2_FLIGHT_HYPERSPACE) flight_return_to_manual(&c);
        return;
    case 5:
        ship = state->ship;
        if (ship->model == 0x12) {
            uint16_t seed;
            game->release(game->opaque, ship);
            seed = (uint16_t)game->random(game->opaque, &edx);
            fe2_args_clear(&args);
            fe2_args_object(&args, P_SUBJECT, ship);
            ship = flight_create_ship(&c, &args, 0x20, seed);
            if (ship == 0) return;
            ship->as.flight.drive = 2;
            state->capacity -= 0xa;
            ship->fuel = 0x20000000;
            ship->as.flight.equipment = 0;
            ship->as.flight.equipment_damaged = 0;
            state->current_objects = state->objects;
            game->refresh_contacts(game->opaque, ship);
        }
        state->shared_countdown = state->event_time - state->event_base;
        if ((int32_t)state->shared_countdown < 0) return;
        game->forward_key(game->opaque, 0xf4);
        state->input_suppressed = 1;
        return;
    default:
        return;
    }
}
