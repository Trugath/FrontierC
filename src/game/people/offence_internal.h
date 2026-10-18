/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_OFFENCES_OFFENCE_INTERNAL_H
#define FE2_OFFENCES_OFFENCE_INTERNAL_H

#include "offences.h"

enum {
    ROLE_BODY = 1, ROLE_SPECIAL = 0x0b, PATROL_MARKER = 0x0a,
    POLICE_BODY = 0x10, SOURCE_LINKED = 0x20, SOURCE_TRICKLE = 0x40,   /* flags +0x118 */
    ALERTED = 0x01
};

/* A police source's ships still to launch (+0xb2, signed). */
static inline int16_t fe2_offences_ships_left(const fe2_object *o) { return (int16_t)o->as.body.radius_class; }
static inline void fe2_offences_set_ships_left(fe2_object *o, int16_t n) { o->as.body.radius_class = (uint16_t)n; }

#endif
