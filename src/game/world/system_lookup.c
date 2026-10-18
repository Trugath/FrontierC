/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the keyed search and the fallback by map distance. */
#include "system_catalogue.h"

/*
 * Key bits: 0..2 a bias, 6..18 the map x coordinate, 19..31 the map y
 * coordinate. The distance is the sum of the squares of |x - 0x1718| + bias
 * and |y - 0x1524| + bias, all in 16 bits. Larger distances take earlier
 * fallbacks.
 */
enum { CENTRE_X = 0x1718, CENTRE_Y = 0x1524, COORDINATE_MASK = 0x1fff, BIAS_MASK = 7 };

static const uint16_t fallback_thresholds[FE2_SYSTEM_FALLBACKS - 1] = {
    0x1fa4, 0xe1, 0x64, 0x31, 0x19, 9
};

static uint16_t offset_from(uint16_t coordinate, uint16_t centre, uint16_t bias)
{
    uint16_t difference = (uint16_t)(coordinate - centre);
    if (difference & 0x8000u) difference = (uint16_t)(0u - difference);
    return (uint16_t)(difference + bias);
}

/* The original squares with IMUL and keeps the low word, which a signed and
 * an unsigned product share. */
static uint16_t square(uint16_t value)
{
    return (uint16_t)((uint32_t)value * value);
}

const fe2_catalogue_record *fe2_system_fallback(const fe2_system_catalogue *catalogue, uint32_t key)
{
    uint16_t bias = (uint16_t)(key & BIAS_MASK);
    uint16_t x = offset_from((uint16_t)((key >> 6) & COORDINATE_MASK), CENTRE_X, bias);
    uint16_t y = offset_from((uint16_t)(key >> 19), CENTRE_Y, bias);
    uint16_t distance = (uint16_t)(square(x) + square(y));
    unsigned index = 0;
    while (index < FE2_SYSTEM_FALLBACKS - 1 && distance < fallback_thresholds[index]) ++index;
    return &catalogue->fallbacks[index];
}

const fe2_catalogue_record *fe2_system_find(const fe2_system_catalogue *catalogue, uint32_t key)
{
    unsigned i;
    for (i = 0; i < catalogue->count && catalogue->records[i].key != 0; ++i)
        if (catalogue->records[i].key == key) return &catalogue->records[i];
    return fe2_system_fallback(catalogue, key);
}
