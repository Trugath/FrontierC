/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* unsigned division, 0x1f1f the matrix product, 0x275b the scaled
 * difference and 0x2857 the wide scalar product. */
#include "math3.h"

/*
 * Shift-and-subtract division. The divisor is doubled while it stays below
 * the dividend (an equal divisor stops it one step short), so a dividend
 * that is a power-of-two multiple of the divisor gives a quotient one bit
 * short: 2 / 1 is 1 remainder 1.
 */
fe2_division fe2_math_divide_unsigned(uint32_t dividend, uint32_t divisor)
{
    fe2_division d;
    uint32_t bit = 1;
    d.quotient = 0;
    if (!(divisor & UINT32_C(0x80000000))) {
        for (;;) {
            bit <<= 1;
            divisor <<= 1;
            if (divisor & UINT32_C(0x80000000)) break;
            if (dividend <= divisor) {
                bit >>= 1;
                divisor >>= 1;
                break;
            }
        }
    }
    for (;;) {
        if (dividend >= divisor) {
            dividend -= divisor;
            d.quotient += bit;
        }
        bit >>= 1;
        if (bit == 0) break;
        divisor >>= 1;
    }
    d.remainder = dividend;
    return d;
}

void fe2_math_matrix_vector(const int16_t m[9], const int16_t v[3], int32_t out[3])
{
    unsigned i;
    for (i = 0; i < 3; ++i) {
        uint32_t sum = (uint32_t)((int32_t)m[i] * v[0]) + (uint32_t)((int32_t)m[3 + i] * v[1]) +
                       (uint32_t)((int32_t)m[6 + i] * v[2]);
        out[i] = (int32_t)(sum << 1);
    }
}

/* The difference's magnitude times scale, times 8, with the difference's
 * sign; all modulo 2^64. */
int64_t fe2_math_scaled_difference(uint64_t a, uint64_t b, uint32_t scale)
{
    uint64_t difference = b - a;
    int negative = (difference >> 63) != 0;
    uint64_t product = ((negative ? 0u - difference : difference) * scale) << 3;
    return (int64_t)(negative ? 0u - product : product);
}

/*
 * component * scalar, keeping 15 bits of the scalar: its magnitude is
 * shifted right h times until it fits 32 bits, then 7 more, then r more
 * until it fits 0..0x7fff (m). The result is ((component * m) >> 2) shifted
 * left by r + h, with the scalar's sign; all modulo 2^64.
 */
int64_t fe2_math_wide_scale(int32_t component, int64_t scalar)
{
    uint64_t magnitude = (uint64_t)scalar, product;
    unsigned shifts = 0;
    uint32_t m;
    int negative = scalar < 0;
    if (negative) magnitude = 0u - magnitude;
    while (magnitude >> 32) {
        magnitude >>= 1;
        ++shifts;
    }
    m = (uint32_t)magnitude >> 7;
    while (m > 0x7fff) {
        m >>= 1;
        ++shifts;
    }
    product = (uint64_t)((int64_t)component * (int64_t)m);
    product = (uint64_t)((int64_t)product >> 2) << shifts;
    return (int64_t)(negative ? 0u - product : product);
}
