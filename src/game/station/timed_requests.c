/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* timed requests (deliveries, missions) the system offers,
 * kept for a number of days in the resident request list. */
#include "market_internal.h"

enum {
    TIMED_SLOTS = FE2_TIMED_RECORDS,
    MIN_DUE_DAYS = 0x0e, DUE_SPREAD_MASK = 0x3f,
    REQUEST_9856 = 0x9856, REQUEST_9854 = 0x9854, REQUEST_9855 = 0x9855,
    REQUEST_VARIANT = 0x9853,
    DETAIL_9856 = 0x99a5, DETAIL_9854 = 0x994f,
    VARIANT_8_DETAIL = 0x9957, VARIANT_4_DETAIL = 0x995f, VARIANT_4T_DETAIL = 0x9963,
    VARIANT_REWARD_UNIT = 0x32, VARIANT_DUE_SPREAD = 0x0e, VARIANT_MIN_DUE = 0x0a
};

/* the first free slot of 25 takes the draft; a zero head follows. */
static void store(fe2_market_state *s, uint16_t selector)
{
    const fe2_market_draft *d = &s->draft;
    unsigned i;
    for (i = 0; i < TIMED_SLOTS; ++i) {
        if (fe2_market_timed(s, i, FE2_TIMED_HEAD) != 0) continue;
        /* Assembly image 0de7: `mov word [si], ax`. The slot was accepted
         * because the whole head dword was zero, and this store does not
         * clear a high word that is already there. */
        fe2_market_set_word(s, FE2_TIMED_AT + i * FE2_TIMED_RECORD_BYTES + FE2_TIMED_HEAD, selector);
        fe2_market_set_timed(s, i, FE2_TIMED_SYSTEM, d->system);
        fe2_market_set_timed(s, i, FE2_TIMED_DUE, d->due);
        fe2_market_set_timed(s, i, FE2_TIMED_REWARD, d->reward);
        fe2_market_set_timed(s, i, FE2_TIMED_DETAIL, d->detail);
        fe2_market_set_timed(s, i, FE2_TIMED_SEED, d->request_seed);
        fe2_market_set_timed(s, i + 1, FE2_TIMED_HEAD, 0);
        return;
    }
}

/* a candidate system of this allegiance; on success its population
 * replaces the low word of draft->lookup. Returns whether one was found. */
static int candidate_population(fe2_market_state *s, const fe2_market_services *game)
{
    fe2_market_draft *d = &s->draft;
    uint32_t key, kind, counter, threshold, seed;
    d->system = fe2_market_allegiance_candidates[s->allegiance];
    fe2_market_set_low(&d->due, game->random_below(game->opaque, 3));
    if (!game->find_candidate(game->opaque, d)) return 0;
    key = d->lookup;
    d->system = key;
    kind = d->kind;
    counter = d->counter;
    threshold = d->threshold;
    seed = d->request_seed;
    fe2_market_set_low(&d->lookup, game->system_trade(game->opaque, d).population);
    d->request_seed = seed;
    d->threshold = threshold;
    d->counter = counter;
    d->kind = kind;
    d->system = key;
    return 1;
}

typedef struct request_kind {
    uint16_t selector, minimum_threshold, detail_base;
    uint8_t minimum_population;
    const uint16_t *rewards;
} request_kind;


static void population_request(fe2_market_state *s, const fe2_market_services *game,
                               const request_kind *k)
{
    fe2_market_draft *d = &s->draft;
    uint32_t key, high;
    if ((uint16_t)d->threshold <= k->minimum_threshold) return;
    candidate_population(s, game);
    if ((uint8_t)d->lookup < k->minimum_population) return;
    key = d->system;
    d->request_seed = game->random(game->opaque, &high);
    fe2_market_set_low(&d->detail, (uint16_t)(high & 0x0eu));
    game->random(game->opaque, &high);
    d->due = s->day + MIN_DUE_DAYS + (high & DUE_SPREAD_MASK);
    d->system = key;
    d->reward = k->rewards[(uint16_t)d->detail / 2u];
    fe2_market_set_low(&d->detail, (uint16_t)(((uint16_t)d->detail >> 1) + k->detail_base));
    store(s, k->selector);
}

static void request_a(fe2_market_state *s, const fe2_market_services *g)
{
    request_kind kind = {REQUEST_9856, 0x100, DETAIL_9856, 3, fe2_market_request_9856_rewards};
    population_request(s, g, &kind);
}
static void request_b(fe2_market_state *s, const fe2_market_services *g)
{
    request_kind kind = {REQUEST_9854, 0x271, DETAIL_9854, 1, fe2_market_request_9854_rewards};
    population_request(s, g, &kind);
}
static void request_c(fe2_market_state *s, const fe2_market_services *g)
{
    request_kind kind = {REQUEST_9855, 0x510, DETAIL_9854, 1, fe2_market_request_9855_rewards};
    population_request(s, g, &kind);
}

/* a request from a populated system of the same allegiance. */
static void variant_request(fe2_market_state *s, const fe2_market_services *game, uint16_t span,
                            uint16_t detail)
{
    fe2_market_draft *d = &s->draft;
    uint16_t offset, index, value;
    uint32_t key;

    fe2_market_set_low(&d->detail, detail);
    offset = game->random_below(game->opaque, span);
    d->system = s->current_system;
    index = game->random_below(game->opaque, 3);
    fe2_market_set_low(&d->due, index);
    if (!game->find_candidate(game->opaque, d)) return;
    key = d->lookup;
    d->system = key;
    if ((uint8_t)game->system_trade(game->opaque, d).population < 3) return;
    fe2_market_set_low(&d->due, (uint16_t)((uint16_t)d->due >> 6));
    if ((uint16_t)d->due != s->allegiance) return;
    d->request_seed = game->random(game->opaque, 0);
    value = (uint16_t)(game->random_below(game->opaque, VARIANT_DUE_SPREAD) + VARIANT_MIN_DUE);
    d->due = (uint32_t)(int32_t)(int16_t)value + s->day;
    d->system = key;
    fe2_market_set_low(&d->reward, index);
    fe2_market_set_low(&d->detail, (uint16_t)(detail + offset));
    value = (uint16_t)(((uint16_t)d->reward << 1) + (uint16_t)d->detail - VARIANT_8_DETAIL + 3u);
    d->reward = (uint32_t)value * VARIANT_REWARD_UNIT;
    store(s, REQUEST_VARIANT);
}

static void variant_8(fe2_market_state *s, const fe2_market_services *g)
{
    variant_request(s, g, 8, VARIANT_8_DETAIL);
}

static void variant_4(fe2_market_state *s, const fe2_market_services *g)
{
    if ((uint16_t)s->draft.threshold <= 0x10) return;
    variant_request(s, g, 4, VARIANT_4_DETAIL);
}

static void variant_4_threshold(fe2_market_state *s, const fe2_market_services *g)
{
    if ((uint16_t)s->draft.threshold <= 0x51) return;
    variant_request(s, g, 4, VARIANT_4T_DETAIL);
}

typedef void (*request_generator)(fe2_market_state *, const fe2_market_services *);

/* dispatch table. */
static const request_generator generators[16] = {
    variant_8, variant_8, variant_8, variant_8, variant_8, variant_8, variant_8, variant_8,
    variant_4, variant_4, variant_4_threshold, variant_4_threshold,
    request_a, request_a, request_b, request_c
};

/* the threshold is the nonnegative difference of the summary words. */
void fe2_market_generate_request(fe2_market_state *s, const fe2_market_services *game)
{
    uint16_t counter = (uint16_t)s->draft.counter;
    uint16_t pick = (uint16_t)(game->random(game->opaque, 0) & 0x1eu);
    uint16_t high = fe2_market_summary(s, SUMMARY_HIGH), low = fe2_market_summary(s, SUMMARY_LOW);
    uint16_t difference = high >= low ? (uint16_t)(high - low) : 0;
    fe2_market_set_low(&s->draft.threshold, difference);
    generators[pick / 2u](s, game);
    fe2_market_set_low(&s->draft.counter, counter);
}
