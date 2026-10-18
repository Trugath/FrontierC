/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_POSITION_H
#define FE2_POSITION_H

/*
 * Positions and bases in the frame tree (image 0x2fb0..0x3807) and the
 * reference (camera) direction state.
 *
 * A record's position is relative to its parent (+0x44, a handle in the
 * saved pool); its absolute position is the sum along the links, cached in
 * the record with the frame number it was computed in. A record in a
 * parent's basis frame (+0x45 nonzero) has its position in that frame.
 * Positions are 64-bit (the record's low and high dwords).
 *
 * The motion engine calls the first three as services (fe2_motion_services
 * absolute_position, relative_position, rebuild_secondary_basis): they take
 * the fe2_position state as their opaque pointer.
 */
#include <stddef.h>
#include <stdint.h>

#include "game/object.h"
#include "pool/pool.h"

typedef struct fe2_position {
    fe2_object_pool *saved;            /* the pool links resolve in */
    fe2_object_pool *current;
    uint8_t generation;                /* the frame number */
} fe2_position;

/* ---- position.c  ------------------------------------------- */
/* (0xcc): the absolute position, from the cache when it is of this
 * frame, else computed (recursively through the parents) and cached. */
void fe2_position_absolute(void *position, fe2_object *record, int64_t absolute[3]);
/* (0xd4): source's position relative to reference. Siblings in the
 * same frame subtract their positions; a child of the source is its
 * negated position; otherwise the source's absolute position less the
 * reference's cached one (not refreshed). */
void fe2_position_relative(void *position, fe2_object *source, fe2_object *reference, int64_t difference[3]);
/* (0xd0): the alternate basis (+0x46): the parent's rows in the
 * record's columns, once per frame (+0x5c). */
void fe2_position_secondary_basis(void *position, fe2_object *record);

/* ---- reference.c  ------------------------------------------ */
typedef struct fe2_reference {
    fe2_position *position;
    uint32_t clock;
    int16_t countdown;                 /* passes until the direction is refreshed */
    int16_t direction[3];              /* toward the reference's frame (normalized) */
    int16_t primary[3];                /* the direction in the reference's basis */
    int16_t secondary[3];              /*..and in its alternate basis */
    uint32_t parameters[4];            /* the frame record's variant parameters */
    uint8_t game_flags;
    uint8_t player;                    /* the player's handle */
    uint16_t basis_angles[2];          /* the effect basis angles */
} fe2_reference;

typedef struct fe2_reference_services {
    void *opaque;
    void (*render)(void *opaque, fe2_object *record);              /* 0xd8 */
    void (*refresh_model)(void *opaque, fe2_object *record);       /* 0xfc */
    uint32_t (*random)(void *opaque);                              /* DX:AX */
    const uint8_t *(*model)(void *opaque, uint16_t model);         /* the descriptor */
} fe2_reference_services;

/* the saved pool becomes the current one; the direction toward the
 * reference's frame record is refreshed when the countdown runs out, and
 * transformed into the reference's bases. Returns the clock phase
 * (clock >> 10). */
uint16_t fe2_reference_update(fe2_reference *r, fe2_object *reference);
/* (0xdc): update, then render the projected (status 8) records of the
 * current pool whose countdown is zero, with their effects and model
 * refreshes. */
void fe2_reference_render(fe2_reference *r, const fe2_reference_services *game, fe2_object *reference);
/* update, then count down the projected records or render them. */
void fe2_reference_refresh(fe2_reference *r, const fe2_reference_services *game, fe2_object *reference);

/* The variant parameter sets (0x121c, generated): six rows and the two the
 * three-bit selector reads past them (the following code's bytes). */
extern const uint32_t (*fe2_variant_parameters)[4];
int fe2_position_bind(const uint8_t *image, size_t count);

#endif
