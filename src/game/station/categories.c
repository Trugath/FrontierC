/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* category records (goods, ships and equipment on offer) of
 * a market: a value drawn from a category table and a random seed. */
#include "market_internal.h"

enum { KIND_BY_ALLEGIANCE_BELOW = 2, KIND_FROM_DETAIL = 0x0c, KIND_POPULATION_SCALED = 1,
       SMALL_POPULATION = 4, SKIP_BIT = 0x08 };

/* kinds 0 and 1 pick the allegiance's table. */
uint16_t fe2_market_category_value(fe2_market_state *s, const fe2_market_services *game)
{
    fe2_market_draft *d = &s->draft;
    uint16_t kind = (uint16_t)d->kind;
    unsigned table = kind < KIND_BY_ALLEGIANCE_BELOW ? s->allegiance * 2u + kind : kind;
    const fe2_category_values *values;
    uint32_t high;
    uint16_t index;

    /* Kinds past the table read code bytes in the original. */
    if (table >= sizeof fe2_market_category_values / sizeof fe2_market_category_values[0]) return 0;
    values = &fe2_market_category_values[table];
    fe2_market_set_low(&d->reward, values->count);
    d->lookup = game->random(game->opaque, &high);
    d->system = high;
    if (kind == KIND_FROM_DETAIL) {
        d->lookup = d->detail;
        d->system = (d->detail >> 16) | (d->detail << 16);
    }
    if (kind == KIND_POPULATION_SCALED && s->population_band < SMALL_POPULATION) {
        uint16_t scaled;
        fe2_market_set_low(&d->system, (uint16_t)((uint16_t)d->system >> 3));
        scaled = (uint16_t)((uint32_t)s->population_band * (uint16_t)d->system >> 16);
        fe2_market_set_low(&d->lookup, (uint16_t)(((uint16_t)d->lookup >> 1) + scaled));
    }
    index = (uint16_t)((uint32_t)values->count * (uint16_t)d->lookup >> 16);
    return values->values[index];
}

/* a category record at the cursor, unless the random draw skips it
 * or the market already lists fourteen. */
void fe2_market_append_category(fe2_market_state *s, const fe2_market_services *game,
                             fe2_market_writer *w)
{
    uint32_t count = w->market + FE2_MARKET_CATEGORY_COUNT;
    uint16_t value;
    if (game->random(game->opaque, 0) & SKIP_BIT) return;
    if (fe2_market_word(s, count) >= FE2_MARKET_CATEGORIES) return;
    value = fe2_market_category_value(s, game);
    fe2_market_set_word(s, w->cursor, value);
    fe2_market_set_word(s, w->cursor + 2, (uint16_t)s->draft.lookup);
    w->cursor += FE2_MARKET_CATEGORY_BYTES;
    fe2_market_set_word(s, count, (uint16_t)(fe2_market_word(s, count) + 1u));
}
