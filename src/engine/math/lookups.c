/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* square roots, 0x9e9..0xba0 bases, 0x24f2 the direction
 * vector and 0x2a67 the basis rotation: the sine and square-root tables. */
#include "math3.h"

/* The table holds half a turn; the top bit of the angle is the sign. */
int16_t fe2_math_sin(uint16_t angle)
{
    int16_t value = fe2_math_sine[(angle & 0x7fffu) >> 4];
    return (angle & 0x8000u) ? (int16_t)(uint16_t)-(uint16_t)value : value;
}

int16_t fe2_math_cos(uint16_t angle) { return fe2_math_sin((uint16_t)(angle + 0x4000u)); }

/* The high word of 2*p*q (modulo 2^32). */
static int16_t fraction(int16_t p, int16_t q)
{
    return (int16_t)(uint16_t)(((uint32_t)((int32_t)p * q) << 1) >> 16);
}

static int16_t negate(int16_t v) { return (int16_t)(uint16_t)-(uint16_t)v; }

void fe2_math_direction(uint16_t a, uint16_t b, int16_t out[3])
{
    out[0] = negate(fraction(fe2_math_sin(b), fe2_math_cos(a)));
    out[1] = fraction(fe2_math_sin(a), fe2_math_sin(b));
    out[2] = fe2_math_cos(b);
}

void fe2_math_basis_a(uint16_t a, uint16_t b, int16_t m[9])
{
    int16_t p = fe2_math_sin(a), q = fe2_math_sin(b), r = fe2_math_cos(a), t = fe2_math_cos(b);
    m[0] = negate(fraction(q, r));
    m[1] = fraction(r, t);
    m[2] = negate(p);
    m[3] = t;
    m[4] = q;
    m[5] = 0;
    m[6] = fraction(p, q);
    m[7] = negate(fraction(p, t));
    m[8] = negate(r);
}

void fe2_math_basis_b(uint16_t a, uint16_t b, int16_t m[9])
{
    int16_t p = fe2_math_sin(a), q = fe2_math_sin(b), r = fe2_math_cos(a), t = fe2_math_cos(b);
    m[0] = fraction(r, t);
    m[1] = p;
    m[2] = negate(fraction(q, r));
    m[3] = negate(fraction(p, t));
    m[4] = r;
    m[5] = fraction(p, q);
    m[6] = q;
    m[7] = 0;
    m[8] = t;
}

/* All of u and v are read before any is written (they may overlap). */
void fe2_math_rotate_pair(int16_t *u, int16_t *v, uint16_t angle)
{
    int16_t s = fe2_math_sin(angle), c = fe2_math_cos(angle), u0[3], v0[3];
    unsigned i;
    for (i = 0; i < 3; ++i) {
        u0[i] = u[3 * i];
        v0[i] = v[3 * i];
    }
    for (i = 0; i < 3; ++i) {
        uint32_t sum = (uint32_t)((int32_t)c * u0[i]) + (uint32_t)((int32_t)s * v0[i]);
        u[3 * i] = (int16_t)(uint16_t)((sum << 1) >> 16);
    }
    for (i = 0; i < 3; ++i) {
        uint32_t difference = (uint32_t)((int32_t)c * v0[i]) - (uint32_t)((int32_t)s * u0[i]);
        v[3 * i] = (int16_t)(uint16_t)((difference << 1) >> 16);
    }
}

/* The table holds floor(sqrt(i * 2^21)); a range of the value picks the
 * index and how far the entry is shifted down. */
uint16_t fe2_math_square_root32(uint32_t value)
{
    uint16_t high = (uint16_t)(value >> 16), index;
    unsigned shift;
    if (high >= 0x4000) index = (uint16_t)(high >> 4), shift = 0;
    else if (high >= 0x1000) index = (uint16_t)(high >> 2), shift = 1;
    else if (high >= 0x400) index = high, shift = 2;
    else if (high >= 0x40) index = (uint16_t)(value >> 14), shift = 3;
    else if (high >= 4) index = (uint16_t)(value >> 10), shift = 5;
    else index = (uint16_t)(value >> 6), shift = 7;
    return (uint16_t)(fe2_math_square_root[(index & 0xfffeu) >> 1] >> shift);
}

uint16_t fe2_math_length3(int16_t x, int16_t y, int16_t z)
{
    return fe2_math_square_root32((uint32_t)((int32_t)x * x) + (uint32_t)((int32_t)y * y) +
                                  (uint32_t)((int32_t)z * z));
}

/* An odd exponent first halves the doubled mantissa. */
fe2_scalar fe2_math_square_root_scalar(fe2_scalar v)
{
    fe2_scalar r;
    uint16_t m = (uint16_t)(v.mantissa << 1);
    r.exponent = (uint16_t)((int16_t)v.exponent >> 1);
    if (v.exponent & 1u) {
        m >>= 1;
        ++r.exponent;
    }
    r.mantissa = (uint16_t)(fe2_math_square_root32((uint32_t)m << 16) >> 1);
    return r;
}

fe2_scalar fe2_math_square_root_scalar32(fe2_scalar32 v)
{
    fe2_scalar r;
    uint32_t m = v.mantissa << 1;
    r.exponent = (uint16_t)((int16_t)v.exponent >> 1);
    if (v.exponent & 1u) {
        m >>= 1;
        ++r.exponent;
    }
    r.mantissa = (uint16_t)(fe2_math_square_root32(m) >> 1);
    return fe2_math_normalize_unsigned(r);
}
