/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Trade : the commodity list of the
 * station's market (or the black market) and buying and selling. */
#include "station_internal.h"


void station_show_trade(station_context *c)
{
    station_set_word(c, P1, FE2_STATION_ACTION_TRADE);
    station_show_result(c, 0x98c9, FE2_STATION_ACTION_TRADE);
}

/* the commodities whose legality (trade code bit 7) matches the
 * list's kind (0x4b3b: 0 legal, 0x80 illegal). */
void station_open_trade(station_context *c)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    fe2_station_market_reply reply;
    unsigned k, at;

    g->show_panel(g->opaque);
    g->set_widgets(g->opaque, fe2_station_widgets_list);
    station_show_heading(c, 0x98c8);
    station_reset_rows(c);
    station_set(c, P_SUBJECT, s->station);
    reply = g->market_market(g->opaque, 0, station_lookup_object(c, s->station)->handle, c->a);
    s->market = reply.market;
    station_set_word(c, P5, s->list_kind);
    for (k = 0, at = 0; k < FE2_STATION_COMMODITIES; ++k, at += 4) {
        if ((s->trade_codes[k] ^ (uint8_t)station_get(c, P5)) & 0x80) continue;
        station_set(c, P1, (uint32_t)(int32_t)(int16_t)station_market_word(s->market, at + 2));
        station_set(c, P2, station_market_word(s->market, at));
        station_set(c, P3, s->cargo[k]);
        station_set_word(c, P_SUBJECT, (uint16_t)(0x8e00 + k));
        station_append(c, 0x9831);
    }
    station_show_trade(c);
}

/* a trade list of kind `kind`: its identity (parameter 1),
 * heading (2) and dealer flag (3's low byte). */
void station_open_list(station_context *c, uint16_t kind)
{
    fe2_station_state *s = c->s;
    s->list_kind = kind;
    s->identity = station_get(c, P1);
    s->portrait_timer = 0x8000;
    s->portrait = 0;
    s->heading = station_word(c, P2);
    s->ship_list = (uint8_t)station_get(c, P3);
    station_open_trade(c);
}


static void trade_outcome(station_context *c, uint16_t text)
{
    station_show_heading(c, text);
    station_show_trade(c);
}

/* buy (or with parameter 7's bit 4, sell) one unit of the
 * row's commodity, or (holding key count / 0x40 more) several; a dealer's
 * list reports the offence instead. */
static void trade(station_context *c)
{
    fe2_station_state *s = c->s;
    fe2_station_row *row;
    uint16_t commodity, market_at;
    uint8_t *market = s->market;
    uint32_t price;

    if (s->ship_list != 0) {
        station_report_dealing(c);
        return;
    }
    s->refit = 0xff;
    commodity = station_word(c, P_NAME);
    station_set_word(c, P1, commodity);
    commodity = (uint16_t)((uint16_t)(commodity - 0x8e00) * 2);
    station_set_word(c, P4, commodity);
    market_at = (uint16_t)(commodity * 2);
    station_set_word(c, P5, market_at);
    station_set(c, P3, (uint32_t)(int32_t)(int16_t)station_market_word(market, (uint16_t)(market_at + 2)));
    if (station_word(c, P_STYLE) & 0x10) {
        station_set_word(c, P_STYLE, (uint16_t)(s->key_count >> 6));
        do {
            uint16_t *held = &s->cargo[station_word(c, P4) / 2];
            if (*held == 0) {
                trade_outcome(c, 0x98ce);
                return;
            }
            price = station_get(c, P3);
            if ((int32_t)price < 0) {
                price = 0u - price;
                station_set(c, P3, price);
                if (price > s->balance) {
                    trade_outcome(c, 0x98cb);
                    return;
                }
                price = 0u - price;
                station_set(c, P3, price);
            }
            s->balance += price;
            --*held;
            market_at = station_word(c, P5);
            station_set_market_word(market, market_at, (uint16_t)(station_market_word(market, market_at) + 1));
            --s->cargo_used;
            row = station_row_at(c, station_word(c, P_RECORD));
            if (row) {
                ++row->value[1];
                --row->value[2];
            }
            station_set_word(c, P_STYLE, (uint16_t)(station_word(c, P_STYLE) - 1));
        } while ((int16_t)station_word(c, P_STYLE) >= 0);
        if (s->key_count >= 0x40) station_show_trade(c);
        else trade_outcome(c, 0x98d0);
        return;
    }
    station_set_word(c, P_STYLE, (uint16_t)(s->key_count >> 6));
    do {
        if ((int16_t)s->cargo_used >= (int16_t)s->capacity) {
            trade_outcome(c, 0x98cc);
            return;
        }
        market_at = station_word(c, P5);
        if (station_market_word(market, market_at) == 0) {
            trade_outcome(c, 0x98cd);
            return;
        }
        price = station_get(c, P3);
        if ((int32_t)price >= 0 && price > s->balance) {
            trade_outcome(c, 0x98ca);
            return;
        }
        s->balance -= price;
        station_set_market_word(market, market_at, (uint16_t)(station_market_word(market, market_at) - 1));
        ++s->cargo[station_word(c, P4) / 2];
        ++s->cargo_used;
        row = station_row_at(c, station_word(c, P_RECORD));
        if (row) {
            --row->value[1];
            ++row->value[2];
        }
        station_set_word(c, P_STYLE, (uint16_t)(station_word(c, P_STYLE) - 1));
    } while ((int16_t)station_word(c, P_STYLE) >= 0);
    if (s->key_count >= 0x40) station_show_trade(c);
    else trade_outcome(c, 0x98cf);
}

void station_trade_handler(station_context *c, unsigned index)
{
    switch (index) {
    case 0x0c: {                                       /* the station's market */
        fe2_ref station = c->s->station;
        station_set(c, P3, 0);
        station_set_word(c, P2, 0x98bf);
        station_set(c, P_SUBJECT, station);
        station_set(c, P1, station_rol(station_lookup_object(c, station)->target_seed, 0x16));
        station_open_list(c, 0);
        break;
    }
    case 0x28:                                         /* a dealer's list */
        station_set_word(c, P3, station_word(c, P2));
        station_set_word(c, P2, 0x98c0);
        station_set(c, P1, station_get(c, P_NAME));
        station_open_list(c, 0x80);
        break;
    default: trade(c); break;
    }
}
