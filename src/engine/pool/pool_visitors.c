/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* visitors and owner updates, 0x1918 the frame reset. */
#include "pool.h"

enum { SCREEN_LEFT = -3, SCREEN_WIDTH = 0x13c, SCREEN_TOP = 4, SCREEN_HEIGHT = 0x9f, OFF_SCREEN = 0x8000 };

/* Records in use and not reusable, from the last slot down. The visitor
 * also gets the slot's status byte and the next one's as a word. */
void fe2_pool_visit(fe2_pool_state *s, fe2_object_pool *pool, fe2_record_visitor visit, void *context)
{
    int slot;
    s->current = s->saved = pool;
    for (slot = FE2_POOL_LAST_SLOT; slot >= 0; --slot) {
        uint8_t status = pool->status[slot];
        if (status == 0 || (status & FE2_SLOT_REUSABLE)) continue;
        visit(context, pool->records[slot],
              (uint16_t)(status | (slot < FE2_POOL_LAST_SLOT ? pool->status[slot + 1] : 0) << 8));
    }
}

/* Projected records whose screen position lies within the view (with a
 * margin). */
void fe2_pool_visit_on_screen(fe2_pool_state *s, fe2_object_pool *pool, fe2_record_visitor visit, void *context)
{
    int slot;
    s->current = s->saved = pool;
    for (slot = FE2_POOL_LAST_SLOT; slot >= 0; --slot) {
        uint8_t status = pool->status[slot];
        fe2_object *o;
        if (!(status & FE2_SLOT_PROJECTED) || (status & FE2_SLOT_REUSABLE)) continue;
        o = pool->records[slot];
        if ((uint16_t)(o->screen_y - SCREEN_TOP) >= SCREEN_HEIGHT) continue;
        if ((uint16_t)(o->screen_x - SCREEN_LEFT) >= SCREEN_WIDTH) continue;
        visit(context, o, 0);
    }
}

static void clear_countdown(void *context, fe2_object *record, uint16_t status_pair)
{
    (void)context;
    (void)status_pair;
    record->update_countdown = 0;
}

void fe2_pool_reset_countdowns(fe2_pool_state *s, fe2_object_pool *pool)
{
    s->countdown_word = 0;
    fe2_pool_visit(s, pool, clear_countdown, 0);
}

/* Each periodic record counts down; at zero it reloads its period and its
 * owner updates it (so a period N updates every N + 1 passes). */
void fe2_pool_periodic_updates(fe2_pool_state *s, const fe2_pool_services *game, fe2_object_pool *pool,
                               const uint16_t inputs[4])
{
    int slot;
    s->current = s->saved = pool;
    for (slot = FE2_POOL_LAST_SLOT; slot >= 0; --slot) {
        fe2_object *o;
        if (!(pool->status[slot] & FE2_SLOT_PERIODIC)) continue;
        o = pool->records[slot];
        if (o->update_countdown != 0) {
            --o->update_countdown;
            continue;
        }
        o->update_countdown = o->update_period;
        game->periodic_update(game->opaque, o->role, o, inputs);
    }
}

void fe2_pool_second_updates(fe2_pool_state *s, const fe2_pool_services *game, fe2_object_pool *pool,
                             const uint16_t inputs[2])
{
    int slot;
    s->saved = pool;
    s->panel_flags &= (uint8_t)~0x02u;
    for (slot = FE2_POOL_LAST_SLOT; slot >= 0; --slot) {
        fe2_object *o;
        if (!(pool->status[slot] & FE2_SLOT_SECOND_UPDATE)) continue;
        o = pool->records[slot];
        o->render_state = 0;
        if (o->role != 0) game->second_update(game->opaque, o->role, o, inputs);
    }
}

/* Records marked for it lose their frame state and screen position,
 * unless the game holds frame resets. */
void fe2_pool_reset_frame(fe2_pool_state *s, fe2_object_pool *pool)
{
    int slot;
    s->current = pool;
    if (s->game_flags & FE2_GAME_NO_FRAME_RESET) return;
    for (slot = FE2_POOL_LAST_SLOT; slot >= 0; --slot) {
        fe2_object *o;
        if (!(pool->status[slot] & FE2_SLOT_FRAME_RESET)) continue;
        o = pool->records[slot];
        o->frame_state = 0;
        o->screen_x = o->screen_y = OFF_SCREEN;
    }
}
