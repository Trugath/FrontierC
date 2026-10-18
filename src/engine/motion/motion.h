/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_MOTION_H
#define FE2_MOTION_H

/*
 * The flight engine (image 0xee87..0x120f4): craft orientation and its
 * renormalization, turning, steering rates, relative motion and feedback,
 * motion integration with control limits, linked (parent) frames and their
 * force, and the basis frame of a linked record.
 *
 * Bases are the record's nine Q15 words in row order (orientation); column
 * k is orientation[k], [3 + k], [6 + k]. Positions are 64-bit (the
 * record's low and high dwords); velocities are 32-bit with a 16-bit
 * fraction below them.
 *
 * The overlays call these through resident vectors, passing values in the
 * argument block; each entry takes and returns them typed. The
 * original leaves intermediate values in the rest of the block, which the
 * port does not keep.
 */
#include <stddef.h>
#include <stdint.h>

#include "math/math3.h"
#include "game/object.h"
#include "pool/pool.h"

/* The game state the engine reads. */
typedef struct fe2_motion {
    fe2_object_pool *pool;             /* the current pool */
    uint16_t time_exponent;            /* the selected time acceleration */
    uint16_t game_time_exponent;
    int16_t radial[3];                 /* the last linked force's direction */
} fe2_motion;

/* A scene probe request (service 0x130); see the probed feedback. */
typedef struct fe2_motion_probe {
    uint8_t handle;                    /* the probing record's handle  */
    uint16_t mode;                     /* 0x1880, or 0x5880 with an upper extent */
    uint32_t extent, extent_high;
    int16_t direction[3];              /* the normalized steering direction */
} fe2_motion_probe;

typedef struct fe2_motion_hit {
    int hit;                           /* the service's sign flag */
    int64_t distance;
    int16_t direction[3];
} fe2_motion_hit;

typedef struct fe2_motion_services {
    void *opaque;
    /* 0xcc: a record's absolute position (refreshing its cache). */
    void (*absolute_position)(void *opaque, fe2_object *record, int64_t position[3]);
    /* 0xd4: source's position relative to reference. */
    void (*relative_position)(void *opaque, fe2_object *source, fe2_object *reference, int64_t difference[3]);
    /* Overlay 1's update entry (+0x18) on a record copy at `time` (four
     * words, low first), with its call counter  raised. */
    void (*estimate_update)(void *opaque, fe2_object *copy, const uint16_t time[4]);
    void (*notify)(void *opaque, uint16_t event, fe2_object *record);
    /* probe the scene around `record` (moving toward `target`). */
    void (*probe)(void *opaque, fe2_object *record, fe2_object *target, const fe2_motion_probe *request,
                  fe2_motion_hit *result);
    /* 0xd0: rebuild the record's alternate basis in its parent's frame. */
    void (*rebuild_secondary_basis)(void *opaque, fe2_object *record);
} fe2_motion_services;

/* Three words and their binary exponent (AX, BX, CX and DX). */
typedef struct fe2_scaled {
    int16_t v[3];
    uint16_t shift;
} fe2_scaled;

/* Feedback in the record's frame  and its exponent
 *. */
typedef struct fe2_feedback {
    int32_t v[3];
    uint16_t shift;
} fe2_feedback;

/* ---- basis.c ------------------------------------------------------------- */
/* (v * 16384) / (length(v) * 0x8101 / 65536); a zero denominator
 * leaves v, and a quotient overflow leaves that component's low word
 * (the resident divide-fault handler's resume). */
void fe2_motion_normalize(int16_t v[3]);
/* column 0 and 2 normalized, column 1 = normalize(col2 x
 * col0), column 0 = col1 x col2 (not normalized). */
void fe2_motion_rebuild_basis(fe2_object *record);

void fe2_motion_transpose_basis(int16_t basis[9]);
void fe2_motion_swap_basis_columns(int16_t basis[9]);

/* ---- rotation.c ---------------------------------------------------------- */
typedef struct fe2_turn {
    uint32_t step_limit;               /* (halved for models from 0x3a and 0x46) */
    uint16_t integrate;                /* add the rates first */
    uint32_t rate_scale;
    uint16_t last_shift;               /* the last step's shift */
} fe2_turn;
/* a remainder's step and its shift. */
int16_t fe2_motion_turn_step(int16_t remainder, uint16_t *shift);
/* integrate the rotation rates into the angular remainders
 * and turn the basis by at most one step per axis. */
void fe2_motion_turn(fe2_object *record, fe2_turn *turn);

/* ---- rotation_rates.c ---------------------------------------------------- */
void fe2_motion_rates_from_controls(fe2_object *record);
void fe2_motion_rates_from_steering(const fe2_motion *m, fe2_object *record);
void fe2_motion_rates_from_reversed_steering(fe2_object *record);
/* rates turning `record` toward `reference`'s basis; `both`
 *  also sets the first and third rates. */
void fe2_motion_rates_toward(fe2_object *record, const fe2_object *reference, int both);
/* the second rate from the last linked force's direction. */
void fe2_motion_radial_rate(const fe2_motion *m, fe2_object *record);

/* ---- scaling.c ----------------------------------------------------------- */
/* three 64-bit values reduced to words by a common shift. */
fe2_scaled fe2_motion_scale_wide(const int64_t v[3]);
/* 32-bit values reduced to words below 0x3f00 and
 * transformed into the record's frame (its basis columns). */
fe2_feedback fe2_motion_transform_feedback(const fe2_object *record, const int32_t v[3]);

/* ---- relative.c ---------------------------------------------------------- */
/* target's position relative to the record. */
void fe2_motion_difference(const fe2_motion_services *game, fe2_object *record, fe2_object *target,
                           int64_t difference[3]);
/* the same, scaled to words; the shift is the distance's
 * exponent. */
fe2_scaled fe2_motion_scaled_difference(const fe2_motion_services *game, fe2_object *record, fe2_object *target);
/* target relative to the record and its source offset (+0xd6, in
 * 1/1024ths), scaled; in the record's parent frame when the record is in
 * a basis frame and the target is not. */
fe2_scaled fe2_motion_offset_difference(const fe2_motion *m, const fe2_motion_services *game, fe2_object *record,
                                        fe2_object *target);
/* target's velocity relative to the record (or the record's
 * negated) when they share a parent. */
void fe2_motion_relative_velocity(const fe2_motion_services *game, fe2_object *record, fe2_object *target,
                                  int32_t velocity[3]);
/* a record's velocity; an owner 1 record's is estimated by updating
 * a copy (see relative.c). */
void fe2_motion_velocity(const fe2_motion_services *game, fe2_object *record, int32_t velocity[3]);

/* ---- feedback.c ---------------------------------------------------------- */
/* steer toward the target: its offset relative position
 * (stored as the record's steering) and relative velocity combine into
 * the feedback limits (+0xc6) and the returned feedback in the record's
 * frame. `exponent` is the caller's. Near the target, with
 * time acceleration on, the record may move into the target's frame. */
fe2_feedback fe2_motion_steer(fe2_motion *m, const fe2_motion_services *game, fe2_object *record,
                              fe2_object *target, uint16_t exponent);
/* the same, probing the scene every few passes; a hit
 * nearer than the target replaces the steering direction. */
fe2_feedback fe2_motion_steer_probed(fe2_motion *m, const fe2_motion_services *game, fe2_object *record,
                                     fe2_object *target, uint16_t exponent);
/* feedback toward `speed` along the record's third basis
 * column, less its velocity. */
fe2_feedback fe2_motion_axis_feedback(fe2_object *record, int32_t speed);

/* ---- integration.c ------------------------------------------------------- */
/* clamp the requested controls  to the record's
 * limits; the record keeps their low words. */
void fe2_motion_set_controls(fe2_object *record, int32_t controls[3]);
/* integrate the controls through the basis
 * over `step`, the first within the feedback limits. */
void fe2_motion_integrate_limited(fe2_object *record, uint32_t step);
void fe2_motion_integrate(fe2_object *record, uint32_t step);
/* the parent's inverse-square pull, scaled by `scale`. */
void fe2_motion_linked_force(fe2_motion *m, fe2_object *record, int32_t scale);

/* ---- frames.c ------------------------------------------------------------ */
/* move the record into the nearest frame (its parent, the
 * grandparent, or a frame record, slot status 0x10). */
void fe2_motion_select_frame(fe2_motion *m, const fe2_motion_services *game, fe2_object *record);
/* into and out of the parent's basis frame. */
void fe2_motion_enter_basis_frame(fe2_object *record, const fe2_object *parent);
void fe2_motion_leave_basis_frame(const fe2_motion_services *game, fe2_object *record, const fe2_object *parent);

/* The rebuild cost of a turning step by shift (2..11), generated (the
 * original keeps a copy per axis and sign). */
extern const uint8_t *fe2_turn_costs;
int fe2_motion_bind(const uint8_t *image, size_t count);

#endif
