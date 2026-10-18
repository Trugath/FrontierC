/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_MINING_MINING_H
#define FE2_MINING_MINING_H

/*
 * Mining machines (mining).
 *
 * The player can deploy mining machines on a body, where they fill a
 * hopper from the body's reserve. Each machine has a record in the resident
 * mining block:
 * - the system it is in;
 * - its hopper and reserve;
 * - its position and object handle;
 * - its yield category;
 * - the name of the body.
 *
 * The overlay handles:
 * - deployment, with the assessment of the body;
 * - loading the hopper into cargo, and recovering an empty machine;
 * - free record slots;
 * - the daily hopper tick;
 * - recreating the machines on arrival in their system;
 * - a machine's deploy/recover progress, and removing its record.
 *
 * Objects, the catalogue/station/offences overlays, randomness, events and the text
 * formatter come through `fe2_mining_services`.
 */
#include <stdint.h>

#include "game/call_args.h"
#include "game/object.h"
#include "game/text_table.h"

enum {
    FE2_MINING_RECORD_BYTES = 0x34,
    FE2_MINING_RECORDS = 8,
    FE2_MINING_YIELDS = 27, FE2_MINING_CATEGORIES = 9, FE2_MINING_COMMODITIES = 31
};

/* Mining record fields (native layout). */
enum fe2_mining_field {
    FE2_MINING_SYSTEM = 0x00,         /* dword; zero marks a free slot */
    FE2_MINING_HOPPER = 0x04,         /* tonnes waiting (up to 10 by the tick) */
    FE2_MINING_RESERVE = 0x05,        /* tonnes left in the body */
    FE2_MINING_POSITION = 0x06,       /* three low dwords, three high dwords */
    FE2_MINING_OBJECT = 0x1e,         /* the machine's object handle */
    FE2_MINING_CATEGORY = 0x1f,       /* yield category */
    FE2_MINING_NAME = 0x20            /* the body's name, NUL-terminated */
};

/*
 * The mining block : FE2_MINING_RECORDS records in their native
 * layout, plus a sentinel record after the last, which the original reads during
 * compaction. Slot allocation is bounded to FE2_MINING_RECORDS. Removal shifts the following records down one
 * slot except the removed record's first byte (see mining_records.c).
 */
typedef struct fe2_mining_space {
    uint8_t bytes[(FE2_MINING_RECORDS + 1) * FE2_MINING_RECORD_BYTES];
} fe2_mining_space;

/* A yield: the category, the assessment text and the reserve (its low
 * byte is used). */
typedef struct fe2_mining_yield {
    uint16_t category, assessment_text, reserve;
} fe2_mining_yield;

typedef struct fe2_mining_state {
    fe2_mining_space records;
    uint16_t count;
    uint16_t free_count;
    uint32_t current_system;
    uint32_t frame_step;
    uint16_t cargo_capacity;
    uint16_t cargo_used;
    uint16_t cargo[FE2_MINING_COMMODITIES];
    uint8_t selected_machine;
    uint8_t ship_flags;               /* bit 4: a machine is selected */
    uint8_t flight_state;             /* 0x68: machines can be selected */
    uint8_t activity;                 /* 0x16 while machines are out */
    fe2_object *ship;
    fe2_object_list *objects;
    fe2_object_list *creation_objects;
} fe2_mining_state;

typedef void (*fe2_mining_visitor)(void *context, fe2_object *object);

/* What the name callback works on. */
typedef struct fe2_mining_pass {
    fe2_mining_state *state;
    unsigned record;                  /* the record being recreated */
    fe2_object *machine;              /* the machine just created */
} fe2_mining_pass;

typedef struct fe2_mining_services {
    void *opaque;
    /* Create an object: parameter 0 its kind (0xd), 1 its model, 8 the
     * ship. The new object, or null. */
    fe2_object *(*create_object)(void *opaque, const fe2_call_args *args);
    /* Add the signed dword deltas in parameters 0..2 to the position. */
    void (*add_position)(void *opaque, fe2_object *object, const fe2_call_args *args);
    /* Link a new object into the list in parameter 12 (8: the object). */
    void (*attach_object)(void *opaque, fe2_object *object, const fe2_call_args *args);
    fe2_object *(*lookup_object)(void *opaque, uint8_t handle, const fe2_call_args *args);
    /* Visit with a fe2_mining_pass (the original passes the record in
     * scratch 0x7e4). */
    void (*visit_objects)(void *opaque, fe2_object_list *objects, fe2_mining_visitor visitor,
                          fe2_mining_pass *pass);
    /* A random word; the original tests its sign. */
    uint16_t (*random)(void *opaque);
    uint16_t (*random_below)(void *opaque, uint16_t bound);
    /* catalogue profile (AL=0) of the system in parameter 1: its field 0x5. */
    uint16_t (*system_profile)(void *opaque, const fe2_call_args *args);    /* catalogue */
    /* station message `text`; some messages also take an amount (DX). */
    void (*show_message)(void *opaque, uint16_t text, uint16_t amount, const fe2_call_args *args);
    void (*commit_offence)(void *opaque, uint16_t offence);                 /* offences */
    void (*post_event)(void *opaque, uint16_t event);
    void (*release_object)(void *opaque, fe2_object *object);
    void (*format_table_text)(void *opaque, uint16_t selector, const fe2_text_table *table,
                              fe2_text_out *out);
} fe2_mining_services;

/* Actions (0x8a AX). */
enum { FE2_MINING_DEPLOY = 0, FE2_MINING_LOAD = 1, FE2_MINING_ADD_SLOT = 2, FE2_MINING_REMOVE_SLOT = 3 };

/* 0x6e: no records. */
void fe2_mining_reset(fe2_mining_state *state);
/* 0x76: event 0x10 ticks the hoppers, 2 recreates the machines of the current
 * system. */
void fe2_mining_event(fe2_mining_state *state, const fe2_mining_services *game, uint8_t event);
/* 0x82: deploy/recover progress of `machine` (scratch 0x7dc). */
void fe2_mining_progress(fe2_mining_state *state, const fe2_mining_services *game,
                         fe2_object *machine);
/* 0x8a: an action; removing a slot returns nonzero (AX 0xffff) when one was
 * free. Other actions return 0. Other values remove all free slots. */
int fe2_mining_action(fe2_mining_state *state, const fe2_mining_services *game, uint16_t action);
/* 0x92: mining text `selector` (AX & 0x3fe). */
void fe2_mining_text(const fe2_mining_services *game, uint16_t selector, fe2_text_out *out);
/* 0x9a: remove `machine`'s record (only for request 0, a machine object). */
void fe2_mining_remove(fe2_mining_state *state, const fe2_mining_services *game, uint16_t request,
                       fe2_object *machine);

/* links the new machine to the body named in its record. */
void fe2_mining_name_object(void *pass, fe2_object *object);

/* Native tables (tools/mining_tables_c.py). */
extern const fe2_mining_yield *fe2_mining_yields;
extern const uint8_t *fe2_mining_category_cargo;
extern const uint8_t *fe2_mining_text_directory;

#endif
