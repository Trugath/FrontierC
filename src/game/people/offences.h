/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_OFFENCES_OFFENCES_H
#define FE2_OFFENCES_OFFENCES_H

/*
 * Offences and police (offences).
 *
 * - An offence (one of eleven) adds a fine to the local fine, a score to the
 *   authority's offence score, and marks the offence in the authority's
 *   record (the list the status profile shows). It may pick the police station
 *   that responds, tell the player, and alert the police (music).
 * - A police source (station) launches patrol ships in batches. Between
 *   batches its launch progress rises or falls with the frame step.
 * - Events: jettisoned cargo is dumping; a smuggling check fines illegal
 *   cargo, then sets the manual-check question.
 *
 * Objects, the market/people/station overlays, randomness, sound, music and the text
 * formatter come through `fe2_offence_services`.
 */
#include <stdint.h>

#include "game/call_args.h"
#include "game/object.h"
#include "game/text_table.h"

enum {
    FE2_OFFENCES = 11, FE2_OWNERSHIP_CHALLENGES = 301, FE2_OFFENCE_COMMODITIES = 31,
    FE2_OFFENCE_AUTHORITIES = 3,
    /* Offence flags. */
    FE2_OFFENCE_ALERT = 1, FE2_OFFENCE_MESSAGE = 2, FE2_OFFENCE_FIND_POLICE = 4,
    /* Launch phases. */
    FE2_LAUNCH_READY = 0, FE2_LAUNCH_RISING = 1, FE2_LAUNCH_FALLING = 0xff,
    /* Offences raised by events. */
    FE2_OFFENCE_SMUGGLING = 7, FE2_OFFENCE_DUMPING = 8
};

typedef struct fe2_offence {
    uint16_t score;                   /* added to the authority's offence score */
    int16_t fine;                     /* the fine is 10 * its unsigned word */
    uint16_t flags;                   /* FE2_OFFENCE_* */
    uint16_t threshold;               /* police search threshold (plus the law level) */
} fe2_offence;

/* A manual-check question: word, line and page; the answer letter less 'A'. */
typedef struct fe2_ownership_challenge {
    uint8_t word, line, page, answer;
} fe2_ownership_challenge;

typedef struct fe2_offence_state {
    uint8_t police_flags;             /* bit 0: police alerted */
    uint8_t launch_phase;             /* FE2_LAUNCH_* */
    fe2_object *police_source;        /* the station that responds */
    /* The police search. */
    int8_t lowest_level;              /* lowest level of a police body or patrol */
    int8_t station_level;
    uint8_t lowest_handle;
    fe2_object *police_station;       /* the lowest-level police station */
    /* Fines and records. */
    uint32_t local_fine;
    uint16_t law_level;
    uint16_t legal_authority;
    uint16_t offence_score[FE2_OFFENCE_AUTHORITIES];
    uint16_t offences[FE2_OFFENCE_AUTHORITIES];      /* bit n: offence n */
    uint8_t player_handle;
    uint8_t fined_handle;
    uint8_t alert_music;              /* nonzero selects music 0xfffe */
    uint32_t frame_step;
    /* Cargo (commodity 0..30). */
    const uint8_t *trade_codes;       /* bit 7: illegal */
    const uint16_t *cargo;            /* tonnes */
    fe2_object *smuggler;
    /* The manual check. */
    uint16_t challenge[3];            /* word, line, page */
    uint8_t expected_letter;
    fe2_object_list *objects;
    fe2_object_list *creation_objects;
} fe2_offence_state;

typedef void (*fe2_offence_visitor)(void *context, fe2_object *object);

typedef struct fe2_offence_services {
    void *opaque;
    uint32_t (*random)(void *opaque);
    uint16_t (*random_below)(void *opaque, uint16_t bound);
    void (*visit_objects)(void *opaque, fe2_object_list *objects, fe2_offence_visitor visitor,
                          void *context);
    void (*show_message)(void *opaque, uint16_t text, const fe2_call_args *args); /* station */
    void (*play_effect)(void *opaque, uint16_t effect);
    void (*check_music)(void *opaque);
    void (*select_music)(void *opaque, uint16_t track);
    /* market reference service 9 (BX): prepare a child of the object in
     * parameter 9; parameter 0 the kind, 7 the attempt. */
    void (*prepare_child)(void *opaque, const fe2_call_args *args);         /* market */
    /* people creation (parameter 7 = 5): the new object, or null. */
    fe2_object *(*create_object)(void *opaque, const fe2_call_args *args);  /* people */
    void (*format_table_text)(void *opaque, uint16_t selector, const fe2_text_table *table,
                              fe2_text_out *out);
} fe2_offence_services;

/* What the police-search callback works on. */
typedef struct fe2_offence_pass {
    fe2_offence_state *state;
    const fe2_offence_services *game;
} fe2_offence_pass;

/* 0x76: event 1 resets the alert, 5 checks for smuggling, 0x11 is dumping. */
void fe2_offence_event(fe2_offence_state *state, const fe2_offence_services *game, uint8_t event,
                       fe2_object *subject);
/* 0x7a: the police source's launch progress. */
void fe2_offence_progress(fe2_offence_state *state);
/* 0x8a: commit offence `offence` (unchecked, 0..10) by `subject` (scratch
 * 0x7e0; the police station found replaces it). */
void fe2_offence_commit(fe2_offence_state *state, const fe2_offence_services *game,
                        uint16_t offence, fe2_object *subject);
/* 0x92: offence text `selector` (AX & 0x3fe). */
void fe2_offence_text(const fe2_offence_services *game, uint16_t selector, fe2_text_out *out);
/* 0x96: the police source launches. */
void fe2_offence_launch(fe2_offence_state *state, const fe2_offence_services *game);

/* the police-search callback; `pass` is a fe2_offence_pass. */
void fe2_offence_search_object(void *pass, fe2_object *object);

/* Native tables (tools/offences_tables_c.py). */
extern const fe2_offence *fe2_offences_offences;
extern const fe2_ownership_challenge *fe2_offences_challenges;
extern const uint8_t *fe2_offences_text_directory;

#endif
