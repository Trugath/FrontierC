/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the random generator and overlay selection. */
#include "objects.h"

static uint32_t ror32(uint32_t v, unsigned n) { return v >> n | v << (32 - n); }
static uint32_t rol32(uint32_t v, unsigned n) { return v << n | v >> (32 - n); }

/* A' = A + ROR(B xor 0x1234567f, 7), B' = ROR(A, 19); returns B' (EAX)
 * and, through mix, the value added (EDX). */
uint32_t fe2_objects_random(fe2_objects_state *s, uint32_t *mix)
{
    uint32_t a = s->random[0], d = ror32(s->random[1] ^ UINT32_C(0x1234567f), 7);
    s->random[0] = a + d;
    s->random[1] = ror32(a, 19);
    if (mix) *mix = d;
    return s->random[1];
}

/* B' = ROL(B, 5), A' = ROR(A + B + B', 16); the high word of range * A'. */
uint16_t fe2_objects_random_below(fe2_objects_state *s, uint16_t range)
{
    uint32_t b = rol32(s->random[1], 5), a = ror32(s->random[0] + s->random[1] + b, 16);
    s->random[0] = a; s->random[1] = b;
    return (uint16_t)(((uint32_t)range * (uint16_t)a) >> 16);
}

uint32_t fe2_objects_scale_pair(uint16_t ax, uint32_t *sum, uint32_t *pair)
{
    uint32_t product;
    unsigned i;
    *sum += *pair;
    for (i = 0; i < 5; ++i) *pair = rol32(*pair, 1);
    *sum += *pair;
    product = (uint32_t)ax * (uint16_t)(*sum >> 16);
    return (product >> 16) << 16 | (product & 0xffffu);
}

void fe2_objects_select_overlay(fe2_objects_state *s, uint16_t slot,
                                void (*notify)(void *context, uint16_t previous), void *context)
{
    uint16_t previous = s->selected_overlay;
    s->selected_overlay = slot;
    if (previous && notify) notify(context, previous);
}
