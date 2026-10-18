/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Station services: docking choices, fines, crime reports, hails and the
 * launch request. */
#include "station_internal.h"


static void cannot_afford(station_context *c)
{
    station_show_heading(c, 0x9939);
    station_reset_selection_keys(c);
}

/* ask market to take the ship to the chosen station (parameter 9);
 * on success it becomes the reference. */
static void dock_at(station_context *c)
{
    fe2_station_state *s = c->s;
    fe2_station_market_reply reply;
    fe2_ref target;

    station_set(c, P_SUBJECT, s->ship);
    reply = c->g->market_market(c->g->opaque, 6, (uint8_t)s->ship, c->a);
    if (reply.result == 0) {
        target = station_get(c, P_RECORD);
        s->station = target;
        s->reference_handle = station_lookup_object(c, target)->handle;
        s->countdown = 0;
    }
    station_reset_selection_keys(c);
}

/* (rows 0x9800, 0x9801): dock at the row's station; in modes 0x48/0x4c
 * market first releases the ship from the current one. */
static void choose_station(station_context *c)
{
    fe2_station_state *s = c->s;
    station_set(c, P_RECORD, station_get(c, P_NAME));
    if ((s->flight_state == FE2_STATION_MODE_4C || s->flight_state == FE2_STATION_MODE_48) &&
        s->release_suppressed == 0) {
        fe2_call_arg kept[3];
        unsigned n;
        for (n = 0; n < 3; ++n) kept[n] = c->a->arg[P_SUBJECT + n];
        station_set(c, P_RECORD, s->station);
        c->g->market_market(c->g->opaque, 2, s->player_handle, c->a);
        for (n = 0; n < 3; ++n) c->a->arg[P_SUBJECT + n] = kept[n];
    }
    dock_at(c);
}

/* pay the fines. */
static void pay_fines(station_context *c)
{
    fe2_station_state *s = c->s;
    station_set(c, P3, s->deferred);
    if (s->deferred > s->balance) {
        cannot_afford(c);
        return;
    }
    s->balance -= s->deferred;
    s->deferred = 0;
    station_show_heading(c, 0x993a);
    station_reset_selection_keys(c);
}

/* a reply about the subject: a sound, it becomes the fined handle
 * (for 0x2f) and its seed parameter 3. */
static void reply_about(station_context *c, uint16_t text)
{
    fe2_object *subject = station_lookup_object(c, station_get(c, P_SUBJECT));
    c->g->play_effect(c->g->opaque, 0x26);
    c->s->fined_handle = subject->handle;
    station_set(c, P3, station_ror(subject->target_seed, 16));
    station_set_word(c, P_STYLE, 0);
    station_show_message(c, text);
}

/* who the subject is (by its map marker and behaviour). */
static void identify(station_context *c)
{
    fe2_object *o;
    uint16_t text;
    station_set(c, P_SUBJECT, station_get(c, P_NAME));
    o = station_lookup_object(c, station_get(c, P_SUBJECT));
    switch (o->map_marker) {
    case 0x0a: text = 0x99ba; break;
    case 0x09: case 0x0c: case 0x01:
        if (o->behaviour == 0x84) text = o->model > 0x30 ? 0x99b7 : 0x99b8;
        else text = o->model > 0x30 ? 0x99b5 : 0x99b6;
        break;
    default: text = 0x99b9; break;
    }
    reply_about(c, text);
}

/* what the subject is doing (its target's name). */
static void ask_intent(station_context *c)
{
    fe2_object *o;
    uint16_t text;
    station_set(c, P_SUBJECT, station_get(c, P_NAME));
    o = station_lookup_object(c, station_get(c, P_SUBJECT));
    if (o->map_marker == 0x08 || o->map_marker == 0xfb) {
        reply_about(c, 0x99b9);
        return;
    }
    if (o->map_marker == 0x0a) {
        reply_about(c, 0x99ba);
        return;
    }
    if (o->behaviour_target != 0) {
        fe2_ref target;
        station_set(c, P_LIST, c->s->objects);
        target = c->g->lookup(c->g->opaque, o->behaviour_target, c->a);
        station_set(c, P_TEXT, station_name_ref(target));
        station_set(c, P_NAME, station_name_ref(target));
        text = 0x99bb;
    } else {
        text = station_lookup_object(c, station_get(c, P_SUBJECT))->behaviour == 0x84 ? 0x99bd : 0x99bc;
    }
    reply_about(c, text);
}

/* hail; a station passes it to offences. */
static void hail(station_context *c)
{
    station_set(c, P_SUBJECT, station_get(c, P_NAME));
    if (!(station_lookup_object(c, station_get(c, P_SUBJECT))->flags & 0x10)) {
        reply_about(c, 0x99be);
        return;
    }
    c->g->offences_police(c->g->opaque, 0xa, c->a);
}

/* a paid call to the station's market (10 credits). */
static void call_market(station_context *c)
{
    fe2_station_state *s = c->s;
    fe2_station_market_reply reply;
    station_set(c, P3, 0xa);
    if (s->balance < 0xa) {
        cannot_afford(c);
        return;
    }
    s->balance -= 0xa;
    station_start_selection(c);
    reply = c->g->market_market(c->g->opaque, 0, station_lookup_object(c, station_get(c, P_NAME))->handle, c->a);
    station_set(c, P_SUBJECT, (reply.ref & 0xffff0000u) | (uint16_t)((uint16_t)reply.ref - STATION_NAME_OFFSET));
    station_append(c, 0x8e1f);
    s->selection_timer = 0x16c164;
    c->g->play_effect(c->g->opaque, 0x26);
}

/* a launch message about the station (no commander notice). */
static void launch_message(station_context *c, uint16_t text)
{
    fe2_ref station = c->s->station;
    station_set(c, P_SUBJECT, station);
    station_set(c, P3, station_ror(station_lookup_object(c, station)->target_seed, 16));
    station_set_word(c, P_STYLE, 0x18);
    station_post_message(c, text);
}

/* request launch: fines first (the session restarts);
 * refused without a drive, with passengers missing or by market; else the
 * launch (a docking-bay station starts mode 0x34). */
void station_request_launch(station_context *c)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    fe2_station_market_reply reply;

    if (s->deferred != 0) {
        station_process_deferred(c);
        return;
    }
    station_set(c, P_SUBJECT, s->ship);
    if (station_lookup_object(c, s->ship)->as.flight.drive == 0) {
        launch_message(c, 0x98a9);
        return;
    }
    station_set_word(c, P1, s->passenger_count);
    if (s->passenger_count != s->passenger_limit) {
        launch_message(c, 0x9921);
        return;
    }
    station_set(c, P_RECORD, s->station);
    reply = g->market_market(g->opaque, 5, s->player_handle, c->a);
    if (reply.result == 0) {
        launch_message(c, 0x98a0);
        return;
    }
    if (!(station_lookup_object(c, station_get(c, P_RECORD))->flags & 0x40)) {
        s->flight_state = FE2_STATION_MODE_DOCKING;
        s->shared_countdown = 0x17530;
    }
    s->mining_mode_active = 1;
    s->release_suppressed = 0;
    g->post_event(g->opaque, 7);
    g->set_slot_flags(g->opaque, s->station, 0x4f);
    g->flight_activate_view(g->opaque);
    launch_message(c, 0x98a4);
}

void station_service_handler(station_context *c, unsigned index)
{
    fe2_station_state *s = c->s;
    switch (index) {
    case 0x00: case 0x01: choose_station(c); break;
    case 0x02:                                                   /* a craft's menu */
        station_start_selection(c);
        station_set(c, P_SUBJECT, station_get(c, P_NAME));
        if (!(station_lookup_object(c, station_get(c, P_SUBJECT))->flags & 0x10)) {
            station_append(c, 0x9804);
            station_append(c, 0x9805);
            station_append(c, 0x9807);
            station_append(c, 0x9808);
            station_append(c, 0x9806);
        } else {
            station_append(c, 0x9806);
            station_append(c, 0x9830);
        }
        break;
    case 0x03:                                                   /* the ship's menu */
        station_set(c, P_RECORD, s->ship);
        station_start_selection(c);
        station_append(c, 0x9807);
        station_append(c, 0x9808);
        break;
    case 0x04: identify(c); break;
    case 0x05: ask_intent(c); break;
    case 0x06: hail(c); break;
    case 0x07: case 0x08: c->g->play_effect(c->g->opaque, 0x26); break;
    case 0x09: pay_fines(c); break;
    case 0x18: station_request_launch(c); break;
    case 0x2f:                                                   /* target the fined craft */
        s->target = s->fined_handle;
        c->g->play_effect(c->g->opaque, 0x15);
        break;
    case 0x30: call_market(c); break;
    default: break;                                              /* nothing */
    }
}
