/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_GALACTIC_MAP_PORT_H
#define FE2_GALACTIC_MAP_PORT_H

#include <stddef.h>
#include <stdint.h>
#include "../world/system_record.h"
#include "game/call_args.h"

enum { FE2_MAP_OK=0, FE2_MAP_INVALID=-1, FE2_MAP_DIVIDE_FAULT=-2,
       FE2_MAP_MISSING_SERVICE=-3, FE2_MAP_BUFFER_SMALL=-4,
       FE2_MAP_MAX_SYSTEMS=62, FE2_MAP_TEXT_CAPACITY=256 };

typedef struct fe2_map_position { uint32_t y, x, z; } fe2_map_position;
typedef struct fe2_map_seed { uint32_t a, b; } fe2_map_seed;
typedef struct fe2_map_samples { uint8_t a, b, c, d; } fe2_map_samples;
typedef struct fe2_map_record { uint8_t depth, style, y, x; } fe2_map_record;
typedef struct fe2_map_projection { uint32_t position[3]; uint16_t flags; } fe2_map_projection;
typedef struct fe2_map_scene {
    uint16_t sector_y, sector_x, flags, draw_kind, marker_range;
    uint32_t position[3];
    int16_t basis[9]; /* reordered map-control basis for alternate models */
    unsigned count;
    uint8_t has_origin;
    fe2_map_record origin;
    fe2_map_record records[FE2_MAP_MAX_SYSTEMS];
    uint16_t identifiers[FE2_MAP_MAX_SYSTEMS];
} fe2_map_scene;

typedef struct fe2_map_details {
    uint8_t status;
    uint16_t result_a, result_b, government, population_text;
    uint8_t trade[31];
} fe2_map_details;
typedef struct fe2_map_query {
    int available;
    uint32_t reference, distance, coordinate_mix, table_value;
    fe2_map_position position;
    uint16_t descriptor, index;
    uint8_t classification, status;
    char name[FE2_MAP_TEXT_CAPACITY];
} fe2_map_query;
typedef struct fe2_map_candidate { int available; uint32_t reference; } fe2_map_candidate;

typedef struct fe2_map_state {
    fe2_map_position home, selected;
    uint32_t center_y, center_x, depth;
    uint32_t current_system;
    uint32_t selection_reference;    /* the selected system, which the jump takes */
    uint32_t selection_distance;     /* its distance */
    uint32_t selection_mix, selection_value;
    fe2_map_seed seed;
    uint16_t display_flags, sample_count, selected_index, selected_descriptor;
    uint16_t travel_result, detail_result_a, detail_result_b, marker_range;
    uint8_t selection_status;        /* nonzero with a system selected */
    uint8_t selection_dirty, detail_status;
    uint8_t menu_open, refresh_phase, named_bodies;
    int8_t mode;
    /* Shared UI colours: status 0x8935, name 0x8938, classification 0x8936, body text 0x8939. */
    uint8_t status_colour, name_colour, classification_colour, information_colour; /* 0x8938 0x8936 0x8939 */
    const fe2_object *ship;
    fe2_record_space bodies;
    char selected_name[FE2_MAP_TEXT_CAPACITY];
    char selected_classification[FE2_MAP_TEXT_CAPACITY];
} fe2_map_state;

enum fe2_map_pan { FE2_MAP_PAN_Y_MINUS=1, FE2_MAP_PAN_Y_PLUS=2,
                  FE2_MAP_PAN_X_PLUS=4, FE2_MAP_PAN_X_MINUS=8 };
typedef struct fe2_map_input {
    uint8_t pan;             /* Independent direction bits; opposites may both run. */
    uint8_t fast;
    uint8_t has_command;     /* Separate from command zero, as in the input service. */
    uint8_t command;
} fe2_map_input;

/* Every dependency has a C domain interface. Drawing pointers refer to actual
 * C scenes/strings/records; callbacks cannot see a CPU or an address space. */
typedef struct fe2_map_services {
    void *opaque;
    int (*density_samples)(void *, uint16_t y, uint16_t x, fe2_map_samples *);
    int (*random_below)(void *, uint16_t bound, uint16_t *value);
    int (*random)(void *, uint32_t *value);
    int (*vector_length)(void *, const int16_t components[3], uint16_t *value);
    int (*system_details)(void *, uint32_t reference, unsigned mode, fe2_map_details *);
    int (*travel)(void *, const fe2_object *ship, uint32_t distance, uint16_t *result, int *negative);
    int (*generate_system)(void *, const fe2_map_query *, fe2_record_space *, uint8_t *named_bodies);
    int (*expand_text)(void *, const char *source, const fe2_call_args *, char *, size_t);
    int (*format_number)(void *, uint16_t selector, const fe2_call_args *, char *, size_t);
    int (*draw_text)(void *, const char *, uint8_t colour, uint16_t x, uint16_t y, int centered);
    int (*draw_text_flow)(void *, const char *, uint8_t colour, uint16_t *x, uint16_t *y);
    int (*draw_resident_text)(void *, uint16_t selector, uint8_t colour, uint16_t x, uint16_t y, const fe2_call_args *);
    int (*draw_button)(void *, uint16_t button, uint16_t x, uint16_t y);
    int (*clear_panel)(void *);
    int (*show_panel)(void *);
    int (*present)(void *);
    int (*set_palette)(void *, const uint8_t *rgb6, unsigned colours);
    int (*set_controls)(void *, uint16_t layout, uint8_t buttons);
    int (*set_panel_mode)(void *, uint16_t mode);
    int (*reset_view)(void *);
    int (*clear_projection)(void *);
    int (*primary_panel)(void *, unsigned request, uint16_t banner);
    int (*secondary_view)(void *, int activate);
    int (*forward_command)(void *, uint8_t command);
    int (*basis)(void *, uint16_t angles[2], int16_t matrix[9]);
    int (*project)(void *, const int16_t matrix[9], const int16_t vector[3], uint32_t projected[3]);
    int (*draw_scene)(void *, const fe2_map_scene *, int extra);
    int (*draw_pixel)(void *, uint16_t x, uint8_t y, uint8_t colour);
    int (*draw_block)(void *, uint16_t left, uint16_t right, uint8_t top, uint8_t bottom, uint8_t colour);
    int (*input_pending)(void *);
    int (*draw_crosshair)(void *, uint16_t x, uint16_t y, uint8_t colour);
    int (*draw_line)(void *, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t colour);
    int (*hierarchy_node)(void *, uint16_t selection, uint16_t spacing, uint16_t *x, uint16_t *y);
    int (*draw_body_sprite)(void *, uint16_t sprite, uint16_t x, uint16_t y);
    /* Normalized body arithmetic. Mantissa/exponent are domain quantities. */
    int (*body_scale)(void *, uint16_t mantissa, uint16_t exponent,
                       uint16_t factor, uint16_t factor_exponent, uint16_t *out_mantissa, uint16_t *out_exponent);
    int (*body_number)(void *, uint16_t mantissa, uint16_t exponent, int sign, uint32_t *value);
} fe2_map_services;

uint32_t fe2_map_next_seed(fe2_map_seed *seed);
int fe2_map_density(const fe2_map_services *, uint16_t y, uint16_t x, uint16_t count, uint16_t *density);
int fe2_map_build_sector(fe2_map_state *, const fe2_map_services *, uint16_t y, uint16_t x,
                          const fe2_map_projection *projected, fe2_map_scene *scene);
fe2_map_position fe2_map_decode_record(uint16_t y, uint16_t x, const fe2_map_record *);
int fe2_map_nearest(const fe2_map_scene *, uint8_t y, uint8_t x);
void fe2_map_project_vector(const int16_t matrix[9],const int16_t vector[3],uint32_t projected[3]);
int fe2_map_distance(const fe2_map_services *, const fe2_map_position *, const fe2_map_position *, uint32_t *);
int fe2_map_name(const fe2_map_services *, uint16_t y, uint16_t x, uint16_t index, char *, size_t);
int fe2_map_format_text(const fe2_map_services *, uint16_t selector, uint16_t y, uint16_t x,
                         uint16_t index, uint32_t reference, const fe2_call_args *, char *, size_t);
int fe2_map_find_candidate(fe2_map_state *, const fe2_map_services *, uint32_t reference, unsigned offset_table, fe2_map_candidate *candidate);
int fe2_map_query_system(fe2_map_state *, const fe2_map_services *, uint32_t reference, unsigned mode, fe2_map_query *);
void fe2_map_cached_selection(const fe2_map_state *, fe2_map_query *);
int fe2_map_select(fe2_map_state *, const fe2_map_services *, uint16_t index, uint16_t descriptor);
int fe2_map_initialize(fe2_map_state *, const fe2_map_services *, unsigned start);
int fe2_map_activate(fe2_map_state *, const fe2_map_services *);
int fe2_map_enter_view(fe2_map_state *, const fe2_map_services *, int16_t request, int *changed);
void fe2_map_promote_selection(fe2_map_state *);
int fe2_map_perturb_home(fe2_map_state *, const fe2_map_services *, uint16_t *range);
int fe2_map_update(fe2_map_state *, const fe2_map_services *, const fe2_map_input *);
int fe2_map_draw(fe2_map_state *, const fe2_map_services *);
int fe2_map_draw_status(const fe2_map_state *, const fe2_map_services *);
int fe2_map_draw_selection(const fe2_map_state *, const fe2_map_services *);
int fe2_map_draw_system(fe2_map_state *, const fe2_map_services *);
int fe2_map_draw_body(fe2_map_state *, const fe2_map_services *, unsigned body);
int fe2_map_draw_trade(fe2_map_state *, const fe2_map_services *);
int fe2_map_draw_government(fe2_map_state *, const fe2_map_services *);
int fe2_map_draw_hierarchy(fe2_map_state *, const fe2_map_services *);
int fe2_map_enter_density(fe2_map_state *, const fe2_map_services *);
int fe2_map_update_density(fe2_map_state *, const fe2_map_services *, const fe2_map_input *);
#endif
