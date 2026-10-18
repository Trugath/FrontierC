/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* linked frame transitions and selection; 0x114d4..0x120f4
 * the parent's basis frame. */
#include "motion_internal.h"

enum {
    FRAME_EVENT = 0x0b,                /* the overlays' event: a record changed frame */
    FRAME_SLOT = 0x10,                 /* slot status of a frame record */
    NO_PARENT_SCORE = 0x32, PARENT_BIAS = 0x20, GRANDPARENT_BIAS = 0x28,
    BASIS_FRAME = 0xff,                /* station_slot (+0x45) in the parent's basis frame */
    POSITION_BIAS = 0x30, DWORD_BITS = 32, COUNT_MASK = 0x3f
};

static int64_t position_of(const fe2_object *o, unsigned i)
{
    return (int64_t)((uint64_t)o->position_high[i] << 32 | o->position_low[i]);
}

static void set_position(fe2_object *o, unsigned i, uint64_t p)
{
    o->position_low[i] = (uint32_t)p;
    o->position_high[i] = (uint32_t)(p >> 32);
}

/* Into the parent's own frame: the parent's position and velocity are
 * added and its parent becomes the record's. */
static void detach(const fe2_motion_services *game, fe2_object *o, fe2_object *parent)
{
    int32_t v[3];
    unsigned i;
    o->parent_handle = parent->parent_handle;
    for (i = 0; i < 3; ++i) set_position(o, i, (uint64_t)position_of(o, i) + (uint64_t)position_of(parent, i));
    fe2_motion_velocity(game, parent, v);
    for (i = 0; i < 3; ++i) o->velocity[i] = (int32_t)((uint32_t)o->velocity[i] + (uint32_t)v[i]);
    game->notify(game->opaque, FRAME_EVENT, o);
}

static void attach(const fe2_motion *m, const fe2_motion_services *game, fe2_object *o, uint8_t handle)
{
    fe2_object *frame;
    int32_t v[3];
    unsigned i;
    o->parent_handle = handle;
    frame = fe2_pool_record(m->pool, handle);
    for (i = 0; i < 3; ++i) set_position(o, i, (uint64_t)position_of(o, i) - (uint64_t)position_of(frame, i));
    fe2_motion_velocity(game, frame, v);
    for (i = 0; i < 3; ++i) o->velocity[i] = (int32_t)((uint32_t)o->velocity[i] - (uint32_t)v[i]);
    game->notify(game->opaque, FRAME_EVENT, o);
}

/* The distance's exponent less the frame's range exponent. */
static int16_t range(const fe2_motion_services *game, fe2_object *o, fe2_object *frame)
{
    return (int16_t)(uint16_t)(fe2_motion_scaled_difference(game, o, frame).shift - frame->as.body.scale_exponent);
}

/* Scores (16-bit, wrapping): the parent 4 * range + 0x20 (0x32 without a
 * parent); the grandparent range + 0x28, taken over the parent when not
 * above its score; a frame record under the same parent 16 * range, the
 * best below the score kept; one under the grandparent 4 * range + 0x20,
 * which (below the score) sends the record to the grandparent's frame. */
void fe2_motion_select_frame(fe2_motion *m, const fe2_motion_services *game, fe2_object *o)
{
    uint8_t chosen = o->parent_handle, grandparent = 0;
    int16_t best = NO_PARENT_SCORE;
    int slot;
    if (o->station_slot != 0) return;
    if (o->parent_handle != 0) {
        fe2_object *parent = fe2_pool_record(m->pool, o->parent_handle);
        best = (int16_t)(uint16_t)((uint16_t)range(game, o, parent) * 4u + PARENT_BIAS);
        grandparent = parent->parent_handle;
        if (grandparent != 0) {
            int16_t score = (int16_t)(uint16_t)((uint16_t)range(game, o, fe2_pool_record(m->pool, grandparent)) +
                                                GRANDPARENT_BIAS);
            if (score <= best) {
                detach(game, o, parent);
                return;
            }
        }
    }
    for (slot = FE2_POOL_LAST_SLOT; slot >= 0; --slot) {
        fe2_object *frame;
        int16_t score;
        if (!(m->pool->status[slot] & FRAME_SLOT)) continue;
        frame = fe2_pool_record(m->pool, (uint8_t)slot);
        if (frame->parent_handle == o->parent_handle) {
            score = (int16_t)(uint16_t)((uint16_t)range(game, o, frame) * 16u);
            if (score < best) {
                chosen = (uint8_t)slot;
                best = score;
            }
        } else if (frame->parent_handle == grandparent) {
            score = (int16_t)(uint16_t)((uint16_t)range(game, o, frame) * 4u + PARENT_BIAS);
            if (score < best) {
                detach(game, o, fe2_pool_record(m->pool, o->parent_handle));
                return;
            }
        }
    }
    if (chosen != o->parent_handle) attach(m, game, o, chosen);
}

/* Shifts by a count the original loops over (any size). */
static int32_t sar32(int32_t v, unsigned n) { return v >> (n > 31u ? 31u : n); }
static uint32_t shl32(uint32_t v, unsigned n) { return n > 31u ? 0u : v << n; }

/* The position reduced to words (a shift s), transformed (twice the full
 * dot products t), and rebuilt as 64 bits for exponent 0x30 - s: the low
 * dword t << (s - 0x10) and the high t >> (0x30 - s) below 32, a sign
 * extended t >> (s - 0x10) from it; the original masks both counts to 63
 * and shifts one bit at a time. */
static void transform_position(fe2_object *o, const int16_t basis[9], int rows)
{
    int64_t position[3];
    fe2_scaled w;
    int16_t k;
    unsigned i;
    for (i = 0; i < 3; ++i) position[i] = position_of(o, i);
    w = fe2_motion_scale_wide(position);
    k = (int16_t)(uint16_t)(POSITION_BIAS - w.shift);
    for (i = 0; i < 3; ++i) {
        int16_t axis[3];
        int32_t t;
        uint32_t low, high;
        if (rows) fe2_motion_row(basis, i, axis);
        else fe2_motion_column(basis, i, axis);
        t = (int32_t)fe2_motion_dot2(w.v, axis);
        if ((int16_t)(k - DWORD_BITS) >= 0) {
            high = t < 0 ? 0xffffffffu : 0;
            low = (uint32_t)sar32(t, (unsigned)(k - DWORD_BITS) & COUNT_MASK);
        } else {
            high = (uint32_t)sar32(t, (unsigned)k & COUNT_MASK);
            low = shl32((uint32_t)t, (unsigned)(DWORD_BITS - k) & COUNT_MASK);
        }
        o->position_low[i] = low;
        o->position_high[i] = high;
    }
}

/* The velocity reduced below 0x3f00, transformed and shifted back. */
static void transform_velocity(fe2_object *o, const int16_t basis[9], int rows)
{
    int16_t w[3], out[3];
    unsigned i, shift = fe2_motion_feedback_shift(o->velocity);
    for (i = 0; i < 3; ++i) w[i] = (int16_t)(uint16_t)(uint32_t)(o->velocity[i] >> shift);
    if (rows) fe2_motion_by_rows(basis, w, out);
    else fe2_motion_by_columns(basis, w, out);
    for (i = 0; i < 3; ++i) o->velocity[i] = (int32_t)shl32((uint32_t)(int32_t)out[i], shift & COUNT_MASK);
}

/* Position, basis (each row of the transposed basis through the parent's
 * columns) and velocity into the parent's frame; the fractions stay. */
void fe2_motion_enter_basis_frame(fe2_object *o, const fe2_object *parent)
{
    unsigned r;
    transform_position(o, parent->orientation, 0);
    fe2_motion_transpose_basis(o->orientation);
    for (r = 0; r < 3; ++r) {
        int16_t row[3];
        fe2_motion_row(o->orientation, r, row);
        fe2_motion_by_columns(parent->orientation, row, &o->orientation[3 * r]);
    }
    fe2_motion_transpose_basis(o->orientation);
    o->basis_generation = 0;
    o->station_slot = BASIS_FRAME;
    transform_velocity(o, parent->orientation, 0);
}

/* Back through the parent's rows; the basis becomes the alternate one,
 * rebuilt in the parent's frame. */
void fe2_motion_leave_basis_frame(const fe2_motion_services *game, fe2_object *o, const fe2_object *parent)
{
    unsigned i;
    transform_position(o, parent->orientation, 1);
    game->rebuild_secondary_basis(game->opaque, o);
    for (i = 0; i < 9; ++i) o->orientation[i] = o->alternate_orientation[i];
    o->station_slot = 0;
    transform_velocity(o, parent->orientation, 1);
}
