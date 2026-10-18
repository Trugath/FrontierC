/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_PEOPLE_OBJECTS_H
#define FE2_PEOPLE_OBJECTS_H

/*
 * Object creation, events, behaviour updates and relationships (people).
 *
 * - Creation (0x8a): modes 1..4 a craft linked to a parent, 5/7/8 a craft at
 *   a position, 6 a craft related to another object (the mode is the low
 *   byte of parameter 7).
 * - Events (0x76): collect the qualifying craft, apply action 0xa to them,
 *   reconfigure them for an event, or populate the system (0x6e too).
 * - Updates: the per-frame update (0x86) and the motion sample (0x82) run the
 *   behaviour (+0xd3)'s pair of handlers (34 behaviours 0x0, 0x4,..., 0x84).
 * - Relationship events (0x9a): a walk of the pool or the behaviour's
 *   callback.
 *
 * Persistent `fe2_call_args` provide inputs and working parameters for the
 * services: parameter 8 holds the current object and 9 the created object.
 * Callbacks, record cursors and model blocks use native state and services
 * rather than addresses in the parameter slots.
 */
#include <stdint.h>

#include "game/call_args.h"
#include "game/object.h"

enum {
    FE2_PEOPLE_TEMPLATE_BYTES = 286,
    FE2_PEOPLE_EVENT_IDS = 256,       /*, a NUL-terminated handle list */
    FE2_PEOPLE_BEHAVIOURS = 34,       /* 0x0, 0x4,..., 0x84 */
    FE2_PEOPLE_MARKETS = 19,          /* records (stride 0x1da) */
    FE2_PEOPLE_CONTRACTS = 6,         /* records (stride 0x2a) */
    FE2_PEOPLE_CARGO = 31,
    FE2_PEOPLE_NAME_BYTES = 40,       /* the name service's workspace */
    FE2_PEOPLE_CRAFT = 0x0b,          /* of every object the module makes */
    /* Event selectors (0x76, AX). */
    FE2_PEOPLE_EVENT_COLLECT = 1,
    FE2_PEOPLE_EVENT_RECONFIGURE = 2,
    FE2_PEOPLE_EVENT_POPULATE = 0x10,
    FE2_PEOPLE_EVENT_APPLY = 0x13
};

/* A market record (0x1da bytes from 0x224d): the station and its flags. */
typedef struct fe2_people_market {
    uint8_t station;                   /* the station's handle */
    uint8_t flags;                     /* bit 4: a flagged market */
} fe2_people_market;

/* A military contract (0x2a bytes from 0x4fe3): the fields the module uses. */
typedef struct fe2_people_contract {
    uint8_t type;                      /* low nibble the mission */
    uint8_t outcome;
    uint32_t target_system;
    uint32_t target_seed;
    uint8_t target;                    /* the target's handle */
} fe2_people_contract;

typedef struct fe2_people_state {
    /* Events. */
    uint8_t event_flags;               /* bit 0 populate after an event, 1 set by 0x4/0x8 */
    uint8_t event_ids[FE2_PEOPLE_EVENT_IDS];
    uint32_t update_step;              /* the 0x86 entry's DX:AX */
    /* The player and the system. */
    fe2_object *ship;                  /* the player's craft */
    fe2_object *station;
    fe2_object_list *objects;
    fe2_object_list *lookup_objects;   /*, set to 0x56fb by population */
    uint8_t player_handle;
    uint8_t motion_flags;              /* bit 3: offences event 0xa sent */
    uint8_t mode;                      /* the flight mode */
    uint8_t game_flags;                /* bits 2, 3 */
    uint8_t police_flags, launch_phase;
    uint16_t basis_angle[2];           /* for the basis service */
    uint32_t time_of_day, day;         /* the 64-bit game time */
    uint16_t system_seed;
    uint16_t market_count;
    fe2_people_market markets[FE2_PEOPLE_MARKETS];
    uint16_t flagged_count;            /* flagged markets */
    uint32_t current_system;
    uint32_t previous_system;          /* the newest previous system */
    uint32_t jump_value;
    uint16_t capacity;                 /* cargo capacity left */
    uint16_t cargo_used;
    uint16_t cargo[FE2_PEOPLE_CARGO];
    uint16_t jump_distance;
    uint32_t frame_step;
    uint32_t time_step;
    uint8_t music_preference, music_enabled;
    uint16_t contract_count;           /* (none from 0x8000) */
    fe2_people_contract contracts[FE2_PEOPLE_CONTRACTS];
    uint8_t emitted_count;
} fe2_people_state;

/* What the installed callbacks work on (the collection's cursor is 0x7e4
 * in the original: the next index of state->event_ids). */
typedef struct fe2_people_pass {
    fe2_people_state *state;
    const struct fe2_people_services *game;
    fe2_call_args *args;
    unsigned cursor;
} fe2_people_pass;

typedef void (*fe2_people_visitor)(void *pass, fe2_object *object);

/* AL, DL and the contact it leaves in parameter 9. */
typedef struct fe2_people_contacts {
    uint8_t flags;
    uint8_t handle;
    fe2_object *contact;
} fe2_people_contacts;

/* AX, BX, CX and DX. */
typedef struct fe2_people_metric {
    int16_t x, y, z;
    int16_t distance;
} fe2_people_metric;

/* Parameters a service leaves (0x7bc + 4n). */
typedef struct fe2_people_reply {
    uint16_t result;                   /* AX (AL) */
    uint32_t value[6];                 /* parameters 0..5 */
} fe2_people_reply;

typedef struct fe2_people_services {
    void *opaque;

    /* Objects. */
    void (*visit)(void *opaque, fe2_object_list *list, fe2_people_visitor visit,
                  void *pass);
    /* Parameter 12 the list. */
    fe2_object *(*lookup)(void *opaque, uint8_t handle, const fe2_call_args *args);
    /* Parameters 0 the kind, 1 the model, 8 the template. Returns nonzero
     * when one was made; `created` is parameter 9 as the service leaves it. */
    int (*allocate)(void *opaque, const fe2_call_args *args, fe2_object **created);
    int (*allocate_upper)(void *opaque, const fe2_call_args *args, fe2_object **created);
    void (*release)(void *opaque, fe2_object *object);
    void (*remove)(void *opaque, fe2_object *object);
    void (*attach)(void *opaque, fe2_object *object);
    void (*set_slot_flags)(void *opaque, fe2_object *object, uint8_t flags);
    /* Parameters 0, 1 the random dwords, 6 and 7 the setup words. */
    void (*setup)(void *opaque, fe2_object *object, const fe2_call_args *args);
    /* The model descriptor (see game/model.h). */
    const uint8_t *(*model)(void *opaque, uint16_t model);
    fe2_people_contacts (*scan_contacts)(void *opaque, fe2_object *object);
    /* Parameter 7 the source. Returns nonzero when the object is destroyed. */
    int (*damage)(void *opaque, fe2_object *object, uint16_t amount,
                  const fe2_call_args *args);
    void (*activate_effect)(void *opaque, fe2_object *object);
    void (*consume_effect)(void *opaque, fe2_object *object);
    /* AL: the capacity used over `step` (DX:AX). */
    uint8_t (*capacity_used)(void *opaque, fe2_object *object, uint32_t step);
    void (*activate_slot)(void *opaque, fe2_object *object, uint8_t slot);
    /* Parameter 7 the target. Returns nonzero when deployed. */
    int (*deploy_slot)(void *opaque, fe2_object *object, uint16_t slot,
                       const fe2_call_args *args);
    /* Parameters 0..5 the position, 7 the kind. */
    void (*spawn_effect)(void *opaque, const fe2_call_args *args);

    /* Flight engine (native in frontier/motion). */
    void (*integrate_limited)(void *opaque, fe2_object *object, uint32_t step); /* EAX */
    /* Parameter 7 the mode (the feedback exponent), 9 the reference; the
     * feedback in the object's frame is left in parameters 0..2
     * for the controls, and the mode it leaves  returned. */
    uint16_t (*relative_motion)(void *opaque, fe2_object *object, fe2_call_args *args);
    uint16_t (*relative_probe)(void *opaque, fe2_object *object, fe2_call_args *args);
    /* Parameter 9 the target. */
    fe2_people_metric (*scaled_difference)(void *opaque, fe2_object *object,
                                            const fe2_call_args *args);
    /* Parameter 9 the target; the low dwords of the difference. */
    void (*difference)(void *opaque, fe2_object *object, const fe2_call_args *args,
                       uint32_t low[3]);
    void (*linked_force)(void *opaque, fe2_object *object, uint32_t scale);     /* DX:AX */
    /* Parameter 7 nonzero: all three rates. */
    void (*rates_toward)(void *opaque, fe2_object *object, fe2_object *reference,
                         const fe2_call_args *args);
    void (*select_frame)(void *opaque, fe2_object *object);
    void (*clamp_controls)(void *opaque, fe2_object *object, const fe2_call_args *args);
    /* Parameters 5..7. */
    void (*turn)(void *opaque, fe2_object *object, const fe2_call_args *args);
    void (*apply_controls)(void *opaque, fe2_object *object, const fe2_call_args *args);
    void (*hold_controls)(void *opaque, fe2_object *object, const fe2_call_args *args);
    void (*steer_controls)(void *opaque, fe2_object *object, const fe2_call_args *args);

    /* Resident math. */
    uint32_t (*random)(void *opaque, uint32_t *second);                          /* EAX, EDX */
    uint16_t (*random_below)(void *opaque, uint16_t bound);
    /* `object`'s orientation from state->basis_angle. */
    void (*basis)(void *opaque, fe2_object *object);
    /* Parameters 2..3 the vector. */
    void (*transform_vector)(void *opaque, fe2_object *object, const fe2_call_args *args);

    /* Events and sound. */
    void (*post_event)(void *opaque, uint16_t event, uint16_t value);           /* AX, DX */
    void (*play_effect)(void *opaque, uint16_t effect);
    void (*play_sound)(void *opaque, uint16_t sound, uint8_t level);            /* AX, BL */
    void (*check_music)(void *opaque);
    void (*select_music)(void *opaque, uint16_t track);

    /* Other overlays (entry +0x20). */
    /* chart request 1 (parameter 1 the system, 8 the object): AX and
     * parameter 0; request 2: parameter 0. */
    fe2_people_reply (*galactic_map_relationship)(void *opaque, uint16_t request, const fe2_call_args *args);
    /* catalogue request 0 (AX; parameter 1 the system): the population counts
     * in parameters 0..5. Request 1 (AL; parameter 1 the relationship):
     * a rank in AL. Both leave parameters 0..5. */
    fe2_people_reply (*catalogue_query)(void *opaque, uint8_t request, const fe2_call_args *args);
    /* market service BX with handle AL (service 8: a model for a new craft
     * of marker AL): AX, and parameters 0..2 (service 6's offsets). */
    fe2_people_reply (*market_overlay_service)(void *opaque, uint16_t service, uint8_t handle,
                                     const fe2_call_args *args);
    /* names: the name of `type` from `seed` (NUL-terminated). */
    void (*names_name)(void *opaque, uint16_t type, uint32_t seed,
                     char out[FE2_PEOPLE_NAME_BYTES]);
    /* commander: AL nonzero sounds the alarm. */
    uint8_t (*commander_check)(void *opaque);
    /* station message `text` (parameters 1, 3, 7, 8 and 10). */
    void (*station_message)(void *opaque, uint16_t text, const fe2_call_args *args);
    /* offences event (parameter 8 the object). */
    void (*offences_event)(void *opaque, uint16_t event, const fe2_call_args *args);
    /* drives request 2 (parameter 1 the distance): parameters 0..2. */
    fe2_people_reply (*hyperspace_arrival)(void *opaque, const fe2_call_args *args);
} fe2_people_services;

/* 0x6e: request 3 (AL) clears event flag 0 and populates the system. */
void fe2_people_initialize(fe2_people_state *state, const fe2_people_services *game,
                            fe2_call_args *args, uint8_t request);

/* 0x76: FE2_PEOPLE_EVENT_* (AX); others return. */
void fe2_people_event(fe2_people_state *state, const fe2_people_services *game,
                       fe2_call_args *args, uint16_t selector);

/* 0x82: store the 64-bit motion sample in `object` (+0x7e) and run its
 * behaviour's handler with the change. */
void fe2_people_motion(fe2_people_state *state, const fe2_people_services *game,
                        fe2_call_args *args, fe2_object *object, uint64_t sample);

/* 0x86: the per-frame update of `object` over `step` (DX:AX). */
void fe2_people_update(fe2_people_state *state, const fe2_people_services *game,
                        fe2_call_args *args, fe2_object *object, uint32_t step);

/* 0x8a: create by the mode (parameter 7's low byte); `parent` is BX (the
 * parent of modes 1..4, the model of mode 6). Modes 5..8 return 1 when the
 * object was made (parameter 9), 0 otherwise. */
uint16_t fe2_people_create(fe2_people_state *state, const fe2_people_services *game,
                            fe2_call_args *args, uint16_t parent);

/* 0x9a: `form` (BX) zero walks the pool with `handle` (AL); otherwise the
 * behaviour callback of `object` (parameter 8). */
void fe2_people_relationship_event(fe2_people_state *state, const fe2_people_services *game,
                                    fe2_call_args *args, fe2_object *object, uint8_t handle,
                                    uint16_t form);

/* Installed callbacks; `pass` is a fe2_people_pass. */
void fe2_people_collect_object(void *pass, fe2_object *object);
void fe2_people_relationship_object(void *pass, fe2_object *object);

#endif
