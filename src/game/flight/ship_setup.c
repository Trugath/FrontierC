/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The starting ship, the player ship's creation and the capacity reset
 *. */
#include <string.h>

#include "flight_internal.h"
#include "game/object_data.h"

enum { NAME_TYPE_SHIP_REGISTRATION = 15, ALLOCATE_PLAYER = 0x4f, ERROR_NO_SLOT = 0xf009,
       SEED_FACTOR = 0x4a0d };

void flight_restore_capacity(fe2_flight_state *s)
{
    s->capacity = s->ship->capacity;
    s->cargo_used = 0;
    s->cabins_used = 0;
    s->cabins = 0;
    memset(s->cargo, 0, sizeof s->cargo);
}

void fe2_flight_reset_capacity(fe2_flight_state *state)
{
    flight_restore_capacity(state);
}

/* The allocator copies the template (parameter 8) into a free slot; the
 * slot word it leaves becomes the player's handle. The seed makes the
 * target seed (its words swapped) and the registration. */
fe2_object *flight_create_ship(flight_context *c, fe2_call_args *args, uint16_t model, uint16_t seed)
{
    fe2_flight_state *s = c->s;
    fe2_object *ship;
    char name[FE2_FLIGHT_NAME_BYTES];
    uint16_t slot = 0;
    uint32_t product;
    unsigned i;
    fe2_args_word(args, P1, model);
    fe2_args_word(args, P_MODE, seed);
    fe2_args_word(args, P0, ALLOCATE_PLAYER);
    ship = c->g->allocate(c->g->opaque, args, &slot);
    if (ship == 0) {
        c->g->fatal_error(c->g->opaque, ERROR_NO_SLOT);
        return 0;
    }
    ship->map_marker = 0xff;
    s->ship = ship;
    memcpy(s->saved_limits, ship->as.flight.control_limits, sizeof s->saved_limits);
    ship->level = 0;
    flight_restore_capacity(s);
    ship->capacity = 0;
    s->target = 0;
    s->fined_handle = 0;
    s->player_handle = (uint8_t)slot;
    ship->role = ROLE_PLAYER;
    s->throttle = 0;
    s->heat = 0;
    s->speed = 0;
    s->combat_rating = 0;
    s->offence_score[0] = s->offence_score[1] = s->offence_score[2] = 0;
    s->countdown = 0;
    s->mining_mode_active = 0;
    s->view_index = 0;
    s->shared_countdown = 0xffffffffu;
    s->equipment_service_day = s->drive_service_day = s->day - 1;
    s->equipment_service_threshold = s->drive_service_threshold = 0x1f4;
    product = (uint32_t)SEED_FACTOR * seed;
    ship->target_seed = product >> 16 | product << 16;
    c->g->names_name(c->g->opaque, NAME_TYPE_SHIP_REGISTRATION, product, name);
    /* The name is copied through its terminator. */
    for (i = 0; i < FE2_OBJECT_NAME_BYTES; ++i) {
        ship->name[i] = name[i];
        if (name[i] == 0) break;
    }
    return ship;
}

static fe2_object *create_player_ship(flight_context *c, uint16_t model, uint16_t seed)
{
    fe2_call_args args;
    fe2_object template_record;
    fe2_args_clear(&args);
    fe2_object_read_template(&template_record, fe2_flight_player_template, 0);
    fe2_args_template(&args, P_SUBJECT, &template_record);
    return flight_create_ship(c, &args, model, seed);
}

/* Every configuration starts docked: 1 and the default at stations 0x5f
 * and 0x6e (their market placement), 2 in flight mode 0x54 at body 0x70. */
int flight_starting_ship(flight_context *c)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_object *ship, *station;
    fe2_flight_market_reply reply;
    fe2_call_args args;
    uint8_t station_handle;
    unsigned i;
    s->previous_systems[0] = s->previous_systems[1] = s->previous_systems[2] = 0;
    s->federal_points = s->imperial_points = 0;
    s->elite_points = 0;
    s->mission_flags = 0;
    s->criminal_record[0] = s->criminal_record[1] = s->criminal_record[2] = 0;
    s->cash = 1000;
    switch (s->starting_config >> 8) {
    case 1:
        ship = create_player_ship(c, 0x20, 0x4a0d);
        if (ship == 0) return 0;
        ship->as.flight.drive = 1;
        ship->as.flight.installed[0] = 0xa1;
        ship->as.flight.installed[4] = 0;
        ship->as.flight.installed[5] = 0;
        ship->as.flight.installed[6] = 0x82;
        ship->as.flight.installed[7] = 0x82;
        ship->as.flight.shield_capacity = 0;
        ship->as.flight.equipment = 0x40000000;
        s->capacity -= 0xc;
        s->jump_range = 0;
        s->cargo_used = 0;
        g->forward_key(g->opaque, 0xf5);
        ship->fuel = 0x40000000;
        ship->parent_handle = 0x60;
        station_handle = 0x5f;
        break;
    case 2:
        ship = create_player_ship(c, 0x34, 0x941a);
        if (ship == 0) return 0;
        ship->as.flight.drive = 3;
        ship->as.flight.installed[0] = 0x88;
        for (i = 4; i < 8; ++i) ship->as.flight.installed[i] = 0x82;
        ship->as.flight.shield_capacity = 0;
        ship->as.flight.equipment = 0x40000;
        s->capacity -= 0x1f;
        s->jump_range = 0x320;
        ship->fuel = 0x40000000;
        s->cargo[19] = 4;
        s->cargo_used = 4;
        s->criminal_record[2] = 0xaae60;
        s->criminal_record[1] = 0xaae60;
        s->offence_score[1] = 0x1f4;
        s->offence_score[2] = 0x1f4;
        ship->parent_handle = 0x70;
        s->reference_handle = 0x70;
        s->current_objects = s->objects;
        s->reference = flight_lookup(c, 0x70);
        s->reference->as.body.docked[0] = s->player_handle;
        ship->station_slot = 0xff;
        s->contact_class = 0;            /* a word store: 0x207d and 0x207e */
        s->release_suppressed = 0;
        s->mode = FE2_FLIGHT_LANDED;
        g->set_slot_flags(g->opaque, ship, 0x4b);
        s->measured = 0xffffffffu;
        return 1;
    default:
        ship = create_player_ship(c, 0x20, 0xde27);
        if (ship == 0) return 0;
        ship->as.flight.installed[0] = 0x88;
        ship->as.flight.drive = 2;
        ship->as.flight.equipment = 0x40240000;
        ship->as.flight.installed[4] = 0x82;
        ship->as.flight.installed[5] = 0x82;
        s->capacity -= 0x10;
        s->cargo[19] = 1;
        s->cargo_used = 1;
        ship->fuel = 0x20000000;
        s->jump_range = 0x320;
        ship->parent_handle = 0x6f;
        station_handle = 0x6e;
        break;
    }
    s->reference_handle = station_handle;
    s->current_objects = s->objects;
    station = flight_lookup(c, station_handle);
    s->reference = station;
    s->contact_handle = station->handle;
    s->contact_status = 1;
    s->mode = FE2_FLIGHT_DOCKED;
    memcpy(ship->orientation, station->orientation, sizeof ship->orientation);
    memcpy(ship->position_low, station->position_low, sizeof ship->position_low);
    memcpy(ship->position_high, station->position_high, sizeof ship->position_high);
    ship->station_slot = 0xff;
    station->as.body.docked[1] = 0xff;
    fe2_args_clear(&args);
    fe2_args_object(&args, P_SUBJECT, ship);
    fe2_args_object(&args, P_OBJECT, station);
    g->market_overlay_service(g->opaque, 1, ship->handle, &args, &reply);
    s->contact_class = reply.value;
    station->as.body.docked[1] = 0;
    s->release_suppressed = 0;
    /* The placement's offset, 64-bit per axis. */
    for (i = 0; i < 3; ++i) {
        uint64_t p = ((uint64_t)ship->position_high[i] << 32 | ship->position_low[i]) +
                     ((uint64_t)reply.offset_high[i] << 32 | reply.offset_low[i]);
        ship->position_low[i] = (uint32_t)p;
        ship->position_high[i] = (uint32_t)(p >> 32);
    }
    g->refresh_contacts(g->opaque, ship);
    return 1;
}
