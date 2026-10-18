/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the reference direction and the projected records' pass. */
#include "math/math3.h"
#include "game/model.h"
#include "motion/motion.h"
#include "position_internal.h"

enum {
    PHASE_SHIFT = 10,
    LAST_SLOT = 0x72,                  /* the frame record when the links end at the root */
    FRAME_RECORD = 0x08,               /* flags (+0x118): a frame record */
    PAIRED_MODEL = 0x20,               /* a frame whose preceding handle is the frame record */
    VARIANT_MASK = 7,
    PROJECTED = 0x08,                  /* slot status */
    RENDER_PASS = 0x02,                /* game flags */
    EFFECT_MODEL = 0xc8, REFRESH_MODEL = 0xc0,
    MODEL_REFRESH = 0x02, TEMPORARY_MODEL = 0x01,  /* frame_state bits (+0x11d) */
    OFF_SCREEN = 0x8000
};

/* out[j] = the direction. basis column j. */
static void transform(const int16_t basis[9], const int16_t d[3], int16_t out[3])
{
    unsigned j;
    for (j = 0; j < 3; ++j) out[j] = fe2_position_dot(d[0], d[1], d[2], basis[j], basis[3 + j], basis[6 + j]);
}

/* The frame record: up the links (in the current pool) to one flagged as a
 * frame, or the last slot of the saved pool at the root; a paired model
 * stands for the record before it. No check for link cycles. */
static void refresh_direction(fe2_reference *r, fe2_object *reference)
{
    fe2_position *s = r->position;
    fe2_object *frame = reference;
    int64_t a[3], d[3];
    int16_t w[3];
    unsigned i;
    for (;;) {
        if (frame->parent_handle == 0) {
            frame = fe2_pool_record(s->saved, LAST_SLOT);
            break;
        }
        frame = fe2_pool_record(s->current, frame->parent_handle);
        if (frame->flags & FRAME_RECORD) break;
    }
    if (frame->model == PAIRED_MODEL) frame = fe2_pool_record(s->current, (uint8_t)(frame->handle - 1));
    /* The reference's absolute position less the frame's cached one. */
    fe2_position_absolute(s, reference, a);
    for (i = 0; i < 3; ++i)
        d[i] = (int64_t)((uint64_t)a[i] - ((uint64_t)frame->camera_target[i] << 32 | frame->camera_offset[i]));
    fe2_position_reduce(d, w);
    fe2_motion_normalize(w);
    for (i = 0; i < 3; ++i) r->direction[i] = w[i];
    for (i = 0; i < 4; ++i) r->parameters[i] = fe2_variant_parameters[frame->flags & VARIANT_MASK][i];
}

uint16_t fe2_reference_update(fe2_reference *r, fe2_object *reference)
{
    fe2_position *s = r->position;
    uint16_t phase;
    int64_t ignored[3];
    s->saved = s->current;
    phase = (uint16_t)(r->clock >> PHASE_SHIFT);
    fe2_position_absolute(s, reference, ignored);
    r->countdown = (int16_t)(uint16_t)((uint16_t)r->countdown - 1u);
    if (r->countdown < 0) {
        refresh_direction(r, reference);
        r->countdown = 0;
    }
    transform(reference->orientation, r->direction, r->primary);
    if (reference->station_slot != 0) {
        fe2_position_secondary_basis(s, reference);
        transform(reference->alternate_orientation, r->direction, r->secondary);
    }
    return phase;
}

/* Two renders of a copy of the record as the effect model, each with a
 * random basis. */
static void render_effect(fe2_reference *r, const fe2_reference_services *game, const fe2_object *o)
{
    fe2_object effect = *o;
    unsigned n;
    effect.model = EFFECT_MODEL;
    effect.cargo_type = (uint16_t)((effect.cargo_type & 0xff00u) | o->frame_state);
    for (n = 0; n < 2; ++n) {
        uint32_t v = game->random(game->opaque);
        r->basis_angles[0] = (uint16_t)v;
        r->basis_angles[1] = (uint16_t)(v >> 16);
        fe2_math_basis_b(r->basis_angles[0], r->basis_angles[1], effect.orientation);
        game->render(game->opaque, &effect);
    }
}

static void render_record(fe2_reference *r, const fe2_reference_services *game, fe2_object *o)
{
    if (o->frame_state != 0) {
        if (o->effect != 0) render_effect(r, game, o);
        if (o->frame_state & MODEL_REFRESH) {
            o->frame_state &= (uint8_t)~MODEL_REFRESH;
            game->refresh_model(game->opaque, o);
        }
        if (o->frame_state & TEMPORARY_MODEL) {
            /* Rendered once as the refresh model, at its own radius. */
            uint16_t model = o->model;
            o->frame_state &= (uint8_t)~TEMPORARY_MODEL;
            o->model_parameter = fe2_model_word(game->model(game->opaque, model), FE2_MODEL_RADIUS);
            o->model = REFRESH_MODEL;
            game->render(game->opaque, o);
            o->model_parameter = 0;
            o->model = model;
            return;
        }
    }
    if (r->player == o->handle) o->screen_x = o->screen_y = OFF_SCREEN;
    else game->render(game->opaque, o);
}

void fe2_reference_render(fe2_reference *r, const fe2_reference_services *game, fe2_object *reference)
{
    fe2_position *s = r->position;
    fe2_object_pool *pool;
    uint16_t phase;
    int slot;
    r->game_flags |= RENDER_PASS;
    phase = fe2_reference_update(r, reference);
    pool = s->current;
    for (slot = LAST_SLOT; slot >= 0; --slot) {
        fe2_object *o;
        if (!(pool->status[slot] & PROJECTED)) continue;
        o = fe2_pool_record(s->current, (uint8_t)slot);
        if (o->update_countdown == 0) {
            o->clock = phase;
            render_record(r, game, o);
        }
        s->current = pool;
    }
}

void fe2_reference_refresh(fe2_reference *r, const fe2_reference_services *game, fe2_object *reference)
{
    fe2_position *s = r->position;
    fe2_object_pool *pool;
    uint16_t phase = fe2_reference_update(r, reference);
    int slot;
    pool = s->current;
    for (slot = LAST_SLOT; slot >= 0; --slot) {
        fe2_object *o;
        if (!(pool->status[slot] & PROJECTED)) continue;
        o = fe2_pool_record(s->saved, (uint8_t)slot);
        if (o->update_countdown != 0) {
            --o->update_countdown;
            continue;
        }
        o->clock = phase;
        game->render(game->opaque, o);
    }
}
