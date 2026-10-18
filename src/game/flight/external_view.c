/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* External view controls, cockpit keys and pointer picking
 *. */
#include "flight_internal.h"

enum {
    KEY_LEFT_A = 0x53, KEY_LEFT_B = 0x33, KEY_LEFT_C = 0xd3, KEY_LEFT_D = 0xb3,
    KEY_RIGHT_A = 0x55, KEY_RIGHT_B = 0x56, KEY_RIGHT_C = 0xd5, KEY_RIGHT_D = 0xd6,
    KEY_UP_A = 0x50, KEY_UP_B = 0x31, KEY_UP_C = 0xd0, KEY_UP_D = 0xb1,
    KEY_DOWN_A = 0x58, KEY_DOWN_B = 0x52, KEY_DOWN_C = 0xd8, KEY_DOWN_D = 0xd2,
    KEY_ZOOM_IN = 0x15, KEY_ZOOM_OUT = 0x14, KEY_EXTERNAL = 0x40
};

/* All four keys of a direction must be down (zero) to move. */
static int held(const fe2_flight_state *s, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return (s->keys[a] & s->keys[b] & s->keys[c] & s->keys[d]) == 0;
}

/*
 * 0x26a3: in the external view, the direction keys turn the view by the
 * real step (at most 0xffff) times the time scale (from 16) / 16, at most
 * 0xffff again; the zoom keys move the distance by that step << 8. Zooming
 * in past zero restarts at 0x80000001 and the zoom-out bound then applies:
 * the distance stays at most -(model extent << (scale shift + 7)).
 */
static void external_keys(flight_context *c)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_model *m;
    uint32_t step = s->real_step > 0xffff ? 0xffff : s->real_step, bound, d;
    uint16_t turn;
    unsigned n;
    if (s->time_scale >= 0x10) {
        step = (step * s->time_scale) >> 4;
        if (step > 0xffff) step = 0xffff;
    }
    turn = (uint16_t)(step >> 4);
    step <<= 8;
    if (held(s, KEY_LEFT_A, KEY_LEFT_B, KEY_LEFT_C, KEY_LEFT_D)) s->external_angle[0] = (uint16_t)(s->external_angle[0] + turn);
    if (held(s, KEY_RIGHT_A, KEY_RIGHT_B, KEY_RIGHT_C, KEY_RIGHT_D)) s->external_angle[0] = (uint16_t)(s->external_angle[0] - turn);
    if (held(s, KEY_UP_A, KEY_UP_B, KEY_UP_C, KEY_UP_D)) s->external_angle[1] = (uint16_t)(s->external_angle[1] + turn);
    if (held(s, KEY_DOWN_A, KEY_DOWN_B, KEY_DOWN_C, KEY_DOWN_D)) s->external_angle[1] = (uint16_t)(s->external_angle[1] - turn);
    if (s->keys[KEY_ZOOM_IN] == 0) {
        s->external_distance -= step;
        if ((int32_t)s->external_distance >= 0) s->external_distance = 0x80000001u;
        else goto bound;
    }
    if (s->keys[KEY_ZOOM_OUT] != 0) return;
    s->external_distance += step;
bound:
    m = c->g->model(c->g->opaque, s->ship->model);
    n = (uint16_t)(m->scale_shift + 7);
    d = n > 31 ? 0 : (uint32_t)m->extent << n;
    bound = (uint32_t)-d;
    if ((int32_t)s->external_distance < 0 && s->external_distance < bound) return;
    s->external_distance = bound;
}

/* Key 0xf7: leave the docking modes (0x60 checks the docking fee and tells
 * offences), step out of mode 0x48, or toggle thrust: switching thrust off
 * engages the autopilot toward a target when the ship has the autopilot
 * (bit 21); otherwise manual flight at the shown speed. */
static int autopilot_key(flight_context *c)
{
    fe2_flight_state *s = c->s;
    fe2_object *ship;
    fe2_call_args args;
    uint8_t mode = s->mode;
    if ((int8_t)mode >= 0x50) {
        if ((int8_t)mode < 0x5c) return 0;
        if (mode == FE2_FLIGHT_DOCKED) {
            if (s->passenger_count != s->passenger_limit) {
                fe2_args_clear(&args);
                fe2_args_word(&args, P1, s->passenger_count);
                fe2_args_object(&args, P_SUBJECT, s->reference);
                fe2_args_word(&args, P_MODE, 0xffff);
                c->g->station_message(c->g->opaque, TEXT_TOO_FAR, &args);
                return 0;
            }
            if (s->mining_mode_active == 0) {
                fe2_args_clear(&args);
                fe2_args_word(&args, P1, s->passenger_count);
                fe2_args_object(&args, P_OBJECT, s->reference);
                c->g->offences_event(c->g->opaque, 1, &args);
                if (s->local_fine > 0x1388) c->g->offences_event(c->g->opaque, 2, &args);
            }
        }
        s->mode = FE2_FLIGHT_DISTANCE;
        s->motion_flags &= (uint8_t)~0x10;
        flight_widgets_if_enabled(c);
        s->ship->behaviour = 0;
        flight_refresh_mode_display(c, 0);
        return 1;
    }
    if (mode == FE2_FLIGHT_DISTANCE) {
        s->mode = FE2_FLIGHT_RELATIVE_SPECIAL;
        s->ship->behaviour = 0;
        flight_refresh_mode_display(c, 0);
        return 1;
    }
    if (mode != FE2_FLIGHT_AUTOPILOT) {
        s->control_flags ^= FE2_CONTROL_THRUST;
        if (s->control_flags & FE2_CONTROL_THRUST) {
            flight_refresh_mode_display(c, 0);
            return 1;
        }
        ship = s->ship;
        ship->behaviour_target = s->target;
        if (s->target != 0 && bit(ship->as.flight.equipment, 21)) {
            if (s->mode == FE2_FLIGHT_RELATIVE_SPECIAL) flight_release_reference(c, ship);
            s->mode = FE2_FLIGHT_AUTOPILOT;
            ship->behaviour = 0x20;
            flight_refresh_mode_display(c, 0);
            return 1;
        }
    }
    s->throttle = (int32_t)s->speed;
    s->mode = FE2_FLIGHT_MANUAL;
    flight_refresh_mode_display(c, 0);
    return 1;
}

static void select_near_pointer(flight_context *c);

/* the cockpit keys (game flag 1 holds them). A handled key plays
 * effect 0x14. Keys other than the cockpit's go to the input queue while
 * the cockpit is not shown. */
static void cockpit_keys(flight_context *c)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_object *ship;
    uint8_t key;
    if (s->game_flags & 1) return;
    if (s->mode != FE2_FLIGHT_LANDED) flight_read_inputs(c);
    if (!g->poll_key(g->opaque, &key)) return;
    ship = s->ship;
    if (key == 0xf5) {
        s->message_timer = 0;
        if (bit(ship->as.flight.equipment, 18) && (s->control_flags & FE2_CONTROL_WIDGETS)) flight_disable_widgets(c);
        else flight_refresh_widgets(c);
        return;
    }
    if (key >= 0x10 && key <= 0x17) {
        key = (uint8_t)(key - 0xf);
        s->pending_action = key == s->pending_action ? 0 : key;
        flight_refresh_widgets(c);
    } else if (key == fe2_flight_command_keys[FE2_FLIGHT_COMMAND_ACTIVATE_SELECTED]) {
        flight_pending_action(c);
    } else if (key == fe2_flight_command_keys[FE2_FLIGHT_COMMAND_ACTIVATE_ECM]) {
        g->activate_effect(g->opaque, ship);
    } else if (key == fe2_flight_command_keys[FE2_FLIGHT_COMMAND_ESCAPE_CAPSULE]) {
        flight_replace_ship(c, ship, s->mode == FE2_FLIGHT_LANDED ? c->subject : ship);
    } else if (key == fe2_flight_command_keys[FE2_FLIGHT_COMMAND_ENERGY_BOMB]) {
        g->consume_effect(g->opaque, ship);
        flight_widgets_if_enabled(c);
    } else if (key == fe2_flight_command_keys[FE2_FLIGHT_COMMAND_TOGGLE_TARGET_STATS]) {
        s->control_flags ^= FE2_CONTROL_TARGET_STATS;
    } else if (key == fe2_flight_command_keys[FE2_FLIGHT_COMMAND_TOGGLE_CLOUD_ANALYSIS]) {
        s->control_flags ^= FE2_CONTROL_CLOUD;
    } else if (key == fe2_flight_command_keys[FE2_FLIGHT_COMMAND_SHIP_COMMAND]) {
        flight_ship_command(c, ship);
    } else if (!cockpit_shown(s)) {
        g->forward_key(g->opaque, key);
        return;
    } else if (key == 0xfa) {
        s->control_flags ^= FE2_CONTROL_ANNOTATE;
        flight_refresh_mode_display(c, 0);
    } else if (key == 0xf8) {
        g->hyperspace_jump(g->opaque);
        return;
    } else if (key == 0xf9) {
        if (ship->behaviour != BEHAVIOUR_PARKED) {
            s->control_flags ^= FE2_CONTROL_MODE_FOUR;
            s->motion_flags &= (uint8_t)~0x02;
            flight_refresh_mode_display(c, 0);
            flight_effect(c, 0xc);
        }
    } else if (key == 0xff) {
        select_near_pointer(c);
        if (s->mode == FE2_FLIGHT_AUTOPILOT) s->mode = FE2_FLIGHT_MANUAL;
        flight_refresh_mode_display(c, 0);
        return;
    } else if (key == 0xf7) {
        if (!autopilot_key(c)) return;
    } else {
        g->forward_key(g->opaque, key);
        return;
    }
    flight_effect(c, 0x14);
}

/* (and 0x269a with game flag 1 tested first, which the keys repeat). */
void flight_external_controls(flight_context *c)
{
    fe2_flight_state *s = c->s;
    if (cockpit_shown(s) && s->view_index == 4 && s->keys[KEY_EXTERNAL] != 0) external_keys(c);
    cockpit_keys(c);
}

/* pick the first projected object near the pointer; none clears
 * the target. */
static void select_near_pointer(flight_context *c)
{
    fe2_flight_pass pass;
    pass.state = c->s;
    pass.game = c->g;
    c->s->pointer_found = 0;
    c->s->current_objects = c->s->objects;
    c->g->visit_projected(c->g->opaque, c->s->objects, fe2_flight_pick_object, &pass);
    if (c->s->pointer_found == 0) c->s->target = 0;
}

/* the first object within 6 (|dx| + |dy| in 16 bits, from the
 * point (x - 3, y + 4)) other than the target becomes it (effect 0x15). */
void fe2_flight_pick_object(void *pass, fe2_object *object, uint16_t x, uint16_t y)
{
    fe2_flight_pass *p = pass;
    fe2_flight_state *s = p->state;
    uint16_t dx = (uint16_t)(x - 3 - s->click[0]), dy = (uint16_t)(y + 4 - s->click[1]);
    if (s->pointer_found != 0) return;
    if ((int16_t)dx < 0) dx = (uint16_t)-dx;
    if ((int16_t)dy < 0) dy = (uint16_t)-dy;
    if ((uint16_t)(dx + dy) > 6) return;
    if (object->handle == s->target) return;
    s->pointer_found = 0xff;
    s->target = object->handle;
    p->game->play_effect(p->game->opaque, 0x15);
}
