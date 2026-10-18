/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_CATALOGUE_SYSTEM_CATALOGUE_H
#define FE2_CATALOGUE_SYSTEM_CATALOGUE_H

/*
 * System catalogue (catalogue): fixed data for the handcrafted star systems.
 * Each record holds a system's population band, government and allegiance,
 * trade codes for the 31 commodities, two packed strengths and its map and
 * system-view text selectors. A system with no record gets one of seven
 * fallback records, chosen by its distance from a centre of the galaxy map.
 *
 * Queries find a record by packed system key and return one of three views:
 * - 0, the profile (fe2_system_profile);
 * - 1, the trade data (fe2_catalogue_trade);
 * - any other value, the summary (fe2_system_summary).
 * The module needs no services.
 */
#include <stddef.h>
#include <stdint.h>

enum {
    FE2_SYSTEM_FALLBACKS = 7,
    FE2_SYSTEM_KEYED = 324,
    FE2_SYSTEM_COMMODITIES = 31
};

/* Trade code bits. */
enum {
    FE2_TRADE_STRENGTH = 0x07, FE2_TRADE_EXPORT = 0x08, FE2_TRADE_ILLEGAL = 0x80
};

typedef struct fe2_catalogue_record {
    uint32_t key;                     /* system key; zero ends the keyed list */
    uint8_t population_band;
    uint8_t resource_richness, category_0_rate, market_profile, category_1_rate, market_counter_seed;
    uint8_t government;               /* bits 0..5 government, 6..7 allegiance */
    uint8_t economic_factor_index;
    uint8_t trade_codes[FE2_SYSTEM_COMMODITIES]; /* FE2_TRADE_* */
    uint8_t strengths;                /* low and high nibbles */
    uint16_t map_text;                /* map description selector */
    uint16_t view_text;               /* system view selector */
} fe2_catalogue_record;

typedef struct fe2_system_catalogue {
    const fe2_catalogue_record *fallbacks;   /* FE2_SYSTEM_FALLBACKS, nearest last */
    const fe2_catalogue_record *records;     /* searched in order */
    unsigned count;
} fe2_system_catalogue;

/* The catalogue generated from the native records. */
extern const fe2_system_catalogue fe2_default_catalogue;

int fe2_catalogue_bind(const uint8_t *image, size_t count);

/* Mode 0, in the original's scratch words 0x7bc..0x7d8. */
typedef struct fe2_system_profile {
    uint16_t resource_richness;
    uint16_t map_text;                /* (replaces the key) */
    uint16_t view_text;
    uint16_t category_0_rate, market_profile, category_1_rate, market_counter_seed;
    uint16_t government;
} fe2_system_profile;

/* Trade query result; trade_codes references immutable catalogue storage. */
typedef struct fe2_catalogue_trade {
    uint16_t population_band;
    uint16_t economic_factor_index;
    uint16_t government;
    const uint8_t *trade_codes;
} fe2_catalogue_trade;

/*
 * Other modes, in scratch words. The two strengths are the record's packed
 * nibbles. `first` and `second` are the caller's resident pair (0x20f1,
 * 0x20f3), put in order: swapped when the high strength is larger, or when
 * the strengths are equal and the allegiance is not 2.
 */
typedef struct fe2_system_summary {
    uint16_t low_strength;
    uint16_t high_strength;
    uint16_t larger;
    uint16_t allegiance;
    uint16_t second;
    uint16_t first;
} fe2_system_summary;

/* The record for `key`: the first keyed record with that key, else a
 * fallback. */
const fe2_catalogue_record *fe2_system_find(const fe2_system_catalogue *catalogue, uint32_t key);
/* The fallback record for an absent key. */
const fe2_catalogue_record *fe2_system_fallback(const fe2_system_catalogue *catalogue, uint32_t key);

fe2_system_profile fe2_system_profile_of(const fe2_catalogue_record *record);
fe2_catalogue_trade fe2_system_trade_of(const fe2_catalogue_record *record);
fe2_system_summary fe2_system_summary_of(const fe2_catalogue_record *record, uint16_t first,
                                         uint16_t second);

#endif
