/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_GAME_SCALAR_H
#define FE2_GAME_SCALAR_H

/* Normalized value used by the resident math: a signed or unsigned 16-bit
 * mantissa and an exponent. */
#include <stdint.h>

typedef struct fe2_scalar {
    uint16_t mantissa;
    uint16_t exponent;
} fe2_scalar;

#endif
