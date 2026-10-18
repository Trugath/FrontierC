/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* bulletin-board text records of a market. A random
 * generator composes a record in the draft and appends it at the writer's
 * cursor. Selectors name the record kinds (0x9819.. text resources). */
#include "market_internal.h"

enum {
    MARKET_FLAG_ILLEGAL_LISTED = 0x01, MARKET_FLAG_ILLEGAL_WARNED = 0x02,
    MARKET_FLAG_IMPERIAL = 0x04, MARKET_FLAG_FEDERAL = 0x08,
    ILLEGAL_CODE = 0x80, CANDIDATE_TRIES = 3, MIN_CANDIDATE_POPULATION = 3,
    TEXT_ILLEGAL = 0x9828, TEXT_IMPERIAL = 0x9826, TEXT_FEDERAL = 0x9827,
    TEXT_SHORTAGE = 0x9829, TEXT_HISTORY = 0x981c, TEXT_HISTORY_DEBT = 0x981d,
    TEXT_PASSENGER = 0x9819, TEXT_PASSENGER_GROUP = 0x981a, TEXT_REMOTE = 0x981e,
    TEXT_RANDOM = 0x9822, TEXT_VARIANT = 0x982b, TEXT_VARIANT_DETAIL = 0x99c0,
    SHORTAGE_NAME = 0x8e00, REMOTE_DETAIL = 0x999d, REMOTE_DAY_OFFSET = 0x11d5a0,
    HISTORY_REWARD_UNIT = 0x28, REMOTE_REWARD_UNIT = 1000, COUNT_FLAGGED_DETAIL = 0x20,
    OBJECTS_PARAMETER = 12
};


static void append(fe2_market_state *s, fe2_market_writer *w, uint16_t selector)
{
    const fe2_market_draft *d = &s->draft;
    uint32_t count = w->market + FE2_MARKET_TEXT_COUNT;
    if ((int16_t)fe2_market_word(s, count) >= FE2_MARKET_TEXTS) return;
    fe2_market_set_word(s, count, (uint16_t)(fe2_market_word(s, count) + 1u));
    fe2_market_set_word(s, w->cursor + FE2_TEXT_SELECTOR, selector);
    fe2_market_set_word(s, w->cursor + FE2_TEXT_WORD_2, (uint16_t)d->due);
    fe2_market_set_word(s, w->cursor + FE2_TEXT_WORD_4, (uint16_t)d->reward);
    fe2_market_set_word(s, w->cursor + FE2_TEXT_WORD_6, (uint16_t)d->detail);
    fe2_market_set_dword(s, w->cursor + FE2_TEXT_DWORD_8, d->system);
    fe2_market_set_dword(s, w->cursor + FE2_TEXT_DWORD_C, d->seed);
    w->cursor += FE2_MARKET_TEXT_BYTES;
}

static int any_illegal(const fe2_market_state *s)
{
    int c;
    for (c = FE2_COMMODITIES - 1; c >= 0; --c)
        if (s->trade_codes[c] & ILLEGAL_CODE) return 1;
    return 0;
}

/* Set a market flag; returns whether it was already set. */
static int test_and_set_flag(fe2_market_state *s, const fe2_market_writer *w, uint8_t flag)
{
    uint8_t flags = fe2_market_byte(s, w->market + FE2_MARKET_FLAGS);
    fe2_market_set_byte(s, w->market + FE2_MARKET_FLAGS, (uint8_t)(flags | flag));
    return (flags & flag) != 0;
}

static fe2_object *station(fe2_market_state *s, const fe2_market_services *game,
                           const fe2_market_writer *w)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_objects(&args, OBJECTS_PARAMETER, s->objects);
    return game->lookup_object(game->opaque, fe2_market_byte(s, w->market + FE2_MARKET_OBJECT), &args);
}

/* illegal goods listed once per market. */
static void illegal_goods(fe2_market_state *s, const fe2_market_services *game, fe2_market_writer *w)
{
    uint32_t seed;
    if (test_and_set_flag(s, w, MARKET_FLAG_ILLEGAL_LISTED) || !any_illegal(s)) return;
    seed = station(s, game, w)->target_seed;
    seed = (seed << 17) | (seed >> 15);
    s->draft.system = seed;
    s->draft.seed = seed;
    s->draft.due = 0;
    append(s, w, TEXT_ILLEGAL);
}


static void illegal_goods_warning(fe2_market_state *s, const fe2_market_services *game,
                                  fe2_market_writer *w)
{
    uint32_t high;
    game->random(game->opaque, &high);
    s->draft.system = high;
    if (!any_illegal(s) || test_and_set_flag(s, w, MARKET_FLAG_ILLEGAL_WARNED)) return;
    s->draft.due = 0xffffffffu;
    s->draft.seed = high;
    append(s, w, TEXT_ILLEGAL);
}

/* a commodity in short supply, counted from the end; the search
 * starts one entry past the last commodity (the object/flags word). */
static void shortage(fe2_market_state *s, const fe2_market_services *game, fe2_market_writer *w)
{
    int16_t remaining = (int16_t)game->random_below(game->opaque, 12);
    int commodity;
    uint16_t price;

    for (commodity = FE2_COMMODITIES; commodity >= 0; --commodity)
        if (fe2_market_word(s, w->market + 4u * (unsigned)commodity) == 0 && --remaining < 0) break;
    if (commodity < 0) return;
    price = fe2_market_word(s, w->market + 4u * (unsigned)commodity + 2);
    fe2_market_set_low(&s->draft.reward, (uint16_t)(price << 1));
    if (((price >> 15) ^ (price >> 14)) & 1u) return;
    fe2_market_set_low(&s->draft.due, (uint16_t)(commodity + SHORTAGE_NAME));
    s->draft.seed = s->random_state_seed;
    append(s, w, TEXT_SHORTAGE);
}

/* allegiance notices, once per market. */
static void allegiance_notice(fe2_market_state *s, const fe2_market_services *game,
                              fe2_market_writer *w)
{
    (void)game;
    if (s->allegiance == 2) {
        if (!test_and_set_flag(s, w, MARKET_FLAG_IMPERIAL)) append(s, w, TEXT_IMPERIAL);
    } else if (s->allegiance == 1) {
        if (!test_and_set_flag(s, w, MARKET_FLAG_FEDERAL)) append(s, w, TEXT_FEDERAL);
    }
}

/* chart candidate query around the current system; the query preserves the
 * draft counters it may disturb. */
static uint16_t find_candidate(fe2_market_state *s, const fe2_market_services *game, uint16_t index)
{
    uint32_t counter = s->draft.counter, threshold = s->draft.threshold;
    uint16_t found;
    s->draft.system = s->current_system;
    fe2_market_set_low(&s->draft.due, index);
    found = game->find_candidate(game->opaque, &s->draft);
    fe2_market_set_low(&s->draft.counter, (uint16_t)counter);
    fe2_market_set_low(&s->draft.threshold, (uint16_t)threshold);
    return found;
}

static uint16_t candidate_population(fe2_market_state *s, const fe2_market_services *game)
{
    uint32_t counter = s->draft.counter, threshold = s->draft.threshold;
    uint16_t population;
    s->draft.system = s->draft.lookup;
    population = game->system_trade(game->opaque, &s->draft).population;
    fe2_market_set_low(&s->draft.counter, (uint16_t)counter);
    fe2_market_set_low(&s->draft.threshold, (uint16_t)threshold);
    return population;
}

/* a record about a system from the history or a random seed. */
static void history_notice(fe2_market_state *s, const fe2_market_services *game, fe2_market_writer *w)
{
    uint16_t detail = (uint16_t)s->draft.detail;
    uint16_t selector;
    if (!find_candidate(s, game, 1)) {
        fe2_market_set_low(&s->draft.detail, detail);
        return;
    }
    fe2_market_set_low(&s->draft.detail, detail);
    s->draft.system = s->draft.lookup;
    selector = (s->draft.seed & 0x80000000u) ? TEXT_HISTORY_DEBT : TEXT_HISTORY;
    s->draft.reward = ((s->draft.seed & 3u) + 2u) * HISTORY_REWARD_UNIT;
    append(s, w, selector);
}

/* the oldest remembered system, newest last. */
static void system_history(fe2_market_state *s, const fe2_market_services *game, fe2_market_writer *w)
{
    int i;
    s->draft.detail = 2;
    for (i = FE2_HISTORY - 1; i >= 0; --i) {
        s->draft.seed = fe2_market_history(s, (unsigned)i);
        fe2_market_set_history(s, (unsigned)i, 0);
        if (s->draft.seed != 0) break;
    }
    if (i < 0) return;
    if (i == 0) fe2_market_set_low(&s->draft.detail, 1);
    history_notice(s, game, w);
}


static void random_history(fe2_market_state *s, const fe2_market_services *game, fe2_market_writer *w)
{
    s->draft.seed = game->random(game->opaque, 0);
    fe2_market_set_low(&s->draft.detail, 0);
    history_notice(s, game, w);
}

/* passengers or parcels for a populated nearby system. */
static void passengers(fe2_market_state *s, const fe2_market_services *game, fe2_market_writer *w)
{
    fe2_market_draft *d = &s->draft;
    for (fe2_market_set_low(&d->threshold, CANDIDATE_TRIES);;) {
        uint16_t index, pair, shape, count;
        uint32_t key, high;
        d->system = s->current_system;
        index = game->random_below(game->opaque, 3);
        if (find_candidate(s, game, index)) {
            key = d->lookup;
            if ((uint8_t)candidate_population(s, game) >= MIN_CANDIDATE_POPULATION) {
                d->lookup = game->random(game->opaque, &high);
                d->seed = d->lookup;
                d->system = high;
                fe2_market_set_low(&d->reward, (uint16_t)(index + 3u));
                pair = (uint16_t)((uint16_t)d->system & 0x1cu);
                fe2_market_set_low(&d->due, fe2_market_record_value_pairs[pair / 2u]);
                fe2_market_set_low(&d->reward, (uint16_t)(fe2_market_record_value_pairs[pair / 2u + 1u] *
                                                       (uint16_t)d->reward));
                shape = (int16_t)pair >= 0x1c ? (uint16_t)(pair | 0x2000u) : pair;
                fe2_market_set_low(&d->detail, shape);
                count = (uint16_t)((uint32_t)(uint16_t)d->system * (uint16_t)d->system >> 16);
                count = (uint16_t)(((uint32_t)count * count >> 16 >> 3) & 0x1e00u);
                fe2_market_set_low(&d->detail, (uint16_t)((uint16_t)d->detail + count));
                count >>= 8;
                fe2_market_set_low(&d->reward, (uint16_t)((uint16_t)d->reward +
                                                       fe2_market_record_count_factors[count / 2u]));
                if (count >= 0x10 && !((uint16_t)d->detail & (COUNT_FLAGGED_DETAIL << 8)))
                    fe2_market_set_history(s, 0, d->seed);
                fe2_market_set_low(&d->reward, (uint16_t)(fe2_market_record_reward_factors[
                    ((uint16_t)d->lookup & 7u) + s->metadata] * (uint16_t)d->reward));
                d->system = key;
                append(s, w, (d->seed & 0x1000u) ? TEXT_PASSENGER_GROUP : TEXT_PASSENGER);
                return;
            }
        }
        fe2_market_set_low(&d->threshold, (uint16_t)(d->threshold - 1u));
        if ((uint16_t)d->threshold & 0x8000u) return;
    }
}

/* a record about a distant system, dated in the past. */
static void remote_system(fe2_market_state *s, const fe2_market_services *game, fe2_market_writer *w)
{
    fe2_market_draft *d = &s->draft;
    for (fe2_market_set_low(&d->threshold, CANDIDATE_TRIES);;) {
        uint16_t index;
        uint32_t key, high, kind, counter, threshold;
        s->draft.system = s->current_system;
        index = game->random_below(game->opaque, 3);
        fe2_market_set_low(&d->due, index);
        if (game->find_candidate(game->opaque, d)) {
            key = d->lookup;
            d->system = key;
            kind = d->kind;
            counter = d->counter;
            threshold = d->threshold;
            {
                uint16_t population = game->system_trade(game->opaque, d).population;
                d->kind = kind;
                d->counter = counter;
                d->threshold = threshold;
                if ((uint8_t)population >= MIN_CANDIDATE_POPULATION) {
                    uint16_t slot, day;
                    d->seed = game->random(game->opaque, &high);
                    fe2_market_set_low(&d->detail, (uint16_t)(high & 0x3cu));
                    d->lookup = game->random(game->opaque, &high);
                    day = (uint16_t)(s->day - REMOTE_DAY_OFFSET);
                    fe2_market_set_low(&d->kind, (uint16_t)(day + 7u + (high & 0x3fu)));
                    d->system = key;
                    fe2_market_set_low(&d->reward, (uint16_t)(index * REMOTE_REWARD_UNIT));
                    slot = (uint16_t)d->detail / 2u;
                    fe2_market_set_low(&d->reward, (uint16_t)((uint16_t)d->reward +
                                                           fe2_market_local_record_words[slot]));
                    fe2_market_set_low(&d->due, (uint16_t)(fe2_market_local_record_words[slot + 1u] +
                                                        REMOTE_DETAIL));
                    fe2_market_set_low(&d->detail, (uint16_t)d->kind);
                    append(s, w, (uint16_t)(((d->lookup >> 16) & 3u) + TEXT_REMOTE));
                    return;
                }
            }
        }
        fe2_market_set_low(&d->threshold, (uint16_t)(d->threshold - 1u));
        if ((uint16_t)d->threshold & 0x8000u) return;
    }
}


static void random_text(fe2_market_state *s, const fe2_market_services *game, fe2_market_writer *w)
{
    uint32_t r = game->random(game->opaque, 0);
    s->draft.seed = r;
    append(s, w, (uint16_t)(((r >> 16) & 3u) + TEXT_RANDOM));
}


static void variant_text(fe2_market_state *s, const fe2_market_services *game, fe2_market_writer *w)
{
    uint32_t high, r = game->random(game->opaque, &high);
    s->draft.system = high;
    s->draft.seed = r;
    fe2_market_set_low(&s->draft.due, (uint16_t)((r & 7u) + TEXT_VARIANT_DETAIL));
    append(s, w, (uint16_t)(((r >> 16) & 1u) + TEXT_VARIANT));
}

typedef void (*text_generator)(fe2_market_state *, const fe2_market_services *, fe2_market_writer *);

/* dispatch table. */
static const text_generator generators[32] = {
    illegal_goods, illegal_goods, illegal_goods, illegal_goods, illegal_goods,
    illegal_goods_warning, allegiance_notice, allegiance_notice, allegiance_notice,
    allegiance_notice, system_history, random_history, random_history,
    passengers, passengers, passengers, passengers, passengers, passengers, passengers,
    passengers, passengers, passengers, passengers, shortage, shortage, shortage,
    variant_text, remote_system, remote_system, random_text, random_text
};


void fe2_market_generate_text(fe2_market_state *s, const fe2_market_services *game, fe2_market_writer *w)
{
    uint32_t high, r = game->random(game->opaque, &high);
    uint16_t counter = (uint16_t)s->draft.counter, threshold = (uint16_t)s->draft.threshold;
    s->draft.system = high;
    generators[(r & 0x3eu) / 2u](s, game, w);
    fe2_market_set_low(&s->draft.threshold, threshold);
    fe2_market_set_low(&s->draft.counter, counter);
}
