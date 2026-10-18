/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_HYPERSPACE_HYPERSPACE_H
#define FE2_HYPERSPACE_HYPERSPACE_H

/*
 * Hyperspace (drives): drive queries and the jump.
 *
 * - The drive query works out a ship's range, the fuel and time a jump
 *   needs and the range left, and can pay for the jump. The player's ship
 *   pays in hydrogen or military fuel cargo; other craft pay from their own
 *   fuel byte.
 * - Preparation generates and instantiates the destination system.
 * - The jump checks its gates, pays, sets the arrival time and resets the
 *   ship. It then arrives normally or takes a misjump outcome.
 *
 * Objects, models, the systems/chart/station overlays, events, random numbers, sound
 * and math come through `fe2_hyperspace_services`. Unchecked 16-bit
 * divisions that fault in the original return FE2_HYPERSPACE_DIVIDE_FAULT.
 */
#include <stdint.h>

#include "game/call_args.h"
#include "game/object.h"

enum {
    FE2_HYPERSPACE_OK = 0, FE2_HYPERSPACE_DIVIDE_FAULT = -2,
    FE2_DRIVE_CLASSES = 15, FE2_DRIVE_OUTCOMES = 16,
    FE2_RANGED_CLASSES = 7,           /* classes 2..8 answer the range query */
    FE2_AUTHORITIES_M16 = 3,
    /* Records the destination system is built in (a stack context in the
     * original). */
    FE2_HYPERSPACE_RECORD_BYTES = 0xf44
};

/* Drive query modes (AX). */
enum {
    FE2_DRIVE_PREPARE = 0, FE2_DRIVE_CONSUME = 2, FE2_DRIVE_CLASS_RANGES = 3,
    FE2_DRIVE_NO_ACTION = 4
};

/* Drive class record (native resource). */
typedef struct fe2_drive_class {
    uint32_t range_factor;            /* range = range_factor / hull mass */
    uint16_t mass;                    /* drives at or above a hull's limit do not fit */
    uint16_t name_text;
} fe2_drive_class;

/* A model's statistics record (descriptor + [0x1c]). */
typedef struct fe2_ship_stats {
    uint16_t hull_mass;
    uint16_t drive_limit;
} fe2_ship_stats;

/*
 * The drive query's inputs and results, the original's scratch dwords
 * 0x7bc..0x7d0. A query writes only what its path sets; the other fields
 * keep the caller's values.
 */
typedef struct fe2_drive_query {
    uint16_t mode;                    /* mode in; 0xffff when the jump is refused */
    uint32_t request;                 /* distance in; jump time (ratio * 7) out */
    uint32_t range;                   /* drive range, sign-extended word */
    uint16_t name_text;               /* the drive's name */
    uint16_t mass;                    /* the drive's mass */
    uint32_t fuel_needed;             /* sign-extended word; low word 0xffff out of range */
    uint16_t range_left;              /* range after paying (accounts only) */
} fe2_drive_query;

/* The destination from the chart query (overlay 0x10, AX=0). */
typedef struct fe2_jump_target {
    uint32_t seed_a, seed_b;          /* DX:AX, CX:BX */
    const char *name;
    uint32_t system;                  /* after the query */
} fe2_jump_target;

typedef struct fe2_hyperspace_state {
    /* Drive classes by class (+0xa6) and misjump outcomes by sample nibble;
     * fe2_hyperspace_drive_classes and fe2_hyperspace_outcomes unless replaced. */
    const fe2_drive_class *drive_classes;
    const uint8_t *outcomes;
    fe2_object *ship;
    uint8_t player_handle;
    uint8_t flight_state;             /* jumps from 0 or 0x48; 8 in hyperspace */
    /* Cargo (tonnes); fuel is cargo. */
    uint16_t cargo_capacity;
    uint16_t cargo_used;
    uint16_t hydrogen_fuel;           /* drive classes below 10 */
    uint16_t military_fuel;           /* classes 10 and above */
    uint16_t radioactives;            /* military fuel spent */
    uint16_t rubbish;                 /* a destroyed drive */
    uint16_t jump_range;              /* range left after the jump */
    /* The jump. */
    uint8_t jump_enabled;
    uint32_t jump_clock;              /* must reach 0xb71b00 */
    uint32_t jump_distance;
    uint16_t jump_distance_word;
    uint32_t jump_value;              /*, copied to 0x2101 */
    uint32_t jump_value_copy;
    uint32_t jump_time;               /* the query's jump time */
    uint32_t published_jump_time;
    uint64_t arrival;                 /* clock + jump time (rotated) */
    uint8_t misjump_guard;            /* zero forces a misjump */
    uint32_t time_of_day;
    uint32_t day;
    uint32_t local_record;
    uint32_t criminal_record[FE2_AUTHORITIES_M16]; /* by authority */
    uint16_t legal_authority;
    uint8_t navigation_target;
    uint8_t fined_handle;               /* cleared */
    uint32_t throttle;              /* cleared */
    uint16_t pool_flag_count;
    uint32_t map_saved_scale;         /* navigation saved map scale */
    uint8_t map_default_selection;    /* navigation */
    uint32_t simulation_time_step;
    uint16_t game_time_exponent;
    /* Systems and generation. */
    uint32_t current_system;
    uint32_t previous_systems[3];     /* oldest.. 0x20fd newest */
    uint8_t sol_station_count;        /* (systems) */
    uint16_t station_count_copy;
    uint8_t named_bodies;             /* (systems) */
    uint16_t generation_random;       /* high word of systems's first seed */
    uint8_t emitted_limit;
    uint8_t reference_index;               /* cleared */
    fe2_object_list *objects;
} fe2_hyperspace_state;

typedef struct fe2_hyperspace_services {
    void *opaque;
    fe2_ship_stats (*ship_stats)(void *opaque, uint16_t model);
    fe2_jump_target (*target_system)(void *opaque);                         /* chart */
    /* systems system action: parameter 2 mode (0 generate, 1 instantiate), 4/5
     * the time, 8 the records, 10 the system name; `target` (seeds in
     * DX:AX, CX:BX) is given for generation. */
    void (*system_action)(void *opaque, const fe2_jump_target *target, const fe2_call_args *args);
    void (*show_message)(void *opaque, uint16_t text, const fe2_call_args *args); /* station */
    void (*post_event)(void *opaque, uint16_t event);
    void (*clear_objects)(void *opaque, const fe2_call_args *args);         /*, parameter 12 */
    void (*set_object_flags)(void *opaque, fe2_object *object, uint8_t flags);
    uint16_t (*random_below)(void *opaque, uint16_t bound);
    uint32_t (*random)(void *opaque, uint32_t *high);                       /* EAX, EDX */
    /* Set `object`'s motion from parameters 0 (word) and 1 (dword) with
     * scale 6 and exponent 7. */
    void (*interpolate)(void *opaque, fe2_object *object, const fe2_call_args *args);
    void (*play_effect)(void *opaque, uint16_t effect);
    /* The misjump handled elsewhere (resident 0x84). */
    void (*misjump)(void *opaque, fe2_object *object, uint8_t player_handle);
} fe2_hyperspace_services;

/* 0x8a modes other than 0, 3 and 4, for `ship` (scratch 0x7dc). */
int fe2_hyperspace_query(fe2_hyperspace_state *state, const fe2_hyperspace_services *game,
                         fe2_object *ship, fe2_drive_query *query);
/* 0x8a mode 3: ranges of drive classes 2..8 in a hull of `model` (BX);
 * zero where the drive does not fit or the range is below 300. */
int fe2_hyperspace_class_ranges(const fe2_hyperspace_state *state,
                                const fe2_hyperspace_services *game, uint16_t model,
                                uint32_t ranges[FE2_RANGED_CLASSES]);
/* 0x8a mode 0: generate and instantiate the destination system. */
void fe2_hyperspace_prepare(fe2_hyperspace_state *state, const fe2_hyperspace_services *game);
/* 0x72: the jump. It runs a consume query for the ship over the jump
 * distance; `query` returns its results (a refused jump has mode 0xffff) and
 * keeps the caller's values in the fields the query does not set. */
int fe2_hyperspace_jump(fe2_hyperspace_state *state, const fe2_hyperspace_services *game,
                        fe2_drive_query *query);

/* The tables generated from the native drive resources. */
extern const fe2_drive_class fe2_hyperspace_drive_classes[FE2_DRIVE_CLASSES];
extern const uint8_t fe2_hyperspace_outcomes[FE2_DRIVE_OUTCOMES];

#endif
