/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_MARKET_MARKET_H
#define FE2_MARKET_MARKET_H

/*
 * Commodities, markets and reference objects (market).
 *
 * Each station or city of the current system (up to 18) has a market
 * record: prices and supplies of the 31 commodities, bulletin-board text
 * records and category (equipment/ship) records. The system also keeps a
 * list of timed requests. Reference objects (stations, docking slots)
 * attach and release craft. Everything else comes through
 * `fe2_market_services`.
 */
#include <stdint.h>

#include "game/call_args.h"
#include "game/text_table.h"
#include "game/object.h"

enum {
    FE2_COMMODITIES = 31,
    FE2_MAX_MARKETS = 18,
    FE2_MARKET_SLOTS = 19,            /* one more record is written and searched */
    FE2_MARKET_RECORD_BYTES = 0x1da,
    FE2_MARKET_TEXTS = 18, FE2_MARKET_TEXT_BYTES = 16,
    FE2_MARKET_CATEGORIES = 14, FE2_MARKET_CATEGORY_BYTES = 4,
    FE2_TIMED_RECORDS = 25,
    FE2_TIMED_RECORD_BYTES = 24,
    FE2_HISTORY = 4,
    /* Serialized market block fields, by byte position. */
    FE2_MARKET_BLOCK_BYTES = 0x23ca,
    FE2_SUMMARY_AT = 0x2154,          /* count, high, low words */
    FE2_TIMED_AT = 0x215a,
    FE2_HISTORY_AT = 0x23b6,          /*, newest first */
    FE2_PROFILE_WORD_AT = 0x23c6
};

/* Market record fields (native layout; text and category writes can run
 * into the counts and the next record, as in the original). */
enum fe2_market_field {
    FE2_MARKET_COMMODITIES = 0x000,   /* per commodity: supply word, price word */
    FE2_MARKET_OBJECT = 0x07c,        /* station object handle */
    FE2_MARKET_FLAGS = 0x07d,
    FE2_MARKET_TEXTS_AT = 0x07e,
    FE2_MARKET_TEXT_COUNT = 0x19e,
    FE2_MARKET_CATEGORIES_AT = 0x1a0,
    FE2_MARKET_CATEGORY_COUNT = 0x1d8
};

/* A text record: selector and five values composed in the draft. */
enum fe2_market_text_field {
    FE2_TEXT_SELECTOR = 0x0, FE2_TEXT_WORD_2 = 0x2, FE2_TEXT_WORD_4 = 0x4,
    FE2_TEXT_WORD_6 = 0x6, FE2_TEXT_DWORD_8 = 0x8, FE2_TEXT_DWORD_C = 0xc
};

/*
 * The market block : 18 market records, then the request
 * summary words, the 25 timed requests with the zero that ends them, the
 * history of visited systems and a profile word. The original keeps them
 * contiguous and overlapping: the market slot after the 18th (written by
 * collection, searched by lookup) is the summary and request area, and a
 * full request list's compaction reads the history as request data.
 */
typedef struct fe2_market_space {
    uint8_t bytes[FE2_MARKET_BLOCK_BYTES];
} fe2_market_space;

/* Timed request fields (24-byte records). The selector is the low word of
 * the head dword; storing one does not clear the high word. */
enum fe2_timed_field {
    FE2_TIMED_HEAD = 0, FE2_TIMED_SYSTEM = 4, FE2_TIMED_DUE = 8, FE2_TIMED_REWARD = 12,
    FE2_TIMED_DETAIL = 16, FE2_TIMED_SEED = 20
};


/*
 * Values the record generators compose. The original builds every record
 * in the resident scratch block; a generator stores only the
 * values it sets, so the rest carry over from earlier work. Resident chart
 * and catalogue queries also return their results here.
 */
typedef struct fe2_market_draft {
    uint32_t lookup;          /* query result; doubled population band */
    uint32_t system;          /* system key; text dword +8 */
    uint32_t due;             /* due day; text word +2 */
    uint32_t reward;          /*; text word +4 */
    uint32_t detail;          /*; text word +6 */
    uint32_t kind;            /* category kind */
    uint32_t counter;         /* loop counter */
    uint32_t threshold;       /* loop counter / summary threshold */
    uint32_t request_seed;    /* timed record seed */
    uint32_t seed;            /* text dword +0xc */
} fe2_market_draft;

typedef struct fe2_market_state {
    fe2_market_draft draft;
    fe2_market_space markets;          /* markets, requests, history */
    uint16_t market_count;
    uint8_t trade_codes[FE2_COMMODITIES]; /* bits 0..2 strength, 3 export, 7 illegal */
    uint16_t population_band;
    uint16_t metadata;
    uint16_t category_0_rate;
    uint16_t category_1_rate;
    uint16_t resource_richness;
    uint16_t allegiance;
    uint32_t station_seed;
    uint32_t day;
    uint32_t random_state_seed;
    uint32_t current_system;
    uint8_t player;                    /* player object handle */
    uint8_t player_slot;
    uint8_t flight_state;
    uint32_t event_base;
    uint32_t local_offence_fine;
    uint8_t music_enabled;
    uint16_t current_music;
    fe2_object_list *objects;
} fe2_market_state;

/* catalogue system trade data (mode 1). */
typedef struct fe2_system_trade {
    uint16_t population;      /* AX */
    uint16_t metadata;        /* BX */
    const uint8_t *codes;     /* 31 commodity trade codes */
} fe2_system_trade;

typedef void (*fe2_market_visitor)(void *context, const fe2_object *object);

typedef struct fe2_market_services {
    void *opaque;
    uint32_t (*random)(void *opaque, uint32_t *high);
    uint16_t (*random_below)(void *opaque, uint16_t bound);
    void (*visit_objects)(void *opaque, fe2_object_list *objects,
                          fe2_market_visitor visitor, void *context);
    fe2_object *(*lookup_object)(void *opaque, uint8_t handle, const fe2_call_args *args);
    /* catalogue queries for the system key in draft->system; results in the draft. */
    void (*system_profile)(void *opaque, fe2_market_draft *draft);          /* mode 0 */
    fe2_system_trade (*system_trade)(void *opaque, fe2_market_draft *draft); /* mode 1 */
    void (*system_summary)(void *opaque, fe2_market_draft *draft);          /* mode 2 */
    /* chart candidate system near draft->system (index draft->due): nonzero
     * when found, key in draft->lookup. */
    uint16_t (*find_candidate)(void *opaque, fe2_market_draft *draft);
    void (*show_message)(void *opaque, uint16_t text, const fe2_call_args *args); /* station */
    void (*select_music)(void *opaque, uint16_t track);
    void (*format_text)(void *opaque, uint16_t selector, const fe2_text_table *table,
                        fe2_text_out *out);
    void (*format_number)(void *opaque, uint16_t format, const fe2_call_args *args,
                          fe2_text_out *out);
} fe2_market_services;

/* The market profile word: the system's law level, among its other fields. */
uint16_t fe2_market_profile_word(const fe2_market_state *state);
/* 0x6e: AL=1 starts a game: clear history, collect stations, set up markets. */
void fe2_market_initialize(fe2_market_state *state, const fe2_market_services *game,
                           uint8_t request);
/* 0x76: event 2 (new system) and 0xf (new day). */
void fe2_market_event(fe2_market_state *state, const fe2_market_services *game, uint16_t event);
/* 0x82: store four motion words in the object's orbit fields. */
void fe2_market_store_motion(fe2_object *object, uint16_t ax, uint16_t dx, uint16_t bx,
                             uint16_t cx);
/* 0x92: commodity name `selector` (AX & 0x3fe) or, for 0x3e, the price list
 * of the market at `market` (byte position of its commodity entries). */
void fe2_market_format(fe2_market_state *state, const fe2_market_services *game,
                       uint16_t selector, uint32_t market, fe2_text_out *out);
/* callback: count a station and record its handle. */
void fe2_market_collect(fe2_market_state *state, const fe2_object *object);

/* 0x8a reference-object services selected by `request` (BX). */
typedef struct fe2_reference_call {
    uint16_t request;         /* BX */
    uint16_t ax;              /* AL object handle; AX returned */
    fe2_object *actor;
    fe2_object *target;
    uint16_t market;          /* request 0: index of the matching market */
} fe2_reference_call;

void fe2_market_reference(fe2_market_state *state, const fe2_market_services *game,
                          fe2_reference_call *call);
/* Interior entries: 0x15c3 attach, 0x1787 claim, 0x1852 player message (BX). */
void fe2_market_attach(fe2_market_state *state, fe2_reference_call *call);
void fe2_market_claim(fe2_reference_call *call);
void fe2_market_player_message(fe2_market_state *state, const fe2_market_services *game,
                               uint8_t handle, uint16_t text, fe2_object *target);

#endif
