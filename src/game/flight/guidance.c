/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The guidance pictures : the timed mode's picture and the
 * countdown modes 0x18..0x44, each drawing a guidance object over the
 * shared countdown. */
#include <string.h>

#include "flight_internal.h"

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
void flight_draw_guidance(flight_context *c, uint16_t model, int set, uint16_t countdown, uint32_t offset);

enum { MODEL_CLOUD = 0xba, MODEL_NAMED = 0x122, MODEL_BAR = 0x126, MODEL_BASE = 0x120,
       TARGET_RGB = 0x292 };

static void set_offset(fe2_object *o, uint32_t offset)
{
    o->camera_offset[0] = 0;
    o->camera_offset[1] = 0;
    o->camera_offset[2] = offset;
}

/*
 * 0x2aaf (mode 0x8): the countdown falls by the frame step's high word;
 * when it would wrap, manual flight returns. While the cockpit is shown
 * the front view (or the rear) draws the cloud model 0xba at the countdown
 * (front, +0x74 0) or at the jump time less it (rear, +0x74 0xffff).
 */
void flight_timed_guidance(flight_context *c)
{
    fe2_flight_state *s = c->s;
    fe2_object *o = &s->guide;
    uint32_t left;
    uint16_t marker;
    if (s->shared_countdown < (s->frame_step >> 16)) {
        flight_return_to_manual(c);
        return;
    }
    left = s->shared_countdown - (s->frame_step >> 16);
    s->shared_countdown = left;
    if (!cockpit_shown(s)) return;
    if (s->view_index == 1) {
        c->g->set_controls(c->g->opaque, 0x33b, 3);
        left = s->jump_time - left;
        marker = 0xffff;
    } else {
        if (s->view_index != 0) {
            s->view_index = 0;
            flight_refresh_mode_display(c, 0);
        }
        c->g->set_controls(c->g->opaque, 0x303, 0x3b);
        marker = 0;
    }
    o->cargo_type = marker;
    o->model = MODEL_CLOUD;
    flight_reset_near_unit_basis(o->orientation);
    o->size_mantissa = 0xffff;
    o->size_exponent = 0xffff;
    set_offset(o, left);
    c->g->render_object(c->g->opaque, o);
}

/* the countdown falls by the frame step; below zero the mode
 * becomes `next` and the countdown `reset` (the step then reads 0).
 * Returns the countdown, or 0 for no picture (the original returns from
 * its caller when the cockpit is not shown). */
static int prepare(flight_context *c, uint8_t next, uint32_t reset, uint32_t *countdown)
{
    fe2_flight_state *s = c->s;
    s->shared_countdown -= s->frame_step;
    *countdown = s->shared_countdown;
    if ((int32_t)s->shared_countdown < 0) {
        s->mode = next;
        s->shared_countdown = reset;
        *countdown = 0;
    }
    return cockpit_shown(s);
}

static int prepare_axis(flight_context *c, uint8_t next, uint32_t *countdown)
{
    return prepare(c, next, 0x1ffff, countdown);
}

/*
 * 0x2bd1: the guidance object: the model, the countdown word (+0x74) where
 * known, the offset (CX:BX) on the third axis, the reference's seed and
 * whether its flags lack bit 5 (+0x7a), the basis from the angles (the
 * first 0), and a label: '1' + the contact class, or (model 0x122) the
 * reference's name. Drawn with the scratch frame and list kept.
 */
void flight_draw_guidance(flight_context *c, uint16_t model, int set, uint16_t countdown, uint32_t offset)
{
    fe2_flight_state *s = c->s;
    fe2_object *o = &s->guide;
    fe2_object *ref = s->reference;
    fe2_object_list *saved = s->current_objects;
    s->basis_angle[0] = 0;
    s->redraw_flags &= (uint8_t)~1;
    o->target_seed = ref->target_seed;
    o->size_mantissa = (uint16_t)((o->size_mantissa & 0xff00) | !(ref->flags & 0x20));
    o->model = model;
    if (set) o->cargo_type = countdown;
    set_offset(o, offset);
    c->g->basis(c->g->opaque, o);
    o->name[0] = (char)(uint8_t)(s->contact_class + 0x31);
    o->name[1] = 0;
    if (model == MODEL_NAMED) memcpy(o->name, ref->name, FE2_OBJECT_NAME_BYTES);
    c->g->render_object(c->g->opaque, o);
    s->current_objects = saved;
    if (s->target != 0) s->target_colour = c->g->palette_rgb(c->g->opaque, TARGET_RGB);
}

/* Models 0x122 at the second angle 0x8000. */
static void named_half(flight_context *c, uint16_t countdown, uint32_t offset)
{
    c->s->basis_angle[1] = 0x8000;
    flight_draw_guidance(c, MODEL_NAMED, 1, countdown, offset);
}

static void named_level(flight_context *c, uint16_t countdown)
{
    c->s->basis_angle[1] = 0;
    flight_draw_guidance(c, MODEL_NAMED, 1, countdown, 0);
}

static void bar(flight_context *c, uint16_t countdown)
{
    c->s->basis_angle[1] = 0;
    flight_draw_guidance(c, MODEL_BAR, 1, countdown, 0);
}

/* The bar, then the base at 0x2ea60. The second picture's +0x74 is BP as
 * the render service leaves it in the original. */
static void bar_and_base(flight_context *c, uint16_t countdown)
{
    bar(c, countdown);
    c->s->basis_angle[1] = 0;
    flight_draw_guidance(c, MODEL_BASE, 0, 0, 0x2ea60);
}

static void base(flight_context *c, uint16_t countdown, uint32_t offset)
{
    c->s->basis_angle[1] = 0;
    flight_draw_guidance(c, MODEL_BASE, 1, countdown, offset);
}

void flight_guidance_turn_named_reference(flight_context *c)
{
    uint32_t t;
    if (prepare(c, 0x1c, 0x18000, &t)) named_half(c, (uint16_t)(t >> 1), 0x30000);
}

void flight_guidance_approach_named_reference(flight_context *c)
{
    uint32_t t;
    if (prepare_axis(c, 0x20, &t)) named_half(c, 0, t << 1);
}

void flight_guidance_rotate_named_reference(flight_context *c)
{
    uint32_t t;
    if (!prepare_axis(c, 0x24, &t)) return;
    c->s->basis_angle[1] = (uint16_t)(t >> 2);
    flight_draw_guidance(c, MODEL_NAMED, 1, (uint16_t)(t >> 2), 0);
}

void flight_guidance_level_named_reference(flight_context *c)
{
    uint32_t t;
    if (prepare_axis(c, 0x28, &t)) named_level(c, (uint16_t)~(t >> 1));
}

static void bar_mode(flight_context *c, uint8_t next)
{
    uint32_t t;
    if (prepare_axis(c, next, &t)) bar(c, 0xffff);
}

void flight_guidance_bar_before_base(flight_context *c) { bar_mode(c, 0x2c); }
void flight_guidance_bar_before_departure(flight_context *c) { bar_mode(c, 0x40); }

void flight_guidance_approach_bar_and_base(flight_context *c)
{
    uint32_t t;
    if (prepare(c, 0x30, 0x17530, &t)) bar_and_base(c, (uint16_t)(t >> 1));
}

void flight_guidance_recede_base(flight_context *c)
{
    uint32_t t;
    if (prepare_axis(c, FE2_FLIGHT_ENTER_LANDED, &t)) base(c, (uint16_t)(t << 1), t << 1);
}

void flight_guidance_approach_base(flight_context *c)
{
    uint32_t t;
    if (prepare_axis(c, 0x38, &t)) base(c, (uint16_t)(t << 1), 0x2ea60 - (t << 1));
}

void flight_guidance_recede_bar_and_base(flight_context *c)
{
    uint32_t t;
    if (prepare_axis(c, 0x3c, &t)) bar_and_base(c, (uint16_t)~(t >> 1));
}

void flight_guidance_level_reference_before_release(flight_context *c)
{
    uint32_t t;
    if (prepare(c, 0x44, 0x18000, &t)) named_level(c, (uint16_t)(t >> 1));
}

/* while shown, the named picture at -(0x30000 - 2t) (or -0x30000
 * when 2t is negative). */
static void countdown_picture(flight_context *c, uint32_t t)
{
    uint32_t offset = 0x30000;
    if (!cockpit_shown(c->s)) return;
    t <<= 1;
    if ((int32_t)t >= 0) offset -= t;
    c->s->basis_angle[1] = 0;
    flight_draw_guidance(c, MODEL_NAMED, 1, 0, (uint32_t)-offset);
}

/*
 * 0x2da0 (mode 0x44): above -0x10000 the countdown picture. Then effects 0x22
 * and 0x13, and the ship leaves the reference's slot: a reference in
 * state 0x20 puts it at the reference (mode 0x70, its basis and position
 * lows), others at (0, 0, 0x10000) in the reference's frame from now
 * (mode 0x6c). The reference's slot (+0x9f) and +0x74 are cleared.
 */
void flight_guidance_release_reference_picture(flight_context *c)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_object *ship, *ref;
    uint32_t t;
    uint16_t x, y;
    unsigned i;
    s->shared_countdown -= s->frame_step;
    t = s->shared_countdown;
    if ((int32_t)t > -0x10000) {
        countdown_picture(c, t);
        return;
    }
    flight_effect(c, 0x22);
    flight_effect(c, 0x13);
    ref = s->reference;
    ship = s->ship;
    if (!(ref->flags & 0x20)) {
        s->mode = FE2_FLIGHT_WAIT_RELATIVE_RELEASE;
        ship->behaviour = 0;
        ship->orbit[0] = s->time_of_day;
        ship->orbit[1] = s->day;
        for (i = 0; i < 3; ++i) ship->position_high[i] = 0;
        ship->position_low[0] = 0;
        ship->position_low[1] = 0;
        ship->position_low[2] = 0x10000;
        ship->render_state = 0;
        ship->station_slot = 0xff;
        flight_clear_motion(c, ship);
        flight_reset_near_unit_basis(ship->orientation);
        s->throttle = 0xb5d;
    } else {
        s->mode = FE2_FLIGHT_WAIT_DISTANCE_RELEASE;
        ship->behaviour = 0;
        for (i = 0; i < 3; ++i) ship->position_low[i] = ref->position_low[i];
        memcpy(ship->orientation, ref->orientation, sizeof ship->orientation);
        g->rebuild_basis(g->opaque, ship);
        flight_clear_motion(c, ship);
        flight_refresh_mode_display(c, 0);
        ship->station_slot = 0xff;
        /* The mode display leaves DX = 0xbc (its last sprite's y): the
         * picture takes that as the countdown's high word. */
        t = 0xbc0000u | (t & 0xffff);
    }
    x = g->pointer(g->opaque, &y);
    s->axis[0] = x;
    s->axis[1] = y;
    ref->as.body.docking_state = 0xff;
    ref->cargo_type = 0xffff;
    countdown_picture(c, t);
    s->current_objects = s->objects;
    g->refresh_contacts(g->opaque, ship);
    s->redraw_flags |= 1;
}
