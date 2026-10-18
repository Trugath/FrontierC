/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The primary flight actions. */
#include "flight_internal.h"

enum { NEAR_LIMIT = 0x1312d00, PROBE_DOWN = 0x1a80, PROBE_AHEAD = 0x1a84 };

void flight_manual_flight(flight_context *c, fe2_object *ship);
void flight_primary_controls(flight_context *c, fe2_object *ship);
void flight_reference_flags(flight_context *c, fe2_object *ship);
void flight_status_flags(flight_context *c, fe2_object *ship);
void flight_distance_check(flight_context *c, fe2_object *ship);

static void position_from_point(fe2_object *o, const int64_t point[3])
{
    unsigned i;
    for (i = 0; i < 3; ++i) {
        o->position_low[i] = (uint32_t)(uint64_t)point[i];
        o->position_high[i] = (uint32_t)((uint64_t)point[i] >> 32);
    }
}

/* (mode 0x60). */
void flight_reference_flags(flight_context *c, fe2_object *ship)
{
    c->s->measured = 0;
    flight_turret_controls(c, ship);
    if (c->s->reference->flags & 0x40) flight_near_reference(c, ship);
}

/* (mode 0x68). */
void flight_status_flags(flight_context *c, fe2_object *ship)
{
    c->s->measured = 0;
    flight_turret_controls(c, ship);
    if (c->s->contact_status & 1) flight_near_reference(c, ship);
    else flight_emission_chance(c, ship);
}

/* (mode 0x48): beyond 200000 or with a throttle, mode 0x4c. With
 * fuel, a probe straight down the ship's second axis leaves an effect
 * where it hits. */
void flight_distance_check(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_flight_contacts r;
    fe2_call_args args;
    fe2_flight_probe probe;
    unsigned i;
    flight_view_controls(c, ship);
    if ((int32_t)s->measured > 0x30d40 || s->throttle != 0) {
        s->mode = FE2_FLIGHT_RELATIVE_SPECIAL;
        flight_refresh_mode_display(c, 0);
    }
    flight_motion_services(c, ship);
    g->integrate(g->opaque, ship, (uint32_t)s->motion_delta);
    flight_relative_motion(c, ship);
    if (ship->fuel != 0) {
        fe2_args_clear(&args);
        fe2_args_word(&args, P3, (uint16_t)-ship->orientation[1]);
        fe2_args_word(&args, P4, (uint16_t)-ship->orientation[4]);
        fe2_args_word(&args, P5, (uint16_t)-ship->orientation[7]);
        fe2_args_dword(&args, P0, 0);
        fe2_args_dword(&args, P1, 0);
        fe2_args_dword(&args, P2, 0);
        fe2_args_byte(&args, P_HANDLE, s->player_handle);
        fe2_args_word(&args, P_KIND, PROBE_DOWN);
        fe2_args_dword(&args, P_MODE, 0x7fff);
        fe2_args_object(&args, P_SUBJECT, ship);
        fe2_args_dword(&args, P_FRAME, 0);
        fe2_args_objects(&args, P_OBJECTS, s->current_objects);
        if (g->probe(g->opaque, &args, &probe)) {
            fe2_args_clear(&args);
            for (i = 0; i < 3; ++i) {
                fe2_args_dword(&args, P0 + i, (uint32_t)(uint64_t)probe.point[i]);
                fe2_args_dword(&args, P3 + i, (uint32_t)((uint64_t)probe.point[i] >> 32));
            }
            fe2_args_word(&args, P_MODE, 2);
            g->spawn_effect(g->opaque, ship, &args);
        }
    }
    r = flight_track_contacts(c, ship);
    if (s->mode == FE2_FLIGHT_SEQUENCE) return;
    if (r.flags & 1) flight_near_reference(c, ship);
    else flight_emission_chance(c, ship);
}

/* the drag. The velocity's words from bit 10 times the frame step
 * (at most 0x7fff), shifted right nine, come off the velocity. */
static void apply_drag(fe2_flight_state *s, fe2_object *ship)
{
    int16_t step = (s->frame_step >> 16) != 0 || (s->frame_step & 0x8000) ? 0x7fff : (int16_t)s->frame_step;
    unsigned i;
    for (i = 0; i < 3; ++i) {
        int16_t high = (int16_t)(uint16_t)(((uint32_t)ship->velocity[i] << 6) >> 16);
        ship->velocity[i] = (int32_t)((uint32_t)ship->velocity[i] - (uint32_t)sar32((int32_t)high * step, 9));
    }
}

static void emission_probe(flight_context *c, fe2_object *ship);

/* within 0x1312d00 of the reference. Unshielded ships heat up
 * with speed (damage when the heat wraps or speed >> 10 is not zero), the
 * parent's temperature may sound a warning, and from speed 3000 the drag
 * applies. Then the emission chance (keeping the warning flag). */
void flight_near_reference(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_call_args args;
    uint32_t heat;
    uint16_t amount = 0xffff;
    int burn;
    if (s->measured > NEAR_LIMIT) return;
    if (!bit(ship->as.flight.equipment, 30)) {
        heat = s->speed >> 4;
        burn = (uint32_t)s->heat + (uint16_t)heat > 0xffff;
        if (!burn) {
            amount = (uint16_t)(heat >> 6);
            burn = (heat >> 6) != 0;
        }
        s->heat = (uint16_t)(s->heat + heat);
        if (burn) {
            fe2_args_clear(&args);
            fe2_args_word(&args, P_MODE, 0);
            g->damage(g->opaque, ship, amount, &args);
        }
    }
    if (s->preferences[FE2_PREF_HEAT_EFFECT] && s->preferences[FE2_PREF_EFFECTS] &&
        flight_lookup(c, ship->parent_handle)->as.body.temperature <= 0x116) {
        s->game_flags |= 0x40;
        flight_effect(c, 0xe);
    }
    if ((int32_t)s->speed >= 0xbb8) {
        apply_drag(s, ship);
        if (s->measured > NEAR_LIMIT) return;
    }
    emission_probe(c, ship);
}

/*
 * 0x11b5: with emissions on, the cockpit shown and fewer than 15 reusable
 * slots, a 1-in-5 chance to probe from the ship's velocity along a random
 * direction. A hit on the ship's parent places an effect object there: its
 * variant comes from the parent's model, its basis from a random angle
 * pair and the hit normal. Near the contact it is removed (or, in contact,
 * becomes model 0xc6).
 */
static void emission_probe(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_call_args args;
    fe2_flight_probe probe;
    uint32_t a, b, edx;
    int16_t direction[3];
    uint16_t model, pair_model, parameter;
    fe2_object *o, *q;
    const uint16_t *pair;
    unsigned i;
    if ((int16_t)s->reusable_count >= 0xf || !s->preferences[FE2_PREF_EMISSIONS] || !cockpit_shown(s)) return;
    if ((uint16_t)g->random(g->opaque, &edx) > 0x3332) return;
    a = g->random(g->opaque, &edx);
    b = g->random(g->opaque, &edx);
    fe2_args_clear(&args);
    fe2_args_word(&args, P2, (uint16_t)a);
    fe2_args_word(&args, P0, (uint16_t)b);
    fe2_args_word(&args, P1, (uint16_t)edx);
    g->normalize(g->opaque, &args, direction);
    for (i = 0; i < 3; ++i)
        fe2_args_dword(&args, P0 + i, ((uint32_t)ship->velocity[i] << 8) +
                                      (uint32_t)((int32_t)ship->orientation[1 + 3 * i] * 1024));
    for (i = 0; i < 3; ++i) fe2_args_word(&args, P3 + i, (uint16_t)direction[i]);
    fe2_args_byte(&args, P_HANDLE, s->player_handle);
    fe2_args_word(&args, P_KIND, PROBE_AHEAD);
    fe2_args_dword(&args, P_MODE, 0x3ffffff);
    fe2_args_object(&args, P_SUBJECT, ship);
    fe2_args_dword(&args, P_FRAME, 0);
    fe2_args_objects(&args, P_OBJECTS, s->current_objects);
    if (!g->probe(g->opaque, &args, &probe) || probe.distance == 0) return;
    if (probe.record->handle != ship->parent_handle) return;
    model = flight_lookup(c, probe.record->handle)->model;
    if (model > 0x98 || model < 0x7a) return;
    pair = fe2_flight_variants[fe2_flight_variant_blocks[(model - 0x7a) / 2]][(g->random(g->opaque, &edx) & 0x1c) / 4];
    pair_model = pair[0];
    parameter = pair[1];
    fe2_args_clear(&args);
    fe2_args_word(&args, P1, pair_model);
    fe2_args_word(&args, P2, parameter);
    fe2_args_word(&args, P0, 0x2a);
    fe2_args_object(&args, P_SUBJECT, ship);
    o = g->allocate(g->opaque, &args, 0);
    if (o == 0) return;
    o->model_parameter = parameter;
    o->role = ROLE_EFFECT;
    o->behaviour = 0x54;
    o->size_mantissa = 0xffff;
    a = g->random(g->opaque, &edx);
    s->basis_angle[0] = (uint16_t)a;
    s->basis_angle[1] = (uint16_t)edx;
    o->target_seed = a >> 16 | a << 16;
    g->basis(g->opaque, o);
    o->orientation[2] = probe.normal[0];
    o->orientation[5] = probe.normal[1];
    o->orientation[8] = probe.normal[2];
    g->rebuild_basis(g->opaque, o);
    for (i = 0; i < 9; i += 3) {
        int16_t t = o->orientation[i + 1];
        o->orientation[i] = (int16_t)-o->orientation[i];
        o->orientation[i + 1] = o->orientation[i + 2];
        o->orientation[i + 2] = t;
    }
    if ((s->contact_status & 1) && (int32_t)g->random(g->opaque, &edx) < 0) goto contact;
    if (s->contact_handle != 0) {
        q = flight_lookup(c, s->contact_handle);
        fe2_args_clear(&args);
        fe2_args_object(&args, P_OBJECT, o);
        if ((int16_t)g->scaled_difference(g->opaque, q, &args).shift <= 0xe) {
            if (s->contact_status & 1) goto contact;
            g->release(g->opaque, o);
            return;
        }
    }
    o->render_state = 0;
    position_from_point(o, probe.point);
    return;
contact:
    o->model = 0xc6;
    o->render_state = 0;
    position_from_point(o, probe.point);
}


void flight_emission_chance(flight_context *c, fe2_object *ship)
{
    c->s->game_flags &= (uint8_t)~0x40;
    if (c->s->measured > NEAR_LIMIT) return;
    emission_probe(c, ship);
}

/* (mode 0x4, the autopilot). */
void flight_primary_controls(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_flight_contacts r;
    fe2_call_args args;
    uint32_t delta;
    flight_turret_controls(c, ship);
    r = flight_track_contacts(c, ship);
    if (s->mode == FE2_FLIGHT_SEQUENCE) return;
    if ((r.flags & 0x20) && (r.flags & 0x40)) {
        flight_adopt_reference(c, ship, &r);
        return;
    }
    if (r.flags & 1) flight_near_reference(c, ship);
    else flight_emit_objects(c, ship);
    flight_relative_motion(c, ship);
    delta = (uint32_t)s->motion_delta;
    g->linked_force(g->opaque, ship, delta);
    g->integrate_limited(g->opaque, ship, delta);
    fe2_args_clear(&args);
    fe2_args_dword(&args, P_HANDLE, delta);
    fe2_args_dword(&args, P_MODE, delta);
    fe2_args_word(&args, P5, 1);
    g->turn(g->opaque, ship, &args);
}

/* (mode 0x0). */
void flight_manual_flight(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_flight_contacts r;
    fe2_flight_market_reply reply;
    fe2_call_args args;
    uint32_t delta;
    flight_view_controls(c, ship);
    flight_motion_services(c, ship);
    delta = (uint32_t)s->motion_delta;
    if (s->control_flags & FE2_CONTROL_THRUST) g->integrate(g->opaque, ship, delta);
    else g->integrate_limited(g->opaque, ship, delta);
    flight_relative_motion(c, ship);
    r = flight_track_contacts(c, ship);
    if (s->mode == FE2_FLIGHT_SEQUENCE) return;
    if (!(r.flags & 0x20)) {
        if (r.flags & 0x40) {
            s->reference = r.record;
            s->reference_handle = r.record->handle;
            flight_clear_motion(c, ship);
            s->release_suppressed = 0xff;
            s->mode = FE2_FLIGHT_ENTER_MINING;
            return;
        }
        s->countdown = 0;
        if (r.flags & 1) flight_near_reference(c, ship);
        else flight_emit_objects(c, ship);
        return;
    }
    if (r.flags & 0x40) {
        s->mode = FE2_FLIGHT_DISTANCE;
        flight_refresh_mode_display(c, 0);
    }
    if (!(r.record->flags & 0x10)) return;
    fe2_args_clear(&args);
    fe2_args_object(&args, P_SUBJECT, ship);
    fe2_args_object(&args, P_OBJECT, r.record);
    g->market_overlay_service(g->opaque, 7, s->player_handle, &args, &reply);
    if (reply.result == 0) flight_begin_countdown(c, r.record);
    else s->mode = FE2_FLIGHT_RELATIVE;
}

/* a zero countdown starts at 6 and the contact becomes the
 * reference. */
void flight_begin_countdown(flight_context *c, fe2_object *contact)
{
    fe2_flight_state *s = c->s;
    if (s->countdown != 0) return;
    s->countdown = 6;
    s->reference = contact;
    s->reference_handle = contact->handle;
    flight_object_message(c, contact, TEXT_COUNTDOWN);
}

/* style 0x18, with the object (8) and its seed's words swapped (3). */
void flight_object_message(flight_context *c, fe2_object *object, uint16_t text)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_object(&args, P_SUBJECT, object);
    fe2_args_dword(&args, P3, object->target_seed << 16 | object->target_seed >> 16);
    fe2_args_word(&args, P_MODE, 0x18);
    c->g->station_message(c->g->opaque, text, &args);
}
