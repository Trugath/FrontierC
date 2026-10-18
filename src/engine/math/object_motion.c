/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* object position updates. Positions are
 * 64-bit, kept as separate low and high dwords. */
#include "math3.h"

static uint64_t position(const fe2_object *o, unsigned i)
{
    return (uint64_t)o->position_high[i] << 32 | o->position_low[i];
}

static void set_position(fe2_object *o, unsigned i, uint64_t value)
{
    o->position_low[i] = (uint32_t)value;
    o->position_high[i] = (uint32_t)(value >> 32);
}

/* An arithmetic shift of a 64-bit value, at most 63. */
static uint64_t sar64(uint64_t value, unsigned count)
{
    if (count > 63) count = 63;
    return (uint64_t)((int64_t)value >> count);
}

void fe2_math_move_object(fe2_object *o, const int32_t delta[3])
{
    unsigned i;
    for (i = 0; i < 3; ++i) set_position(o, i, position(o, i) + (uint64_t)(int64_t)delta[i]);
}

void fe2_math_advance_object(fe2_object *o, int64_t scalar)
{
    unsigned i;
    for (i = 0; i < 3; ++i)
        set_position(o, i, position(o, i) + (uint64_t)fe2_math_wide_scale(o->velocity[i], scalar));
}

/* The vector exponent sets the shift: 61 - exponent + 1 (at least 1). */
static unsigned right_shift(uint16_t exponent)
{
    int16_t count = (int16_t)(0x3d - exponent);
    return count < 0 ? 1u : (unsigned)count + 1u;
}

void fe2_math_step_object(fe2_object *o, uint16_t factor)
{
    int16_t step = (int16_t)(uint16_t)(((uint32_t)factor * (uint16_t)o->scalar) >> 16);
    unsigned shift = right_shift((uint16_t)(o->scalar >> 16)), i;
    for (i = 0; i < 3; ++i) {
        uint64_t product = (uint64_t)(uint32_t)((int32_t)(int16_t)o->vector_delta[i] * step) << 32;
        set_position(o, i, position(o, i) + sar64(product, shift));
    }
}

/*
 * The arc point: the direction at (angle, phase & 0xfff0) and its step to
 * the next 16 units, interpolated by the phase's low 20 bits:
 *   sum = step * high((phase >> 4) * factor) + start * factor
 * (32-bit, both products signed) placed in the high dword and shifted down
 * by 62 - exponent. Above exponent 61 the high dwords are shifted up and
 * the original leaves register values in the low dwords: the x low dword
 * is start.z * factor, y's is start.z:start.y, and z's high word is the
 * factor (its low word the original's stack pointer; 0 here).
 */
void fe2_math_place_on_arc(fe2_object *o, uint16_t angle, uint32_t phase, int16_t factor, uint16_t exponent)
{
    uint16_t base = (uint16_t)((phase >> 16) & 0xfff0u);
    int16_t start[3], next[3];
    uint32_t sum[3];
    int16_t weight;
    unsigned i;
    fe2_math_direction(angle, base, start);
    fe2_math_direction(angle, (uint16_t)(base + 0x10u), next);
    weight = (int16_t)(uint16_t)(((uint32_t)(uint16_t)(phase >> 4) * (uint16_t)factor) >> 16);
    for (i = 0; i < 3; ++i) {
        int16_t step = (int16_t)(uint16_t)((uint16_t)next[i] - (uint16_t)start[i]);
        o->vector_delta[i] = (uint16_t)step;
        sum[i] = (uint32_t)((int32_t)step * weight) + (uint32_t)((int32_t)start[i] * factor);
    }
    /* A signed comparison of the exponent with 61; the right shift's count
     * is the 16-bit 61 - exponent (+1, at least 1). */
    if ((int16_t)exponent > 0x3d) {
        unsigned shift = (unsigned)(uint16_t)(exponent - 0x3d) - 1u;
        for (i = 0; i < 3; ++i) o->position_high[i] = shift > 31 ? 0 : sum[i] << shift;
        o->position_low[0] = (uint32_t)((int32_t)start[2] * factor);
        o->position_low[1] = (uint32_t)(uint16_t)start[2] << 16 | (uint16_t)start[1];
        o->position_low[2] = (uint32_t)(uint16_t)factor << 16;
        return;
    }
    for (i = 0; i < 3; ++i) set_position(o, i, sar64((uint64_t)sum[i] << 32, right_shift(exponent)));
}
