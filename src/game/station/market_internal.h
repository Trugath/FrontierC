/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_MARKET_MARKET_INTERNAL_H
#define FE2_MARKET_MARKET_INTERNAL_H

/* Shared declarations between the market translation units. */
#include "market.h"
#include "market_tables.h"

/* market_space.c: native-layout access by byte position. */
uint8_t fe2_market_byte(const fe2_market_state *s, uint32_t position);
uint16_t fe2_market_word(const fe2_market_state *s, uint32_t position);
uint32_t fe2_market_dword(const fe2_market_state *s, uint32_t position);
void fe2_market_set_byte(fe2_market_state *s, uint32_t position, uint8_t value);
void fe2_market_set_word(fe2_market_state *s, uint32_t position, uint16_t value);
void fe2_market_set_dword(fe2_market_state *s, uint32_t position, uint32_t value);
uint32_t fe2_market_market_at(unsigned market);
/* The rest of the market block: summary words, timed records, history and
 * the profile word. */
enum { SUMMARY_COUNT = 0, SUMMARY_HIGH = 2, SUMMARY_LOW = 4 };
uint16_t fe2_market_summary(const fe2_market_state *s, unsigned field);
void fe2_market_set_summary(fe2_market_state *s, unsigned field, uint16_t value);
uint32_t fe2_market_timed(const fe2_market_state *s, unsigned record, unsigned field);
void fe2_market_set_timed(fe2_market_state *s, unsigned record, unsigned field, uint32_t value);
uint32_t fe2_market_history(const fe2_market_state *s, unsigned age);
void fe2_market_set_history(fe2_market_state *s, unsigned age, uint32_t value);
void fe2_market_set_profile_word(fe2_market_state *s, uint16_t value);
/* Low-word stores into a draft dword keep its high word. */
void fe2_market_set_low(uint32_t *field, uint16_t value);

/* commodity_prices.c */
void fe2_market_set_prices(fe2_market_state *s, const fe2_market_services *game, uint32_t market);
void fe2_market_refresh_prices(fe2_market_state *s, const fe2_market_services *game, uint32_t market);

/* market_texts.c: the record being written is at *cursor in market `market`. */
typedef struct fe2_market_writer {
    uint32_t market;
    uint32_t cursor;
} fe2_market_writer;
void fe2_market_generate_text(fe2_market_state *s, const fe2_market_services *game,
                           fe2_market_writer *w);

/* categories.c */
void fe2_market_append_category(fe2_market_state *s, const fe2_market_services *game,
                             fe2_market_writer *w);
uint16_t fe2_market_category_value(fe2_market_state *s, const fe2_market_services *game);

/* timed_requests.c */
void fe2_market_generate_request(fe2_market_state *s, const fe2_market_services *game);

/* market_setup.c / market_update.c */
void fe2_market_collect_markets(fe2_market_state *s, const fe2_market_services *game);
void fe2_market_set_up_markets(fe2_market_state *s, const fe2_market_services *game);
void fe2_market_update_markets(fe2_market_state *s, const fe2_market_services *game);

#endif
