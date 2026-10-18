/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_MARKET_MARKET_TABLES_H
#define FE2_MARKET_MARKET_TABLES_H

/* Native market tables. fe2_market_bind points these into frontier.dat table/market. */
#include <stddef.h>
#include <stdint.h>

enum {
    FE2_MARKET_ATTACHMENT_VECTORS = 146,
    FE2_MARKET_COMMAND_VECTORS = 152,
    FE2_MARKET_CONTROL_VECTORS = 92
};

/* Price and supply coefficients (Q15) by trade strength, population band
 * and system metadata. */
extern const uint16_t *fe2_market_strength_value_factors;
extern const uint16_t *fe2_market_population_value_factors;
extern const uint16_t *fe2_market_metadata_value_factors;
extern const uint16_t *fe2_market_population_supply_factors;
extern const uint16_t *fe2_market_metadata_supply_factors;
/* Per commodity: base price, price variation, base supply, supply variation. */
extern const uint16_t *fe2_market_commodity_parameters;
/* Bulletin record tables. */
extern const uint16_t *fe2_market_record_value_pairs;
extern const uint16_t *fe2_market_record_count_factors;
extern const uint16_t *fe2_market_record_reward_factors;
/* Reference-frame vectors (three words per slot, eight bytes apart). */
extern const uint16_t *fe2_market_attachment_vectors;
extern const uint16_t *fe2_market_command_vectors;
extern const uint16_t *fe2_market_control_vectors;
extern const uint32_t *fe2_market_allegiance_candidates;
extern const uint16_t *fe2_market_request_9856_rewards;
extern const uint16_t *fe2_market_request_9854_rewards;
extern const uint16_t *fe2_market_request_9855_rewards;
extern const uint16_t *fe2_market_local_record_words;
/* Commodity-name text directory. */
extern const uint8_t *fe2_market_text_directory;

/* Category values: a random index below `count` selects a value. */
typedef struct fe2_category_values {
    uint16_t count;
    const uint16_t *values;
} fe2_category_values;
extern fe2_category_values fe2_market_category_values[13];

int fe2_market_bind(const uint8_t *image, size_t count);

#endif
