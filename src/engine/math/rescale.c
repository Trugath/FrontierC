/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* vector rescaling. */
#include "math3.h"

static uint32_t magnitude(uint32_t v) { return (v & UINT32_C(0x80000000)) ? 0u - v : v; }

/*
 * A restoring division of `dividend` by `divisor`, 32 steps: each shifts the
 * remainder left (losing its top bit) and records a borrow; the quotient is
 * the complemented borrows without the last (31 bits).
 */
static uint32_t trial_quotient(uint32_t dividend, uint32_t divisor)
{
    uint32_t borrows = 0;
    unsigned i;
    for (i = 0; i < 32; ++i) {
        int borrow;
        dividend <<= 1;
        borrow = dividend < divisor;
        dividend -= divisor;
        if (borrow) dividend += divisor;
        borrows = borrows << 1 | (uint32_t)borrow;
    }
    return ~borrows >> 1;
}

/* Average the divisor with the trial quotient of 2 * accumulator until the
 * quotient is no more than 32768 below it (an approximate square root). */
static uint32_t refine_divisor(uint32_t accumulator)
{
    uint32_t n = accumulator << 1, divisor = (n >> 1) + UINT32_C(0x40000000);
    for (;;) {
        uint32_t quotient = trial_quotient(n, divisor);
        divisor = (divisor + quotient) >> 1;
        if ((int32_t)(quotient - divisor) >= -32768) return divisor;
    }
}

/* The high dword of 2 * f * t, from three of the four word products (the
 * low product and the carry of the cross sum are dropped). */
static uint32_t product_estimate(uint32_t f, uint32_t t)
{
    uint32_t fh = f >> 16, fl = f & 0xffffu, th = t >> 16, tl = t & 0xffffu;
    uint32_t cross = fh * tl + fl * th;
    return (fh * th + (cross >> 16)) << 1;
}

/* The upper dword of v^2, without the low square's carry. */
static uint32_t square_estimate(uint32_t v)
{
    uint32_t m = magnitude(v), hi = m >> 16, lo = m & 0xffffu;
    return hi * hi + (((lo * hi) << 1) >> 16);
}

static int32_t scale_component(int32_t component, uint32_t divisor, uint32_t multiplier)
{
    uint32_t f = trial_quotient(magnitude((uint32_t)component), divisor), p;
    if (component < 0) f = 0u - f;
    p = product_estimate(magnitude(f), multiplier);
    return (int32_t)((f & UINT32_C(0x80000000)) ? 0u - p : p);
}

void fe2_math_rescale(int32_t v[3], uint32_t *accumulator, uint32_t multiplier)
{
    uint32_t divisor = refine_divisor(*accumulator);
    unsigned i;
    *accumulator = divisor;
    for (i = 0; i < 3; ++i) v[i] = scale_component(v[i], divisor, multiplier);
}

void fe2_math_rescale_to_length(int32_t v[3], uint32_t multiplier)
{
    uint32_t accumulator = square_estimate((uint32_t)v[0]) + square_estimate((uint32_t)v[1]) +
                           square_estimate((uint32_t)v[2]);
    fe2_math_rescale(v, &accumulator, multiplier);
}

/* The renderer's 32-bit helper performs the same steps. */
void fe2_math_rescale32(int32_t v[3], uint32_t multiplier) { fe2_math_rescale_to_length(v, multiplier); }
