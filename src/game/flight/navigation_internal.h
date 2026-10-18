/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_NAVIGATION_NAVIGATION_INTERNAL_H
#define FE2_NAVIGATION_NAVIGATION_INTERNAL_H

#include "navigation.h"
#include "navigation_tables.h"

enum {
    /* Palette entries (state->colours). */
    LABEL_COLOUR = 1, TIME_COLOUR = 10,
    /* Object roles (+0x5e) and states (+0xd3). */
    ROLE_BODY = 1, ROLE_CONVERTIBLE = 3, ROLE_MARKER = 5, ROLE_SPECIAL = 0x0b,
    BEHAVIOUR_FIXED = 0x84, BEHAVIOUR_EXCLUDED = 0x3c, BEHAVIOUR_FOLLOW = 0x20,
    OBJECT_HIDDEN = 0x20,
    SPECIAL_MODEL = 0xba,
    /* Parameters. */
    CAMERA_PARAMETER = 11, OBJECTS_PARAMETER = 12
};

static inline uint64_t fe2_navigation_clock(const fe2_navigation_state *s)
{
    return (uint64_t)s->day << 32 | s->time_of_day;
}

/* navigation_setup.c */
void fe2_navigation_draw_controls(fe2_navigation_state *s, const fe2_navigation_services *game);
/* navigation_view.c */
uint16_t fe2_navigation_selected_scale(fe2_navigation_state *s, const fe2_navigation_services *game);
void fe2_navigation_build_view(fe2_navigation_state *s, const fe2_navigation_services *game);
void fe2_navigation_camera_args(fe2_navigation_state *s, fe2_call_args *args);

#endif
