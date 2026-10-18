/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_NAVIGATION_NAVIGATION_H
#define FE2_NAVIGATION_NAVIGATION_H

/*
 * Navigation and autopilot panel (navigation): the system map. It covers:
 * - the map's object list: a filtered copy of the system's objects, or a
 *   system generated for the map;
 * - the view camera, its scale and its time point;
 * - keys for options, zoom, time stepping and the time slider;
 * - target picking, labels and markers.
 *
 * It also provides one motion service, which replaces an object's motion
 * sample. Drawing, objects, the other overlays and the resident math come
 * through `fe2_navigation_services`.
 */
#include <stdint.h>

#include "game/call_args.h"
#include "game/object.h"
#include "game/scalar.h"
#include "game/text_table.h"

enum {
    /* Flags. */
    FE2_NAV_TARGET_PICKING = 0x01,  /* a click picks the autopilot target */
    FE2_NAV_HIDE_LABELS = 0x02,
    FE2_NAV_HIDE_PROJECTIONS = 0x04,
    FE2_NAV_OPTIONS_OPEN = 0x08,
    FE2_NAV_COPIED_LIST = 0x10,     /* the map shows the current system */
    /* Records the generated map system is built in (a stack context in the
     * original). */
    FE2_NAV_SYSTEM_RECORD_BYTES = 0xf44,
    /* The resident text line in the stack segment. */
    FE2_NAV_TEXT_LINE = 0xfa08
};

typedef struct fe2_navigation_state {
    /*
     * View camera, an object record the map renderer reads:
     * - orientation: the view basis;
     * - render_state (+0x13);
     * - position: the camera offset;
     * - parent_handle (+0x44): the handle of the object the map centres on.
     */
    fe2_object camera;
    uint16_t view_angle[2];
    uint8_t flags;                /* FE2_NAV_* */
    uint8_t default_selection;
    fe2_scalar scale;
    uint64_t time;                /* map time point (time of day, day) */
    fe2_scalar saved_scale;
    uint16_t basis_angle[2];      /* shared with the math service */
    uint32_t time_of_day;         /* bit 8 blinks the target marker */
    uint32_t day;
    uint8_t autopilot_target;
    uint8_t selection_found;
    uint8_t flight_state;         /* 4: the ship follows a picked target */
    fe2_object *ship;
    uint16_t click_x, click_y;
    uint16_t pointer_x, pointer_y;
    uint8_t last_key;
    uint8_t key_repeat;
    uint32_t frame_step;
    uint16_t panel_view;          /* 0xffff while the map shows */
    uint8_t colours[11];          /* palette indices */
    fe2_object_list *map_objects;
    fe2_object_list *objects;
    uint8_t *stack_segment;       /* the text line at FE2_NAV_TEXT_LINE */
} fe2_navigation_state;

/* Model descriptor fields (resident 0x50). */
typedef struct fe2_navigation_model {
    uint16_t radius[2];           /*, +0xc */
    int has_size;                 /* nonzero offset at +0x1c */
    uint16_t size;                /* the word 6 bytes past that offset */
} fe2_navigation_model;

/* Current system seeds and display name. */
typedef struct fe2_system_seeds {
    uint32_t seed_a, seed_b;
    const char *name;
} fe2_system_seeds;

typedef void (*fe2_navigation_visitor)(void *context, fe2_object *object);
/* Visitor of projected objects, with their screen position (AX, BX). */
typedef void (*fe2_navigation_screen_visitor)(void *context, fe2_object *object, uint16_t x,
                                              uint16_t y);

typedef struct fe2_navigation_services {
    void *opaque;

    /* Panels, keys and drawing. */
    void (*set_panel_mode)(void *opaque, uint16_t mode);
    /* Nonzero (ZF clear) when *key holds a key or event. */
    int (*poll_key)(void *opaque, uint8_t *key);
    void (*forward_key)(void *opaque, uint8_t key);
    void (*show_panel)(void *opaque);       /*; reads state->panel_view */
    void (*restore_view)(void *opaque);
    void (*set_controls)(void *opaque, uint16_t layout, uint8_t buttons);
    void (*set_pointer)(void *opaque, uint16_t x, uint16_t y);
    /* Widget words: left, top, right, bottom, event; 0xffff ends. */
    void (*set_widgets)(void *opaque, const uint16_t *widgets);
    void (*draw_sprite)(void *opaque, uint16_t sprite, uint16_t x, uint16_t y);
    void (*draw_string)(void *opaque, const uint8_t *text, uint8_t colour, uint16_t x,
                        uint16_t y);
    void (*draw_name)(void *opaque, const fe2_object *object, uint8_t colour, uint16_t x,
                      uint16_t y);
    void (*format_table_text)(void *opaque, uint16_t selector, const fe2_text_table *table,
                              const fe2_call_args *args, fe2_text_out *out);
    void (*show_message)(void *opaque, uint16_t text, const fe2_call_args *args); /* station */
    void (*play_effect)(void *opaque, uint16_t effect);                      /* sound effect */

    /* Object lists. The lists hold a handle table first: byte `handle`
     * bit 3 marks a selectable handle. */
    uint8_t (*handle_flags)(void *opaque, const fe2_object_list *objects, uint8_t handle);
    /* Copy the whole list (0x80ec bytes; an in-module block copy). */
    void (*copy_objects)(void *opaque, fe2_object_list *to, const fe2_object_list *from);
    void (*clear_objects)(void *opaque, const fe2_call_args *args);         /*, parameter 12 */
    void (*remove_object)(void *opaque, fe2_object *object);
    void (*visit_objects)(void *opaque, fe2_object_list *objects,
                          fe2_navigation_visitor visitor, void *context);
    void (*visit_projected)(void *opaque, fe2_object_list *objects,
                            fe2_navigation_screen_visitor visitor, void *context);
    void (*refresh_objects)(void *opaque, fe2_object_list *objects);
    void (*set_objects_time)(void *opaque, fe2_object_list *objects, uint64_t time);
    fe2_object *(*lookup_object)(void *opaque, uint8_t handle, const fe2_call_args *args);
    fe2_navigation_model (*model)(void *opaque, uint16_t model);
    /* Render the map; parameter 11 is the camera, 12 the list. */
    void (*render_map)(void *opaque, const fe2_call_args *args);
    /* Draw `object` as seen from the camera in parameter 11. */
    void (*draw_object)(void *opaque, fe2_object *object, const fe2_call_args *args);
    uint16_t (*view_angles)(void *opaque, uint16_t *second);
    fe2_system_seeds (*current_system)(void *opaque);                       /* chart, AX=0 */
    /* systems system action: parameter 2 mode (0 generate, 1 instantiate), 4/5
     * the time, 8 the records, 10 the system name. `seeds` (DX:AX, CX:BX)
     * is given for generation. */
    void (*system_action)(void *opaque, const fe2_system_seeds *seeds, const fe2_call_args *args);
    /* Motion: parameters 0/1 the old sample, 2/3 the change. */
    void (*motion_update)(void *opaque, fe2_object *object, const fe2_call_args *args);
    void (*reference_update)(void *opaque, fe2_object *object);

    /* Resident math (math). */
    fe2_scalar (*normalize_word)(void *opaque, fe2_scalar value);
    /* Doubled signed product: high word, exponents added. */
    fe2_scalar (*multiply)(void *opaque, fe2_scalar a, fe2_scalar b);
    /* Basis from state->basis_angle (resident 0x1f35/0x1f37), permutation B. */
    void (*basis_b)(void *opaque, int16_t basis[9]);
    /* Add `object`'s vector times the signed wide scalar in parameters 2/3
     * to its position. */
    void (*transform_vector)(void *opaque, fe2_object *object, const fe2_call_args *args);
} fe2_navigation_services;

/* What the installed callbacks work on. */
typedef struct fe2_navigation_pass {
    fe2_navigation_state *state;
    const fe2_navigation_services *game;
} fe2_navigation_pass;

/* The motion samples the 0x82 service publishes (scratch 0x7bc, 0x7c4). */
typedef struct fe2_motion_samples {
    uint64_t previous;
    uint64_t change;        /* the new sample when the object skips motion */
} fe2_motion_samples;

/* 0x6e: AL=1 resets the map; AH selects the default object (1: 0x60,
 * 2: 0x70, otherwise 0x6f). */
void fe2_navigation_initialize(fe2_navigation_state *state, uint8_t request, uint8_t mode);
/* 0x72: show the current system. */
void fe2_navigation_select(fe2_navigation_state *state, const fe2_navigation_services *game);
/* 0x7a: handle a key, then rebuild and render the view. */
void fe2_navigation_render(fe2_navigation_state *state, const fe2_navigation_services *game);
/* 0x7e: the time point and target labels. */
void fe2_navigation_render_targets(fe2_navigation_state *state,
                                   const fe2_navigation_services *game);
/* 0x82: store `sample` in `object` (scratch 0x7dc) and move it. */
void fe2_navigation_update_motion(const fe2_navigation_services *game, fe2_object *object,
                                  uint64_t sample, fe2_motion_samples *published);
/* 0x8a: show a generated system. */
void fe2_navigation_select_generated(fe2_navigation_state *state,
                                     const fe2_navigation_services *game);
/* 0x8e: leave the map, keeping the copied view's selection and scale. */
void fe2_navigation_restore(fe2_navigation_state *state, const fe2_navigation_services *game);
/* 0x92: time point text `selector` (AX & 0x3fe), parameters from the caller. */
void fe2_navigation_time_point(const fe2_navigation_services *game, uint16_t selector,
                               fe2_text_out *out);

/* Installed callbacks; `pass` is a fe2_navigation_pass. */
void fe2_navigation_filter_object(void *pass, fe2_object *object);
void fe2_navigation_label_object(void *pass, fe2_object *object, uint16_t x, uint16_t y);
void fe2_navigation_click_object(void *pass, fe2_object *object, uint16_t x, uint16_t y);
void fe2_navigation_project_object(void *pass, fe2_object *object);

#endif
