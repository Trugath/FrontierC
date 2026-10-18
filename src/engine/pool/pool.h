/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_POOL_H
#define FE2_POOL_H

/*
 * The object pool (image 0x84f..0xe6d, 0x13fd..0x151c, 0x1918..0x1951).
 *
 * A pool is 115 slot status bytes and its records (the record directory
 * places slot 0 and 1 specially, then 0x11e-byte records). The game keeps
 * three pool selections (saved, current, root), a count of reusable slots,
 * and its own flags; owner overlays are notified through
 * `fe2_pool_services`.
 */
#include <stddef.h>
#include <stdint.h>

#include "game/object.h"

enum {
    FE2_POOL_SLOTS = 115,
    FE2_POOL_LAST_SLOT = 0x72,
    FE2_POOL_FIRST_UPPER = 0x14,       /* the upper allocation's lowest slot */
    /* Slot status flags. */
    FE2_SLOT_PERIODIC = 0x01, FE2_SLOT_SECOND_UPDATE = 0x02, FE2_SLOT_PROJECTED = 0x08,
    FE2_SLOT_REUSABLE = 0x20, FE2_SLOT_FRAME_RESET = 0x40,
    FE2_RECORD_BYTES = 0x11e
};

typedef struct fe2_object_pool {
    uint8_t status[FE2_POOL_SLOTS];
    fe2_object *records[FE2_POOL_SLOTS];
} fe2_object_pool;

typedef struct fe2_pool_state {
    fe2_object_pool *saved;
    fe2_object_pool *current;
    fe2_object_pool *root;
    uint16_t reusable_count;
    uint8_t game_flags;
    uint8_t panel_flags;
    uint16_t player_capacity;
    uint16_t countdown_word;           /* cleared with the countdowns */
    uint32_t clock[2];                 /* copied into new records */
} fe2_pool_state;

/* Game flags. */
enum { FE2_GAME_NO_FRAME_RESET = 0x02, FE2_GAME_EFFECT = 0x04, FE2_GAME_STRONG_EFFECT = 0x08,
       FE2_GAME_NEARBY_EFFECT = 0x10 };

typedef struct fe2_pool_services {
    void *opaque;
    /* Owners notified when pooled objects are released. */
    unsigned (*overlay_count)(void *opaque);
    int (*receives_releases)(void *opaque, unsigned overlay);      /* metadata bit 2 */
    void (*released)(void *opaque, unsigned overlay, fe2_object *record, uint8_t slot,
                     uint32_t request);                            /* entry +0x30 */
    void (*periodic_update)(void *opaque, uint8_t owner, fe2_object *record, const uint16_t inputs[4]);
    void (*second_update)(void *opaque, uint8_t owner, fe2_object *record, const uint16_t inputs[2]);
    const uint8_t *(*model)(void *opaque, uint16_t model);         /* the descriptor */
    void (*play_level)(void *opaque, uint16_t effect, uint8_t level);
    int16_t (*distance)(void *opaque, const fe2_object *from, const fe2_object *to);
    void (*damage)(void *opaque, fe2_object *record, uint16_t kind, uint8_t source);
} fe2_pool_services;

/* Records and slots. */
fe2_object *fe2_pool_record(const fe2_object_pool *pool, uint8_t slot);       /* 0xb0, 0xb4 */
void fe2_pool_clear(fe2_pool_state *state);
void fe2_pool_clear_reusable(fe2_pool_state *state);
void fe2_pool_release_quietly(fe2_pool_state *state, fe2_object *record);
void fe2_pool_release(fe2_pool_state *state, const fe2_pool_services *game, fe2_object *record,
                      uint32_t request);
void fe2_pool_set_slot_flags(fe2_pool_state *state, const fe2_object *record, uint8_t flags);

/* Allocation: a slot with `flags`, the template copied in, and (for a
 * nonzero model) the record initialized from it. Returns the slot (0 when
 * none is free; *record is then 0). */
typedef struct fe2_new_record {
    uint8_t flags;
    const fe2_object *template_record;
    uint16_t model;
} fe2_new_record;
uint8_t fe2_pool_allocate_upper(fe2_pool_state *state, const fe2_pool_services *game, const fe2_new_record *request,
                                fe2_object **record);
uint8_t fe2_pool_allocate(fe2_pool_state *state, const fe2_pool_services *game, const fe2_new_record *request,
                          fe2_object **record);
/* Copy a template into `record` (unless it is the record) and initialize
 * it from a nonzero model; the record takes the template's handle. */
uint8_t fe2_pool_copy_template(fe2_pool_state *state, const fe2_pool_services *game, const fe2_new_record *request,
                               fe2_object *record);

/* Visitors. */
typedef void (*fe2_record_visitor)(void *context, fe2_object *record, uint16_t status_pair);
void fe2_pool_visit(fe2_pool_state *state, fe2_object_pool *pool, fe2_record_visitor visit, void *context);
void fe2_pool_visit_on_screen(fe2_pool_state *state, fe2_object_pool *pool, fe2_record_visitor visit,
                              void *context);
void fe2_pool_reset_countdowns(fe2_pool_state *state, fe2_object_pool *pool);
void fe2_pool_periodic_updates(fe2_pool_state *state, const fe2_pool_services *game, fe2_object_pool *pool,
                               const uint16_t inputs[4]);
void fe2_pool_second_updates(fe2_pool_state *state, const fe2_pool_services *game, fe2_object_pool *pool,
                             const uint16_t inputs[2]);
void fe2_pool_reset_frame(fe2_pool_state *state, fe2_object_pool *pool);

/* Equipment effects. */
void fe2_pool_equipment_effect(fe2_pool_state *state, const fe2_pool_services *game, fe2_object *record);
void fe2_pool_nearby_effect(fe2_pool_state *state, const fe2_pool_services *game, fe2_object *record);

/* Generated drive-class adjustments. */
extern const uint16_t *fe2_drive_adjustments;
int fe2_pool_bind(const uint8_t *image, size_t count);

#endif
