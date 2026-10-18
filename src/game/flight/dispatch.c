/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The motion update and the two mode dispatches (0xf52..0xfb1, the
 * dispatch table at 0x1e1). */
#include "flight_internal.h"

typedef void (*flight_action)(flight_context *c, fe2_object *ship);
typedef void (*cockpit_action)(flight_context *c);

void flight_noop(flight_context *c, fe2_object *ship)
{
    (void)c;
    (void)ship;
}

/* Wrappers for the actions that take no record. */
static void latch_countdown_sound(flight_context *c, fe2_object *ship) { (void)ship; flight_latch_from_countdown(c); }
static void latch_reference_sound(flight_context *c, fe2_object *ship) { (void)ship; flight_latch_reference_sound(c); }
static void unlatch(flight_context *c, fe2_object *ship) { (void)ship; flight_clear_latch(c); }

/* flight_modes.c and relative_modes.c */
void flight_manual_flight(flight_context *c, fe2_object *ship);
void flight_primary_controls(flight_context *c, fe2_object *ship);
void flight_reference_flags(flight_context *c, fe2_object *ship);
void flight_status_flags(flight_context *c, fe2_object *ship);
void flight_distance_check(flight_context *c, fe2_object *ship);

static const flight_action flight_actions[FE2_FLIGHT_MODES] = {
    flight_manual_flight, flight_primary_controls, flight_noop, flight_relative_controls,
    flight_reference_transition, flight_reference_transition, latch_reference_sound, flight_noop,
    unlatch, latch_reference_sound, unlatch, latch_reference_sound,
    flight_noop, unlatch, latch_reference_sound, unlatch,
    latch_reference_sound, latch_countdown_sound, flight_distance_check, flight_relative_special,
    flight_noop, flight_noop, flight_apply_delta, flight_noop,
    flight_reference_flags, flight_noop, flight_status_flags, flight_noop,
    flight_noop
};

/* cockpit_modes.c */
void flight_cockpit_display(flight_context *c);
void flight_timed_guidance(flight_context *c);
void flight_guidance_turn_named_reference(flight_context *c);
void flight_guidance_approach_named_reference(flight_context *c);
void flight_guidance_rotate_named_reference(flight_context *c);
void flight_guidance_level_named_reference(flight_context *c);
void flight_guidance_bar_before_base(flight_context *c);
void flight_guidance_approach_bar_and_base(flight_context *c);
void flight_guidance_recede_base(flight_context *c);
void flight_guidance_approach_base(flight_context *c);
void flight_guidance_recede_bar_and_base(flight_context *c);
void flight_guidance_bar_before_departure(flight_context *c);
void flight_guidance_level_reference_before_release(flight_context *c);
void flight_guidance_release_reference_picture(flight_context *c);
void flight_enter_landed(flight_context *c);
void flight_landed_guidance(flight_context *c);
void flight_view_sequence(flight_context *c);
void flight_enter_docked_mode(flight_context *c);
void flight_mode_display(flight_context *c);
void flight_enter_mining_mode(flight_context *c);
void flight_wait_for_relative_release(flight_context *c);
void flight_wait_for_distance_release(flight_context *c);

static const cockpit_action cockpit_actions[FE2_FLIGHT_MODES] = {
    flight_cockpit_display, flight_cockpit_display, flight_timed_guidance, flight_cockpit_display,
    flight_cockpit_display, flight_cockpit_display, flight_guidance_turn_named_reference, flight_guidance_approach_named_reference,
    flight_guidance_rotate_named_reference, flight_guidance_level_named_reference, flight_guidance_bar_before_base, flight_guidance_approach_bar_and_base,
    flight_guidance_recede_base, flight_guidance_approach_base, flight_guidance_recede_bar_and_base, flight_guidance_bar_before_departure,
    flight_guidance_level_reference_before_release, flight_guidance_release_reference_picture, flight_cockpit_display, flight_cockpit_display,
    flight_enter_landed, flight_landed_guidance, flight_view_sequence, flight_enter_docked_mode,
    flight_mode_display, flight_enter_mining_mode, flight_mode_display, flight_wait_for_relative_release,
    flight_wait_for_distance_release
};

/* The mode is a byte offset (four per record); there is no check. */
void flight_flight_action(flight_context *c, fe2_object *ship)
{
    flight_actions[c->s->mode / 4](c, ship);
}

void flight_cockpit_action(flight_context *c)
{
    cockpit_actions[c->s->mode / 4](c);
}

void fe2_flight_update(fe2_flight_state *state, const fe2_flight_services *game, fe2_object *record,
                       uint64_t sample)
{
    flight_context c;
    uint64_t previous = (uint64_t)record->orbit[1] << 32 | record->orbit[0];
    c.s = state;
    c.g = game;
    c.subject = record;
    record->orbit[0] = (uint32_t)sample;
    record->orbit[1] = (uint32_t)(sample >> 32);
    state->motion_delta = sample - previous;
    record->update_period = record->update_countdown = 0;
    flight_ship_countdowns(&c, record);
    flight_flight_action(&c, record);
}

void fe2_flight_cockpit(fe2_flight_state *state, const fe2_flight_services *game, fe2_object *subject)
{
    flight_context c;
    c.s = state;
    c.g = game;
    c.subject = subject;
    flight_cockpit_action(&c);
}
