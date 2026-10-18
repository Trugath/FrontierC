/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Contact tracking and the emitted objects. */
#include "flight_internal.h"

enum { FAR_LIMIT = 0xb71b00, NEAR_LIMIT = 0x1312d00 };

/*
 * 0x1849: scan the ship's contacts. The distance becomes the measured
 * value; crossing 0xb71b00 (either way, latched in motion flag 0x40, not in
 * mode 0x58 when leaving) redraws the mode display. In contact (flag 1)
 * with a station-class model (0x92..0xae): a ship with the scoop (bit 20)
 * near enough, fast enough and on the heat timer gathers a unit of
 * hydrogen while there is room; from model 0x98 the heat rises with the
 * step (/256, /1024 below 0x9e) and wraps into damage to the contact.
 * Out of contact, a ship without equipment bit 17 moves two cargo counts
 * on (12 to 9, 11 to 4).
 */
fe2_flight_contacts flight_track_contacts(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_flight_contacts r = g->scan_contacts(g->opaque, ship);
    fe2_call_args args;
    uint16_t model;
    uint32_t rise;
    int redraw;
    s->measured = r.distance;
    s->contact_status = r.flags;
    if (r.distance < FAR_LIMIT) {
        redraw = (s->motion_flags & 0x40) != 0;
        s->motion_flags &= (uint8_t)~0x40;
    } else {
        redraw = s->mode != FE2_FLIGHT_SEQUENCE && !(s->motion_flags & 0x40);
        if (s->mode != FE2_FLIGHT_SEQUENCE) s->motion_flags |= 0x40;
    }
    if (redraw && cockpit_shown(s)) flight_refresh_mode_display(c, 0);
    if (r.flags & 1) {
        model = r.record->model;
        if (model < 0x92 || model > 0xae) return r;
        if (bit(ship->as.flight.equipment, 20)) {
            if (r.distance > 0x1c9c380 || (int32_t)s->speed < 0x2328) return r;
            if (s->heat_timer >= s->frame_step) {
                s->heat_timer -= s->frame_step;
                return r;
            }
            s->heat_timer -= s->frame_step;
            if (s->cargo_used < s->capacity) {
                ++s->cargo_used;
                ++s->cargo[19];
                fe2_args_clear(&args);
                fe2_args_dword(&args, P1, s->cargo[19]);
                flight_message(c, TEXT_FUEL_SCOOPED, &args);
                g->post_event_value(g->opaque, 0x16, 0x13);
                flight_effect(c, 0x11);
            }
        }
        model = r.record->model;
        if (model >= 0x98) {
            rise = s->frame_step >> 8;
            if (model < 0x9e) rise >>= 2;
            if ((uint32_t)s->heat + (uint16_t)rise > 0xffff) {
                s->heat = (uint16_t)(s->heat + rise);
                fe2_args_clear(&args);
                fe2_args_word(&args, P_MODE, 0);
                g->damage(g->opaque, r.record, 0xffff, &args);
            } else {
                s->heat = (uint16_t)(s->heat + rise);
            }
        }
        s->heat_timer = 0x2468a;
        return r;
    }
    if (!bit(ship->as.flight.equipment, 17)) {
        if (s->cargo[12] != 0) {
            --s->cargo[12];
            ++s->cargo[9];
        }
        if (s->cargo[11] != 0) {
            --s->cargo[11];
            ++s->cargo[4];
        }
    }
    return r;
}

/*
 * The emitted objects' offsets : `v` and the shift word
 * make a 64-bit offset per axis, sign(v):v shifted right by the shift
 * (counts taken mod 64, as the original's 32-bit halves are shifted).
 */
static void wide_offset(const int32_t v[3], uint16_t shift, uint32_t low[3], uint32_t high[3])
{
    uint16_t over = (uint16_t)(shift - 0x20);
    unsigned i, n;
    for (i = 0; i < 3; ++i) {
        if ((int16_t)over < 0) {
            high[i] = (uint32_t)sar32(v[i], shift & 0x3f);
            n = (uint16_t)-over & 0x3f;
            low[i] = n > 31 ? 0 : (uint32_t)v[i] << n;
        } else {
            high[i] = v[i] < 0 ? 0xffffffffu : 0;
            low[i] = (uint32_t)sar32(v[i], over & 0x3f);
        }
    }
}

static void add_offset(fe2_object *o, const uint32_t low[3], const uint32_t high[3])
{
    unsigned i;
    for (i = 0; i < 3; ++i) {
        uint64_t p = ((uint64_t)o->position_high[i] << 32 | o->position_low[i]) +
                     ((uint64_t)high[i] << 32 | low[i]);
        o->position_low[i] = (uint32_t)p;
        o->position_high[i] = (uint32_t)(p >> 32);
    }
}

/* near body 0x72, a 1-in-10 chance (while fewer than the limit are
 * out) to emit a model 0x7c object (kind 0xf, upper slots), placed by the
 * ship's velocity plus a random offset, at shift 0x15 less the time
 * exponent. A miss restores ES:SI and returns; the trail roll still runs. */
static void emit_limited_variant(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_call_args args;
    fe2_object *o;
    uint32_t r, edx, low[3], high[3];
    int32_t v[3];
    if ((uint16_t)g->random(g->opaque, &edx) > 0x1999) return;
    if (ship->parent_handle != 0x72) return;
    if (s->emitted_count >= s->emitted_limit) return;
    fe2_args_clear(&args);
    fe2_args_word(&args, P0, 0xf);
    fe2_args_word(&args, P1, 0x7c);
    fe2_args_object(&args, P_SUBJECT, ship);
    o = g->allocate_upper(g->opaque, &args);
    if (o == 0) return;
    ++s->emitted_count;
    o->spin[0] = o->spin[1] = 0;
    o->station_slot = 0;
    o->role = ROLE_EFFECT;
    o->behaviour = 0x58;
    o->map_marker = 0xfc;
    r = g->random(g->opaque, &edx);
    o->target_seed = r >> 16 | r << 16;
    v[2] = sar16((int16_t)r, 1);
    r = g->random(g->opaque, &edx);
    v[0] = sar16((int16_t)r, 1);
    v[1] = (int16_t)edx;
    v[0] = (int32_t)((uint32_t)v[0] + (uint32_t)ship->velocity[0]);
    v[1] = (int32_t)((uint32_t)v[1] + (uint32_t)ship->velocity[1]);
    v[2] = (int32_t)((uint32_t)v[2] + (uint32_t)ship->velocity[2]);
    wide_offset(v, (uint16_t)(0x15 - s->time_exponent), low, high);
    add_offset(o, low, high);
    o->velocity[0] = o->velocity[1] = o->velocity[2] = 0;
}

/*
 * 0x19fe: beyond 0x1312d00, the limited variant, then (emissions on, the
 * cockpit shown, a 0x3332 roll, time running) a model 0x76 trail on the
 * ship's velocity at shift 0x19 less the time exponent, with a random basis
 * and a parameter from the range shift. The variant's misses return to
 * that roll; they do not cancel it. Nearer, the emission chance instead.
 */
void flight_emit_objects(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_call_args args;
    fe2_object *o;
    uint32_t r, edx, low[3], high[3];
    if (s->measured < NEAR_LIMIT) {
        flight_emission_chance(c, ship);
        return;
    }
    s->game_flags &= (uint8_t)~0x40;
    emit_limited_variant(c, ship);
    if (!s->preferences[FE2_PREF_EMISSIONS] || !cockpit_shown(s)) return;
    if ((uint16_t)g->random(g->opaque, &edx) > 0x3332) return;
    if (s->time_step == 0) return;
    fe2_args_clear(&args);
    fe2_args_word(&args, P0, 0x2a);
    fe2_args_word(&args, P1, 0x76);
    fe2_args_object(&args, P_SUBJECT, ship);
    o = g->allocate(g->opaque, &args, 0);
    if (o == 0) return;
    o->spin[0] = o->spin[1] = 0;
    o->station_slot = 0;
    o->role = ROLE_EFFECT;
    o->behaviour = 0x60;
    r = g->random(g->opaque, &edx);
    s->basis_angle[0] = (uint16_t)r;
    s->basis_angle[1] = (uint16_t)edx;
    g->basis(g->opaque, o);
    r = g->random(g->opaque, &edx);
    o->model_parameter = (uint16_t)((r & 3) + s->time_exponent - 7 + s->range_shift);
    wide_offset(ship->velocity, (uint16_t)(0x19 - s->time_exponent), low, high);
    add_offset(o, low, high);
    o->position_low[0] &= ~UINT32_C(0xff);
}
