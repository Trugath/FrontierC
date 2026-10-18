/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The per-pass motion services, the motion controls and the ambient
 * sound. */
#include "flight_internal.h"

enum { KEY_FASTER = 0x24, KEY_SLOWER = 0x3e, FRAME_PASSES = 0x0d };

static void clamp(flight_context *c, fe2_object *ship, uint32_t x, uint32_t y, uint32_t z)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_dword(&args, P0, x);
    fe2_args_dword(&args, P1, y);
    fe2_args_dword(&args, P2, z);
    c->g->clamp_controls(c->g->opaque, ship, &args);
}

static void half_turn(flight_context *c, fe2_object *ship)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_word(&args, P5, 0);
    fe2_args_dword(&args, P_HANDLE, c->s->frame_step >> 1);
    fe2_args_dword(&args, P_MODE, c->s->frame_step >> 1);
    c->g->turn(c->g->opaque, ship, &args);
}

/* a set ambient latch (motion flag 0x80) stops the ambient sound. */
static void stop_ambient(flight_context *c)
{
    if (!(c->s->motion_flags & 0x80)) return;
    c->s->motion_flags &= (uint8_t)~0x80;
    c->g->ambient(c->g->opaque, 0, 1, 300);
}

/*
 * 0x2024: two half-step turns, the parent's pull over the step, the
 * radial rate within 0x2625a00 (taking the second rotation rate), a frame
 * check every 14 passes (the flags' high byte counts), then the controls:
 * mode 0x48 asks for 0x884 on the second axis; thrust mode applies the
 * keys' full limit on the third (or stops the ambient sound); otherwise
 * the feedback toward the throttle speed.
 */
void flight_motion_services(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_call_args args;
    fe2_flight_feedback f;
    uint8_t passes;
    int16_t limit;
    half_turn(c, ship);
    half_turn(c, ship);
    g->linked_force(g->opaque, ship, s->frame_step);
    if (s->measured <= 0x2625a00) {
        g->radial_rate(g->opaque, ship);
        ship->spin[1] = ship->rotation_rates[1];
    }
    passes = (uint8_t)(ship->flags >> 8);
    --passes;
    ship->flags = (uint16_t)((ship->flags & 0xff) | passes << 8);
    if ((int8_t)passes < 0) {
        g->select_frame(g->opaque, ship);
        ship->flags = (uint16_t)((ship->flags & 0xff) | FRAME_PASSES << 8);
    }
    if (s->mode == FE2_FLIGHT_DISTANCE) {
        clamp(c, ship, 0, 0x884, 0);
        return;
    }
    if (!(s->control_flags & FE2_CONTROL_THRUST)) {
        fe2_args_clear(&args);
        fe2_args_dword(&args, P_HANDLE, (uint32_t)s->throttle);
        f = g->axis_feedback(g->opaque, ship, &args);
        clamp(c, ship, (uint32_t)f.v[0], (uint32_t)f.v[1], (uint32_t)f.v[2]);
        return;
    }
    ship->controls[0] = ship->controls[1] = ship->controls[2] = 0;
    if (s->keys[KEY_FASTER] == 0) limit = 0x7fff;
    else if (s->keys[KEY_SLOWER] == 0) limit = (int16_t)0x8000;
    else {
        stop_ambient(c);
        return;
    }
    ship->controls[2] = limit;
    clamp(c, ship, 0, 0, (uint32_t)(int32_t)limit);
}


void flight_clear_motion(flight_context *c, fe2_object *ship)
{
    ship->velocity[0] = ship->velocity[1] = ship->velocity[2] = 0;
    ship->spin[0] = ship->spin[1] = ship->spin[2] = 0;
    c->s->throttle = 0;
    ship->controls[0] = ship->controls[1] = ship->controls[2] = 0;
    stop_ambient(c);
}

/* (also mode 0x58's flight action): move by the motion sample's
 * change (parameters 2 and 3). */
void flight_apply_delta(flight_context *c, fe2_object *ship)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_dword(&args, P2, (uint32_t)c->s->motion_delta);
    fe2_args_dword(&args, P3, (uint32_t)(c->s->motion_delta >> 32));
    c->g->transform_vector(c->g->opaque, ship, &args);
}

/*
 * 0x215d: move, leave a trail effect at the ship's position (+0x11a), then
 * the ambient sound: with the setting on and no music, the largest
 * |control| (0x8000 counts as 32768) from 0x221 on sets the ambient (AX the
 * first |control|, BX (max - 0x221) / 64, CX 430 - BX) and the latch.
 */
void flight_relative_motion(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    fe2_call_args args;
    uint16_t a, b, m;
    unsigned i;
    flight_apply_delta(c, ship);
    if (ship->trail != 0) {
        fe2_args_clear(&args);
        fe2_args_word(&args, P_MODE, 0);
        for (i = 0; i < 3; ++i) {
            fe2_args_dword(&args, P0 + i, ship->position_low[i]);
            fe2_args_dword(&args, P3 + i, ship->position_high[i]);
        }
        c->g->spawn_effect(c->g->opaque, ship, &args);
    }
    if (!s->preferences[FE2_PREF_AMBIENT] || s->current_music >= 0) return;
    a = ship->controls[0] < 0 ? (uint16_t)-(uint16_t)ship->controls[0] : (uint16_t)ship->controls[0];
    b = ship->controls[1] < 0 ? (uint16_t)-(uint16_t)ship->controls[1] : (uint16_t)ship->controls[1];
    m = ship->controls[2] < 0 ? (uint16_t)-(uint16_t)ship->controls[2] : (uint16_t)ship->controls[2];
    if (b <= a) b = a;
    if (b <= m) b = m;
    if (b < 0x221) return;
    b = (uint16_t)((b - 0x221) >> 6);
    c->g->ambient(c->g->opaque, a, b, (uint16_t)(0x1ae - b));
    s->motion_flags |= 0x80;
}
