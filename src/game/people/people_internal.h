/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_PEOPLE_INTERNAL_H
#define FE2_PEOPLE_INTERNAL_H

/* Shared declarations of the people translation units. */
#include "objects.h"
#include "people_tables.h"

/* Named parameter slots used by the people services. */
enum {
    P0, P1, P2, P3, P4, P5,
    P_SETUP = 6,       /* setup word, creation subtype */
    P_MODE = 7,        /* creation mode, service modes, handles */
    P_CURRENT = 8,     /* the current object (or a template) */
    P_CREATED = 9,     /* the created (or looked-up) object */
    P_CURSOR = 10,     /* a record or list cursor; linked creation's reward */
    P_CALLBACK = 11,   /* a callback or model block */
    P_LIST = 12        /* the list lookups use */
};

/* Behaviours (+0xd3). */
enum {
    BEHAVIOUR_ESCORT_PARENT = 0x00, BEHAVIOUR_PATROL_CRUISE = 0x04, BEHAVIOUR_PATROL_HOLD = 0x08, BEHAVIOUR_TRACK_PARENT = 0x0c, BEHAVIOUR_APPROACH_PARENT = 0x10, BEHAVIOUR_CHOOSE_PATROL_OFFSET = 0x14, BEHAVIOUR_LEAVE_PARENT = 0x1c,
    BEHAVIOUR_ADOPT_TARGET_PARENT = 0x20, BEHAVIOUR_REPORT_ESCORT_ARRIVAL = 0x24, BEHAVIOUR_INACTIVE_RELATIONSHIP = 0x2c, BEHAVIOUR_WAIT_AFTER_OFFSET = 0x38, BEHAVIOUR_ARRIVAL_PENDING = 0x3c, BEHAVIOUR_ARRIVAL_COUNTDOWN = 0x40, BEHAVIOUR_DRIFTING_CARGO = 0x50,
    BEHAVIOUR_BURNING_WRECK = 0x64, BEHAVIOUR_HIDDEN_CRAFT = 0x68, BEHAVIOUR_LAUNCH_FLASH = 0x6c, BEHAVIOUR_DEPARTURE_REQUEST = 0x70, BEHAVIOUR_DOCKING = 0x74, BEHAVIOUR_LAUNCHING = 0x78, BEHAVIOUR_RETURN_TO_PARENT = 0x80,
    BEHAVIOUR_DOCKED = 0x84
};

/* Map markers (+0xec). */
enum {
    MARKER_EVENT = 0x08, MARKER_STATION_SEED = 0x0a, MARKER_CONTRACT = 0x0c,
    MARKER_LAUNCHED = 0xfe, MARKER_VALUED = 0xfb
};

enum {
    MODEL_EVENT = 0xba,                /* an event craft's model while it waits */
    FLAG_COPY_BASIS = 0x20,            /* of a parent: take its position and basis */
    FLAG_LOUD = 0x40,                  /* : slot flags 0x4e rather than 0x2 */
    FLAG_ARMED = 0x10
};

typedef struct people_context {
    fe2_people_state *s;
    const fe2_people_services *g;
    fe2_call_args *args;
} people_context;

typedef void (*people_update_handler)(people_context *c, fe2_object *object);
typedef void (*people_motion_handler)(people_context *c, fe2_object *object, uint64_t change);

typedef struct people_behaviour {
    people_update_handler update;         /* 0x86 */
    people_motion_handler motion;         /* 0x82 */
    people_update_handler walk;           /* 0x9a pool walk */
    people_update_handler callback;       /* 0x9a direct */
} people_behaviour;

extern const people_behaviour people_behaviours[FE2_PEOPLE_BEHAVIOURS];

static inline const people_behaviour *people_behaviour_of(const fe2_object *object)
{
    /* The behaviour is a byte offset into 4-byte rows (unchecked). */
    return &people_behaviours[object->behaviour / 4u];
}

static inline void people_init(people_context *c, fe2_people_state *s, const fe2_people_services *g,
                            fe2_call_args *args)
{
    c->s = s;
    c->g = g;
    c->args = args;
}

/* Argument block helpers. */
static inline fe2_object *people_object_arg(const people_context *c, unsigned n)
{
    const fe2_call_arg *arg = &c->args->arg[n];
    return arg->kind == FE2_ARG_OBJECT ? (fe2_object *)arg->ref : 0;
}

static inline void people_set_object(people_context *c, unsigned n, fe2_object *object)
{
    fe2_args_object(c->args, n, object);
}

/* A slot the original fills with a resident address the port does not
 * model (it is not compared). */
static inline void people_forget(people_context *c, unsigned n)
{
    c->args->arg[n].kind = FE2_ARG_UNSET;
    c->args->arg[n].value = 0;
    c->args->arg[n].ref = 0;
}

static inline uint32_t people_arg(const people_context *c, unsigned n)
{
    return c->args->arg[n].value;
}

static inline int16_t people_sar16(uint16_t v, unsigned n)
{
    return (int16_t)((int32_t)(int16_t)v >> n);
}

static inline int32_t people_sar32(uint32_t v, unsigned n)
{
    return (int32_t)v < 0 ? (int32_t)~(~v >> n) : (int32_t)(v >> n);
}

/* Services as the module calls them. */
fe2_object *people_lookup(people_context *c, uint8_t handle);
/* Looks up the parent (+0xd2) into parameter 9. */
fe2_object *people_lookup_parent(people_context *c, fe2_object *object);
uint32_t people_random(people_context *c, uint32_t *second);
uint16_t people_random_below(people_context *c, uint16_t bound);
fe2_people_reply people_market(people_context *c, uint16_t service, uint8_t handle);
void people_release(people_context *c, fe2_object *object);
void people_remove(people_context *c, fe2_object *object);
void people_slot_flags(people_context *c, fe2_object *object, uint8_t flags);
void people_spawn_effect_at(people_context *c, const fe2_object *object, uint16_t kind);
void people_position_args(people_context *c, const fe2_object *object);
void people_transform(people_context *c, fe2_object *object, uint64_t change);
void people_play_sound(people_context *c, uint16_t sound, uint8_t level);

/* setup.c */
void people_setup_fields(people_context *c, fe2_object *object);
void people_generate_identity(people_context *c, fe2_object *object, uint16_t type);
void people_setup_near_ship(people_context *c, fe2_object *object);
void people_setup_distant(people_context *c, fe2_object *object);

/* allocate.c: nonzero when the object was made (parameter 9). */
int people_allocate(people_context *c, uint8_t marker);
int people_allocate_model(people_context *c, uint8_t marker, uint16_t model);

/* creation.c */
uint16_t people_create_linked(people_context *c, uint16_t subtype, uint16_t parent);
uint16_t people_create_positioned(people_context *c);
uint16_t people_create_related(people_context *c, uint16_t model);

/* events.c */
void people_populate_after_event(people_context *c);

/* population.c */
void people_populate(people_context *c);
/* nonzero with the market's station in parameter 8 and the market
 * in *market. */
int people_select_market(people_context *c, const fe2_people_market **market);

/* relationships.c: 0x2307, nonzero on success. */
int people_prepare_relationships(people_context *c, fe2_object *object);

/* update_dispatch.c */
void people_use_capacity(people_context *c, fe2_object *object);
void people_common_motion(people_context *c, fe2_object *object, uint64_t change);
void people_contact_transition(people_context *c, fe2_object *object);

/* effect_states.c */
void people_spawn_wreck(people_context *c, fe2_object *object);
void people_hit(people_context *c, fe2_object *object);

/* relationship_states.c */
uint8_t people_resolve_related(people_context *c, fe2_object *object, uint8_t handle);

/* behavior_states.c */
void people_engage_parent(people_context *c, fe2_object *object, uint8_t handle);
void people_follow(people_context *c, fe2_object *object);
void people_patrol(people_context *c, fe2_object *object);
void people_approach(people_context *c, fe2_object *object);
void people_track(people_context *c, fe2_object *object);

/* movement_states.c */
void people_arrive(people_context *c, fe2_object *object);

/* transition_states.c */
void people_wait_for_event(people_context *c, fe2_object *object);
void people_seek_market(people_context *c, fe2_object *object);

/* Update handlers (0x86). */
void people_update_escort_parent(people_context *c, fe2_object *object);
void people_update_patrol_parent(people_context *c, fe2_object *object);
void people_update_track_parent(people_context *c, fe2_object *object);
void people_update_approach_parent(people_context *c, fe2_object *object);
void people_update_choose_patrol_offset(people_context *c, fe2_object *object);
void people_update_consume_frame_capacity(people_context *c, fe2_object *object);
void people_update_leave_parent(people_context *c, fe2_object *object);
void people_update_adopt_target_as_parent(people_context *c, fe2_object *object);
void people_update_report_escort_arrival(people_context *c, fe2_object *object);
void people_update_spin_and_emit_effects(people_context *c, fe2_object *object);
void people_update_offset_and_wait_for_event(people_context *c, fe2_object *object);
void people_update_start_arrival_countdown(people_context *c, fe2_object *object);
void people_update_complete_arrival(people_context *c, fe2_object *object);
void people_update_home_missile(people_context *c, fe2_object *object);
void people_update_stationary_projectile(people_context *c, fe2_object *object);
void people_update_countdown_to_drift(people_context *c, fe2_object *object);
void people_update_remove_distant_cargo(people_context *c, fe2_object *object);
void people_update_remove_distant_effect(people_context *c, fe2_object *object);
void people_update_remove_distant_emission(people_context *c, fe2_object *object);
void people_update_expire_timed_effect(people_context *c, fe2_object *object);
void people_update_expire_hidden_craft(people_context *c, fe2_object *object);
void people_update_expire_launch_flash(people_context *c, fe2_object *object);
void people_update_request_departure(people_context *c, fe2_object *object);
void people_update_dock_at_parent(people_context *c, fe2_object *object);
void people_update_launch_from_parent(people_context *c, fe2_object *object);
void people_update_wait_for_station_clearance(people_context *c, fe2_object *object);
void people_update_return_to_parent(people_context *c, fe2_object *object);
void people_update_wait_for_departure(people_context *c, fe2_object *object);
void people_update_none(people_context *c, fe2_object *object);

/* Motion handlers (0x82). */
void people_motion_patrol_and_activate_slot(people_context *c, fe2_object *object, uint64_t change);
void people_motion_integrate_homing_missile(people_context *c, fe2_object *object, uint64_t change);
void people_motion_apply_projectile_force(people_context *c, fe2_object *object, uint64_t change);
void people_motion_emit_trail_and_drift(people_context *c, fe2_object *object, uint64_t change);
void people_motion_drift_and_collect_cargo(people_context *c, fe2_object *object, uint64_t change);
void people_motion_rotate_from_seed(people_context *c, fe2_object *object, uint64_t change);
void people_motion_expire_countdown_or_distance(people_context *c, fe2_object *object, uint64_t change);
void people_motion_burning_wreck(people_context *c, fe2_object *object, uint64_t change);
void people_motion_follow_docking_parent(people_context *c, fe2_object *object, uint64_t change);
void people_motion_none(people_context *c, fe2_object *object, uint64_t change);

/* Relationship handlers (0x9a). */
void people_reroute_on_parent_event(people_context *c, fe2_object *object);
void people_reroute_to_event_target(people_context *c, fe2_object *object);
void people_expire_or_untarget_missile(people_context *c, fe2_object *object);
void people_assign_patrol_parent(people_context *c, fe2_object *object);
void people_turn_and_track(people_context *c, fe2_object *object);
void people_report_player_offence_once(people_context *c, fe2_object *object);
void people_release_market_and_assign_parent(people_context *c, fe2_object *object);
void people_grant_departure_clearance(people_context *c, fe2_object *object);
void people_depart_immediately(people_context *c, fe2_object *object);

#endif
