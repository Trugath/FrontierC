/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_HYPERSPACE_HYPERSPACE_INTERNAL_H
#define FE2_HYPERSPACE_HYPERSPACE_INTERNAL_H

#include "hyperspace.h"

enum {
    /* Parameters. */
    WORD_PARAMETER = 0, DWORD_PARAMETER = 1, MODE_PARAMETER = 2, TIME_PARAMETER = 4,
    DAY_PARAMETER = 5, SCALE_PARAMETER = 6, EXPONENT_PARAMETER = 7, MESSAGE_PARAMETER = 7,
    RECORDS_PARAMETER = 8, NAME_PARAMETER = 10, OBJECTS_PARAMETER = 12
};

static inline uint64_t fe2_hyperspace_clock(const fe2_hyperspace_state *s)
{
    return (uint64_t)s->day << 32 | s->time_of_day;
}

/* jump.c */
uint16_t fe2_hyperspace_destroy_drive(fe2_hyperspace_state *s, fe2_object *object);
void fe2_hyperspace_start_motion(fe2_hyperspace_state *s, const fe2_hyperspace_services *game,
                          fe2_object *object, uint16_t scale, uint16_t exponent);

#endif
