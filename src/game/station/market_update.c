/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the daily update - drop the first expired request, maybe
 * add new ones, drift prices and supplies, replace a bulletin record and
 * turn over the category records. */
#include "market_internal.h"

enum {
    EXPIRY_GRACE_DAYS = 4, REQUEST_TRIES = 3,
    TEXT_ILLEGAL = 0x9828, TEXT_IMPERIAL = 0x9826, TEXT_FEDERAL = 0x9827,
    MARKET_FLAG_ILLEGAL_LISTED = 0x01, MARKET_FLAG_ILLEGAL_WARNED = 0x02,
    MARKET_FLAG_IMPERIAL = 0x04, MARKET_FLAG_FEDERAL = 0x08,
    KIND_STATION = 9, KIND_GOODS = 0, KIND_EQUIPMENT = 1
};

/* The first request past its due date (plus grace) is removed; later
 * records move down up to and including the zero head that ends the list. */
static void expire_request(fe2_market_state *s)
{
    static const unsigned fields[6] = {FE2_TIMED_SYSTEM, FE2_TIMED_DUE, FE2_TIMED_REWARD,
                                       FE2_TIMED_DETAIL, FE2_TIMED_SEED, FE2_TIMED_HEAD};
    uint32_t limit = s->day + EXPIRY_GRACE_DAYS;
    unsigned i, f;
    for (i = 0; i <= FE2_TIMED_RECORDS && fe2_market_timed(s, i, FE2_TIMED_HEAD) != 0; ++i) {
        if (limit < fe2_market_timed(s, i, FE2_TIMED_DUE)) continue;
        /* The original copies until it has moved a zero head; the area ends
         * after the record holding the history. */
        for (++i; i <= FE2_TIMED_RECORDS; ++i) {
            for (f = 0; f < 6; ++f) fe2_market_set_timed(s, i - 1, fields[f], fe2_market_timed(s, i, fields[f]));
            if (fe2_market_timed(s, i, FE2_TIMED_HEAD) == 0) break;
        }
        return;
    }
}

static void clear_flag(fe2_market_state *s, uint32_t market, uint8_t flag)
{
    fe2_market_set_byte(s, market + FE2_MARKET_FLAGS,
                     (uint8_t)(fe2_market_byte(s, market + FE2_MARKET_FLAGS) & ~flag));
}

/* Replace a random bulletin record; a notice that is withdrawn may be
 * posted again. */
static void replace_text(fe2_market_state *s, const fe2_market_services *game, uint32_t market)
{
    uint32_t count = market + FE2_MARKET_TEXT_COUNT;
    uint16_t saved, selector;
    fe2_market_writer w;

    w.market = market;
    w.cursor = market + FE2_MARKET_TEXTS_AT +
               (uint16_t)(game->random_below(game->opaque, fe2_market_word(s, count)) << 4);
    selector = fe2_market_word(s, w.cursor);
    if (selector == TEXT_ILLEGAL)
        clear_flag(s, market, (fe2_market_word(s, w.cursor + 2) & 0x8000u) ? MARKET_FLAG_ILLEGAL_WARNED
                                                                         : MARKET_FLAG_ILLEGAL_LISTED);
    else if (selector == TEXT_IMPERIAL)
        clear_flag(s, market, MARKET_FLAG_IMPERIAL);
    if (selector == TEXT_FEDERAL) clear_flag(s, market, MARKET_FLAG_FEDERAL);
    /* With eighteen or more records the chosen slot can be the count. */
    fe2_market_set_word(s, w.cursor, 0);
    saved = fe2_market_word(s, count);
    fe2_market_set_word(s, count, (uint16_t)(saved - 1u));
    fe2_market_generate_text(s, game, &w);
    fe2_market_set_word(s, count, saved);
}

/* Remove a random category record, then perhaps add new ones. */
static void turn_over_categories(fe2_market_state *s, const fe2_market_services *game,
                                 uint32_t market)
{
    uint32_t count = market + FE2_MARKET_CATEGORY_COUNT;
    uint32_t first = market + FE2_MARKET_CATEGORIES_AT;
    uint16_t removed = game->random_below(game->opaque, FE2_MARKET_CATEGORIES);
    int16_t following = (int16_t)(fe2_market_word(s, count) - removed);
    fe2_market_writer w;

    if (following > 0) {
        uint32_t at = first + (uint16_t)(removed << 2);
        for (; following > 0; --following, at += 4) fe2_market_set_dword(s, at, fe2_market_dword(s, at + 4));
        fe2_market_set_word(s, count, (uint16_t)(fe2_market_word(s, count) - 1u));
    }
    w.market = market;
    w.cursor = first + (uint16_t)(fe2_market_word(s, count) << 2);
    if ((int16_t)game->random_below(game->opaque, 9) < (int16_t)s->category_0_rate) {
        fe2_market_set_low(&s->draft.kind, KIND_GOODS);
        fe2_market_append_category(s, game, &w);
    }
    if ((int16_t)game->random_below(game->opaque, 9) < (int16_t)s->category_1_rate) {
        fe2_market_set_low(&s->draft.kind, KIND_EQUIPMENT);
        fe2_market_append_category(s, game, &w);
    }
    if ((int16_t)game->random_below(game->opaque, 0x1b) < (int16_t)s->market_count) {
        fe2_market_set_low(&s->draft.kind, KIND_STATION);
        fe2_market_append_category(s, game, &w);
    }
}

void fe2_market_update_markets(fe2_market_state *s, const fe2_market_services *game)
{
    fe2_market_draft *d = &s->draft;
    unsigned index;

    expire_request(s);
    fe2_market_set_low(&d->counter, REQUEST_TRIES - 1);
    do {
        if (game->random_below(game->opaque, 0x19) <= fe2_market_summary(s, SUMMARY_COUNT))
            fe2_market_generate_request(s, game);
        fe2_market_set_low(&d->counter, (uint16_t)(d->counter - 1u));
    } while (!((uint16_t)d->counter & 0x8000u));

    fe2_market_set_low(&d->lookup, (uint16_t)(s->population_band << 1));
    fe2_market_set_low(&d->system, (uint16_t)(s->metadata << 1));
    if (s->market_count == 0) return;
    fe2_market_set_low(&d->threshold, s->market_count);
    for (index = 0; (uint16_t)d->threshold != 0; ++index) {
        uint32_t market = fe2_market_market_at(index);
        uint16_t lookup, system;
        fe2_market_refresh_prices(s, game, market);
        lookup = (uint16_t)d->lookup;
        system = (uint16_t)d->system;
        if ((int16_t)game->random_below(game->opaque, 7) < (int16_t)s->population_band)
            replace_text(s, game, market);
        turn_over_categories(s, game, market);
        fe2_market_set_low(&d->system, system);
        fe2_market_set_low(&d->lookup, lookup);
        fe2_market_set_low(&d->threshold, (uint16_t)(d->threshold - 1u));
    }
}
