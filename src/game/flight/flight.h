/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_FLIGHT_FLIGHT_H
#define FE2_FLIGHT_FLIGHT_H

/*
 * Flight and cockpit control (flight). It covers:
 * - the starting ship and the player ship's creation and replacement;
 * - the flight modes: each pass moves the ship and runs the mode's flight
 *   action, and the cockpit pass runs the mode's display action
 *   (guidance pictures, transitions, the view sequence);
 * - the cockpit: views, turret and external-view controls, keys, the
 *   scene drawn from the ship, the status lines, target markers and the
 *   command widgets;
 * - targets: selection, tracking and the target range label;
 * - timers, the contact countdown and the ship events.
 *
 * Objects, drawing, keys, sound, the flight engine, the
 * resident math and the other overlays come through `fe2_flight_services`.
 * The flight mode is the original's byte offset into its 29-record
 * dispatch table (FE2_FLIGHT_MODE_*); other values are outside the domain.
 */
#include <stdint.h>

#include "game/call_args.h"
#include "game/object.h"
#include "game/scalar.h"
#include "game/text_table.h"

#include "game/flight_mode.h"

/* Control flags. */
enum {
    FE2_CONTROL_THRUST = 0x01,         /* the controls integrate without feedback limits */
    FE2_CONTROL_MODE_FOUR = 0x02,      /* synchronized with behaviour <= 0x6c in mode 4 */
    FE2_CONTROL_ANNOTATE = 0x04,       /* label objects in the status pass */
    FE2_CONTROL_WIDGETS = 0x08,        /* the command widgets are shown */
    FE2_CONTROL_TARGET_STATS = 0x10,   /* the target's statistics were drawn */
    FE2_CONTROL_LATCH = 0x20,          /* edge-triggered sound latch (effects 0x12/0x13/0x22) */
    FE2_CONTROL_CLOUD = 0x40,          /* show hyperspace cloud details */
    FE2_CONTROL_RETARD = 0x80          /* the throttle is falling back to zero */
};

/* Preference bytes, by offset from 0x462d. */
enum {
    FE2_PREF_EMISSIONS = 0x0,
    FE2_PREF_SCENE_FLASH = 0x1,
    FE2_PREF_EXTERNAL_BASIS = 0x2,
    FE2_PREF_INVERT_X = 0x3,
    FE2_PREF_INVERT_Y = 0x4,
    FE2_PREF_ROLL_AXIS = 0x5,
    FE2_PREF_TARGET_MARKERS = 0x6,
    FE2_PREF_ALL_MARKERS = 0x7,
    FE2_PREF_EFFECTS = 0x9,            /* sound effects enabled */
    FE2_PREF_AMBIENT = 0xa,
    FE2_PREF_DURATION = 0xb,
    FE2_PREF_HEAT_EFFECT = 0xc,
    FE2_FLIGHT_PREF_MUSIC = 0xe,
    FE2_PREF_FLIGHT_MUSIC = 0xf,
    FE2_PREFERENCES = 16
};

enum {
    FE2_FLIGHT_KEYS = 0xe0,            /* key states from 0x8700; zero: held */
    FE2_FLIGHT_CARGO = 31,
    FE2_FLIGHT_NAME_BYTES = 32,        /* a generated name  */
    FE2_FLIGHT_RECORD_BYTES = 0x2a,    /* a mission record  */
    FE2_FLIGHT_TEMPLATE_BYTES = 0x11e
};

/* A mission record (42 bytes from 0x4fe3): the fields the ship command
 * scan reads and writes; the rest is kept as is. */
typedef struct fe2_flight_record {
    uint8_t kind;                      /* low nibble 3 or 4 */
    uint8_t seen;                      /* set to 0xff */
    uint32_t system;
    uint8_t handle;
    uint8_t lowest_level;
} fe2_flight_record;

typedef struct fe2_flight_state {
    /* Objects. */
    fe2_object *ship;                  /* the player's craft */
    fe2_object *reference;             /* the body the flight refers to */
    fe2_object_list *objects;
    fe2_object_list *current_objects;  /* the list lookups use */
    uint8_t player_handle;
    uint8_t pending_action;            /* a pending equipment slot (1..8) */
    uint8_t reference_handle;
    uint8_t motion_flags;              /* 0x2 speed warning, 0x10, 0x40 far, 0x80 ambient latch */
    uint8_t mode;                      /* FE2_FLIGHT_* */
    uint8_t control_flags;             /* FE2_CONTROL_* */
    uint8_t view_index;                /* 0 front, 1 rear, 2/3 turrets, 4 external */
    int8_t countdown;                  /* the contact countdown */
    uint8_t target;                    /* the target's handle */
    uint8_t pointer_found;
    uint8_t mining_mode_active;
    uint8_t contact_class;
    uint8_t release_suppressed;
    uint16_t starting_config;          /* AH: the starting configuration */
    uint16_t target_colour;
    fe2_scalar target_range;
    uint32_t shared_countdown;         /* signed in most readers */
    uint32_t current_system;
    uint16_t delay_ticks;
    uint8_t fined_handle;
    uint32_t cash;
    uint16_t capacity;                 /* cargo capacity left */
    uint16_t cabins, cargo_used, cabins_used;
    uint16_t external_angle[2];
    uint32_t external_distance;
    uint16_t cargo[FE2_FLIGHT_CARGO];  /*; [19] hydrogen fuel  */
    uint16_t jump_range;
    uint16_t offence_score[3];
    uint32_t elite_points;
    uint16_t federal_points, imperial_points;
    uint32_t previous_systems[3];      /* oldest.. 0x20fd newest */
    int32_t throttle;                  /* the requested speed */
    uint32_t heat_timer;
    uint16_t heat;
    uint16_t combat_rating;
    uint16_t equipment_service_threshold, drive_service_threshold;
    uint32_t equipment_service_day, drive_service_day;
    int16_t turret_angle[2];           /* (0x2129 negative: no turret) */
    int16_t turret_axis[3];
    uint8_t contact_handle;
    uint8_t contact_status;            /* the last contact flags */
    int16_t saved_limits[3][2];        /* the ship's control limits at creation */
    uint8_t jump_enabled;
    uint32_t jump_distance;
    /* Speeds. */
    uint16_t range_shift;
    uint32_t speed;                    /* the shown speed */
    uint32_t measured;                 /* the contact distance */
    uint64_t motion_delta;             /* the last motion sample's change */
    /* Cockpit. */
    uint16_t view_mode;                /* low byte negative: the cockpit is shown */
    uint16_t axis[2];                  /* the pointer axes */
    uint8_t view_owner;
    uint16_t line_colour;
    uint16_t scene_flash;
    uint8_t redraw_flags;
    uint16_t click[2];
    uint16_t stick[2];
    uint8_t keys[FE2_FLIGHT_KEYS];
    uint8_t input_suppressed, gameport_flags, primary_button, secondary_button;
    uint8_t widget_colour;
    uint8_t preferences[FE2_PREFERENCES];
    /* Game. */
    uint8_t police_flags, launch_phase;
    uint16_t basis_angle[2];           /* shared with the math service */
    uint16_t time_scale;
    uint32_t time_of_day, day;
    uint16_t reusable_count;
    uint8_t game_flags;
    uint32_t event_base, event_time;
    uint32_t message_timer;
    uint32_t local_fine;
    uint16_t contract_count;
    uint8_t next_page;
    uint16_t record_count;
    fe2_flight_record *records;
    uint16_t passenger_limit, passenger_count;
    uint32_t unpaid_wages, mission_flags;
    uint32_t criminal_record[3];
    uint8_t emitted_count, emitted_limit;
    uint16_t free_mining_machine_count;
    uint32_t jump_time;
    int16_t current_music;
    uint32_t frame_step;
    uint32_t real_step;
    uint32_t time_step;
    uint16_t time_exponent;
    /* Work records (stack copies in the original): the cockpit scene's
     * frame (the first 0x60 bytes of the ship) and the guidance picture's
     * object. */
    fe2_object frame;
    fe2_object guide;
} fe2_flight_state;

/* The contact scan's report (0x60: AL, 0x7e0, 0x7c4, 0x7c8). */
typedef struct fe2_flight_contacts {
    uint8_t flags;                     /* 1 in contact, 0x20, 0x40, 0x80 */
    fe2_object *record;                /* the reported record */
    uint32_t distance;
    uint16_t classification;
} fe2_flight_contacts;

/* Three words and their exponent (0x188: AX, BX, CX and DX). */
typedef struct fe2_flight_scaled {
    int16_t v[3];
    uint16_t shift;
} fe2_flight_scaled;

/* Feedback (0x17c: 0x7bc..0x7c7) and its exponent. */
typedef struct fe2_flight_feedback {
    int32_t v[3];
    uint16_t shift;
} fe2_flight_feedback;

/* Model data (resident 0x50): the descriptor's words and its statistics
 * record (descriptor + stats_offset; the descriptor itself when the
 * offset is zero). */
typedef struct fe2_flight_model {
    uint16_t scale_shift;
    uint16_t extent;
    uint16_t stats_offset;
    uint16_t mass;                     /* stats +6 */
    uint16_t ship_name_text;                  /* stats +0xe */
    int16_t views[12];                 /* stats +0x18..+0x2f: front, rear,..., top, bottom */
} fe2_flight_model;

/* The scene probe's results. */
typedef struct fe2_flight_probe {
    int hit;                           /* the returned sign flag */
    uint32_t distance;
    fe2_object *record;
    int64_t point[3];                  /* the hit point (low dwords, then high) */
    int16_t normal[3];
} fe2_flight_probe;

/* market's reply (overlay 0x38). */
typedef struct fe2_flight_market_reply {
    uint8_t result;                    /* AL */
    uint8_t value;
    uint32_t offset_low[3];            /* a position change (service 1) */
    uint32_t offset_high[3];
} fe2_flight_market_reply;

enum { FE2_FLIGHT_NO_HANDLE = 0xffff };

/* drives's drive query reply (overlay 0x80). */
typedef struct fe2_flight_drive_reply {
    uint16_t mode;                     /* negative: refused */
    uint16_t range_left;
} fe2_flight_drive_reply;

typedef void (*fe2_flight_visitor)(void *context, fe2_object *object);
/* Visitor of projected objects, with their screen position (AX, BX). */
typedef void (*fe2_flight_screen_visitor)(void *context, fe2_object *object, uint16_t x, uint16_t y);

typedef struct fe2_flight_services {
    void *opaque;

    /* Objects and lists. */
    void (*visit_objects)(void *opaque, fe2_object_list *list, fe2_flight_visitor visit, void *context);
    void (*visit_projected)(void *opaque, fe2_object_list *list, fe2_flight_screen_visitor visit,
                            void *context);
    /* Parameter 12 is the list. */
    fe2_object *(*lookup)(void *opaque, uint8_t handle, const fe2_call_args *args);
    /* The list's status byte of `handle` (bit 3: projected). */
    uint8_t (*handle_flags)(void *opaque, const fe2_object_list *list, uint8_t handle);
    /* The word at offset handle + 0xba of `record`'s segment: the status
     * pass reads it with the handle where the record offset was meant. */
    uint16_t (*stray_word)(void *opaque, const fe2_object *record, uint8_t handle);
    /* Parameters 0 kind, 1 model, 8 template; null when none is free.
     * `slot` (if not null) is the slot word left in parameter 0. */
    fe2_object *(*allocate)(void *opaque, const fe2_call_args *args, uint16_t *slot);
    fe2_object *(*allocate_upper)(void *opaque, const fe2_call_args *args);
    void (*release)(void *opaque, fe2_object *record);
    void (*set_slot_flags)(void *opaque, fe2_object *record, uint8_t flags);
    const fe2_flight_model *(*model)(void *opaque, uint16_t model);
    fe2_flight_contacts (*scan_contacts)(void *opaque, fe2_object *record);
    void (*refresh_contacts)(void *opaque, fe2_object *record);
    /* Parameter 7 the source handle. */
    void (*damage)(void *opaque, fe2_object *record, uint16_t amount, const fe2_call_args *args);
    void (*activate_effect)(void *opaque, fe2_object *record);
    void (*consume_effect)(void *opaque, fe2_object *record);
    void (*update_capacity)(void *opaque, fe2_object *record);
    void (*activate_slot)(void *opaque, fe2_object *record, uint8_t view);
    /* Parameter 7 the target. */
    void (*deploy_slot)(void *opaque, fe2_object *record, uint16_t slot, uint8_t target,
                        const fe2_call_args *args);
    void (*advance_failures)(void *opaque, fe2_object *record, uint32_t interval);
    /* Parameters 0..5 the position, 7 the mode. */
    void (*spawn_effect)(void *opaque, fe2_object *record, const fe2_call_args *args);
    /* Parameters 0..2 offset, 3..5 direction, 6 excluded, 7 extent, 8
     * source, 10 mode, 11 high extent. Returns the hit. */
    int (*probe)(void *opaque, const fe2_call_args *args, fe2_flight_probe *result);
    void (*rebuild_secondary_basis)(void *opaque, fe2_object *record,
                                    const fe2_call_args *args);                  /*, parameter 8 */

    /* Flight engine. */
    void (*integrate_limited)(void *opaque, fe2_object *record, uint32_t step);
    void (*integrate)(void *opaque, fe2_object *record, uint32_t step);
    /* Parameter 6 the speed; the feedback  and its exponent. */
    fe2_flight_feedback (*axis_feedback)(void *opaque, fe2_object *record, const fe2_call_args *args);
    /* Parameter 9 the target. */
    fe2_flight_scaled (*scaled_difference)(void *opaque, fe2_object *record, const fe2_call_args *args);
    /* Parameter 9 the target; the low dwords of the difference. */
    void (*difference)(void *opaque, fe2_object *record, const fe2_call_args *args, int32_t low[3]);
    void (*linked_force)(void *opaque, fe2_object *record, uint32_t scale);       /* DX:AX */
    void (*radial_rate)(void *opaque, fe2_object *record);
    /* Parameters 0..2 words; the normalized words. */
    void (*normalize)(void *opaque, const fe2_call_args *args, int16_t out[3]);
    /* Parameter 7 nonzero: all three rates. */
    void (*rates_toward)(void *opaque, fe2_object *record, fe2_object *reference,
                         const fe2_call_args *args);
    void (*select_frame)(void *opaque, fe2_object *record);
    void (*clamp_controls)(void *opaque, fe2_object *record, const fe2_call_args *args);
    void (*turn)(void *opaque, fe2_object *record, const fe2_call_args *args);  /*, 5..7 */
    void (*rebuild_basis)(void *opaque, fe2_object *record);
    /* Parameters 3..5; the three words it leaves at 0x7bc, 0x7c0, 0x7c4. */
    void (*transform_feedback)(void *opaque, fe2_object *record, const fe2_call_args *args,
                               int16_t out[3]);

    /* Panels, keys and drawing. */
    void (*set_panel_mode)(void *opaque, uint16_t mode);
    void (*show_panel)(void *opaque);
    int (*poll_key)(void *opaque, uint8_t *key);
    void (*forward_key)(void *opaque, uint8_t key);
    uint16_t (*pointer)(void *opaque, uint16_t *y);
    void (*set_pointer)(void *opaque, uint16_t x, uint16_t y);
    void (*set_controls)(void *opaque, uint16_t layout, uint8_t buttons);
    void (*set_widgets)(void *opaque, const uint16_t *rectangles);
    /* A pointer region at (x, y): the position it was placed at. */
    void (*add_region)(void *opaque, uint16_t x, uint16_t y, uint16_t extent, uint16_t id,
                       uint16_t *placed_x, uint16_t *placed_y);
    void (*remove_region)(void *opaque, uint16_t id);
    void (*clear_regions)(void *opaque);
    /* Lines in state->line_colour. */
    void (*draw_clipped_line)(void *opaque, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void (*draw_line)(void *opaque, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void (*draw_sprite)(void *opaque, uint16_t sprite, uint16_t x, uint16_t y);
    void (*draw_name)(void *opaque, const fe2_object *object, uint8_t colour, uint16_t x,
                      uint16_t y);                                               /*, +0xf6 */
    void (*draw_text)(void *opaque, uint16_t text, uint16_t colour, uint16_t x, uint16_t y,
                      const fe2_call_args *args);
    void (*draw_text_both)(void *opaque, uint16_t text, uint8_t colour, uint16_t x, uint16_t y,
                           const fe2_call_args *args);
    void (*draw_text_shadowed)(void *opaque, uint16_t text, uint8_t colour, uint16_t x, uint16_t y,
                               const fe2_call_args *args);
    void (*format_table_text)(void *opaque, uint16_t selector, const fe2_text_table *table,
                              fe2_text_out *out);
    uint16_t (*palette_index)(void *opaque, uint8_t handle, uint16_t colour);    /* BX, DI */
    uint16_t (*palette_rgb)(void *opaque, uint16_t rgb);
    void (*clear_drawing)(void *opaque);
    void (*present)(void *opaque);
    /* Parameter 11 the frame, 12 the list. */
    void (*render_scene)(void *opaque, const fe2_call_args *args);
    void (*render_frame_model)(void *opaque, fe2_object *frame);
    void (*render_object)(void *opaque, fe2_object *record);

    /* Resident math. */
    uint32_t (*random)(void *opaque, uint32_t *edx);                             /* EAX, EDX */
    fe2_scalar (*normalize_unsigned)(void *opaque, fe2_scalar value);
    /* `record`'s orientation from state->basis_angle. */
    void (*basis)(void *opaque, fe2_object *record);
    void (*move_object)(void *opaque, fe2_object *record, const fe2_call_args *args); /*, 0..2 */
    void (*transform_vector)(void *opaque, fe2_object *record, const fe2_call_args *args); /*, 2..3 */
    void (*project)(void *opaque, int32_t x, int32_t y, int32_t depth, uint16_t *sx,
                    uint16_t *sy);
    /* Rotate the vectors at orientation words `a` and `b` of `record`
     * (stride 3) by the angle in parameter 7. */
    void (*rotate_pair)(void *opaque, fe2_object *record, unsigned a, unsigned b,
                        const fe2_call_args *args);
    uint16_t (*vector_length)(void *opaque, int16_t x, int16_t y, int16_t z);

    /* Events and sound. */
    void (*post_event)(void *opaque, uint16_t event);                            /* AX */
    void (*post_event_value)(void *opaque, uint16_t event, uint16_t value);      /* AX, DX */
    void (*play_effect)(void *opaque, uint16_t effect);
    void (*play_duration)(void *opaque, uint16_t effect, uint16_t duration);
    void (*ambient)(void *opaque, uint16_t a, uint16_t b, uint16_t c);           /* AX BX CX */
    void (*check_music)(void *opaque);
    void (*select_music)(void *opaque, uint16_t track);
    /* Do not return in the original (far jumps). */
    void (*fatal_error)(void *opaque, uint16_t code);
    void (*restart_session)(void *opaque, uint16_t mode);

    /* Other overlays. */
    void (*panels_panel)(void *opaque, uint16_t request, const fe2_call_args *args);  /* AX */
    void (*panels_widgets)(void *opaque, uint16_t request, uint16_t widgets,
                        const fe2_call_args *args);                              /* AX, BX */
    /* Service BX with handle AL (FE2_FLIGHT_NO_HANDLE: the original's AL
     * is the low byte of the reference's address); parameters 8 and 9 the
     * objects. */
    void (*market_overlay_service)(void *opaque, uint16_t service, uint16_t handle, const fe2_call_args *args,
                        fe2_flight_market_reply *reply);
    void (*names_name)(void *opaque, uint16_t type, uint32_t seed, char out[FE2_FLIGHT_NAME_BYTES]);
    void (*commander_reset)(void *opaque);
    /* Entry +0x1c (0x86): update `record` over `interval` (DX:AX); the
     * object in parameter 9 after it. */
    fe2_object *(*people_update)(void *opaque, fe2_object *record, uint32_t interval);
    void (*station_message)(void *opaque, uint16_t text, const fe2_call_args *args);
    void (*offences_event)(void *opaque, uint16_t event, const fe2_call_args *args);
    void (*hyperspace_query)(void *opaque, uint16_t mode, const fe2_call_args *args,
                      fe2_flight_drive_reply *reply);
    void (*hyperspace_jump)(void *opaque);                                              /* entry +0x8 */
    void (*mining_command)(void *opaque, uint16_t request, const fe2_call_args *args);
} fe2_flight_services;

/* What the installed callbacks work on. */
typedef struct fe2_flight_pass {
    fe2_flight_state *state;
    const fe2_flight_services *game;
} fe2_flight_pass;

/* 0x6e: request 1 sets up a new game with `config` (AX); others return. */
void fe2_flight_initialize(fe2_flight_state *state, const fe2_flight_services *game,
                           uint16_t config);
/* 0x72: enter the cockpit, or cycle its view while it is shown. */
void fe2_flight_activate_view(fe2_flight_state *state, const fe2_flight_services *game);
/* 0x76: a ship event (AX); `selected` is event 0x11's handle. */
void fe2_flight_event(fe2_flight_state *state, const fe2_flight_services *game, uint16_t event,
                      uint8_t selected);
/* 0x7a: the cockpit pass of the current mode. `subject` is 0x7dc as the
 * caller left it (in mode 0x54 the ship replacement key's template). */
void fe2_flight_cockpit(fe2_flight_state *state, const fe2_flight_services *game, fe2_object *subject);
/* 0x7e: target tracking and the status lines. `subject` is 0x7dc as the
 * caller left it (the ship a selection of the player's handle replaces). */
void fe2_flight_status(fe2_flight_state *state, const fe2_flight_services *game, fe2_object *subject);
/* the status panel, then the mode icons drawn over it. */
void fe2_flight_refresh_mode(fe2_flight_state *state, const fe2_flight_services *game);
/* 0x82: store the motion sample in `record`  and run the mode's
 * flight action. */
void fe2_flight_update(fe2_flight_state *state, const fe2_flight_services *game, fe2_object *record,
                       uint64_t sample);
/* 0x86: timers over `interval` (DX:AX), for `record`. */
void fe2_flight_timers(fe2_flight_state *state, const fe2_flight_services *game, fe2_object *record,
                       uint32_t interval);
/* 0x8a. */
void fe2_flight_reset_capacity(fe2_flight_state *state);
/* 0x8e: AX zero requests redraw mode 0x7f; positive refreshes the widgets,
 * negative removes them. */
void fe2_flight_change_view(fe2_flight_state *state, const fe2_flight_services *game, uint16_t request);
/* 0x92: text `selector` (AX & 0x3fe). */
void fe2_flight_text(const fe2_flight_services *game, uint16_t selector, fe2_text_out *out);
/* 0x96. */
void fe2_flight_countdown(fe2_flight_state *state, const fe2_flight_services *game);
/* 0x9a: select `handle` (AL); `form` is BX; `record` is 0x7dc. */
void fe2_flight_select_target(fe2_flight_state *state, const fe2_flight_services *game, uint8_t handle,
                              uint16_t form, fe2_object *record);

/* Installed callbacks; `pass` is a fe2_flight_pass. */
void fe2_flight_pick_object(void *pass, fe2_object *object, uint16_t x, uint16_t y);
void fe2_flight_adopt_object(void *pass, fe2_object *object);
void fe2_flight_annotate_object(void *pass, fe2_object *object, uint16_t x, uint16_t y);

#endif
