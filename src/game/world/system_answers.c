/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The three answer modes of the 0x8a query. */
#include "system_catalogue.h"

enum { ALLEGIANCE_SHIFT = 6, EQUAL_KEEPS_ORDER = 2 };

fe2_system_profile fe2_system_profile_of(const fe2_catalogue_record *r)
{
    fe2_system_profile p;
    p.resource_richness = r->resource_richness;
    p.map_text = r->map_text;
    p.view_text = r->view_text;
    p.category_0_rate = r->category_0_rate;
    p.market_profile = r->market_profile;
    p.category_1_rate = r->category_1_rate;
    p.market_counter_seed = r->market_counter_seed;
    p.government = r->government;
    return p;
}

fe2_catalogue_trade fe2_system_trade_of(const fe2_catalogue_record *r)
{
    fe2_catalogue_trade t;
    t.population_band = r->population_band;
    t.economic_factor_index = r->economic_factor_index;
    t.government = r->government;
    t.trade_codes = r->trade_codes;
    return t;
}

fe2_system_summary fe2_system_summary_of(const fe2_catalogue_record *r, uint16_t first,
                                         uint16_t second)
{
    fe2_system_summary s;
    int swap;
    s.low_strength = r->strengths & 0x0fu;
    s.high_strength = r->strengths >> 4;
    s.allegiance = r->government >> ALLEGIANCE_SHIFT;
    s.larger = s.low_strength > s.high_strength ? s.low_strength : s.high_strength;
    swap = s.low_strength < s.high_strength ||
           (s.low_strength == s.high_strength && s.allegiance != EQUAL_KEEPS_ORDER);
    s.first = swap ? second : first;
    s.second = swap ? first : second;
    return s;
}
