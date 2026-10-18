/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_PANELS_PANELS_INTERNAL_H
#define FE2_PANELS_PANELS_INTERNAL_H

#include "panels.h"
#include "panel_tables.h"

enum {
    MARKER_COLOUR = 1, GAUGE_SECOND_COLOUR = 2, TEXT_COLOUR = 3, GAUGE_FUEL_COLOUR = 4,
    PANEL_Y = 0xa8, TEXT_Y = 0xa9,
    TEXT_STYLE_PARAMETER = 7, TEXT_VALUE_PARAMETER = 1
};

/* panel_drawing.c */
void fe2_panels_draw_clock(fe2_panel_state *s, const fe2_panel_services *game);
void fe2_panels_draw_date(fe2_panel_state *s, const fe2_panel_services *game);
void fe2_panels_draw_mode(fe2_panel_state *s, const fe2_panel_services *game, int16_t mode,
                       uint16_t argument);

#endif
