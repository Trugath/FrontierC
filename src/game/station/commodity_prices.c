/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* commodity prices and supplies. Each market entry holds a
 * supply word (+0) and a price word (+2). Both scale a commodity's base and
 * variation by the system's population band, metadata and the commodity's
 * trade strength, then randomize. Coefficients are Q15; products keep the
 * high word of the doubled 32-bit result. */
#include "market_internal.h"

enum {
    STRENGTH_MASK = 7, EXPORTED = 0x08, ILLEGAL = 0x80,
    PRICE_RANDOM_MASK = 0x3ff, PRICE_RANDOM_BASE = 0x3e00,
    SUPPLY_RANDOM_HIGH_MASK = 0x3f, SUPPLY_RANDOM_HIGH_BASE = 0x40,
    LAST_COMMODITY = FE2_COMMODITIES - 1
};

static uint16_t q15(uint16_t a, uint16_t b)
{
    return (uint16_t)(((uint32_t)((int32_t)(int16_t)a * (int16_t)b) << 1) >> 16);
}

/* Doubling a signed 32-bit value overflows when its top two bits differ. */
static int doubling_overflows(uint32_t value)
{
    return ((value >> 31) ^ (value >> 30)) & 1u;
}

/* Factor tables are indexed by the doubled bands the original keeps in its
 * scratch block. */
static uint16_t factor(const uint16_t *table, uint32_t doubled_index)
{
    return table[(uint16_t)doubled_index / 2u];
}


static uint16_t price(fe2_market_state *s, const fe2_market_services *game, unsigned commodity,
                      uint8_t code)
{
    const uint16_t *p = &fe2_market_commodity_parameters[commodity * 4];
    fe2_market_draft *d = &s->draft;
    uint16_t value = q15(p[1], factor(fe2_market_population_value_factors, d->lookup));
    uint16_t result;
    uint32_t product;

    value = q15(value, factor(fe2_market_strength_value_factors, d->reward));
    if (code & EXPORTED) value = (uint16_t)(0u - value);
    value = (uint16_t)(value + p[0]);
    if (code & ILLEGAL) value = (uint16_t)(value << 1);
    value = q15(value, factor(fe2_market_metadata_value_factors, d->system));
    value = doubling_overflows((uint32_t)value << 16) ? 0x7fff : (uint16_t)(value << 1);

    product = (uint32_t)((int32_t)(int16_t)((game->random(game->opaque, 0) & PRICE_RANDOM_MASK) +
                                            PRICE_RANDOM_BASE) * (int16_t)value);
    if (doubling_overflows(product) || doubling_overflows(product << 1)) return 0x7fff;
    result = (uint16_t)((product << 2) >> 16);
    /* Radioactives and rubbish start from a negative base: the station pays
     * to have them hauled away, and selling them charges the hold. A positive
     * base driven through zero is a free purchase instead. */
    if ((result & 0x8000u) && !(p[0] & 0x8000u)) return 0;
    return result;
}

/* never negative. */
static uint16_t supply(fe2_market_state *s, const fe2_market_services *game, unsigned commodity,
                       uint8_t code)
{
    const uint16_t *p = &fe2_market_commodity_parameters[commodity * 4];
    fe2_market_draft *d = &s->draft;
    uint16_t value = q15(p[3], factor(fe2_market_strength_value_factors, d->reward));
    uint16_t scaled, random, magnitude, result;

    if (!(code & EXPORTED)) value = (uint16_t)(0u - value);
    value = (uint16_t)(value + p[2]);
    value = q15(value, factor(fe2_market_population_supply_factors, d->lookup));
    if (code & ILLEGAL) value >>= 3;
    scaled = q15(value, factor(fe2_market_metadata_supply_factors, d->system));
    random = (uint16_t)game->random(game->opaque, 0);
    random = (uint16_t)((((random >> 8) & SUPPLY_RANDOM_HIGH_MASK) + SUPPLY_RANDOM_HIGH_BASE) << 8 |
                        (random & 0xffu));
    magnitude = (scaled & 0x8000u) ? (uint16_t)(0u - scaled) : scaled;
    result = (uint16_t)(q15(random, magnitude) + scaled);
    return (result & 0x8000u) ? 0 : result;
}

/* The strength index of each commodity is kept doubled in the draft. */
static uint8_t select_commodity(fe2_market_state *s, unsigned commodity)
{
    uint8_t code = s->trade_codes[commodity];
    fe2_market_set_low(&s->draft.reward, (uint16_t)((code & STRENGTH_MASK) * 2u));
    fe2_market_set_low(&s->draft.counter, (uint16_t)(LAST_COMMODITY - commodity));
    return code;
}

void fe2_market_set_prices(fe2_market_state *s, const fe2_market_services *game, uint32_t market)
{
    unsigned c;
    for (c = 0; c < FE2_COMMODITIES; ++c) {
        uint8_t code = select_commodity(s, c);
        uint32_t entry = market + 4u * c;
        fe2_market_set_word(s, entry + 2, price(s, game, c, code));
        fe2_market_set_word(s, entry, supply(s, game, c, code));
    }
    fe2_market_set_low(&s->draft.counter, 0xffff);
}

/* Signed floor((new + 3 * old) / 4) in 32-bit arithmetic. */
static uint16_t blend(uint16_t fresh, uint16_t old)
{
    uint32_t sum = (uint32_t)(int32_t)(int16_t)fresh + 3u * (uint32_t)(int32_t)(int16_t)old;
    return (uint16_t)(sum & 0x80000000u ? ~(~sum >> 2) : sum >> 2);
}

void fe2_market_refresh_prices(fe2_market_state *s, const fe2_market_services *game, uint32_t market)
{
    unsigned c;
    for (c = 0; c < FE2_COMMODITIES; ++c) {
        uint8_t code = select_commodity(s, c);
        uint32_t entry = market + 4u * c;
        fe2_market_set_word(s, entry + 2, blend(price(s, game, c, code), fe2_market_word(s, entry + 2)));
        fe2_market_set_word(s, entry, blend(supply(s, game, c, code), fe2_market_word(s, entry)));
    }
    fe2_market_set_low(&s->draft.counter, 0xffff);
}
