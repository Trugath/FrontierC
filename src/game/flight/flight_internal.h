/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_FLIGHT_INTERNAL_H
#define FE2_FLIGHT_INTERNAL_H

/* Shared declarations between the flight translation units. */
#include "flight.h"
#include "flight_tables.h"

/* Named parameter slots used by flight services. */
enum {
    P0, P1, P2, P3, P4, P5,
    P_HANDLE = 6,
    P_MODE = 7,
    P_SUBJECT = 8,
    P_OBJECT = 9,
    P_KIND = 10,
    P_FRAME = 11,
    P_OBJECTS = 12
};

enum {
    COLOUR_WHITE = 0x0f,
    STYLE_MESSAGE = 0xffff,
    /* Behaviours (+0xd3) and roles (+0x5e). */
    BEHAVIOUR_PARKED = 0x84, ROLE_PLAYER = 3, ROLE_EFFECT = 0x0b,
    /* Messages (station text). */
    TEXT_COUNTDOWN = 0x98a7, TEXT_COUNTDOWN_END = 0x98a8, TEXT_NO_TARGET = 0x98aa,
    TEXT_TARGET_CLEARED = 0x98ab, TEXT_SPEED_WARNING = 0x98b7, TEXT_FUEL_SCOOPED = 0x98ef,
    TEXT_COMMAND_RESULT = 0x8633, TEXT_AUTOPILOT_OFF = 0x8637, TEXT_TOO_FAR = 0x9921
};

/* The translation units' context. */
typedef struct flight_context {
    fe2_flight_state *s;
    const fe2_flight_services *g;
    fe2_object *subject;            /* as the entry's caller left it */
} flight_context;

static inline int cockpit_shown(const fe2_flight_state *s) { return (int8_t)(uint8_t)s->view_mode < 0; }
static inline int32_t sar32(int32_t v, unsigned n)
{
    if (n > 31) n = 31;
    return v < 0 ? (int32_t)~(~(uint32_t)v >> n) : (int32_t)((uint32_t)v >> n);
}
static inline int16_t sar16(int16_t v, unsigned n) { return (int16_t)sar32(v, n); }
static inline int bit(uint32_t value, unsigned n) { return (int)(value >> n & 1u); }

/* Small service wrappers. */
void flight_effect(flight_context *c, uint16_t effect);
void flight_message(flight_context *c, uint16_t text, fe2_call_args *args);   /* parameter 7 0xffff */
fe2_object *flight_lookup(flight_context *c, uint8_t handle);                  /* list: current_objects */
void flight_select_root(flight_context *c, fe2_call_args *args);               /* = 0x56fb */

/* ship_setup.c */
void flight_restore_capacity(fe2_flight_state *s);
/* allocate the player's ship from `template_args` (parameter 8,
 * the template) with `model` and `seed`; null after the fatal error. */
fe2_object *flight_create_ship(flight_context *c, fe2_call_args *template_args, uint16_t model, uint16_t seed);
int flight_starting_ship(flight_context *c);                                   /*; 0 after the fatal error */

/* cockpit_view.c */
void flight_cycle_view(flight_context *c);
void flight_enter_cockpit(flight_context *c);
void flight_clear_regions(flight_context *c);

/* mode_display.c: returns the accumulator the original leaves (EAX). */
uint32_t flight_refresh_mode_display(flight_context *c, uint32_t eax);
void flight_disable_widgets(flight_context *c);
void flight_clear_widget_flag(flight_context *c);
void flight_widgets_if_enabled(flight_context *c);
void flight_refresh_widgets(flight_context *c);
int32_t flight_control_step(flight_context *c, int32_t value);
void flight_ship_command(flight_context *c, fe2_object *ship);

/* timers.c */
void flight_ship_countdowns(flight_context *c, fe2_object *ship);

/* flight_modes.c: the flight actions for the moving object. */
void flight_flight_action(flight_context *c, fe2_object *ship);
void flight_near_reference(flight_context *c, fe2_object *ship);
void flight_emission_chance(flight_context *c, fe2_object *ship);
void flight_begin_countdown(flight_context *c, fe2_object *contact);
void flight_object_message(flight_context *c, fe2_object *object, uint16_t text);

/* relative_modes.c */
void flight_adopt_reference(flight_context *c, fe2_object *ship, const fe2_flight_contacts *r);
void flight_reference_transition(flight_context *c, fe2_object *ship);
void flight_latch_approach_sound(flight_context *c);
void flight_latch_from_countdown(flight_context *c);
void flight_clear_latch(flight_context *c);
void flight_latch_reference_sound(flight_context *c);
void flight_relative_controls(flight_context *c, fe2_object *ship);
void flight_relative_special(flight_context *c, fe2_object *ship);
void flight_release_reference(flight_context *c, fe2_object *ship);

/* tracking.c */
fe2_flight_contacts flight_track_contacts(flight_context *c, fe2_object *ship);
void flight_emit_objects(flight_context *c, fe2_object *ship);

/* view_controls.c */
void flight_view_controls(flight_context *c, fe2_object *ship);
void flight_turret_controls(flight_context *c, fe2_object *ship);
void flight_read_inputs(flight_context *c);

/* motion_services.c */
void flight_motion_services(flight_context *c, fe2_object *ship);
void flight_clear_motion(flight_context *c, fe2_object *ship);
void flight_apply_delta(flight_context *c, fe2_object *ship);
void flight_relative_motion(flight_context *c, fe2_object *ship);

/* cockpit_modes.c: the cockpit actions. */
void flight_cockpit_action(flight_context *c);
int flight_build_scene(flight_context *c);                                     /*; 1: the scene was drawn */
void flight_external_controls(flight_context *c);
void flight_return_to_manual(flight_context *c);
void flight_reset_near_unit_basis(int16_t basis[9]);
void flight_scale_basis(int16_t basis[9]);

/* ship_actions.c */
void flight_pending_action(flight_context *c);
/* `template_record` is the allocator's template. */
int flight_replace_ship(flight_context *c, fe2_object *ship, fe2_object *template_record); /* 1 replaced */

/* status.c */
void flight_track_target(flight_context *c);
void flight_clear_target(flight_context *c, uint8_t handle);

/* Flight and cockpit actions by mode (dispatch.c). */
void flight_noop(flight_context *c, fe2_object *ship);

#endif
