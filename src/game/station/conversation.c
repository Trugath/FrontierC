/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Conversation replies : the responses to a bulletin
 * advertiser, accepting a delivery contract, and the shared reply steps.
 * The advertiser's values are kept in `saved`. */
#include "station_internal.h"


void station_show_responses(station_context *c)
{
    station_set_word(c, P1, FE2_STATION_ACTION_CONVERSATION);
    station_show_result(c, 0x98c3, FE2_STATION_ACTION_CONVERSATION);
}

/* the reply text in parameter 11, about the chosen row (its text
 * and index are parameter 2). */
void station_reply(station_context *c)
{
    const fe2_station_row *row = station_row_at(c, station_word(c, P_RECORD));
    if (row) station_set(c, P2, (uint32_t)row->text_high << 16 | row->text);
    station_show_heading(c, 0x9916);
}

/* rows `first`.. + parameter 7 (0x9833 skipped while the
 * advertiser's flag 0x2000 is set), then the responses. */
static void list_responses(station_context *c, uint16_t first)
{
    station_reset_rows(c);
    do {
        if (!(first == 0x9833 && (c->s->saved[3] & 0x2000))) station_append(c, first);
        ++first;
        station_set_word(c, P_STYLE, (uint16_t)(station_word(c, P_STYLE) - 1));
    } while ((int16_t)station_word(c, P_STYLE) >= 0);
    station_show_responses(c);
}

/* the conversation's end row. */
void station_end_reply(station_context *c)
{
    station_set_word(c, P_STYLE, 0);
    list_responses(c, 0x983e);
}

/* the bulletin entry (parameter 5) is taken off the board. */
void station_clear_board_entry(station_context *c)
{
    station_set_market_word(c->s->market, (uint16_t)(station_word(c, P5) << 4) + 0x7e, 0);
}

/* resolve parameter 1 through the mission destination rules;
 * return its mission-state bit in parameter 0. */
static int resolve_mission_destination(station_context *c)
{
    uint32_t system = station_get(c, P1);
    unsigned i;
    for (i = 0; i < fe2_station_mission_destination_count; ++i) {
        const fe2_station_mission_destination *rule = &fe2_station_mission_destinations[i];
        station_set(c, P0, rule->mission_flag_bit);
        if (system == rule->system_key) return 1;
    }
    return 0;
}

static uint32_t *saved(station_context *c) { return c->s->saved; }

/* The advertiser's values in parameters 1..3, 5, 10 and 11's high word. */
static void load_offer(station_context *c)
{
    uint32_t *v = saved(c);
    station_set(c, P1, v[0]);
    station_set(c, P2, v[1]);
    station_set(c, P3, v[2]);
    station_set(c, P5, v[4]);
    station_set(c, P_NAME, v[5]);
}

/* what the advertiser offers (a text by the detail). */
static void offer_detail(station_context *c, uint16_t mask, uint16_t base)
{
    uint32_t *v = saved(c);
    uint16_t text;
    load_offer(c);
    text = (uint16_t)((((v[3] >> 8) & 0xff) >> 1 & mask) + base);
    station_set(c, P4, (v[3] & 0xffff0000u) | text);
    station_set_word(c, P_TEXT, text);
    station_set(c, P_NAME, v[5] - 0x1234);
    station_set_high(c, P_TEXT, (uint16_t)(v[6] >> 16));
    station_reply(c);
}

/* the advertiser questioned or bargained. */
static void bargain(station_context *c, unsigned index)
{
    fe2_station_state *s = c->s;
    uint32_t *v = saved(c);
    fe2_station_sample sample;
    uint8_t detail, was;

    if (index == 0x39) {                                   /* haggle */
        load_offer(c);
        station_set(c, P4, v[3]);
        station_set_high(c, P_TEXT, (uint16_t)(v[6] >> 16));
        station_set_word(c, P_TEXT, 0x990a);
        if (s->rating < 5) {
            station_reply(c);
            return;
        }
        if (v[1] & 0x20000000u) {
            station_set_word(c, P_TEXT, 0x990f);
            station_reply(c);
            return;
        }
        v[1] |= 0x20000000u;
        {
            uint32_t raised = ((uint32_t)0x8ccc * station_word(c, P3)) << 1;
            station_set(c, P3, raised >> 16);
            v[2] = raised >> 16;
        }
        station_set_word(c, P_TEXT, 0x9910);
        station_reply(c);
        return;
    }
    /* threaten or bribe. */
    station_set(c, P1, v[0]);
    station_set(c, P2, v[1]);
    station_set(c, P3, v[2]);
    station_set(c, P5, v[4]);
    station_set(c, P_NAME, v[5]);
    if (index == 0x3a) station_set_high(c, P_TEXT, (uint16_t)(v[6] >> 16));
    else station_set(c, P_TEXT, v[6]);
    detail = (uint8_t)(v[3] >> 8);
    station_set(c, P4, (v[3] & 0xffff0000u) | detail);
    if ((detail & 0x1f) == 0x12) {
        station_set_word(c, P_TEXT, 0x990b);
        station_reply(c);
        return;
    }
    was = (uint8_t)(v[1] >> 24);
    v[1] |= 0x80000000u;
    if (was & 0x80) {
        station_set_word(c, P_TEXT, 0x990f);
        station_reply(c);
        return;
    }
    if (index == 0x3a) {
        if (s->rating >= 2) {
            station_set_word(c, P_TEXT, 0x990c);
            station_reply(c);
            return;
        }
        sample = c->g->bounded_sample(c->g->opaque, (uint16_t)(2 - s->rating), station_get(c, P1),
                                      station_get(c, P_NAME));
    } else {
        sample = c->g->bounded_sample(c->g->opaque, (uint16_t)(0xa - s->rating), station_get(c, P1),
                                      station_get(c, P_NAME));
    }
    station_set(c, P1, sample.seed_a);
    station_set(c, P2, sample.seed_b);
    if (sample.result != 0) {
        v[1] &= 0x7fffffffu;
        station_set_word(c, P_TEXT, 0x990e);
    } else if (index == 0x3a) {
        station_set_word(c, P_TEXT, 0x990c);
    } else {
        v[1] |= 0x40000000u;
        station_set_word(c, P_TEXT, 0x990d);
    }
    station_reply(c);
}

/* accept the delivery: cabins (unless the detail says none), a
 * special system's mission flag, the advance (half, or all with 0x4000 of
 * the detail), status records the contract. */
static void accept(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t *v = saved(c);
    uint16_t paid;

    if ((int16_t)s->contract_count >= 0x3b) {
        station_show_heading(c, 0x9912);
        return;
    }
    station_set(c, P1, v[0]);
    station_set(c, P2, v[1]);
    station_set(c, P3, v[2]);
    station_set(c, P5, v[4]);
    station_set(c, P_NAME, v[5]);
    station_set(c, P_TEXT, v[6]);
    station_set(c, P4, v[3]);
    if (!(v[3] & 0x2000)) {
        uint16_t free = (uint16_t)(s->cabins - s->cabins_used);
        uint16_t need = (uint16_t)(((uint8_t)station_get(c, P4) >> 2) + 1);
        station_set_word(c, P4, need);
        if ((int16_t)(free - need) < 0) {
            station_show_heading(c, 0x9911);
            return;
        }
        s->cabins_used = (uint16_t)(s->cabins_used + need);
    }
    if ((station_get(c, P2) >> 24) & 0x10) {
        if (resolve_mission_destination(c)) s->mission_flags |= (uint32_t)1 << (station_word(c, P0) & 31);
    }
    station_set(c, P1, v[0]);
    station_set(c, P3, v[2]);
    station_set(c, P4, v[3]);
    station_set(c, P5, v[4]);
    station_set(c, P_NAME, v[5]);
    station_set(c, P2, v[1]);
    station_set_word(c, P_TEXT, 0x9913);
    if ((station_get(c, P2) >> 24) & 0x80) {
        if ((station_get(c, P2) >> 24) & 0x40) {
            station_set_word(c, P_TEXT, 0x9915);
            paid = station_word(c, P3);
            station_set(c, P3, 0);
        } else {
            station_set_word(c, P_TEXT, 0x9914);
            station_set_word(c, P3, (uint16_t)(station_word(c, P3) >> 1));
            paid = station_word(c, P3);
        }
        v[0] = station_get(c, P1);
        v[1] = station_get(c, P2);
        v[2] = station_get(c, P3);
        s->balance += (uint32_t)paid * 0xa;
    }
    station_reply(c);
    station_set(c, P1, v[0]);
    station_set(c, P2, v[1]);
    station_set(c, P3, v[2]);
    station_set(c, P4, v[3]);
    station_set(c, P5, v[4]);
    station_set(c, P_NAME, v[5]);
    station_set(c, P_TEXT, v[6]);
    station_clear_board_entry(c);
    station_set(c, P2, s->day + 0x1c);
    c->g->status_contract(c->g->opaque, c->a);
    station_end_reply(c);
}

void station_talk_handler(station_context *c, unsigned index)
{
    uint32_t *v = saved(c);
    switch (index) {
    case 0x32: accept(c); break;
    case 0x33: {                                          /* how many passengers */
        uint8_t count;
        station_set(c, P2, v[1]);
        station_set(c, P3, v[2]);
        station_set(c, P5, v[4]);
        station_set(c, P_NAME, v[5]);
        station_set(c, P_TEXT, v[6]);
        count = (uint8_t)((uint8_t)v[3] >> 2);
        station_set(c, P1, (uint8_t)(count + 1));
        station_set(c, P4, (v[3] & 0xffffff00u) | count);
        station_set_word(c, P_TEXT, 0x9909);
        station_reply(c);
        break;
    }
    case 0x34: offer_detail(c, 0x1f, 0x9967); break;
    case 0x35: offer_detail(c, 0xf, 0x9987); break;
    case 0x37:                                            /* who the advertiser is */
        load_offer(c);
        station_set(c, P4, v[3]);
        station_set(c, P_TEXT, v[6]);
        station_show_heading(c, 0x9906);
        break;
    case 0x38:                                            /* where to */
        load_offer(c);
        station_set(c, P4, v[3]);
        station_set(c, P_TEXT, v[6]);
        if (resolve_mission_destination(c)) {
            station_set_word(c, P_TEXT, 0x9919);
            v[1] |= 0x10000000u;
        } else {
            station_set_word(c, P_TEXT, 0x9918);
        }
        station_reply(c);
        break;
    case 0x39: case 0x3a: case 0x3b: bargain(c, index); break;
    default:                                              /* back to the board */
        c->g->show_panel(c->g->opaque);
        c->g->set_widgets(c->g->opaque, fe2_station_widgets_station);
        station_open_bulletin(c);
        break;
    }
}
