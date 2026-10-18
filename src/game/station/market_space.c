/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native-layout access to the market block. */
#include "market_internal.h"

static uint8_t *at(const fe2_market_state *s, uint32_t position)
{
    static uint8_t outside;
    if (position >= sizeof s->markets.bytes) {
        outside = 0;
        return &outside;
    }
    return (uint8_t *)&s->markets.bytes[position];
}

uint8_t fe2_market_byte(const fe2_market_state *s, uint32_t position) { return *at(s, position); }

uint16_t fe2_market_word(const fe2_market_state *s, uint32_t position)
{
    return (uint16_t)(fe2_market_byte(s, position) | fe2_market_byte(s, position + 1) << 8);
}

uint32_t fe2_market_dword(const fe2_market_state *s, uint32_t position)
{
    return fe2_market_word(s, position) | (uint32_t)fe2_market_word(s, position + 2) << 16;
}

void fe2_market_set_byte(fe2_market_state *s, uint32_t position, uint8_t value)
{
    *at(s, position) = value;
}

void fe2_market_set_word(fe2_market_state *s, uint32_t position, uint16_t value)
{
    fe2_market_set_byte(s, position, (uint8_t)value);
    fe2_market_set_byte(s, position + 1, (uint8_t)(value >> 8));
}

void fe2_market_set_dword(fe2_market_state *s, uint32_t position, uint32_t value)
{
    fe2_market_set_word(s, position, (uint16_t)value);
    fe2_market_set_word(s, position + 2, (uint16_t)(value >> 16));
}

uint32_t fe2_market_market_at(unsigned market)
{
    return (uint32_t)market * FE2_MARKET_RECORD_BYTES;
}

void fe2_market_set_low(uint32_t *field, uint16_t value)
{
    *field = (*field & 0xffff0000u) | value;
}

uint32_t fe2_market_timed(const fe2_market_state *s, unsigned record, unsigned field)
{
    return fe2_market_dword(s, FE2_TIMED_AT + record * FE2_TIMED_RECORD_BYTES + field);
}

void fe2_market_set_timed(fe2_market_state *s, unsigned record, unsigned field, uint32_t value)
{
    fe2_market_set_dword(s, FE2_TIMED_AT + record * FE2_TIMED_RECORD_BYTES + field, value);
}

uint32_t fe2_market_history(const fe2_market_state *s, unsigned age)
{
    return fe2_market_dword(s, FE2_HISTORY_AT + 4u * age);
}

void fe2_market_set_history(fe2_market_state *s, unsigned age, uint32_t value)
{
    fe2_market_set_dword(s, FE2_HISTORY_AT + 4u * age, value);
}

void fe2_market_set_profile_word(fe2_market_state *s, uint16_t value)
{
    fe2_market_set_word(s, FE2_PROFILE_WORD_AT, value);
}

uint16_t fe2_market_summary(const fe2_market_state *s, unsigned field)
{
    return fe2_market_word(s, FE2_SUMMARY_AT + field);
}

void fe2_market_set_summary(fe2_market_state *s, unsigned field, uint16_t value)
{
    fe2_market_set_word(s, FE2_SUMMARY_AT + field, value);
}
