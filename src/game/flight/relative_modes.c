/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Relative flight, the reference transition and the sound latch
 *. */
#include "flight_internal.h"

/* a contact with flags 0x20 and 0x40 (and not 0x80) becomes the
 * reference; market is told (service 3). A contact with flag 0x40 then
 * enters mode 0x5c, others start the 0x70000 countdown in mode 0x10 or 0x14. */
void flight_adopt_reference(flight_context *c, fe2_object *ship, const fe2_flight_contacts *r)
{
    fe2_flight_state *s = c->s;
    fe2_flight_market_reply reply;
    fe2_call_args args;
    fe2_object *o = r->record;
    if (r->flags & 0x80) return;
    s->reference = o;
    s->release_suppressed = 0;
    s->reference_handle = o->handle;
    flight_clear_motion(c, ship);
    fe2_args_clear(&args);
    fe2_args_object(&args, P_SUBJECT, ship);
    fe2_args_object(&args, P_OBJECT, o);
    c->g->market_overlay_service(c->g->opaque, 3, s->player_handle, &args, &reply);
    if (o->flags & 0x40) {
        s->mode = FE2_FLIGHT_ENTER_DOCKED;
        flight_refresh_mode_display(c, 0);
        return;
    }
    s->shared_countdown = 0x70000;
    flight_effect(c, 0x13);
    s->control_flags &= (uint8_t)~(FE2_CONTROL_LATCH | FE2_CONTROL_ANNOTATE);
    if (o->flags & 0x20) {
        s->mode = FE2_FLIGHT_REFERENCE_ALIGNED;
        s->measured = 0;
    } else {
        s->mode = FE2_FLIGHT_REFERENCE_OFFSET;
    }
    flight_refresh_mode_display(c, 0);
}

/*
 * 0x168a (modes 0x10 and 0x14): the ship parks (behaviour 0x84) and moves
 * with the reference: the difference to it (mode 0x14 adds 0x10000 to the
 * third), / 64, is the velocity for one turn and motion pass, then
 * cleared. A docking reference (state 2) sounds effect 0x22 once. When the
 * shared countdown runs out, mode 0x18 starts with countdown 0x1ffff, market
 * is told (service 4) and the front view returns.
 */
void flight_reference_transition(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_flight_market_reply reply;
    fe2_call_args args;
    int32_t low[3];
    unsigned i;
    s->throttle = 0;
    s->target = 0;
    ship->behaviour = BEHAVIOUR_PARKED;
    if (s->reference->as.body.docking_state == 2 && !(s->control_flags & FE2_CONTROL_LATCH)) {
        s->control_flags |= FE2_CONTROL_LATCH;
        flight_effect(c, 0x22);
    }
    fe2_args_clear(&args);
    fe2_args_word(&args, P_MODE, 0xffff);
    g->rates_toward(g->opaque, ship, s->reference, &args);
    fe2_args_clear(&args);
    fe2_args_object(&args, P_OBJECT, s->reference);
    g->difference(g->opaque, ship, &args, low);
    if (s->mode == FE2_FLIGHT_REFERENCE_OFFSET) low[2] = (int32_t)((uint32_t)low[2] + 0x10000);
    for (i = 0; i < 3; ++i) ship->velocity[i] = sar32(low[i], 6);
    fe2_args_clear(&args);
    fe2_args_word(&args, P5, 1);
    fe2_args_dword(&args, P_HANDLE, s->frame_step);
    fe2_args_dword(&args, P_MODE, s->frame_step);
    g->turn(g->opaque, ship, &args);
    flight_relative_motion(c, ship);
    ship->velocity[0] = ship->velocity[1] = ship->velocity[2] = 0;
    g->refresh_contacts(g->opaque, ship);
    s->shared_countdown -= s->frame_step;
    if ((int32_t)s->shared_countdown >= 0) return;
    s->control_flags &= (uint8_t)~FE2_CONTROL_LATCH;
    s->mode = FE2_FLIGHT_GUIDANCE_TURN;
    s->shared_countdown = 0x1ffff;
    fe2_args_clear(&args);
    fe2_args_object(&args, P_SUBJECT, ship);
    fe2_args_object(&args, P_OBJECT, s->reference);
    g->market_overlay_service(g->opaque, 4, FE2_FLIGHT_NO_HANDLE, &args, &reply);
    s->view_index = 0;
    flight_refresh_mode_display(c, 0);
}

/* The latch (control flag 0x20) plays its effect on the 0-to-1 edge. */
void flight_latch_approach_sound(flight_context *c)
{
    if (c->s->control_flags & FE2_CONTROL_LATCH) return;
    c->s->control_flags |= FE2_CONTROL_LATCH;
    flight_effect(c, 0x12);
}

void flight_latch_from_countdown(flight_context *c)
{
    if ((int32_t)c->s->shared_countdown < 0) flight_latch_approach_sound(c);
    else flight_clear_latch(c);
}

void flight_clear_latch(flight_context *c)
{
    c->s->control_flags &= (uint8_t)~FE2_CONTROL_LATCH;
}

void flight_latch_reference_sound(flight_context *c)
{
    if (c->s->control_flags & FE2_CONTROL_LATCH) return;
    c->s->control_flags |= FE2_CONTROL_LATCH;
    flight_effect(c, 0x13);
}

/* turn toward the contact (the first rate only) and take its
 * second rotation rate. `ship` is null where the original's ES:SI is
 * what the station message call left (see flight_relative_controls). */
static void follow_contact(flight_context *c, fe2_object *ship, fe2_object *contact)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_word(&args, P_MODE, 0);
    c->g->rates_toward(c->g->opaque, ship, contact, &args);
    if (ship) ship->spin[1] = ship->rotation_rates[1];
}

/*
 * 0x1588 (mode 0xc). Without contact flag 0x20, manual flight. A contact
 * class  less one in 0..7 that differs from the last starts the
 * countdown; otherwise the countdown is cleared and a flag-0x40 contact is
 * adopted. Then the ship follows the contact.
 *
 * When the countdown starts, the original continues with the registers
 * the station message call left (SI = 0x60 in its overlay record segment), so
 * the follow applies to no record of the port: the rates service gets a
 * null record and the rate copy is skipped.
 */
void flight_relative_controls(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_flight_contacts r;
    uint8_t cls;
    flight_view_controls(c, ship);
    flight_motion_services(c, ship);
    g->integrate_limited(g->opaque, ship, (uint32_t)s->motion_delta);
    flight_relative_motion(c, ship);
    r = flight_track_contacts(c, ship);
    if (s->mode == FE2_FLIGHT_SEQUENCE) return;
    if (!(r.flags & 0x20)) {
        s->mode = FE2_FLIGHT_MANUAL;
        return;
    }
    cls = (uint8_t)(r.classification - 1);
    if (cls <= 7 && cls != s->contact_class) {
        if (s->countdown == 0) {
            flight_begin_countdown(c, r.record);
            ship = 0;
        }
    } else {
        s->countdown = 0;
        if (r.flags & 0x40) {
            flight_adopt_reference(c, ship, &r);
            return;
        }
    }
    follow_contact(c, ship, r.record);
}

/* (mode 0x4c). Without contact flag 0x20, manual flight and the
 * reference is released; a flag-0x40 contact with a reference in state
 * 0x20 enters mode 0x48. */
void flight_relative_special(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_flight_contacts r;
    flight_view_controls(c, ship);
    flight_motion_services(c, ship);
    g->integrate_limited(g->opaque, ship, (uint32_t)s->motion_delta);
    flight_relative_motion(c, ship);
    r = flight_track_contacts(c, ship);
    if (s->mode == FE2_FLIGHT_SEQUENCE) return;
    if (!(r.flags & 0x20)) {
        s->mode = FE2_FLIGHT_MANUAL;
        flight_refresh_mode_display(c, 0);
        flight_release_reference(c, ship);
        return;
    }
    if (!(r.flags & 0x40) || !(s->reference->flags & 0x20)) return;
    s->throttle = 0;
    s->mode = FE2_FLIGHT_DISTANCE;
    flight_refresh_mode_display(c, 0);
}

/* unless suppressed, market is told the reference is released
 * (service 2). */
void flight_release_reference(flight_context *c, fe2_object *ship)
{
    fe2_flight_market_reply reply;
    fe2_call_args args;
    if (c->s->release_suppressed != 0) return;
    fe2_args_clear(&args);
    fe2_args_object(&args, P_SUBJECT, ship);
    fe2_args_object(&args, P_OBJECT, c->s->reference);
    c->g->market_overlay_service(c->g->opaque, 2, c->s->player_handle, &args, &reply);
}
