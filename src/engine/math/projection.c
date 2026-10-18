/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* projection through the reciprocal table. */
#include "math3.h"

static int32_t sar(int32_t v, unsigned n) { return v >> n; }

/* Halve both until each lies strictly within +-0x1f00. */
static void reduce(int16_t *x, int16_t *y)
{
    if (*x < 0) while (*x <= -0x1f00) *x >>= 1, *y >>= 1;
    else while (*x >= 0x1f00) *x >>= 1, *y >>= 1;
    if (*y < 0) while (*y <= -0x1f00) *x >>= 1, *y >>= 1;
    else while (*y >= 0x1f00) *x >>= 1, *y >>= 1;
}

/* The high word of x * reciprocal (signed), shifted down. */
static int16_t scaled(int32_t x, uint16_t reciprocal, unsigned shift)
{
    return (int16_t)((int16_t)(uint16_t)(((uint32_t)((int32_t)(int16_t)(uint16_t)x * (int16_t)reciprocal)) >> 16) >> shift);
}

static int16_t doubled(int32_t x, uint16_t reciprocal)
{
    return (int16_t)(uint16_t)(((uint32_t)((int32_t)(int16_t)(uint16_t)x * (int16_t)reciprocal) << 1) >> 16);
}

/* Reduce all three by 4 until `v` fits a signed word. */
static void fit_word(int32_t *v, int32_t *x, int32_t *y, int32_t *z)
{
    if (*v >= 0) {
        while ((uint32_t)*v >= 0x8000u) *x = sar(*x, 2), *y = sar(*y, 2), *z = sar(*z, 2);
    } else {
        while ((uint32_t)*v < UINT32_C(0xffff8000)) *x = sar(*x, 2), *y = sar(*y, 2), *z = sar(*z, 2);
    }
}

/*
 * The original compares x and y with the depth and its negation unsigned,
 * reducing whichever dominates (the branch graph is kept as it is), then
 * picks a reciprocal-table range by depth.
 */
fe2_point fe2_math_project(int32_t x, int32_t y, int32_t z)
{
    fe2_point p;
    if ((uint32_t)z >= (uint32_t)x) goto second_against_negated;
    z = -z;
    if ((uint32_t)z >= (uint32_t)x) goto first_dominates;
compare_second:
    if ((uint32_t)z < (uint32_t)y) goto negate_back;
    z = -z;
    if ((uint32_t)z < (uint32_t)y) goto second_dominates;
    z = -z;
negate_back:
    z = -z;
    goto select;
second_against_negated:
    z = -z;
    if ((uint32_t)z < (uint32_t)y) goto negate_back;
    z = -z;
    if ((uint32_t)z >= (uint32_t)y) goto select;
second_dominates:
    fit_word(&y, &x, &y, &z);
    goto select;
first_dominates:
    fit_word(&x, &x, &y, &z);
    goto compare_second;
select:
    if ((uint32_t)z >= 0x4000u) {
        while ((uint32_t)z >= 0x8000u) x = sar(x, 1), y = sar(y, 1), z = sar(z, 1);
        p.x = scaled(x, fe2_math_reciprocal[(uint32_t)z >> 6], 5);
        p.y = scaled(y, fe2_math_reciprocal[(uint32_t)z >> 6], 5);
    } else if (z >= 0x1000) {
        p.x = scaled(x, fe2_math_reciprocal[z >> 4], 3);
        p.y = scaled(y, fe2_math_reciprocal[z >> 4], 3);
    } else if (z >= 0x400) {
        p.x = scaled(x, fe2_math_reciprocal[z >> 2], 1);
        p.y = scaled(y, fe2_math_reciprocal[z >> 2], 1);
        reduce(&p.x, &p.y);
    } else {
        p.x = doubled(x, fe2_math_reciprocal[z]);
        p.y = doubled(y, fe2_math_reciprocal[z]);
        reduce(&p.x, &p.y);
    }
    return p;
}
