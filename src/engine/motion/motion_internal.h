/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_MOTION_INTERNAL_H
#define FE2_MOTION_INTERNAL_H

/* Shared between the motion files. */
#include "motion.h"

/* Twice the 32-bit wrapped dot product, and its high word. */
uint32_t fe2_motion_dot2(const int16_t a[3], const int16_t b[3]);
int16_t fe2_motion_dot_high(const int16_t a[3], const int16_t b[3]);
void fe2_motion_column(const int16_t basis[9], unsigned k, int16_t out[3]);
void fe2_motion_row(const int16_t basis[9], unsigned k, int16_t out[3]);
/* out[k] = the high word of twice w. column k (row k). */
void fe2_motion_by_columns(const int16_t basis[9], const int16_t w[3], int16_t out[3]);
void fe2_motion_by_rows(const int16_t basis[9], const int16_t w[3], int16_t out[3]);
/* The shift bringing three 32-bit magnitudes below 0x3f00. */
unsigned fe2_motion_feedback_shift(const int32_t v[3]);

/* The target's position relative to the record plus the record's source
 * offset (0xfa77's first part). */
void fe2_motion_offset_position(const fe2_motion_services *game, fe2_object *record, fe2_object *target,
                                int64_t difference[3]);

/* A 48-bit add of `v` (sign extended) to a velocity and its fraction. */
void fe2_motion_add_fractional(fe2_object *record, const int32_t v[3]);

/* The steering tail shared by 0xfd2a and 0x10fc8: from the steering words
 * and the reduced relative velocity (`motion`, `motion_shift`). */
fe2_feedback fe2_motion_steering_feedback(fe2_motion *m, const fe2_motion_services *game, fe2_object *record,
                                          fe2_object *target, uint16_t exponent, const int16_t motion[3],
                                          int16_t motion_shift);

#endif
