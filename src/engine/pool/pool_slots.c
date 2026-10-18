/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* slots, release and allocation. */
#include "pool.h"

fe2_object *fe2_pool_record(const fe2_object_pool *pool, uint8_t slot) { return pool->records[slot]; }

/* The current pool becomes the saved one, emptied. */
void fe2_pool_clear(fe2_pool_state *s)
{
    unsigned slot;
    s->saved = s->current;
    for (slot = 0; slot < FE2_POOL_SLOTS; ++slot) s->saved->status[slot] = 0;
}

/* The root pool becomes current, without its reusable slots. */
void fe2_pool_clear_reusable(fe2_pool_state *s)
{
    int slot;
    s->reusable_count = 0;
    s->current = s->root;
    for (slot = FE2_POOL_LAST_SLOT; slot >= 0; --slot)
        if (s->root->status[slot] & FE2_SLOT_REUSABLE) s->root->status[slot] = 0;
}

void fe2_pool_release_quietly(fe2_pool_state *s, fe2_object *record)
{
    uint8_t *status = &s->saved->status[record->handle];
    if (*status & FE2_SLOT_REUSABLE) --s->reusable_count;
    *status = 0;
}

/* Owner overlays that ask for releases are told before the slot is freed;
 * the saved pool then becomes current. (A reusable slot is outside this
 * path: the original returns through the quiet release's 16-bit tail.) */
void fe2_pool_release(fe2_pool_state *s, const fe2_pool_services *game, fe2_object *record, uint32_t request)
{
    uint8_t slot = record->handle;
    unsigned overlay, count = game->overlay_count(game->opaque);
    for (overlay = 0; overlay < count; ++overlay)
        if (game->receives_releases(game->opaque, overlay))
            game->released(game->opaque, overlay, record, slot, request);
    s->saved->status[slot] = 0;
    s->current = s->saved;
}

void fe2_pool_set_slot_flags(fe2_pool_state *s, const fe2_object *record, uint8_t flags)
{
    s->saved->status[record->handle] = flags;
}

uint8_t fe2_pool_initialize_record(fe2_pool_state *s, const fe2_pool_services *game, const fe2_new_record *request,
                                   fe2_object *record, uint8_t handle, int copy);

static int last_free(const fe2_object_pool *pool)
{
    int slot;
    for (slot = FE2_POOL_LAST_SLOT; slot >= 0; --slot)
        if (pool->status[slot] == 0) return slot;
    return -1;
}

static int last_reusable(const fe2_object_pool *pool)
{
    int slot;
    for (slot = FE2_POOL_LAST_SLOT; slot >= 0; --slot)
        if (pool->status[slot] & FE2_SLOT_REUSABLE) return slot;
    return -1;
}

static uint8_t take(fe2_pool_state *s, const fe2_pool_services *game, const fe2_new_record *request, int slot,
                    fe2_object **record)
{
    s->saved->status[slot] = request->flags;
    if (request->flags & FE2_SLOT_REUSABLE) ++s->reusable_count;
    *record = s->saved->records[slot];
    return fe2_pool_initialize_record(s, game, request, *record, (uint8_t)slot, 1);
}

/* A free slot from 0x14 up (the highest first), else a reusable one there. */
uint8_t fe2_pool_allocate_upper(fe2_pool_state *s, const fe2_pool_services *game, const fe2_new_record *request,
                                fe2_object **record)
{
    int slot = last_free(s->saved);
    if (slot >= FE2_POOL_FIRST_UPPER) return take(s, game, request, slot, record);
    slot = last_reusable(s->saved);
    if (slot >= FE2_POOL_FIRST_UPPER) {
        --s->reusable_count;
        return take(s, game, request, slot, record);
    }
    *record = 0;
    return 0;
}

/* Any slot but 0 (the highest first), else a reusable one. */
uint8_t fe2_pool_allocate(fe2_pool_state *s, const fe2_pool_services *game, const fe2_new_record *request,
                          fe2_object **record)
{
    int slot = last_free(s->saved);
    if (slot > 0) return take(s, game, request, slot, record);
    slot = last_reusable(s->saved);
    if (slot > 0) {
        --s->reusable_count;
        return take(s, game, request, slot, record);
    }
    *record = 0;
    return 0;
}
