/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* positions and velocities relative to a target. */
#include "motion_internal.h"

enum {
    SOURCE_OFFSET_SHIFT = 10,          /* the source offset is in 1/1024ths */
    ESTIMATED_OWNER = 1,               /* role whose velocity is estimated */
    NO_ESTIMATE = 0x20,                /* flags: a slot 1 owner's record that stays still */
    ESTIMATE_DELAY = 0x82c00,          /* added to the record's time for the update */
    ESTIMATE_SHIFT = 10
};

/* The record's absolute position refreshes its cache, which the relative
 * position reads. */
void fe2_motion_difference(const fe2_motion_services *game, fe2_object *o, fe2_object *target,
                           int64_t difference[3])
{
    int64_t ignored[3];
    game->absolute_position(game->opaque, o, ignored);
    game->relative_position(game->opaque, target, o, difference);
}

fe2_scaled fe2_motion_scaled_difference(const fe2_motion_services *game, fe2_object *o, fe2_object *target)
{
    int64_t d[3];
    fe2_motion_difference(game, o, target, d);
    return fe2_motion_scale_wide(d);
}

void fe2_motion_offset_position(const fe2_motion_services *game, fe2_object *o, fe2_object *target, int64_t d[3])
{
    unsigned i;
    fe2_motion_difference(game, o, target, d);
    for (i = 0; i < 3; ++i) d[i] += (int64_t)((int32_t)o->source_offset[i] * (1 << SOURCE_OFFSET_SHIFT));
}

fe2_scaled fe2_motion_offset_difference(const fe2_motion *m, const fe2_motion_services *game, fe2_object *o,
                                        fe2_object *target)
{
    int64_t d[3];
    fe2_scaled r;
    unsigned i;
    fe2_motion_offset_position(game, o, target, d);
    r = fe2_motion_scale_wide(d);
    /* The basis frame mode (+0x45). */
    if (o->station_slot != 0 && target->station_slot == 0) {
        const fe2_object *parent = fe2_pool_record(m->pool, o->parent_handle);
        int16_t w[3];
        for (i = 0; i < 3; ++i) w[i] = r.v[i];
        fe2_motion_by_columns(parent->orientation, w, r.v);
    }
    return r;
}

/* The velocity of an owner 1 record is the movement of a copy updated by
 * its overlay 0x82c00 time units ahead (its low position dwords' change,
 * over 1024). */
static void estimate_velocity(const fe2_motion_services *game, const fe2_object *r, int32_t v[3])
{
    fe2_object copy;
    uint32_t before[3];
    uint64_t time;
    uint16_t words[4];
    unsigned i;
    if (r->flags & NO_ESTIMATE) {
        v[0] = v[1] = v[2] = 0;
        return;
    }
    copy = *r;
    for (i = 0; i < 3; ++i) before[i] = copy.position_low[i];
    time = ((uint64_t)copy.orbit[1] << 32 | copy.orbit[0]) + ESTIMATE_DELAY;
    for (i = 0; i < 4; ++i) words[i] = (uint16_t)(time >> (16 * i));
    game->estimate_update(game->opaque, &copy, words);
    for (i = 0; i < 3; ++i) v[i] = (int32_t)(copy.position_low[i] - before[i]) >> ESTIMATE_SHIFT;
}

void fe2_motion_velocity(const fe2_motion_services *game, fe2_object *r, int32_t v[3])
{
    unsigned i;
    if (r->role == ESTIMATED_OWNER) {
        estimate_velocity(game, r, v);
        return;
    }
    for (i = 0; i < 3; ++i) v[i] = r->velocity[i];
}

void fe2_motion_relative_velocity(const fe2_motion_services *game, fe2_object *o, fe2_object *target,
                                  int32_t v[3])
{
    unsigned i;
    if (o->parent_handle == target->handle || o->parent_handle != target->parent_handle) {
        for (i = 0; i < 3; ++i) v[i] = (int32_t)(0u - (uint32_t)o->velocity[i]);
        return;
    }
    fe2_motion_velocity(game, target, v);
    for (i = 0; i < 3; ++i) v[i] = (int32_t)((uint32_t)v[i] - (uint32_t)o->velocity[i]);
}
