/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_GAME_MODEL_H
#define FE2_GAME_MODEL_H

/*
 * A model descriptor in the model bank. Some fields hold offsets of other
 * data relative to the descriptor, so the descriptor is kept as its bytes
 * with named field offsets.
 */
#include <stdint.h>

enum {
    FE2_MODEL_RADIUS = 0x0a,           /* word: the radius exponent... */
    FE2_MODEL_RADIUS_EXTRA = 0x0c,     /* word:...and its addend */
    FE2_MODEL_VARIANTS = 0x13,         /* byte: high nibble, the variant flags */
    FE2_MODEL_SCALE_A = 0x16,          /* words: two scale mantissas */
    FE2_MODEL_SCALE_B = 0x18,
    FE2_MODEL_MARKER = 0x1a,           /* word: offset of a byte; nonzero marks variant 0x80 */
    FE2_MODEL_FLIGHT = 0x1c            /* word: offset of the flight parameters, or 0 */
};

/* Flight parameter words (from FE2_MODEL_FLIGHT). */
enum {
    FE2_FLIGHT_THIRD_LIMIT = 0x00, FE2_FLIGHT_LIMIT = 0x02, FE2_FLIGHT_CAPACITY_CLASS = 0x04,
    FE2_FLIGHT_HULL = 0x06, FE2_FLIGHT_CAPACITY = 0x08, FE2_FLIGHT_DRIVE = 0x14
};

static inline uint16_t fe2_model_word(const uint8_t *model, unsigned offset)
{
    return (uint16_t)(model[offset] | model[offset + 1] << 8);
}

#endif
