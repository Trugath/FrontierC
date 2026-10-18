/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The throttle, pointer and turret controls  and the
 * control step. */
#include "flight_internal.h"

enum { KEY_FASTER = 0x24, KEY_SLOWER = 0x3e, KEY_SLOT = 0x41 };

/* the throttle step from `value`: doubled five times (0x7fff if
 * that overflows), its high word plus one (0x7fff when that wraps to
 * zero); then, with a time scale above 16 (signed), times the scale / 16
 * (at most 0x7fff); then times the real step's low word / 64. Products keep
 * their low 32 bits; the shifts are logical. */
int32_t flight_control_step(flight_context *c, int32_t value)
{
    uint32_t v = (uint32_t)value;
    unsigned i;
    for (i = 0; i < 5; ++i) {
        if (((v >> 31) ^ (v >> 30)) & 1) break;
        v <<= 1;
    }
    if (i == 5 && (uint16_t)((v >> 16) + 1) != 0) v = (uint16_t)((v >> 16) + 1);
    else v = 0x7fff;
    if ((int16_t)c->s->time_scale > 0x10) {
        v = (v * c->s->time_scale) >> 4;
        if (v > 0x7fff) v = 0x7fff;
    }
    return (int32_t)((v * (uint16_t)c->s->real_step) >> 6);
}

/* The throttle keys: faster adds the step (saturating at 0x7fffffff) unless
 * the throttle is negative (then it is cleared); slower takes the step off
 * a positive throttle (cleared on an unsigned borrow) and marks the
 * retard flag, or, without that flag, takes real step / 256 off a
 * non-positive one (saturating at 0x80000000). */
static void throttle_keys(flight_context *c)
{
    fe2_flight_state *s = c->s;
    int64_t sum;
    uint32_t step;
    if (s->keys[KEY_FASTER] == 0) {
        if (s->throttle < 0) {
            s->throttle = 0;
            return;
        }
        sum = (int64_t)s->throttle + flight_control_step(c, s->throttle);
        s->throttle = sum > INT32_MAX ? INT32_MAX : (int32_t)sum;
    }
    if (s->keys[KEY_SLOWER] != 0) {
        s->control_flags &= (uint8_t)~FE2_CONTROL_RETARD;
        return;
    }
    if (s->throttle > 0) {
        s->control_flags |= FE2_CONTROL_RETARD;
        step = (uint32_t)flight_control_step(c, s->throttle);
        if ((uint32_t)s->throttle < step) s->throttle = 0;
        else s->throttle = (int32_t)((uint32_t)s->throttle - step);
        return;
    }
    if (s->control_flags & FE2_CONTROL_RETARD) return;
    sum = (int64_t)s->throttle - (int64_t)(s->real_step >> 8);
    s->throttle = sum < INT32_MIN ? INT32_MIN : (int32_t)sum;
}

/* High word of twice the product: the stick's contribution. */
static int16_t stick_term(int16_t axis, int16_t step)
{
    return (int16_t)(uint16_t)(((uint32_t)((int32_t)axis * step) << 1) >> 16);
}

/* pointer movement (x64, y negated) plus the stick turn the ship
 * (+0x86 by y, +0x8a against x, or +0x88 by x on the roll axis setting) or,
 * in the turret views, the turret. */
static void pointer_controls(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    uint16_t x, y;
    int16_t dx, dy, a, step;
    x = c->g->pointer(c->g->opaque, &y);
    dx = (int16_t)(uint16_t)((uint16_t)(x - s->axis[0]) << 6);
    dy = (int16_t)(uint16_t)((uint16_t)-(uint16_t)(y - s->axis[1]) << 6);
    s->axis[0] = x;
    s->axis[1] = y;
    step = s->real_step < 0x7fff ? (int16_t)s->real_step : 0x7fff;
    a = (int16_t)-(uint16_t)s->stick[0];
    if (!s->preferences[FE2_PREF_ROLL_AXIS]) a = sar16(a, 1);
    a = sar16(a, 1);
    if (a != 0) dx = (int16_t)(dx - stick_term(a, step));
    a = sar16((int16_t)s->stick[1], 2);
    if (a != 0) dy = (int16_t)(dy + stick_term(a, step));
    if (s->view_index == 2 || s->view_index == 3) {
        s->turret_angle[0] = (int16_t)(s->turret_angle[0] + dx);
        s->turret_angle[1] = (int16_t)(s->turret_angle[1] - dy);
        if (s->turret_angle[1] < 0) {
            s->view_index = 2;
            if (s->turret_angle[1] <= (int16_t)0xc000) s->turret_angle[1] = (int16_t)0xc001;
        } else if (ship->as.flight.capacity_class < 4) {
            s->turret_angle[1] = -1;
        } else {
            s->view_index = 3;
            if (s->turret_angle[1] >= 0x4000) s->turret_angle[1] = 0x3fff;
        }
        return;
    }
    if (!s->preferences[FE2_PREF_INVERT_X]) dx = (int16_t)-dx;
    if (s->preferences[FE2_PREF_INVERT_Y]) dy = (int16_t)-dy;
    ship->spin[0] = (int16_t)(ship->spin[0] + dy);
    if (s->preferences[FE2_PREF_ROLL_AXIS] && !(s->contact_status & 0x20)) {
        ship->spin[1] = (int16_t)(ship->spin[1] + dx);
        if (s->measured > 0x2625a00) return;
    }
    ship->spin[2] = (int16_t)(ship->spin[2] - dx);
}

/* only while the cockpit is shown. */
void flight_view_controls(flight_context *c, fe2_object *ship)
{
    if (!cockpit_shown(c->s)) return;
    if (!(c->s->control_flags & FE2_CONTROL_THRUST)) throttle_keys(c);
    pointer_controls(c, ship);
}

/* the pointer moves the turrets in the turret views only. */
void flight_turret_controls(flight_context *c, fe2_object *ship)
{
    if (c->s->view_index == 2 || c->s->view_index == 3) pointer_controls(c, ship);
}

/* the slot key fires the current view's equipment slot (the
 * external view's is 0), unless the 0x8911/0x8914/0x8915 states hold it. */
void flight_read_inputs(flight_context *c)
{
    fe2_flight_state *s = c->s;
    uint16_t y;
    c->g->pointer(c->g->opaque, &y);
    s->current_objects = s->objects;
    if (s->keys[KEY_SLOT] != 0 && ((s->gameport_flags & 0x10) || !(s->gameport_flags & 0x20)) &&
        (s->primary_button == 0 || s->secondary_button == 0))
        return;
    c->g->activate_slot(c->g->opaque, s->ship, s->view_index == 4 ? 0 : s->view_index);
}
