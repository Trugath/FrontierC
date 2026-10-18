/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_PANELS_PANELS_H
#define FE2_PANELS_PANELS_H

/*
 * Control panel (panels): the panel strip below the view - the selected
 * panel's buttons, the clock and date, the flight status message, three
 * double-line gauges (fuel, equipment busy time and a third resident
 * value) - and the scanner markers of nearby objects.
 *
 * Drawing and object services come from the rest of the game through
 * `fe2_panel_services`.
 */
#include <stdint.h>

#include "game/call_args.h"
#include "game/object.h"

typedef struct fe2_panel_state {
    uint8_t enabled;
    uint16_t selection;           /* overlay owning the selected panel; 0 none */
    uint8_t default_selected;
    uint8_t selection_argument;
    uint8_t refresh_style;        /* bit 7: partial refresh */
    fe2_object *ship;
    uint16_t line_colours;        /* colour pair for line drawing */
    uint32_t time_of_day;
    uint32_t day;
    uint16_t third_gauge;
    uint32_t simulation_time_step;
    uint8_t ship_status;          /* bit 0: status message 0x8619 */
    uint8_t flight_state;
    uint8_t scanner_mode;         /* bit 7: markers clipped to the panel */
    uint16_t pointer_redraw;      /* the pointer redraw interval (1: redraw at the next tick) */
    uint8_t colours[11];          /* palette indices */
    fe2_object_list *objects;
} fe2_panel_state;

/* What the scanner callback compares candidates against. The original
 * passes these in its scratch block. */
typedef struct fe2_scanner_view {
    uint8_t target;               /* handle (or parent handle) to show */
    uint8_t own;                  /* handle to exclude */
    uint16_t flags;               /* bit 3: markers enabled */
    fe2_object *viewer;           /* object whose model colours the marker */
} fe2_scanner_view;

/* Model descriptor colour value (resident 0x50: descriptor +0x1c offset, +6). */
typedef struct fe2_model_colour {
    int present;                  /* nonzero offset at +0x1c */
    uint16_t value;
} fe2_model_colour;

/* Three signed 64-bit coordinates. */
typedef struct fe2_wide_vector {
    uint32_t low[3], high[3];
} fe2_wide_vector;

struct fe2_panel_services;
typedef void (*fe2_panel_visitor)(void *context, fe2_object *object);
typedef void (*fe2_gauge_callback)(fe2_panel_state *state, const struct fe2_panel_services *game);

typedef struct fe2_panel_services {
    void *opaque;
    void (*draw_sprite)(void *opaque, uint16_t sprite, uint16_t x, uint16_t y);
    void (*draw_sprite_over)(void *opaque, uint16_t sprite, uint16_t x, uint16_t y);
    void (*draw_text)(void *opaque, uint16_t text, uint8_t colour, uint16_t x, uint16_t y,
                      const fe2_call_args *args);
    void (*draw_text_over)(void *opaque, uint16_t text, uint8_t colour, uint16_t x,
                           uint16_t y, const fe2_call_args *args);
    /* Line in the colour pair state->line_colours. */
    void (*draw_line)(void *opaque, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void (*present)(void *opaque);
    void (*selection_changed)(void *opaque);
    void (*install_gauges)(void *opaque, fe2_gauge_callback gauges);
    void (*object_event)(void *opaque, uint8_t event);
    void (*secondary_basis)(void *opaque, fe2_object *object, const fe2_call_args *args);
    fe2_object *(*linked_object)(void *opaque, fe2_object *object);
    /* Position of `object` relative to `viewer`. */
    void (*relative_position)(void *opaque, fe2_object *object, const fe2_object *viewer,
                              fe2_wide_vector *position);
    fe2_model_colour (*model_colour)(void *opaque, uint16_t model);
    /* Scanner marker at screen x, base y and signed height; parameters 3
     * (clip to panel) and 8 (colour byte). */
    void (*draw_marker)(void *opaque, uint16_t x, uint16_t y, uint16_t height,
                        const fe2_call_args *args);
    /* The selected panel's overlay (+0x24 entry); returns its AL. */
    uint8_t (*notify_panel)(void *opaque, uint16_t overlay, uint16_t request);
    void (*visit_objects)(void *opaque, fe2_object_list *objects, fe2_panel_visitor visitor,
                          void *context);
} fe2_panel_services;

/* 0x6e: AL=1 resets the selection and copies the palette indices. */
void fe2_panels_initialize(fe2_panel_state *state, uint8_t request);
/* 0x76: events 3 and 0xf redraw the clock and date. */
void fe2_panels_event(fe2_panel_state *state, const fe2_panel_services *game, uint16_t event);
/* 0x7a: scanner markers and gauges. `view_flags` (scratch 0x7c0) and
 * `viewer` (0x7dc, replaced by the ship while docked) pass through to the
 * marker callback. */
void fe2_panels_render_objects(fe2_panel_state *state, const fe2_panel_services *game,
                               uint16_t view_flags, fe2_object *viewer);
/* 0x8a: AX >= 2 draws panel mode AX; otherwise select the panel of overlay
 * `overlay` (BX). `argument` is scratch 0x7c4. */
void fe2_panels_select(fe2_panel_state *state, const fe2_panel_services *game, int16_t request,
                       uint16_t overlay, uint16_t argument);
/* 0x96: refresh the panel. */
void fe2_panels_refresh(fe2_panel_state *state, const fe2_panel_services *game);
/* the three gauges. */
void fe2_panels_gauges(fe2_panel_state *state, const fe2_panel_services *game);
/* a scanner marker for `candidate`. */
void fe2_panels_scanner_marker(fe2_panel_state *state, const fe2_panel_services *game,
                               const fe2_scanner_view *view, fe2_object *candidate);

#endif
