/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_POSITION_INTERNAL_H
#define FE2_POSITION_INTERNAL_H

/* Shared between the position files. */
#include "position.h"

/* Three 64-bit values reduced to words by a common shift (returned): a
 * magnitude envelope of their ones' complements picks it. Below 2^31 the
 * words are the low dwords shifted until the envelope is under 0x4000;
 * above, the values shift until the envelope's high dword is clear and the
 * words are their second words halved (17 more bits). */
unsigned fe2_position_reduce(const int64_t v[3], int16_t w[3]);

/* The high word of twice the 32-bit wrapped a. b. */
int16_t fe2_position_dot(int16_t a0, int16_t a1, int16_t a2, int16_t b0, int16_t b1, int16_t b2);

#endif
