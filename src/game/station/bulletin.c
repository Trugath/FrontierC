/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The bulletin board : advertisements from market's market
 * record, passengers, donations, deliveries, the mission search and the
 * mission offer it leads to. */
#include "station_internal.h"

static uint32_t *saved(station_context *c) { return c->s->saved; }

static void conversation_end(station_context *c)
{
    station_reset_rows(c);
    station_append(c, 0x983e);
    station_show_responses(c);
}

/* An advertiser's screen: the board widgets and heading 0x98c1. */
static void board_screen(station_context *c)
{
    c->g->show_panel(c->g->opaque);
    c->g->set_widgets(c->g->opaque, fe2_station_widgets_board);
    c->s->heading = 0x98c1;
}

/* the advertisements (texts 0x7e.. of the market record, 16 bytes
 * each; zero texts are skipped but counted). */
void station_open_bulletin(station_context *c)
{
    fe2_station_state *s = c->s;
    fe2_station_market_reply reply;
    fe2_object *station;
    unsigned at;

    station_reset_rows(c);
    s->first_row = s->event_word;
    s->heading = 0x98be;
    station_set(c, P_SUBJECT, s->station);
    station = station_lookup_object(c, s->station);
    s->identity = station_ror(station->target_seed, 0x13);
    s->portrait_timer = 0x8000;
    s->portrait = 0x1c;
    reply = c->g->market_market(c->g->opaque, 0, station->handle, c->a);
    s->market = reply.market;
    station_set_word(c, P5, 0);
    station_set_word(c, P6, station_market_word(s->market, 0x19e));
    if (station_word(c, P6) == 0) {
        station_draw_board(c, 0x98c7);
        return;
    }
    at = 0x7e;
    do {
        uint8_t *m = s->market;
        uint16_t text = station_market_word(m, at);
        station_set(c, P2, station_market_word(m, at + 2));
        station_set(c, P3, station_market_word(m, at + 4));
        station_set_word(c, P4, station_market_word(m, at + 6));
        station_set(c, P1, (uint32_t)station_market_word(m, at + 10) << 16 | station_market_word(m, at + 8));
        station_set(c, P_SUBJECT, (uint32_t)station_market_word(m, at + 14) << 16 | station_market_word(m, at + 12));
        station_set_word(c, P0, text);
        at += 0x10;
        if (text != 0) station_append(c, text);
        station_set_word(c, P5, (uint16_t)(station_word(c, P5) + 1));
    } while ((int16_t)station_word(c, P5) < (int16_t)station_word(c, P6));
    station_draw_board(c, 0x98c6);
}

/* a charity: the donation amounts. */
static void charity(station_context *c, unsigned index)
{
    fe2_station_state *s = c->s;
    uint32_t *v = saved(c);
    static const uint32_t amounts[6] = {1, 0xa, 0x64, 0x3e8, 0x2710, 0x186a0};
    unsigned i;

    v[0] = (v[0] & 0xffff0000u) | (uint16_t)(index * 2);
    v[1] = station_get(c, P1);
    v[2] = station_get(c, P2);
    v[3] = station_get(c, P3);
    v[4] = station_get(c, P4);
    v[5] = station_get(c, P5);
    v[6] = station_get(c, P_NAME);
    station_reset_rows(c);
    c->g->show_panel(c->g->opaque);
    c->g->set_widgets(c->g->opaque, fe2_station_widgets_board);
    s->heading = 0x98c1;
    s->identity = station_get(c, P_NAME);
    s->portrait_timer = 0x8000;
    s->portrait = 0;
    station_set(c, P1, v[1]);
    station_set(c, P2, v[2]);
    station_set(c, P4, v[4]);
    station_set(c, P5, v[5]);
    station_set(c, P_NAME, v[6]);
    for (i = 0; i < 6; ++i) {
        station_set(c, P3, amounts[i]);
        station_append(c, 0x986d);
    }
    station_append(c, 0x983e);
    station_show_heading(c, 0x99c8);
    station_show_responses(c);
}

/* donate parameter 3: 1000+ raises the rating (to at most
 * 5), 10000+ sets it to 5. */
static void donate(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t amount = station_get(c, P3);
    if (s->balance < amount) {
        station_show_heading(c, 0x9939);
        return;
    }
    s->balance -= amount;
    if (amount >= 0x3e8) {
        if (amount >= 0x2710 || ++s->rating > 5) s->rating = 5;
    }
    station_set_word(c, P_TEXT, amount >= 0x186a0 ? 0x99ca : 0x99c9);
    station_reply(c);
    station_show_responses(c);
}

/* systems generates the system (parameters 0, 1) into a work record
 * and instantiates it; parameters other than 9 are kept around the first
 * call, the seeds pass from one call to the next. */
void station_generate_system(station_context *c, enum fe2_station_work work)
{
    const fe2_station_services *g = c->g;
    fe2_call_arg kept[FE2_CALL_ARGS];
    uint32_t seed_a, seed_b;
    unsigned n;

    station_set(c, P_SUBJECT, g->work_record(g->opaque, work));
    for (n = 0; n < FE2_CALL_ARGS; ++n) kept[n] = c->a->arg[n];
    seed_a = station_get(c, P0);
    seed_b = station_get(c, P1);
    station_set_word(c, P2, 0);
    g->systems_system(g->opaque, &seed_a, &seed_b, c->a);
    for (n = 0; n < FE2_CALL_ARGS; ++n)
        if (n != P_RECORD) c->a->arg[n] = kept[n];
    station_set_word(c, P2, 1);
    g->systems_system(g->opaque, &seed_a, &seed_b, c->a);
}

void fe2_station_search_visitor(void *context, fe2_ref object)
{
    station_context *c = context;
    fe2_station_count_police(c->s, object, station_lookup_object(c, object));
}

/* search for the mission's target: chart places the system, systems
 * generates it, a visit of the parked list picks a station by the
 * countdown 0x204d. Nonzero with a station chosen. */
int station_find_mission(station_context *c)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    uint32_t *v = saved(c);

    station_set(c, P_LIST, s->parked_ref);
    g->clear_objects(g->opaque, c->a);
    station_set(c, P1, v[1]);
    if (g->galactic_map_system(g->opaque, 3, c->a) != 0) return 0;
    station_generate_system(c, FE2_STATION_WORK_MISSION);
    station_set(c, P2, v[2]);
    station_set(c, P3, v[3]);
    station_set(c, P4, v[4]);
    station_set(c, P5, v[5]);
    station_set(c, P_NAME, v[6]);
    station_set(c, P1, v[6]);
    if ((uint8_t)s->search_counter == 0) return 0;
    s->search_counter = (int8_t)(((uint32_t)(uint16_t)(int16_t)s->search_counter * station_word(c, P1)) >> 16);
    s->chosen = 0;
    g->visit_objects(g->opaque, s->parked_ref, fe2_station_search_visitor, c, c->a);
    return s->chosen != 0;
}

/* each station counts the search down; the one that takes it below
 * zero is chosen (the count restarts at 0x7f). */
void fe2_station_count_police(fe2_station_state *state, fe2_ref object, fe2_object *record)
{
    (void)object;
    if (!(record->flags & 0x10)) return;
    if (--state->search_counter >= 0) return;
    state->chosen = record->handle;
    state->search_counter = 0x7f;
}


static void out_of_range(station_context *c)
{
    station_show_heading(c, 0x99ae);
    station_append(c, 0x983e);
    station_show_responses(c);
}

/* a mission advertiser: within jump range and with
 * a target found, the offer (market picks the ship model). */
static void mission_advert(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t *v = saved(c);
    const fe2_station_row *row = station_row_at(c, station_word(c, P_RECORD));
    uint16_t range;

    if (row == 0) return;                                 /* not a row (domain) */
    v[0] = (uint32_t)row->text_high << 16 | row->text;
    station_set(c, P0, v[0]);
    station_set_word(c, P5, row->text_high);
    v[5] = row->text_high;
    station_set(c, P1, row->value[0]);
    v[1] = row->value[0];
    station_set(c, P2, (row->value[1] & 0xffff0000u) | (uint16_t)(row->value[1] - 0x999d));
    v[2] = station_get(c, P2);
    station_set(c, P3, row->value[2]);
    v[3] = row->value[2];
    station_set(c, P4, (uint16_t)row->value[3] + 0x11d5a0u);
    v[4] = station_get(c, P4);
    station_set(c, P_NAME, row->subject);
    v[6] = row->subject;
    s->identity = row->subject;
    station_set_word(c, P_TEXT, station_word(c, P_RECORD));
    station_reset_rows(c);
    s->portrait_timer = 0x8000;
    s->portrait = 0;
    c->g->show_panel(c->g->opaque);
    c->g->set_widgets(c->g->opaque, fe2_station_widgets_board);
    s->heading = 0x98c1;
    station_set(c, P0, v[0]);
    station_set(c, P1, v[1]);
    station_set(c, P2, v[2]);
    station_set(c, P3, v[3]);
    station_set(c, P4, v[4]);
    station_set(c, P5, v[5]);
    station_set(c, P_NAME, v[6]);
    range = (uint16_t)((uint16_t)(((uint32_t)(uint16_t)v[6] * 0x4000) >> 16) + 0x1000);
    station_set(c, P0, range);
    if (range > s->jump_limit || !station_find_mission(c)) {
        out_of_range(c);
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
    station_append(c, 0x983d);
    station_append(c, 0x983e);
    station_show_responses(c);
}

/* The offer's values back in parameters 0..5 and 10 (0x2cf2, 0x3a76..). */
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

/* The chosen station's name into a mission record. */
static void name_mission(station_context *c, fe2_station_mission *m)
{
    fe2_ref ref;
    const fe2_object *o;
    unsigned i;
    station_set(c, P_LIST, c->s->parked_ref);
    ref = c->g->lookup(c->g->opaque, c->s->chosen, c->a);
    o = station_lookup_object(c, ref);
    for (i = 0; i < FE2_STATION_NAME; ++i) {
        m->name[i] = o->name[i];
        if (o->name[i] == 0) break;
    }
}

/* accept the mission: a record (at most six) with the
 * target station's name; half the reward in advance with flag 0x80. An
 * assassination  stays offered and takes the allegiance. */
static void accept_mission(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t *v = saved(c);
    fe2_station_mission *m;
    uint16_t index = s->mission_count;

    if (index >= FE2_STATION_MISSIONS) {
        station_show_heading(c, 0x99b4);
        return;
    }
    ++s->mission_count;
    m = &s->missions[index];
    station_set(c, P1, v[1]);
    m->system = v[1];
    station_set(c, P2, (v[2] & 0xffff0000u) | (uint16_t)(v[2] << 4));
    m->kind = (uint8_t)station_get(c, P2);
    station_set(c, P3, v[3]);
    station_set(c, P4, v[4]);
    m->due = v[4];
    station_set(c, P5, v[5]);
    station_set(c, P_NAME, v[6]);
    m->seed = v[6];
    m->origin = s->current_system;
    m->seen = 0;
    m->model = s->offer_model;
    station_set_word(c, P_TEXT, 0x99b3);
    if ((station_get(c, P2) >> 24) & 0x80) {
        uint32_t advance;
        station_set(c, P3, station_get(c, P3) >> 1);
        advance = (uint32_t)0xa * station_word(c, P3);
        s->balance += advance;
        station_set(c, P5, advance);
        station_set_word(c, P_TEXT, 0x99b2);
    }
    m->reward = station_word(c, P3);
    name_mission(c, m);
    station_show_heading(c, station_word(c, P_TEXT));
    offer_values(c);
    if ((uint16_t)v[0] == 0x9856) {
        m->kind = (uint8_t)(m->kind + s->allegiance);
        station_end_mission_offer(c);
        return;
    }
    station_clear_board_entry(c);
    station_reset_rows(c);
    station_append(c, 0x983e);
    station_show_responses(c);
}

/* about the target station. */
static void about_target(station_context *c, uint16_t text)
{
    uint32_t *v = saved(c);
    fe2_ref ref;
    station_set(c, P0, v[0]);
    station_set(c, P1, v[1]);
    station_set(c, P2, v[2]);
    station_set(c, P4, v[4]);
    station_set(c, P3, v[4]);
    station_set(c, P5, v[5]);
    station_set(c, P_NAME, v[6]);
    station_set(c, P2, v[6]);
    station_set(c, P_LIST, c->s->parked_ref);
    ref = c->g->lookup(c->g->opaque, c->s->chosen, c->a);
    station_set(c, P_SUBJECT, ref);
    station_set(c, P_TEXT, station_name_ref(ref));
    station_set(c, P1, v[1]);
    station_show_heading(c, text);
}

/* the target's ship: its registration and crew. */
static void about_ship(station_context *c)
{
    uint32_t *v = saved(c);
    fe2_station_name name;
    const fe2_station_model *m;
    c->g->names_name(c->g->opaque, 0xf, v[6], FE2_STATION_WORK_REGISTRATION, &name);
    m = station_lookup_model(c, c->s->offer_model);
    station_set(c, P0, v[0]);
    station_set(c, P1, v[1]);
    station_set(c, P2, (v[2] & 0xffff0000u) | (uint16_t)(v[2] + 0x999d));
    station_set(c, P3, v[3]);
    station_set(c, P4, v[4]);
    station_set(c, P5, v[5]);
    station_set(c, P_NAME, v[6]);
    station_set_word(c, P3, m->word[FE2_MODEL_NAME_TEXT]);
    station_set(c, P_TEXT, name.ref);
    station_show_heading(c, 0x99b0);
}

/* (0x9824, 0x9825; 0x9822, 0x9823 by rating): a passenger advert: the
 * fares offered. */
static void passenger_advert(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t *v = saved(c);
    const fe2_station_row *row = station_row_at(c, station_word(c, P_RECORD));
    uint16_t text;

    if (row == 0) return;                                 /* not a row (domain) */
    v[0] = (uint32_t)row->text_high << 16 | row->text;
    station_set(c, P0, v[0]);
    station_set_word(c, P5, row->text_high);
    v[4] = row->text_high;
    station_set(c, P1, row->value[0]);
    station_set(c, P2, row->value[1]);
    v[1] = row->value[1];
    station_set(c, P3, row->value[2]);
    v[2] = row->value[2];
    station_set(c, P4, row->value[3]);
    v[3] = row->value[3];
    station_set(c, P_NAME, row->subject);
    v[5] = row->subject;
    station_set_word(c, P_TEXT, station_word(c, P_RECORD));
    v[6] = (v[6] & 0xffff0000u) | station_word(c, P_RECORD);
    station_reset_rows(c);
    s->heading = 0x98c1;
    s->identity = station_get(c, P_NAME);
    s->portrait_timer = 0x8000;
    s->portrait = 0;
    c->g->show_panel(c->g->opaque);
    c->g->set_widgets(c->g->opaque, fe2_station_widgets_board);
    station_set_word(c, P_STYLE, 5);
    text = 0x9840;
    do {
        station_append(c, text++);
        station_set_word(c, P_STYLE, (uint16_t)(station_word(c, P_STYLE) - 1));
    } while ((int16_t)station_word(c, P_STYLE) >= 0);
    station_append(c, 0x983e);
    station_set(c, P2, v[1]);
    station_set(c, P3, v[2]);
    station_set(c, P4, v[3]);
    station_set(c, P5, v[4]);
    station_set(c, P_NAME, v[5]);
    station_set(c, P_TEXT, v[6]);
    station_show_heading(c, 0x991a);
    station_show_responses(c);
}

/* The passenger advert's values in parameters 0 and 2..5, 10, 11. */
static void passenger_values(station_context *c)
{
    uint32_t *v = saved(c);
    station_set(c, P0, v[0]);
    station_set(c, P2, v[1]);
    station_set(c, P3, v[2]);
    station_set(c, P4, v[3]);
    station_set(c, P5, v[4]);
    station_set(c, P_TEXT, v[6]);
    station_set(c, P_NAME, v[5]);
}

/* take passengers at a fare and a deadline:
 * refused when the cabins are full or the deadline is too short. */
static void take_passengers(station_context *c, uint16_t limit, uint16_t fare)
{
    fe2_station_state *s = c->s;
    uint32_t *v = saved(c);
    fe2_station_passenger *p;

    station_set_word(c, P2, limit);
    station_set_word(c, P3, fare);
    v[2] = station_word(c, P3);
    station_set(c, P4, v[3]);
    station_set(c, P5, v[4]);
    station_set(c, P_NAME, v[5]);
    station_set(c, P_TEXT, v[6]);
    if ((int16_t)s->passenger_count >= (int16_t)s->passenger_limit) {
        station_show_heading(c, 0x9920);
        return;
    }
    if (station_word(c, P_NAME) >= station_word(c, P2)) {
        station_set(c, P0, v[0]);
        station_set(c, P2, v[1]);
        station_set(c, P3, v[2]);
        station_set(c, P4, v[3]);
        station_set(c, P5, v[4]);
        station_set(c, P_NAME, v[5]);
        station_set_word(c, P_TEXT, 0x991d);
        station_reply(c);
        return;
    }
    p = &s->passengers[s->passenger_count];
    p->seed = station_get(c, P_NAME);
    p->fare = station_word(c, P3);
    ++s->passenger_count;
    station_set_word(c, P_TEXT, 0x991c);
    station_reply(c);
    passenger_values(c);
    station_clear_board_entry(c);
    conversation_end(c);
}

/* the passenger's destination answers. */
static void passenger_answer(station_context *c, int detail)
{
    uint32_t *v = saved(c);
    uint16_t text;
    station_set(c, P0, v[0]);
    station_set(c, P2, v[1]);
    station_set(c, P3, v[2]);
    station_set(c, P4, v[3]);
    station_set(c, P5, v[4]);
    station_set(c, P_TEXT, v[6]);
    station_set(c, P_NAME, v[5]);
    if (!detail) station_set(c, P1, v[5]);
    if (detail) {
        text = (uint16_t)(((uint16_t)v[5] >> 0xd) + 0x9922);
        station_set_word(c, P1, text);
    } else {
        text = (int16_t)v[5] < 0 ? 0x991f : 0x991e;
    }
    station_set_word(c, P_TEXT, text);
    station_reply(c);
}

/* a delivery contract handed in (its
 * contract record goes, the rating rises) or questioned. */
static void hand_in(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t *v = saved(c);
    station_reply(c);
    station_set(c, P1, v[0]);
    station_set(c, P2, v[1]);
    station_set(c, P3, v[2]);
    station_set(c, P4, v[3]);
    station_set(c, P5, v[4]);
    station_set(c, P_NAME, v[5]);
    station_set(c, P_TEXT, v[6]);
    s->balance += (uint32_t)0xa * station_word(c, P3);
    station_clear_board_entry(c);
    station_reset_rows(c);
    station_append(c, 0x983e);
    station_show_responses(c);
}

static void refuse_delivery(station_context *c, uint16_t text)
{
    station_set_word(c, P_TEXT, text);
    --c->s->rating;
    station_reply(c);
    conversation_end(c);
}

static void deliver(station_context *c)
{
    fe2_station_state *s = c->s;
    int16_t left = (int16_t)(s->contract_count - 1);
    unsigned k;

    for (k = 0; left >= 0; ++k, --left) {
        fe2_station_contract *t = &s->contracts[k];
        if (t->seed != station_get(c, P_NAME)) continue;
        if ((t->detail >> 8) & 0x20) break;
        s->cabins_used = (uint16_t)(s->cabins_used - (uint16_t)((int16_t)(int8_t)((uint8_t)t->detail >> 2) + 1));
        for (; left >= 0; --left, ++k) s->contracts[k] = s->contracts[k + 1];
        --s->contract_count;
        s->rating = (int16_t)(s->rating - 8);
        station_set_word(c, P_TEXT, 0x992c);
        hand_in(c);
        return;
    }
    refuse_delivery(c, 0x992f);
}

/* a delivery advert: the systems visited lately. */
static void delivery_advert(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t *v = saved(c);
    const fe2_station_row *row = station_row_at(c, station_word(c, P_RECORD));
    unsigned i;

    if (row == 0) return;                                 /* not a row (domain) */
    station_set(c, P0, (uint32_t)row->text_high << 16 | row->text);
    station_set_word(c, P5, row->text_high);
    v[4] = row->text_high;
    v[0] = row->value[0];
    v[1] = row->value[1];
    v[2] = row->value[2];
    v[3] = row->value[3];
    v[6] = (c->s->refs.rows & 0xffff0000u) | station_word(c, P_RECORD);
    v[5] = row->subject;
    s->identity = station_ror(row->subject, 4);
    s->portrait_timer = 0x8000;
    s->portrait = 0;
    board_screen(c);
    for (i = 0; i < 2; ++i) {
        station_set(c, P1, v[0]);
        station_set(c, P2, v[1]);
        station_set(c, P3, v[2]);
        station_set(c, P4, v[3]);
        station_set(c, P5, v[4]);
        station_set(c, P_NAME, v[5]);
        station_set(c, P_TEXT, v[6]);
        if (i == 0) {
            if (s->rating < 0) {
                station_show_heading(c, 0x9908);
                conversation_end(c);
                return;
            }
            station_show_heading(c, 0x992a);
            station_reset_rows(c);
        }
    }
    station_set(c, P_SUBJECT, station_get(c, P_NAME));
    station_append(c, 0x9846);
    for (i = 0; i < 3; ++i) {
        station_set(c, P2, s->visited_days[i]);
        station_set(c, P1, s->visited[2 - i]);
        if (s->visited[2 - i] == 0) break;
        station_append(c, 0x9847);
    }
    station_append(c, 0x9848);
    station_append(c, 0x9849);
    station_append(c, 0x983e);
    station_show_responses(c);
}

void station_board_handler(station_context *c, unsigned index)
{
    fe2_station_state *s = c->s;
    switch (index) {
    case 0x0b: station_open_bulletin(c); break;
    case 0x1c: case 0x1d: delivery_advert(c); break;
    case 0x1e: case 0x1f: case 0x20: case 0x21: mission_advert(c); break;
    case 0x22:
        if (s->rating >= 0) passenger_advert(c);
        else {
            station_reset_rows(c);
            station_append(c, 0x983e);
            station_show_heading(c, 0x991b);
            station_show_responses(c);
        }
        break;
    case 0x23:
        if (s->rating < -9) {
            station_reset_rows(c);
            station_append(c, 0x983e);
            station_show_heading(c, 0x991b);
            station_show_responses(c);
        } else {
            passenger_advert(c);
        }
        break;
    case 0x24: case 0x25: passenger_advert(c); break;
    case 0x2b: case 0x2c: charity(c, index); break;
    case 0x40: passenger_answer(c, 0); break;
    case 0x41: passenger_answer(c, 1); break;
    case 0x42: take_passengers(c, 0xffff, 0xc8); break;
    case 0x43: take_passengers(c, 0xe000, 0x96); break;
    case 0x44: take_passengers(c, 0x6000, 0x64); break;
    case 0x45: take_passengers(c, 0x4000, 0x32); break;
    case 0x46: deliver(c); break;
    case 0x47:                                            /* is it the right one */
        if (station_get(c, P2) == station_get(c, P_NAME)) {
            station_set_word(c, P_TEXT, 0x992b);
            hand_in(c);
        } else {
            refuse_delivery(c, 0x992d);
        }
        break;
    case 0x48:
        if (station_word(c, P4) == 1) {
            station_set_word(c, P_TEXT, 0x9931);
            hand_in(c);
        } else {
            refuse_delivery(c, 0x992e);
        }
        break;
    case 0x49:
        station_set_word(c, P_TEXT, 0x9930);
        station_reply(c);
        break;
    case 0x60: accept_mission(c); break;
    case 0x61: about_target(c, 0x99af); break;
    case 0x62: about_ship(c); break;
    case 0x63: about_target(c, 0x99b1); break;
    default: donate(c); break;                            /* 0x6D */
    }
}
