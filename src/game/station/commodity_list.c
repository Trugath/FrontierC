/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* commodity names and the two-column price list. */
#include "market_internal.h"

enum {
    TEXT_SELECTOR_MASK = 0x03fe, PRICE_LIST = 0x3e, ILLEGAL_CODE = 0x80,
    LEFT_COLUMN = 0x2d, RIGHT_COLUMN = 0x7d, PRICE_FORMAT = 0x3004,
    /* Formatter control bytes. */
    LIST_HEADER_0 = 0x1f, LIST_HEADER_1 = 0x0f, LIST_HEADER_2 = 0x01,
    SET_COLUMN = 0x1e, SECOND_COLUMN_MARK = 0x5a, NEW_LINE = 0x0d
};

static void put(fe2_text_out *out, uint16_t at, uint8_t byte) { out->buffer[at] = byte; }

/* The cursor through the commodities (draft.counter) and the entry
 * position; returns 0 once all commodities are done. */
typedef struct list_cursor {
    fe2_market_state *s;
    const fe2_market_services *game;
    uint32_t entry;
} list_cursor;

static int done(const list_cursor *c)
{
    return (uint16_t)c->s->draft.counter == FE2_COMMODITIES;
}

static void advance(list_cursor *c)
{
    fe2_market_set_low(&c->s->draft.counter, (uint16_t)(c->s->draft.counter + 1u));
    c->entry += 4;
}

/* the next legal commodity's name and price at `column`. Returns 0
 * when the list is exhausted (including after formatting the last one). */
static int next_entry(list_cursor *c, fe2_text_out *out, uint8_t column)
{
    fe2_market_state *s = c->s;
    for (;;) {
        uint16_t index = (uint16_t)s->draft.counter;
        if (!(s->trade_codes[index] & ILLEGAL_CODE)) {
            int16_t price = (int16_t)fe2_market_word(s, c->entry + 2);
            fe2_call_args args;
            c->game->format_text(c->game->opaque, (uint16_t)(index << 1),
                                 (const fe2_text_table *)fe2_market_text_directory, out);
            put(out, (uint16_t)(out->at - 1u), SET_COLUMN);
            put(out, out->at++, column);
            s->draft.system = (uint32_t)(int32_t)price;
            fe2_args_clear(&args);
            fe2_args_dword(&args, 1, s->draft.system);
            c->game->format_number(c->game->opaque, PRICE_FORMAT, &args, out);
            advance(c);
            return !done(c);
        }
        advance(c);
        if (done(c)) return 0;
    }
}

static void price_list(fe2_market_state *s, const fe2_market_services *game, uint32_t market,
                       fe2_text_out *out)
{
    fe2_market_draft saved = s->draft;
    list_cursor c;
    put(out, out->at, LIST_HEADER_0);
    put(out, (uint16_t)(out->at + 1u), LIST_HEADER_1);
    put(out, (uint16_t)(out->at + 2u), LIST_HEADER_2);
    out->at = (uint16_t)(out->at + 3u);
    c.s = s;
    c.game = game;
    c.entry = market;
    fe2_market_set_low(&s->draft.counter, 0);
    for (;;) {
        fe2_market_set_low(&s->draft.due, LEFT_COLUMN);
        if (!next_entry(&c, out, LEFT_COLUMN)) break;
        put(out, (uint16_t)(out->at - 1u), SET_COLUMN);
        put(out, out->at++, SECOND_COLUMN_MARK);
        fe2_market_set_low(&s->draft.due, RIGHT_COLUMN);
        next_entry(&c, out, RIGHT_COLUMN);
        put(out, (uint16_t)(out->at - 1u), NEW_LINE);
        if (done(&c)) break;
    }
    put(out, out->at++, 0);
    /* The scratch values are restored, except the commodity cursor. */
    s->draft.system = saved.system;
    s->draft.due = saved.due;
    s->draft.reward = saved.reward;
    s->draft.seed = saved.seed;
}

void fe2_market_format(fe2_market_state *s, const fe2_market_services *game, uint16_t selector,
                       uint32_t market, fe2_text_out *out)
{
    selector &= TEXT_SELECTOR_MASK;
    if (selector == PRICE_LIST) price_list(s, game, market, out);
    else game->format_text(game->opaque, selector, (const fe2_text_table *)fe2_market_text_directory, out);
}
