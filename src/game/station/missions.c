/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Military missions and the cargo screen :
 * the timed requests (market's list), their offers, accepting them as
 * mission records, and selling cargo from the hold. */
#include "station_internal.h"

static uint32_t *saved(station_context *c) { return c->s->saved; }

/* the requests. */
void station_open_missions(station_context *c)
{
    fe2_station_state *s = c->s;
    unsigned i;

    station_set(c, P_SUBJECT, s->station);
    station_show_heading(c, s->requests[0].head != 0 ? 0x98f3 : 0x98f4);
    c->g->show_panel(c->g->opaque);
    c->g->set_widgets(c->g->opaque, fe2_station_widgets_station);
    station_reset_rows(c);
    for (i = 0; i < FE2_STATION_REQUESTS; ++i) {      /* a full list ends at 0x45ff (domain) */
        const fe2_station_request *r = &s->requests[i];
        station_set(c, P1, r->value[0]);
        station_set(c, P2, r->value[1]);
        station_set(c, P3, r->value[2]);
        station_set(c, P_SUBJECT, r->value[3]);
        station_set(c, P4, r->seed);
        station_set(c, P0, r->head);
        if ((uint16_t)r->head == 0) break;
        station_append(c, (uint16_t)r->head);
    }
    station_append(c, 0x983e);
    station_show_missions(c);
}


void station_show_missions(station_context *c)
{
    station_set_word(c, P1, FE2_STATION_ACTION_MISSIONS);
    station_show_result(c, 0x98c3, FE2_STATION_ACTION_MISSIONS);
}

/* the accepted request (the saved values) leaves the list. */
void station_remove_request(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t *v = saved(c);
    unsigned i;

    station_set(c, P5, v[0]);
    station_set(c, P6, v[1]);
    station_set(c, P_STYLE, v[2]);
    station_set(c, P_NAME, v[3]);
    station_set(c, P_TEXT, v[4]);
    station_set(c, P_LIST, v[6]);
    for (i = 0; i < FE2_STATION_REQUESTS; ++i) {
        fe2_station_request *r = &s->requests[i];
        if ((uint16_t)r->head == 0) return;
        if ((uint16_t)r->head != station_word(c, P5) || r->value[0] != station_get(c, P6) ||
            r->value[1] != station_get(c, P_STYLE) || r->value[2] != station_get(c, P_NAME) ||
            r->value[3] != station_get(c, P_LIST) || r->seed != station_get(c, P_TEXT))
            continue;
        for (; i + 1 < FE2_STATION_REQUESTS; ++i) {
            s->requests[i] = s->requests[i + 1];
            if ((uint16_t)s->requests[i].head == 0) return;
        }
        return;                                       /* past the list (domain) */
    }
}

/* the offer's end row and the responses. */
static void offer_end(station_context *c)
{
    station_append(c, 0x985f);
    station_show_responses(c);
}

/* after accepting a reconnaissance or assassination the offer's
 * values move round and the request is removed. */
void station_end_mission_offer(station_context *c)
{
    uint32_t *v = saved(c);
    station_set(c, P0, v[0]);
    station_set(c, P1, v[1]);
    station_set(c, P_NAME, (v[2] & 0xffff0000u) | (uint16_t)(v[2] + 0x99a5));
    station_set(c, P3, v[3]);
    station_set(c, P2, v[4]);
    station_set(c, P5, v[5]);
    station_set(c, P4, v[6]);
    v[2] = station_get(c, P2);
    v[4] = station_get(c, P4);
    v[6] = station_get(c, P_NAME);
    station_remove_request(c);
    station_reset_rows(c);
    offer_end(c);
}

/* choose a target ship (models 0x7e, 0x80, 0x82); after the first,
 * each takes it only if the next two bits of parameter 4 (rotated out)
 * are set. */
void fe2_station_choose_target(fe2_station_state *state, fe2_call_args *args, fe2_ref object,
                               fe2_object *record)
{
    station_context c = {state, 0, args, 0};
    uint32_t bits;
    (void)object;
    if (record->model != 0x80 && record->model != 0x7e && record->model != 0x82) return;
    if (state->chosen != 0) {
        bits = station_get(&c, P4);
        station_set(&c, P4, station_ror(bits, 1));
        if (!(bits & 1)) return;
        bits = station_get(&c, P4);
        station_set(&c, P4, station_ror(bits, 1));
        if (!(bits & 1)) return;
    }
    state->chosen = record->handle;
}

void fe2_station_target_visitor(void *context, fe2_ref object)
{
    station_context *c = context;
    fe2_station_choose_target(c->s, c->a, object, station_lookup_object(c, object));
}

/* a request's offer: its system generated, a target
 * ship chosen; the briefing rows. */
static void offer(station_context *c)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    uint32_t *v = saved(c);
    const fe2_station_row *row = station_row_at(c, station_word(c, P_RECORD));
    uint16_t text;

    if (row == 0) return;                                 /* not a row (domain) */
    station_set(c, P1, row->value[0]);
    v[1] = row->value[0];
    station_set(c, P2, row->value[1]);
    v[2] = row->value[1];
    station_set(c, P3, row->value[2]);
    v[3] = row->value[2];
    station_set(c, P4, row->value[3]);
    v[4] = row->value[3];
    v[5] = station_get(c, P5);
    station_set(c, P_NAME, row->subject);
    v[6] = row->subject;
    v[0] = (uint32_t)row->text_high << 16 | row->text;
    station_set(c, P0, v[0]);
    station_show_heading(c, (uint16_t)v[0]);
    g->show_panel(g->opaque);
    g->set_widgets(g->opaque, fe2_station_widgets_board);
    station_reset_rows(c);
    station_set(c, P_LIST, s->parked_ref);
    g->clear_objects(g->opaque, c->a);
    station_set(c, P1, v[1]);
    if (g->galactic_map_system(g->opaque, 3, c->a) != 0) {
        offer_end(c);
        return;
    }
    station_generate_system(c, FE2_STATION_WORK_OFFER);
    s->chosen = 0;
    station_set(c, P0, v[0]);
    station_set(c, P1, v[1]);
    station_set(c, P2, v[2]);
    station_set(c, P3, v[3]);
    station_set(c, P4, v[4]);
    station_set(c, P5, v[5]);
    station_set(c, P_SUBJECT, v[6]);
    g->visit_objects(g->opaque, s->parked_ref, fe2_station_target_visitor, c, c->a);
    if (s->chosen == 0) {
        offer_end(c);
        return;
    }
    station_append(c, 0x9857);
    station_set(c, P1, v[1]);
    station_set(c, P2, v[2]);
    station_set(c, P3, v[3]);
    station_set(c, P4, v[4]);
    station_set(c, P5, v[5]);
    station_set(c, P_SUBJECT, v[6]);
    station_set(c, P0, v[0]);
    if ((uint16_t)v[0] == 0x9854) {
        station_append(c, 0x9865);
    } else {
        station_append(c, 0x9866);
        station_append(c, 0x9867);
    }
    station_append(c, 0x9864);
    station_set_word(c, P_STYLE, 6);
    text = 0x9858;
    do {
        station_append(c, text++);
        station_set_word(c, P_STYLE, (uint16_t)(station_word(c, P_STYLE) - 1));
    } while ((int16_t)station_word(c, P_STYLE) >= 0);
    offer_end(c);
}

/* The offer's values in parameters 1..5, 10 and 0 (0x3b5c, 0x3c51..). */
static void offer_values(station_context *c)
{
    uint32_t *v = saved(c);
    station_set(c, P1, v[1]);
    station_set(c, P2, v[2]);
    station_set(c, P3, v[3]);
    station_set(c, P4, v[4]);
    station_set(c, P5, v[5]);
    station_set(c, P_NAME, v[6]);
    station_set(c, P0, v[0]);
}

/* accept a request (0x9854: reconnaissance needs a free installed
 * slot for the camera; 0x9855: photographs need the radar mapper): the
 * mission record. */
static void accept_request(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t *v = saved(c);
    fe2_station_mission *m;
    fe2_object *ship;
    fe2_ref ref;
    const fe2_object *target;
    uint16_t index, code;
    unsigned i;

    if (s->mission_count >= FE2_STATION_MISSIONS) {
        station_show_heading(c, 0x99b4);
        return;
    }
    station_set(c, P_SUBJECT, s->ship);
    offer_values(c);
    ship = station_lookup_object(c, s->ship);
    if ((uint16_t)v[0] == 0x9855) {
        int16_t slot = (int16_t)station_lookup_model(c, ship->model)->word[FE2_MODEL_MISSILE_PYLONS];
        if (s->cargo_used == s->capacity) {
            station_show_heading(c, 0x98e1);
            return;
        }
        ship = station_lookup_object(c, station_get(c, P_SUBJECT));
        for (--slot; slot >= 0 && ship->as.flight.installed[4 + slot] != 0; --slot) {}
        if (slot < 0) {
            station_show_heading(c, 0x98e1);
            return;
        }
        --s->capacity;
        ship->as.flight.installed[4 + slot] = 0x85;
    } else {
        ship = station_lookup_object(c, station_get(c, P_SUBJECT));
        ship->as.flight.equipment |= 4;
        ship->as.flight.equipment_damaged &= ~(uint32_t)4;
    }
    index = s->mission_count++;
    m = &s->missions[index];
    offer_values(c);
    station_set_word(c, P6, (uint16_t)(((uint16_t)((uint16_t)v[6] - 0x994f) << 4) + 3));
    code = (uint16_t)((uint16_t)((uint16_t)(station_word(c, P0) - 0x9854) << 1) + s->allegiance - 1 + station_word(c, P6));
    station_set_word(c, P0, code);
    m->kind = (uint8_t)code;
    m->system = station_get(c, P1);
    m->origin = s->current_system;
    m->due = station_get(c, P2);
    m->seed = station_get(c, P4);
    m->seen = 0;
    m->model = (uint16_t)(m->model & 0xff00);
    m->reward = station_word(c, P3);
    station_set(c, P_LIST, s->parked_ref);
    ref = c->g->lookup(c->g->opaque, s->chosen, c->a);
    target = station_lookup_object(c, ref);
    for (i = 0; i < FE2_STATION_NAME; ++i) {
        m->name[i] = target->name[i];
        if (target->name[i] == 0) break;
    }
    station_show_heading(c, 0x99b3);
    station_remove_request(c);
    station_reset_rows(c);
    offer_end(c);
}

/* accept: a request goes to `accept_request`; an advert
 * becomes a contract for status (its kind from the detail). */
static void accept(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t *v = saved(c);
    uint16_t detail;

    offer_values(c);
    if ((uint16_t)v[0] >= 0x9854) {
        accept_request(c);
        return;
    }
    if ((int16_t)s->contract_count >= 0x3b) {
        station_show_heading(c, 0x9912);
        return;
    }
    station_set_word(c, P_TEXT, 0x9913);
    station_reply(c);
    station_set(c, P0, v[0]);
    station_set(c, P1, v[1]);
    station_set(c, P2, v[2]);
    station_set(c, P3, v[3]);
    station_set(c, P_NAME, v[4]);
    station_set(c, P5, v[5]);
    station_set(c, P4, v[6]);
    detail = (uint16_t)v[6];
    station_set_word(c, P6, detail);
    station_set_word(c, P4, 0x2000);
    if (detail >= 0x995f) {
        station_set_word(c, P4, (uint16_t)(station_word(c, P4) + 0x100));
        if (detail >= 0x9963) station_set_word(c, P4, (uint16_t)(station_word(c, P4) + 0x100));
    }
    if (s->board_kind != 2) station_set_word(c, P4, (uint16_t)(station_word(c, P4) | 0x4000));
    station_set(c, P_NAME, 0);
    c->g->status_contract(c->g->opaque, c->a);
    station_remove_request(c);
    station_reset_rows(c);
    offer_end(c);
}

/* an advert's briefing rows. */
static void briefing(station_context *c)
{
    uint32_t *v = saved(c);
    const fe2_station_row *row = station_row_at(c, station_word(c, P_RECORD));
    uint16_t text;
    int16_t left;

    if (row == 0) return;                                 /* not a row (domain) */
    station_set(c, P_TEXT, (c->s->refs.rows & 0xffff0000u) | station_word(c, P_RECORD));
    station_set(c, P1, row->value[0]);
    v[1] = row->value[0];
    station_set(c, P2, row->value[1]);
    v[2] = row->value[1];
    station_set(c, P3, row->value[2]);
    v[3] = row->value[2];
    station_set(c, P4, row->value[3]);
    v[4] = row->value[3];
    station_set(c, P_NAME, row->subject);
    v[6] = row->subject;
    v[5] = station_get(c, P5);
    v[0] = (uint32_t)row->text_high << 16 | row->text;
    station_set(c, P0, v[0]);
    station_show_heading(c, (uint16_t)v[0]);
    c->g->show_panel(c->g->opaque);
    c->g->set_widgets(c->g->opaque, fe2_station_widgets_board);
    station_reset_rows(c);
    for (text = 0x9857, left = 7; left >= 0; --left) station_append(c, text++);
    offer_end(c);
}

/* an assassination advert: the target searched for. */
static void assassination(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t *v = saved(c);
    const fe2_station_row *row = station_row_at(c, station_word(c, P_RECORD));
    uint16_t text;

    if (row == 0) return;                                 /* not a row (domain) */
    v[0] = (uint32_t)row->text_high << 16 | row->text;
    station_set(c, P0, v[0]);
    station_set(c, P1, row->value[0]);
    v[1] = row->value[0];
    station_set(c, P4, row->value[1]);
    v[4] = row->value[1];
    station_set(c, P3, row->value[2]);
    v[3] = row->value[2];
    station_set(c, P_NAME, row->value[3]);
    v[6] = row->value[3];
    v[5] = station_get(c, P5);
    station_set(c, P2, (row->subject & 0xffff0000u) | (uint16_t)(row->subject - 0x99a5));
    v[2] = station_get(c, P2);
    c->g->show_panel(c->g->opaque);
    c->g->set_widgets(c->g->opaque, fe2_station_widgets_board);
    station_reset_rows(c);
    if (!station_find_mission(c)) {
        offer_end(c);
        return;
    }
    station_set(c, P0, v[0]);
    station_set(c, P1, v[1]);
    station_set(c, P2, (v[2] & 0xffff0000u) | (uint16_t)(v[2] + 0x999d));
    station_set(c, P3, v[3]);
    station_set(c, P4, v[4]);
    station_set(c, P5, v[5]);
    station_set(c, P_NAME, v[6]);
    station_show_heading(c, 0x99ad);
    station_set(c, P2, v[2]);
    station_set(c, P3, v[3]);
    station_set(c, P5, v[5]);
    station_set(c, P_NAME, v[6]);
    station_set(c, P4, v[6]);
    s->offer_model = c->g->market_market(c->g->opaque, 8, 0xc, c->a).result;
    station_set(c, P0, v[0]);
    station_set(c, P1, v[1]);
    station_set(c, P2, (v[2] & 0xffff0000u) | (uint16_t)(v[2] + 0x999d));
    station_set(c, P3, v[3]);
    station_set(c, P4, v[4]);
    station_set(c, P5, v[5]);
    station_set(c, P_NAME, v[6]);
    station_set(c, P_SUBJECT, v[6]);
    station_append(c, 0x9860);
    station_append(c, 0x9861);
    station_append(c, 0x9862);
    station_append(c, 0x9863);
    station_set_word(c, P_STYLE, 6);
    text = 0x9858;
    do {
        station_append(c, text++);
        station_set_word(c, P_STYLE, (uint16_t)(station_word(c, P_STYLE) - 1));
    } while ((int16_t)station_word(c, P_STYLE) >= 0);
    offer_end(c);
}

/* sell cargo: the commodity's row. */
static void cargo_screen(station_context *c)
{
    fe2_station_state *s = c->s;
    uint16_t at;
    station_reset_rows(c);
    s->heading = 0x98c1;
    s->identity = station_get(c, P_NAME);
    s->portrait_timer = 0x8000;
    s->portrait = 0;
    at = (uint16_t)((uint16_t)(station_word(c, P2) - 0x8e00) * 2);
    station_set_word(c, P4, at);
    station_set(c, P1, s->cargo[at / 2]);
    station_append(c, 0x983f);
    station_append(c, 0x983e);
    station_draw_status_list(c, 0x9917);
}

/* sell one unit at parameter 3. */
static void sell_cargo(station_context *c)
{
    fe2_station_state *s = c->s;
    fe2_station_row *row;
    uint16_t at = station_word(c, P4);
    station_set_word(c, P1, station_word(c, P2));
    if (s->cargo[at / 2] == 0) {
        station_show_heading(c, 0x98ce);
        return;
    }
    s->refit = 0xff;
    s->balance += station_get(c, P3);
    --s->cargo[at / 2];
    --s->cargo_used;
    row = station_row_at(c, station_word(c, P_RECORD));
    if (row) --row->value[0];
    if (s->key_count != 0) station_draw_default(c);
    else station_draw_status_list(c, 0x98d0);
}

/* a police contact: a random check against the
 * rating, then the questions. */
static void police_contact(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t *v = saved(c);
    const fe2_station_row *row = station_row_at(c, station_word(c, P_RECORD));
    uint16_t text;

    if (row == 0) return;                                 /* not a row (domain) */
    station_set(c, P1, row->value[0]);
    v[0] = row->value[0];
    station_set(c, P2, row->value[1]);
    v[1] = row->value[1];
    station_set(c, P3, row->value[2]);
    v[2] = row->value[2];
    station_set(c, P4, row->value[3]);
    v[3] = row->value[3];
    station_set(c, P_NAME, row->subject);
    s->identity = row->subject;
    v[5] = row->subject;
    station_set_word(c, P5, row->text_high);
    v[4] = (v[4] & 0xffff0000u) | row->text_high;
    v[6] = (v[6] & 0xffff0000u) | station_word(c, P_RECORD);
    s->portrait_timer = 0x8000;
    s->portrait = 0;
    c->g->show_panel(c->g->opaque);
    c->g->set_widgets(c->g->opaque, fe2_station_widgets_board);
    s->heading = 0x98c1;
    station_set(c, P1, v[0]);
    station_set(c, P2, v[1]);
    station_set(c, P3, v[2]);
    station_set(c, P4, v[3]);
    station_set(c, P5, v[4]);
    station_set(c, P_NAME, v[5]);
    station_set(c, P_TEXT, v[6]);
    if ((int16_t)(0u - c->g->random_below(c->g->opaque, 9)) > s->rating) {
        station_show_heading(c, 0x9907);
        station_reset_rows(c);
        station_append(c, 0x983e);
        station_show_responses(c);
        return;
    }
    station_show_heading(c, 0x9906);
    station_reset_rows(c);
    station_set_word(c, P_STYLE, 0xc);
    text = 0x9832;
    do {
        if (!(text == 0x9833 && (v[3] & 0x2000))) station_append(c, text);
        ++text;
        station_set_word(c, P_STYLE, (uint16_t)(station_word(c, P_STYLE) - 1));
    } while ((int16_t)station_word(c, P_STYLE) >= 0);
    station_show_responses(c);
}

void station_mission_handler(station_context *c, unsigned index)
{
    fe2_station_state *s = c->s;
    uint32_t *v = saved(c);
    switch (index) {
    case 0x19: case 0x1a: police_contact(c); break;
    case 0x26: case 0x27:                                 /* the military boards */
        if (index == 0x26) {
            s->portrait = 0x36;
            s->heading = 0x98f1;
            s->board_kind = 2;
            s->identity = station_ror(station_lookup_object(c, s->station)->target_seed, 0x18);
        } else {
            s->portrait = 0x26;
            s->heading = 0x98f2;
            s->board_kind = 1;
            s->identity = station_ror(station_lookup_object(c, s->station)->target_seed, 0x11) & 0xfffffffeu;
        }
        s->portrait_timer = 0x8000;
        station_open_missions(c);
        break;
    case 0x29: cargo_screen(c); break;
    case 0x3f: sell_cargo(c); break;
    case 0x53: briefing(c); break;
    case 0x54: case 0x55: offer(c); break;
    case 0x56: assassination(c); break;
    case 0x57: accept(c); break;
    case 0x58: case 0x5a: case 0x5b: station_set_word(c, P_TEXT, 0x98f5); station_reply(c); break;
    case 0x5c: station_set_word(c, P_TEXT, 0x98f6); station_reply(c); break;
    case 0x5d: station_set_word(c, P_TEXT, 0x98f7); station_reply(c); break;
    case 0x59:                                            /* what the request is */
        offer_values(c);
        if ((uint16_t)v[0] != 0x9856) {
            station_show_heading(c, (uint16_t)v[0]);
        } else {
            station_set_word(c, P2, (uint16_t)(station_word(c, P2) + 0x999d));
            station_show_heading(c, 0x99ad);
        }
        break;
    case 0x5e: {                                          /* the reward's terms */
        uint16_t text = 0x98f8;
        offer_values(c);
        if ((uint16_t)v[0] == 0x9856) ++text;
        else if ((uint16_t)v[0] == 0x9855 || (uint16_t)v[0] == 0x9854) text = (uint16_t)(text + 2);
        else if (station_word(c, P_NAME) >= 0x9963) ++text;
        station_set_word(c, P_TEXT, text);
        station_reply(c);
        break;
    }
    case 0x5f:
        c->g->show_panel(c->g->opaque);
        c->g->set_widgets(c->g->opaque, fe2_station_widgets_station);
        station_open_missions(c);
        break;
    case 0x64: {                                          /* the target */
        fe2_ref ref;
        station_set(c, P_LIST, s->parked_ref);
        ref = c->g->lookup(c->g->opaque, s->chosen, c->a);
        station_set(c, P_SUBJECT, ref);
        station_set(c, P_TEXT, station_name_ref(ref));
        station_set(c, P0, v[0]);
        station_set(c, P1, v[1]);
        station_set(c, P2, v[2]);
        station_set(c, P3, v[3]);
        station_set(c, P4, v[4]);
        station_set(c, P5, v[5]);
        station_set(c, P_NAME, v[6]);
        station_show_heading(c, 0x9944);
        break;
    }
    default: {
        static const uint16_t replies[3] = {0x9941, 0x9942, 0x9943};
        if (index != 0x67) {
            station_set(c, P0, v[0]);
            station_set(c, P1, v[1]);
            station_set(c, P2, v[2]);
            station_set(c, P3, v[3]);
            station_set(c, P4, v[4]);
            station_set(c, P5, v[5]);
            station_set(c, P_NAME, v[6]);
        }
        station_set_word(c, P_TEXT, replies[index - 0x65]);
        station_reply(c);
        break;
    }
    }
}
