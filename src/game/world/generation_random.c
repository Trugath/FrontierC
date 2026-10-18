/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* system random generator and 0x4e29 profile curve. */
#include "systems_internal.h"

enum { CURVE_LIMIT = 0x4000, CURVE_STEP_SHIFT = 6 };

static uint32_t rotate_left(uint32_t value, unsigned count)
{
    return (value << count) | (value >> (32 - count));
}

/* T = A + B; B' = rol(T, 5); A' = rol(A, 3) + T + B' (all modulo 2^32). */
uint32_t fe2_systems_random(fe2_system_generation *g)
{
    uint32_t sum = g->random_a + g->random_b;
    uint32_t b = rotate_left(sum, 5);
    g->random_a = rotate_left(g->random_a, 3) + sum + b;
    g->random_b = b;
    return g->random_a;
}

uint16_t fe2_systems_curve(uint16_t position)
{
    if (position >= CURVE_LIMIT) return 0xffff;
    return fe2_systems_profile_curve[position >> CURVE_STEP_SHIFT];
}

fe2_system_record *fe2_systems_record(fe2_system_generation *g, unsigned index)
{
    return &g->space.records[index % g->space.count];
}

uint32_t fe2_systems_position(unsigned index, unsigned offset)
{
    return (uint32_t)index * FE2_RECORD_BYTES + offset;
}
