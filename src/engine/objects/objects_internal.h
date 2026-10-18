/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_FRONTIER_OBJECTS_INTERNAL_H
#define FE2_FRONTIER_OBJECTS_INTERNAL_H

/* Helpers shared between the objects sources. */
#include "objects.h"

static inline uint16_t fe2_objects_word(const uint8_t *d, unsigned at) { return (uint16_t)(d[at] | d[at + 1] << 8); }
static inline uint16_t fe2_objects_preamble_word(unsigned at)
{
    return (uint16_t)(fe2_objects_preamble[at] | fe2_objects_preamble[at + 1] << 8);
}
/* The model's extension (its descriptor + the descriptor's word 0x1c). */
static inline const uint8_t *fe2_objects_extension(const fe2_objects_services *v, const fe2_object *o)
{
    const uint8_t *d = v->model(v->context, o->model);
    return d + fe2_objects_word(d, 0x1c);
}
/* A drive's mass back to the player's cargo. */
static inline void fe2_objects_add_mass(fe2_objects_state *s, uint16_t mass)
{
    s->cargo[FE2_CARGO_TOTAL] = (uint16_t)(s->cargo[FE2_CARGO_TOTAL] + mass);
    s->cargo_space = (uint16_t)(s->cargo_space + mass);
    s->free_capacity = (uint16_t)(s->free_capacity + mass);
}
fe2_object *fe2_objects_fragment(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *source, uint16_t kind);

#endif
