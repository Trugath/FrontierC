/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* normalization and the basis rebuild, 0x1147f..0x114d3 the
 * basis permutations. */
#include "motion.h"

enum { LENGTH_SCALE = 0x8101, UNIT = 16384 };

/* (component << 16 >> 2) / denominator as IDIV does; an overflowing
 * quotient leaves the dividend's low word (the divide-fault resume). */
static int16_t scaled_quotient(int16_t component, int16_t denominator)
{
    int32_t dividend = (int32_t)component * UNIT, q = dividend / denominator;
    if (q > INT16_MAX || q < INT16_MIN) return (int16_t)(uint16_t)(uint32_t)dividend;
    return (int16_t)q;
}

void fe2_motion_normalize(int16_t v[3])
{
    uint16_t length = fe2_math_length3(v[0], v[1], v[2]);
    int16_t denominator = (int16_t)(uint16_t)(((uint32_t)length * LENGTH_SCALE) >> 16);
    unsigned i;
    if (denominator == 0) return;
    for (i = 0; i < 3; ++i) v[i] = scaled_quotient(v[i], denominator);
}

static void column(const fe2_object *o, unsigned k, int16_t v[3])
{
    v[0] = o->orientation[k];
    v[1] = o->orientation[3 + k];
    v[2] = o->orientation[6 + k];
}

static void set_column(fe2_object *o, unsigned k, const int16_t v[3])
{
    o->orientation[k] = v[0];
    o->orientation[3 + k] = v[1];
    o->orientation[6 + k] = v[2];
}

/* The high word of 2 * (p*q - r*s), 32-bit wrapping. */
static int16_t cross_term(int16_t p, int16_t q, int16_t r, int16_t s)
{
    uint32_t d = (uint32_t)((int32_t)p * q) - (uint32_t)((int32_t)r * s);
    return (int16_t)(uint16_t)((d << 1) >> 16);
}

/* a x b. */
static void cross(const int16_t a[3], const int16_t b[3], int16_t out[3])
{
    out[0] = cross_term(a[1], b[2], a[2], b[1]);
    out[1] = cross_term(a[2], b[0], a[0], b[2]);
    out[2] = cross_term(a[0], b[1], a[1], b[0]);
}

void fe2_motion_rebuild_basis(fe2_object *o)
{
    int16_t a[3], b[3], c[3];
    column(o, 0, a);
    fe2_motion_normalize(a);
    column(o, 2, b);
    fe2_motion_normalize(b);
    set_column(o, 2, b);
    cross(b, a, c);
    fe2_motion_normalize(c);
    set_column(o, 1, c);
    cross(c, b, a);
    set_column(o, 0, a);
}

void fe2_motion_transpose_basis(int16_t m[9])
{
    int16_t t;
    t = m[1], m[1] = m[3], m[3] = t;
    t = m[2], m[2] = m[6], m[6] = t;
    t = m[5], m[5] = m[7], m[7] = t;
}

/* Columns 1 and 2 swap; column 0 is negated. */
void fe2_motion_swap_basis_columns(int16_t m[9])
{
    unsigned row;
    for (row = 0; row < 3; ++row) {
        int16_t t = m[3 * row + 1];
        m[3 * row + 1] = m[3 * row + 2];
        m[3 * row + 2] = t;
        m[3 * row] = (int16_t)(uint16_t)-(uint16_t)m[3 * row];
    }
}
