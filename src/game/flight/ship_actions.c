/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Manual flight's return, the pending equipment action and the ship
 * replacement. */
#include "flight_internal.h"

/* Systems with a one-time offences event (mission flags 13, 12 and 14 mark
 * them done). */
static const struct { uint32_t system; unsigned flag; } announced[] = {
    {0xa92dc602u, 13}, {0xa925c605u, 12}, {0xa925c682u, 14}};

/*
 * 0x302a: manual flight (commander told), the ship's time reset to now, and a
 * hyperspace cloud (model 0xba) left behind it at its origin system; the
 * ship gets the 0x7f00 basis and forward speed 0x71b7. Some systems tell
 * offences (event 9). Then the flight music.
 */
void flight_return_to_manual(flight_context *c)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_object *ship, *cloud;
    fe2_call_args args;
    unsigned i;
    uint64_t t;
    s->mode = FE2_FLIGHT_MANUAL;
    g->commander_reset(g->opaque);
    if (cockpit_shown(s)) {
        g->set_controls(g->opaque, 0x303, 0x17);
        flight_refresh_mode_display(c, 0);
    }
    ship = s->ship;
    ship->orbit[0] = s->time_of_day;
    ship->orbit[1] = s->day;
    ship->station_slot = 0;
    fe2_args_clear(&args);
    fe2_args_object(&args, P_SUBJECT, ship);
    fe2_args_word(&args, P0, 0xa);
    fe2_args_word(&args, P1, 0xba);
    cloud = g->allocate(g->opaque, &args, 0);
    if (cloud) {
        flight_reset_near_unit_basis(ship->orientation);
        ship->velocity[0] = 0;
        ship->velocity[1] = 0;
        ship->velocity[2] = 0x71b7;
        ship->hyperspace[0] = ship->hyperspace[1] = ship->hyperspace[2] = 0;
        cloud->model_parameter = 8;
        cloud->size_mantissa = 0xffff;
        cloud->behaviour = 0x6c;
        cloud->role = ROLE_EFFECT;
        cloud->cargo_type = 1;
        cloud->hyperspace[0] = s->previous_systems[2];
        cloud->map_marker = 0xfe;
        t = ((uint64_t)cloud->orbit[1] << 32 | cloud->orbit[0]) + 0xff9e8e;
        cloud->orbit[0] = (uint32_t)t;
        cloud->orbit[1] = (uint32_t)(t >> 32);
    }
    for (i = 0; i < 3; ++i) {
        if (bit(s->mission_flags, announced[i].flag)) continue;
        if (s->current_system == announced[i].system) {
            g->offences_event(g->opaque, 9, 0);
            break;
        }
    }
    if (s->preferences[FE2_FLIGHT_PREF_MUSIC] & s->preferences[FE2_PREF_FLIGHT_MUSIC]) g->select_music(g->opaque, 0xfffe);
}

/* (key 0x6d): deploy the pending slot at the target; without a
 * target, message 0x98aa (the slot stays pending). */
void flight_pending_action(flight_context *c)
{
    fe2_flight_state *s = c->s;
    fe2_call_args args;
    uint8_t slot = s->pending_action;
    if (slot == 0) return;
    if (s->target == 0) {
        flight_message(c, TEXT_NO_TARGET, 0);
        return;
    }
    s->pending_action = 0;
    fe2_args_clear(&args);
    fe2_args_byte(&args, P_MODE, s->target);
    c->g->deploy_slot(c->g->opaque, s->ship, slot, s->target, &args);
    flight_widgets_if_enabled(c);
}

/*
 * 0x31a6 (key 0x78): a ship with equipment bit 27 is replaced by a new
 * model 0x12 craft (kind 0x4f) that becomes the player's, in mode 0x48 with
 * the front view, its equipment reset; the old one becomes an effect
 * object (behaviour 0x50, marker 1). The new ship is moved by its second
 * basis column, mining is told (request 4), and the widgets, effect 0x20 and
 * mode display follow. Returns 1 when replaced.
 */
int flight_replace_ship(flight_context *c, fe2_object *old, fe2_object *template_record)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_object *ship;
    fe2_call_args args;
    unsigned i;
    if (!bit(old->as.flight.equipment, 27)) return 0;
    fe2_args_clear(&args);
    fe2_args_word(&args, P1, 0x12);
    fe2_args_word(&args, P0, 0x4f);
    fe2_args_object(&args, P_SUBJECT, template_record);
    ship = g->allocate(g->opaque, &args, 0);
    if (ship == 0) return 0;
    s->ship = ship;
    old->map_marker = 1;
    old->controls[0] = old->controls[1] = old->controls[2] = 0;
    ship->map_marker = 0xff;
    s->player_handle = ship->handle;
    old->role = ROLE_EFFECT;
    old->behaviour = 0x50;
    s->mode = FE2_FLIGHT_DISTANCE;
    s->view_index = 0;
    flight_restore_capacity(s);
    ship->as.flight.equipment = 0x40200000;
    ship->as.flight.equipment_damaged = 0;
    for (i = 0; i < 12; ++i) ship->as.flight.installed[i] = 0;
    ship->as.flight.shield_charge = 0;
    ship->as.flight.shield_capacity = 0;
    s->passenger_limit = s->passenger_count = 0;
    s->contract_count = 0;
    s->unpaid_wages = 0;
    s->next_page = 0;
    ship->fuel = 0x20000000;
    fe2_args_clear(&args);
    fe2_args_dword(&args, P0, (uint32_t)(int32_t)ship->orientation[1]);
    fe2_args_dword(&args, P1, (uint32_t)(int32_t)ship->orientation[4]);
    fe2_args_dword(&args, P2, (uint32_t)(int32_t)ship->orientation[7]);
    g->move_object(g->opaque, ship, &args);
    g->mining_command(g->opaque, 4, 0);
    flight_refresh_widgets(c);
    flight_effect(c, 0x20);
    flight_refresh_mode_display(c, 0);
    s->launch_phase = 0;
    s->police_flags &= (uint8_t)~1;
    return 1;
}
