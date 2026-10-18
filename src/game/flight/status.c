/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The cockpit status pass: target tracking, the status lines, the weapon
 * indicator and the markers. */
#include "flight_internal.h"

enum { TARGET_LINE = 0x0f0f, MARKER_LINES = 4 };

static void text(flight_context *c, uint16_t id, uint16_t x, uint16_t y, fe2_call_args *args)
{
    fe2_args_word(args, P_MODE, 0);
    c->g->draw_text_shadowed(c->g->opaque, id, COLOUR_WHITE, x, y, args);
}

/* the gun sight. */
static void weapon_indicator(flight_context *c)
{
    const fe2_flight_services *g = c->g;
    c->s->line_colour = TARGET_LINE;
    g->draw_line(g->opaque, 0x91, 0x54, 0x99, 0x54);
    g->draw_line(g->opaque, 0xaf, 0x54, 0xa7, 0x54);
    g->draw_line(g->opaque, 0xa0, 0x45, 0xa0, 0x4d);
    g->draw_line(g->opaque, 0xa0, 0x63, 0xa0, 0x5b);
}

/* the view marker when (x, y) lies in 14..305 x 14..153 (signed). */
static void view_marker(flight_context *c, int16_t x, int16_t y)
{
    unsigned i;
    c->s->line_colour = TARGET_LINE;
    if (x < 0xe || y < 0xe || x > 0x131 || y > 0x99) return;
    for (i = 0; i < MARKER_LINES; ++i) {
        const int16_t *r = fe2_flight_view_markers[i];
        c->g->draw_line(c->g->opaque, (uint16_t)(x + r[0]), (uint16_t)(y + r[1]), (uint16_t)(x + r[2]),
                        (uint16_t)(y + r[3]));
    }
}

/* a box of radius r around (x, y) in the target's colour; the
 * right edge runs one pixel further. */
static void marker_box(flight_context *c, uint16_t x, uint16_t y, uint16_t r)
{
    const fe2_flight_services *g = c->g;
    c->s->line_colour = c->s->target_colour;
    g->draw_clipped_line(g->opaque, (uint16_t)(x - r), (uint16_t)(y - r), (uint16_t)(x + r), (uint16_t)(y - r));
    g->draw_clipped_line(g->opaque, (uint16_t)(x + r), (uint16_t)(y - r), (uint16_t)(x + r), (uint16_t)(y + r + 1));
    g->draw_clipped_line(g->opaque, (uint16_t)(x + r), (uint16_t)(y + r), (uint16_t)(x - r), (uint16_t)(y + r));
    g->draw_clipped_line(g->opaque, (uint16_t)(x - r), (uint16_t)(y + r), (uint16_t)(x - r), (uint16_t)(y - r));
}

/* the box at ((127 - w) * p + w * (160, 84)) >> 7 (signed
 * products, a 32-bit logical shift), radius w. */
static void marker(flight_context *c, uint16_t x, uint16_t y, uint16_t w)
{
    int16_t k = (int16_t)(0x7f - w);
    uint32_t mx = (uint32_t)((int32_t)(int16_t)x * k) + (uint32_t)((int32_t)0xa0 * (int16_t)w);
    uint32_t my = (uint32_t)((int32_t)(int16_t)y * k) + (uint32_t)((int32_t)0x54 * (int16_t)w);
    marker_box(c, (uint16_t)(mx >> 7), (uint16_t)(my >> 7), w);
}

/*
 * 0x37ec: with a target, its colour and (when projected; otherwise the
 * target is cleared) its range from the player (a child of an owned
 * craft in state 0x10 is ranged by its parent): three markers toward the
 * centre sized by the range (with the setting, for models without
 * statistics unless all are marked), and the range label in km
 * or AU  units at the target, or offscreen in a quadrant record
 * (blinking with the time of day's bit 15).
 */
void flight_track_target(flight_context *c)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_object *t;
    fe2_call_args args;
    fe2_flight_scaled d;
    fe2_scalar range;
    uint32_t value;
    uint16_t w, unit, x, y, label = 0x8604;
    int16_t shift;
    int offscreen;
    const uint16_t *record;
    if (s->target == 0) return;
    s->target_colour = g->palette_index(g->opaque, s->target, s->target_colour);
    s->current_objects = s->objects;
    if (!(g->handle_flags(g->opaque, s->objects, s->target) & 8)) {
        flight_clear_target(c, s->target);
        return;
    }
    t = flight_lookup(c, s->target);
    if (t->update_period != 0 && (t->flags & 0x10)) t = flight_lookup(c, t->parent_handle);
    fe2_args_clear(&args);
    fe2_args_object(&args, P_OBJECT, s->ship);
    d = g->scaled_difference(g->opaque, t, &args);
    range.mantissa = g->vector_length(g->opaque, d.v[0], d.v[1], d.v[2]);
    range.exponent = d.shift;
    range = g->normalize_unsigned(g->opaque, range);
    s->target_range = range;
    if (s->view_index != 4 && s->preferences[FE2_PREF_TARGET_MARKERS] &&
        (s->preferences[FE2_PREF_ALL_MARKERS] || g->model(g->opaque, t->model)->stats_offset == 0)) {
        w = (uint16_t)((((uint16_t)-range.mantissa & 0x7ff) >> 5) + 0x40);
        x = (uint16_t)(t->screen_x - 1);
        y = t->screen_y;
        marker(c, x, y, w);
        w >>= 1;
        marker(c, x, y, w);
        w >>= 1;
        marker(c, x, y, w);
    }
    shift = (int16_t)(0x1d - range.exponent);
    if (shift >= 0) {
        unit = 0;
        value = (uint32_t)range.mantissa * 0xd1d0;
    } else {
        unit = 1;
        value = (uint32_t)range.mantissa * 0xeb54;
        shift = (int16_t)((shift + 0x18) & 0x3f);
    }
    value = (uint16_t)shift > 31 ? 0 : value >> (uint16_t)shift;
    x = t->screen_x;
    y = t->screen_y;
    offscreen = x >= 0x138 || y >= 0x9c;
    if (!offscreen) {
        x = (uint16_t)(x - 4);
        y = (uint16_t)(y - 4);
        offscreen = (int16_t)y < 0;
    }
    if (offscreen) {
        if (s->time_of_day & 0x8000) return;
        if (x == 0x8000) record = fe2_flight_label_records[t->update_period != 0 ? 5 : 0];
        else if ((int16_t)x <= 0xa0) record = fe2_flight_label_records[(int16_t)y <= 0x54 ? 1 : 3];
        else record = fe2_flight_label_records[(int16_t)y <= 0x54 ? 2 : 4];
        label = record[0];
        x = record[1];
        y = record[2];
    }
    fe2_args_clear(&args);
    fe2_args_dword(&args, P1, value);
    fe2_args_word(&args, P_MODE, 0);
    g->draw_text(g->opaque, (uint16_t)(label + unit), s->target_colour, x, y, &args);
}

/* The status pass's 32-bit magnitude reduction: shift right until below
 * 0x3f00, counting. */
static uint32_t reduce(uint32_t v, unsigned *count)
{
    while (v >= 0x3f00) {
        v >>= 1;
        ++*count;
    }
    return v;
}

static uint32_t magnitude(int32_t v)
{
    return v < 0 ? (uint32_t)-(uint32_t)v : (uint32_t)v;
}

/* A 32/16 division; the original's divide fault resumes after it with
 * AX unchanged (the dividend's low word). */
static uint16_t divide(uint32_t dividend, uint16_t divisor, int *fault)
{
    *fault = divisor == 0 || dividend / divisor > 0xffff;
    return *fault ? (uint16_t)dividend : (uint16_t)(dividend / divisor);
}

/*
 * 0x3456: the speed. The velocity's magnitudes are reduced together below
 * 0x3f00 (the shift kept in 0x2063), the reduced vector's length is scaled
 * back up (0x7fffffff on overflow).
 */
static void speed(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    unsigned n = 0, i;
    uint32_t v;
    int16_t w[3];
    reduce(magnitude(ship->velocity[2]), &n);
    reduce(n > 31 ? 0 : magnitude(ship->velocity[0]) >> n, &n);
    reduce(n > 31 ? 0 : magnitude(ship->velocity[1]) >> n, &n);
    for (i = 0; i < 3; ++i) w[i] = (int16_t)sar32(ship->velocity[i], n);
    v = c->g->vector_length(c->g->opaque, w[0], w[1], w[2]);
    s->range_shift = (uint16_t)n;
    for (i = 0; i < n; ++i) {
        if (((v >> 31) ^ (v >> 30)) & 1) {
            v = 0x7fffffff;
            break;
        }
        v <<= 1;
    }
    s->speed = v;
}

void fe2_flight_status(fe2_flight_state *state, const fe2_flight_services *game, fe2_object *subject)
{
    flight_context cx;
    flight_context *c = &cx;
    fe2_flight_state *s = state;
    const fe2_flight_services *g = game;
    fe2_object *ship, *t, *cur;
    const fe2_flight_model *m;
    fe2_call_args args;
    fe2_flight_pass pass;
    int16_t out[3];
    uint16_t sx, sy, q, id;
    uint32_t big;
    int fault;
    unsigned i;
    cx.s = state;
    cx.g = game;
    cx.subject = subject;
    flight_track_target(c);
    ship = s->ship;
    if (s->view_index != 4) {
        if (ship->as.flight.installed[s->view_index] != 0) weapon_indicator(c);
        if (s->view_index == 0) {
            fe2_args_clear(&args);
            for (i = 0; i < 3; ++i) fe2_args_dword(&args, P3 + i, (uint32_t)ship->velocity[i]);
            g->transform_feedback(g->opaque, ship, &args, out);
            if ((int16_t)(out[2] + 0xff) >= 0x40) {
                g->project(g->opaque, out[0], out[1], (int16_t)(out[2] + 0xff), &sx, &sy);
                view_marker(c, (int16_t)(sx + 0xa0), (int16_t)(0x54 - sy));
            }
        } else if (s->view_index >= 2) {
            fe2_args_clear(&args);
            fe2_args_dword(&args, P1, (uint32_t)sar32((int32_t)0xe10 * s->turret_angle[0], 16));
            fe2_args_dword(&args, P2, (uint32_t)sar32((int32_t)(int16_t)0xf1f0 * s->turret_angle[1], 16));
            text(c, 0x8636, 0xa, 0x4c, &args);
        }
    }
    speed(c, ship);
    fe2_args_clear(&args);
    fe2_args_dword(&args, P1, (uint32_t)s->throttle);
    fe2_args_dword(&args, P2, s->speed);
    s->current_objects = s->objects;
    if (ship->parent_handle != 0) fe2_args_string(&args, P_KIND, flight_lookup(c, ship->parent_handle)->name);
    else fe2_args_string(&args, P_KIND, "");
    id = 0x8620;
    if ((int32_t)s->measured >= 0) {
        q = divide(s->measured, 1000, &fault);
        if (!fault && q <= 0xea60) {
            fe2_args_dword(&args, P3, q);
            id = 0x8622;
        }
    }
    fe2_args_word(&args, P0, id);
    if ((s->control_flags & FE2_CONTROL_THRUST) || s->mode == FE2_FLIGHT_AUTOPILOT) ++id;
    text(c, id, 2, 0x9e, &args);
    if (s->control_flags & FE2_CONTROL_ANNOTATE) {
        pass.state = s;
        pass.game = g;
        g->visit_projected(g->opaque, s->objects, fe2_flight_annotate_object, &pass);
    }
    ship = s->ship;
    if (ship->as.flight.shield_charge != ship->as.flight.shield_capacity) {
        q = divide((uint32_t)ship->as.flight.shield_charge * 100, ship->as.flight.shield_capacity, &fault);
        if (!fault) {
            fe2_args_clear(&args);
            fe2_args_dword(&args, P1, (uint32_t)(int32_t)(int16_t)q);
            text(c, 0x8624, 2, 1, &args);
        }
    }
    if (s->delay_ticks != 0) {
        q = divide((uint32_t)ship->hull * 0xfa, g->model(g->opaque, ship->model)->mass, &fault);
        fe2_args_clear(&args);
        fe2_args_dword(&args, P1, (uint32_t)(int32_t)(int16_t)q);
        text(c, 0x8638, 2, 0xb, &args);
    }
    /* The target's statistics; `cur` follows the original's ES:SI. */
    cur = ship;
    if (s->control_flags & FE2_CONTROL_TARGET_STATS) {
        s->control_flags &= (uint8_t)~FE2_CONTROL_TARGET_STATS;
        if (bit(ship->as.flight.equipment, 22) && s->target != 0) {
            s->current_objects = s->objects;
            t = flight_lookup(c, s->target);
            cur = t;
            if (t->model <= 0x58 && (m = g->model(g->opaque, t->model))->stats_offset != 0) {
                fe2_args_clear(&args);
                id = 0x8626;
                if (t->as.flight.shield_capacity != 0) {
                    id = 0x8625;
                    big = (uint32_t)t->as.flight.shield_charge * 100;
                    fe2_args_dword(&args, P2, big >> 6);
                    q = divide(big, t->as.flight.shield_capacity, &fault);
                    fe2_args_dword(&args, P1, (uint32_t)(int32_t)(int16_t)q);
                }
                fe2_args_word(&args, P0, id);
                q = divide((uint32_t)t->hull * 0xfa, m->mass, &fault);
                fe2_args_dword(&args, P3, (uint32_t)(int32_t)(int16_t)q);
                fe2_args_word(&args, P_FRAME, m->ship_name_text);
                fe2_args_object(&args, P_SUBJECT, t);
                fe2_args_word(&args, P_KIND, fe2_flight_status_texts[t->as.flight.drive]);
                text(c, id, 0xeb, 1, &args);
                if (bit(t->as.flight.equipment, 24)) text(c, 0x862f, 0xeb, 0x33, &args);
                if (bit(t->as.flight.equipment, 26)) text(c, 0x9890, 0xeb, 0x3d, &args);
                if (t->map_marker == 0xfb && t->reward != 0) {
                    fe2_args_dword(&args, P3, t->reward);
                    if (!(s->time_of_day & 0x8000)) text(c, 0x8634, 0xeb, 0x47, &args);
                }
                s->control_flags |= FE2_CONTROL_TARGET_STATS;
            }
        }
    }
    if (!(s->control_flags & FE2_CONTROL_CLOUD) || !bit(cur->as.flight.equipment, 24) || s->target == 0) return;
    s->current_objects = s->objects;
    t = flight_lookup(c, s->target);
    if (t->model != 0xba) return;
    fe2_args_clear(&args);
    fe2_args_dword(&args, P1, t->hyperspace[0]);
    fe2_args_dword(&args, P2, t->orbit[0]);
    fe2_args_dword(&args, P3, t->orbit[1]);
    fe2_args_dword(&args, P_KIND, (uint32_t)(int32_t)(int16_t)
                   g->model(g->opaque, g->stray_word(g->opaque, t, s->target))->mass);
    id = (int16_t)t->cargo_type < 0 ? 0x8631 : t->cargo_type == 0 ? 0x8630 : 0x8632;
    text(c, id, 0xe1, 1, &args);
    s->control_flags |= FE2_CONTROL_CLOUD;
}
