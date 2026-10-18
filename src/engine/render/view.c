/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "view.h"

static int16_t word(unsigned value)
{
    value &= 65535u;
    return (int16_t)(value < 32768u ? (int)value : (int)value - 65536);
}

static uint32_t magnitude(int32_t value)
{
    return value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
}

static int16_t shift_word(int16_t value, unsigned count)
{
    unsigned bits = (uint16_t)value;
    count &= 31u;
    if (!count) return value;
    if (count >= 16) return value < 0 ? -1 : 0;
    return word((bits >> count) | (value < 0 ? 65535u << (16 - count) : 0u));
}

static int16_t shift_dword_to_word(int32_t value, unsigned count)
{
    uint32_t bits = (uint32_t)value;
    if (count) bits = (bits >> count) | (value < 0 ? UINT32_MAX << (32 - count) : 0u);
    return word(bits);
}

static void transform(const int16_t vector[3], const int16_t basis[9], int16_t out[3])
{
    unsigned row, column;
    for (row = 0; row < 3; ++row) {
        uint32_t sum = 0;
        for (column = 0; column < 3; ++column)
            sum += (uint32_t)((int32_t)vector[column] * basis[row * 3 + column]);
        out[row] = word((sum * 2u) >> 16);
    }
}

void fe2_render_prepare_view(const int32_t position[3], const int16_t direction[3],
                              const int16_t basis[9], uint16_t exponent, fe2_render_view *out)
{
    fe2_render_view result;
    int16_t normalized[3];
    unsigned shift = 0, i, delta;
    uint32_t largest = 0;
    for (i = 0; i < 3; ++i)
        if (magnitude(position[i]) > largest) largest = magnitude(position[i]);
    while ((largest >> shift) >= 0x3f00u) ++shift;
    for (i = 0; i < 3; ++i) normalized[i] = shift_dword_to_word(position[i], shift);
    result.shift = (uint16_t)shift;
    delta = (unsigned)(uint16_t)exponent;
    if ((int)word(delta) > (int)(shift + 7u)) {
        delta -= shift + 7u;
        result.shift = (uint16_t)(result.shift + delta);
        for (i = 0; i < 3; ++i) normalized[i] = shift_word(normalized[i], delta);
    }
    for (i = 0; i < 3; ++i) normalized[i] = word(0u - (uint16_t)normalized[i]);
    transform(normalized, basis, result.position);
    transform(direction, basis, result.direction);
    *out = result;
}

int fe2_render_center_visible(const int32_t position[3], uint32_t radius)
{
    uint32_t far_depth = (uint32_t)position[2] + radius;
    unsigned i;
    if (far_depth & UINT32_C(0x80000000)) return 0;
    for (i = 0; i < 2; ++i) {
        uint32_t distance = magnitude(position[i]) - radius;
        if (distance & UINT32_C(0x80000000)) distance = 0;
        if (distance > far_depth) return 0;
    }
    return 1;
}

int fe2_render_model_visible(const int32_t position[3], uint16_t bound, uint16_t exponent)
{
    return fe2_render_center_visible(position, (uint32_t)bound << (exponent & 31u));
}
