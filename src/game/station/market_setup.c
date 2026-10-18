/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* collecting the stations, setting up their
 * markets, the system's requests, and the module's events. */
#include "market_internal.h"

enum {
    STATION_FLAG = 0x10, NO_BLACK_MARKET_FLAG = 0x20, MARKET_FLAG_BLACK_MARKET = 0x10,
    ALLEGIANCE_SHIFT = 6, KIND_STATION = 9, KIND_GOODS = 0, KIND_EQUIPMENT = 1,
    START_GAME = 1, EVENT_NEW_SYSTEM = 2, EVENT_NEW_DAY = 0x0f, FLIGHT_HYPERSPACE = 8,
    OBJECTS_PARAMETER = 12, SUMMARY_SHIFT = 3, SUMMARY_BASE = 8
};


void fe2_market_collect(fe2_market_state *s, const fe2_object *object)
{
    uint16_t slot = s->market_count;
    if (!(object->flags & STATION_FLAG)) return;
    if (slot < FE2_MAX_MARKETS) ++s->market_count;
    /* A full list keeps overwriting the slot after the last one. */
    fe2_market_set_byte(s, fe2_market_market_at(slot) + FE2_MARKET_OBJECT, object->handle);
}

static void visit(void *context, const fe2_object *object)
{
    fe2_market_collect(context, object);
}


void fe2_market_collect_markets(fe2_market_state *s, const fe2_market_services *game)
{
    s->market_count = 0;
    game->visit_objects(game->opaque, s->objects, visit, s);
}

/* Runs `body` count + 1 times, like the original's DEC/JNS loops. */
static unsigned iterations(uint16_t count)
{
    return (count & 0x8000u) ? 1u : count + 1u;
}

static void set_up_categories(fe2_market_state *s, const fe2_market_services *game, uint32_t market)
{
    fe2_market_writer w;
    unsigned n;
    w.market = market;
    w.cursor = market + FE2_MARKET_CATEGORIES_AT;
    fe2_market_set_word(s, market + FE2_MARKET_CATEGORY_COUNT, 0);
    if (s->market_count != 0 && !((uint16_t)(s->market_count - 1u) & 0x8000u))
        for (n = ((uint16_t)(s->market_count - 1u) >> 1) + 1u; n > 0; --n) {
            fe2_market_set_low(&s->draft.kind, KIND_STATION);
            fe2_market_append_category(s, game, &w);
        }
    for (n = iterations((uint16_t)(s->category_0_rate << 1)); n > 0; --n) {
        fe2_market_set_low(&s->draft.kind, KIND_GOODS);
        fe2_market_append_category(s, game, &w);
    }
    for (n = iterations((uint16_t)(s->category_1_rate << 1)); n > 0; --n) {
        fe2_market_set_low(&s->draft.kind, KIND_EQUIPMENT);
        fe2_market_append_category(s, game, &w);
    }
}

static void set_up_market(fe2_market_state *s, const fe2_market_services *game, unsigned index)
{
    uint32_t market = fe2_market_market_at(index);
    uint16_t lookup, system;
    fe2_market_writer w;
    fe2_call_args args;
    fe2_object *station;

    fe2_market_set_prices(s, game, market);
    lookup = (uint16_t)s->draft.lookup;
    system = (uint16_t)s->draft.system;
    w.market = market;
    w.cursor = market + FE2_MARKET_TEXTS_AT;
    fe2_market_set_word(s, market + FE2_MARKET_TEXT_COUNT, 0);
    fe2_market_set_byte(s, market + FE2_MARKET_FLAGS, 0);
    fe2_market_set_low(&s->draft.counter, (uint16_t)(s->population_band << 2));
    do fe2_market_generate_text(s, game, &w);
    while (fe2_market_set_low(&s->draft.counter, (uint16_t)(s->draft.counter - 1u)),
           !((uint16_t)s->draft.counter & 0x8000u));

    fe2_args_clear(&args);
    fe2_args_objects(&args, OBJECTS_PARAMETER, s->objects);
    station = game->lookup_object(game->opaque, fe2_market_byte(s, market + FE2_MARKET_OBJECT), &args);
    s->station_seed = (station->target_seed >> 16) | (station->target_seed << 16);
    if (!(station->flags & NO_BLACK_MARKET_FLAG))
        fe2_market_set_byte(s, market + FE2_MARKET_FLAGS,
                         (uint8_t)(fe2_market_byte(s, market + FE2_MARKET_FLAGS) | MARKET_FLAG_BLACK_MARKET));
    set_up_categories(s, game, market);
    fe2_market_set_low(&s->draft.system, system);
    fe2_market_set_low(&s->draft.lookup, lookup);
}

/* markets of the system whose key is in draft.system. */
void fe2_market_set_up_markets(fe2_market_state *s, const fe2_market_services *game)
{
    fe2_market_draft *d = &s->draft;
    uint32_t key = d->system;
    fe2_system_trade trade;
    unsigned i;

    game->system_profile(game->opaque, d);
    s->resource_richness = (uint16_t)d->lookup;
    s->category_0_rate = (uint16_t)d->reward;
    fe2_market_set_profile_word(s, (uint16_t)d->detail);
    s->category_1_rate = (uint16_t)d->kind;
    s->allegiance = (uint16_t)((uint16_t)d->threshold >> ALLEGIANCE_SHIFT);
    d->system = key;
    trade = game->system_trade(game->opaque, d);
    s->population_band = trade.population;
    fe2_market_set_low(&d->lookup, (uint16_t)(trade.population << 1));
    s->metadata = trade.metadata;
    fe2_market_set_low(&d->system, (uint16_t)(trade.metadata << 1));
    for (i = 0; i < FE2_COMMODITIES; ++i) s->trade_codes[i] = trade.codes[i];

    if (s->market_count != 0) {
        fe2_market_set_low(&d->threshold, s->market_count);
        for (i = 0; (uint16_t)d->threshold != 0; ++i) {
            set_up_market(s, game, i);
            fe2_market_set_low(&d->threshold, (uint16_t)(d->threshold - 1u));
        }
    }

    fe2_market_set_timed(s, 0, FE2_TIMED_HEAD, 0);
    d->system = key;
    game->system_summary(game->opaque, d);
    fe2_market_set_summary(s, SUMMARY_LOW, (uint16_t)d->counter);
    fe2_market_set_summary(s, SUMMARY_COUNT, (uint16_t)(((uint16_t)d->due << SUMMARY_SHIFT) + SUMMARY_BASE));
    fe2_market_set_low(&d->counter, fe2_market_summary(s, SUMMARY_COUNT));
    fe2_market_set_summary(s, SUMMARY_HIGH, (uint16_t)d->threshold);
    do fe2_market_generate_request(s, game);
    while (fe2_market_set_low(&d->counter, (uint16_t)(d->counter - 1u)),
           !((uint16_t)d->counter & 0x8000u));
}


void fe2_market_initialize(fe2_market_state *s, const fe2_market_services *game, uint8_t request)
{
    unsigned i;
    if (request != START_GAME) return;
    for (i = 0; i < FE2_HISTORY; ++i) fe2_market_set_history(s, FE2_HISTORY - 1 - i, 0);
    fe2_market_collect_markets(s, game);
    s->draft.system = s->current_system;
    fe2_market_set_up_markets(s, game);
}


void fe2_market_event(fe2_market_state *s, const fe2_market_services *game, uint16_t event)
{
    if (event == EVENT_NEW_SYSTEM) {
        fe2_market_collect_markets(s, game);
        fe2_market_set_history(s, 3, fe2_market_history(s, 2));
        fe2_market_set_history(s, 2, fe2_market_history(s, 1));
        fe2_market_set_history(s, 1, fe2_market_history(s, 0));
        fe2_market_set_history(s, 0, 0);
        s->draft.system = s->current_system;
        fe2_market_set_up_markets(s, game);
    } else if (event == EVENT_NEW_DAY && s->flight_state != FLIGHT_HYPERSPACE) {
        s->draft.system = s->current_system;
        fe2_market_update_markets(s, game);
    }
}


void fe2_market_store_motion(fe2_object *object, uint16_t ax, uint16_t dx, uint16_t bx,
                             uint16_t cx)
{
    object->orbit[0] = (uint32_t)dx << 16 | ax;
    object->orbit[1] = (uint32_t)cx << 16 | bx;
}
