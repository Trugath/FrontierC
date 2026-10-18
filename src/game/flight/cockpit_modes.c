/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Mode transitions, the cockpit scene and the mode events (0x21fe..0x2699,
 * 0x2f5a..0x3029). */
#include <string.h>

#include "flight_internal.h"

void flight_cockpit_display(flight_context *c);
void flight_enter_landed(flight_context *c);
void flight_landed_guidance(flight_context *c);
void flight_enter_docked_mode(flight_context *c);
void flight_mode_display(flight_context *c);
void flight_enter_mining_mode(flight_context *c);
void flight_wait_for_relative_release(flight_context *c);
void flight_wait_for_distance_release(flight_context *c);
/* guidance.c: `set` says whether +0x74 takes `countdown`. */
void flight_draw_guidance(flight_context *c, uint16_t model, int set, uint16_t countdown, uint32_t offset);

enum { SCENE_COPY = 0x60, TARGET_RGB = 0x292 };

static void after_transition(flight_context *c)
{
    fe2_object *ship = c->s->ship;
    flight_clear_motion(c, ship);
    c->s->control_flags &= (uint8_t)~FE2_CONTROL_THRUST;
    flight_refresh_mode_display(c, 0);
    flight_mode_display(c);
}

/* (mode 0x64). */
void flight_enter_mining_mode(flight_context *c)
{
    c->g->post_event(c->g->opaque, 0xe);
    c->s->mode = FE2_FLIGHT_MINING;
    c->s->release_suppressed = 0xff;
    c->s->mining_mode_active = 1;
    flight_widgets_if_enabled(c);
    after_transition(c);
}

/* (mode 0x5c): market is told of the reference (service 4). */
void flight_enter_docked_mode(flight_context *c)
{
    fe2_flight_market_reply reply;
    fe2_call_args args;
    c->g->post_event(c->g->opaque, 5);
    c->s->mode = FE2_FLIGHT_DOCKED;
    c->s->mining_mode_active = 0;
    fe2_args_clear(&args);
    fe2_args_object(&args, P_OBJECT, c->s->reference);
    c->g->market_overlay_service(c->g->opaque, 4, c->s->player_handle, &args, &reply);
    c->g->check_music(c->g->opaque);
    after_transition(c);
}

/* (modes 0x60 and 0x68): parked; the external angle's second word is
 * kept non-negative while the cockpit is shown. */
void flight_mode_display(flight_context *c)
{
    c->s->ship->behaviour = BEHAVIOUR_PARKED;
    flight_external_controls(c);
    if (!cockpit_shown(c->s)) return;
    if ((int16_t)c->s->external_angle[1] < 0) c->s->external_angle[1] = 0;
    flight_build_scene(c);
}


void flight_cockpit_display(flight_context *c)
{
    flight_external_controls(c);
    if (!cockpit_shown(c->s)) return;
    flight_build_scene(c);
}

/* when the reference's +0x74 byte is zero, mode 0x4c or 0x48. */
static void reference_mode(flight_context *c, uint8_t next)
{
    if ((uint8_t)c->s->reference->cargo_type == 0) c->s->mode = next;
    flight_external_controls(c);
    if (!cockpit_shown(c->s)) return;
    flight_build_scene(c);
}

void flight_wait_for_relative_release(flight_context *c) { reference_mode(c, FE2_FLIGHT_RELATIVE_SPECIAL); }
void flight_wait_for_distance_release(flight_context *c) { reference_mode(c, FE2_FLIGHT_DISTANCE); }

/* (mode 0x54): an optional duration effect, the ship parked, the
 * external controls unless input is held (game flag 1), then the guidance
 * picture of model 0x120. */
void flight_landed_guidance(flight_context *c)
{
    fe2_flight_state *s = c->s;
    if (s->preferences[FE2_PREF_DURATION]) c->g->play_duration(c->g->opaque, 0xf, 8);
    s->ship->behaviour = BEHAVIOUR_PARKED;
    if (!(s->game_flags & 1)) flight_external_controls(c);
    if (!cockpit_shown(s)) return;
    s->basis_angle[1] = 0;
    /* The original's +0x74 is whatever BP holds here (the caller's, or an
     * overlay record offset); the port leaves the work record's. */
    flight_draw_guidance(c, 0x120, 0, 0, 0);
}

/* (mode 0x50). */
void flight_enter_landed(flight_context *c)
{
    fe2_flight_state *s = c->s;
    fe2_object_list *saved = s->current_objects;
    c->g->post_event(c->g->opaque, 5);
    c->g->check_music(c->g->opaque);
    c->g->set_slot_flags(c->g->opaque, s->ship, 0x4b);
    s->target = 0;
    s->mode = FE2_FLIGHT_LANDED;
    flight_refresh_mode_display(c, 0);
    s->current_objects = saved;
    flight_landed_guidance(c);
}

/* the basis 0x7f00 * identity. */
void flight_reset_near_unit_basis(int16_t basis[9])
{
    unsigned i;
    for (i = 0; i < 9; ++i) basis[i] = i % 4 == 0 ? 0x7f00 : 0;
}

/* The product's high-word pair, arithmetically shifted. */
static int32_t scaled(int16_t a, int16_t b, unsigned shift)
{
    return sar32((int32_t)a * b, shift);
}

static void target_colour(flight_context *c)
{
    if (c->s->target != 0) c->s->target_colour = c->g->palette_rgb(c->g->opaque, TARGET_RGB);
}

/*
 * 0x2297: draw the scene from the ship. The frame is the ship's first 0x60
 * bytes (its basis generation cleared). The view sets the frame's basis
 * and an eye offset, which is added (sign-extended to 64 bits) to its
 * position:
 * - front: the model's front eye (a secondary basis rebuilt when the ship
 *   is docked);
 * - rear: the basis' first and third columns reversed, the rear eye;
 * - turrets (and any index above 4): the top (y + 0x3e) or bottom
 *   (y - 0x3e) eye, then the basis turned by the turret angles;
 * - external: the basis turned by the external angles (or built from
 *   them), the eye on the third column at the external distance's high
 *   word / 128. A probe along the reversed third column (in the root
 *   list) abandons the scene when it returns a zero distance.
 * Eye products are >> 12. Returns 0 when the probe abandoned the scene.
 */
int flight_build_scene(flight_context *c)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_object *f = &s->frame;
    fe2_object *ship = s->ship;
    const fe2_flight_model *m;
    fe2_call_args args;
    fe2_flight_probe probe;
    int32_t eye[3];
    int16_t *o = f->orientation;
    int16_t a, b;
    unsigned i;
    memset(f, 0, sizeof *f);
    memcpy(f->orientation, ship->orientation, sizeof f->orientation);
    f->rebuild_budget = ship->rebuild_budget;
    f->render_state = ship->render_state;
    memcpy(f->camera_offset, ship->camera_offset, sizeof f->camera_offset);
    memcpy(f->camera_target, ship->camera_target, sizeof f->camera_target);
    memcpy(f->position_low, ship->position_low, sizeof f->position_low);
    memcpy(f->position_high, ship->position_high, sizeof f->position_high);
    f->parent_handle = ship->parent_handle;
    f->station_slot = ship->station_slot;
    memcpy(f->alternate_orientation, ship->alternate_orientation, sizeof f->alternate_orientation);
    f->model_parameter = ship->model_parameter;
    f->model = ship->model;
    f->handle = ship->handle;
    f->role = ship->role;
    f->level = ship->level;
    f->basis_generation = 0;
    m = g->model(g->opaque, f->model);
    if (s->view_index == 4) {
        if (s->preferences[FE2_PREF_EXTERNAL_BASIS]) {
            s->view_owner = 0;
            fe2_args_clear(&args);
            fe2_args_word(&args, P_MODE, s->external_angle[0]);
            g->rotate_pair(g->opaque, f, 2, 0, &args);
            fe2_args_clear(&args);
            fe2_args_word(&args, P_MODE, s->external_angle[1]);
            g->rotate_pair(g->opaque, f, 1, 2, &args);
        } else {
            s->basis_angle[0] = s->external_angle[0];
            s->basis_angle[1] = s->external_angle[1];
            g->basis(g->opaque, f);
        }
        a = (int16_t)(s->external_distance >> 16);
        for (i = 0; i < 3; ++i) eye[i] = scaled(o[2 + 3 * i], a, 7);
        fe2_args_clear(&args);
        for (i = 0; i < 3; ++i) fe2_args_word(&args, P3 + i, (uint16_t)-o[2 + 3 * i]);
        fe2_args_word(&args, P_HANDLE, 0);
        fe2_args_word(&args, P_KIND, 0x1a80);
        fe2_args_dword(&args, P_MODE, 0xa);
        fe2_args_dword(&args, P_FRAME, 0);
        flight_select_root(c, &args);
        fe2_args_object(&args, P_SUBJECT, f);
        for (i = 0; i < 3; ++i) fe2_args_dword(&args, P0 + i, (uint32_t)eye[i]);
        g->probe(g->opaque, &args, &probe);
        if (probe.distance == 0) {
            target_colour(c);
            s->view_owner = 0;
            return 0;
        }
    } else if (s->view_index == 1) {
        for (i = 0; i < 9; i += 3) {
            o[i + 2] = (int16_t)-o[i + 2];
            o[i] = (int16_t)-o[i];
        }
        s->view_owner = 0;
        a = m->views[2];
        b = m->views[3];
        for (i = 0; i < 3; ++i) {
            eye[i] = (int32_t)o[2 + 3 * i] * a;
            if (b != 0) eye[i] = (int32_t)((uint32_t)eye[i] + (uint32_t)((int32_t)o[1 + 3 * i] * b));
            eye[i] = sar32(eye[i], 12);
        }
    } else if (s->view_index == 0) {
        if (ship->station_slot != 0) {
            fe2_args_clear(&args);
            fe2_args_object(&args, P_SUBJECT, ship);
            g->rebuild_secondary_basis(g->opaque, ship, &args);
        }
        s->view_owner = s->player_handle;
        a = m->views[0];
        b = m->views[1];
        for (i = 0; i < 3; ++i) {
            eye[i] = (int32_t)o[2 + 3 * i] * a;
            if (b != 0) eye[i] = (int32_t)((uint32_t)eye[i] + (uint32_t)((int32_t)o[1 + 3 * i] * b));
            eye[i] = sar32(eye[i], 12);
        }
    } else {
        s->view_owner = 0;
        if (s->view_index == 2) {
            a = m->views[8];
            b = (int16_t)(m->views[9] + 0x3e);
        } else {
            a = m->views[10];
            b = (int16_t)(m->views[11] - 0x3e);
        }
        for (i = 0; i < 3; ++i)
            eye[i] = (int32_t)((uint32_t)((int32_t)o[2 + 3 * i] * a) + (uint32_t)((int32_t)o[1 + 3 * i] * b));
        fe2_args_clear(&args);
        fe2_args_word(&args, P_MODE, (uint16_t)s->turret_angle[0]);
        g->rotate_pair(g->opaque, f, 2, 0, &args);
        fe2_args_clear(&args);
        fe2_args_word(&args, P_MODE, (uint16_t)s->turret_angle[1]);
        g->rotate_pair(g->opaque, f, 1, 2, &args);
        for (i = 0; i < 3; ++i) s->turret_axis[i] = o[2 + 3 * i];
        for (i = 0; i < 3; ++i) eye[i] = sar32(eye[i], 12);
    }
    for (i = 0; i < 3; ++i) {
        uint64_t p = ((uint64_t)f->position_high[i] << 32 | f->position_low[i]) + (uint64_t)(int64_t)eye[i];
        f->position_low[i] = (uint32_t)p;
        f->position_high[i] = (uint32_t)(p >> 32);
    }
    if (s->preferences[FE2_PREF_SCENE_FLASH]) {
        if ((int16_t)s->scene_flash >= 0) g->render_frame_model(g->opaque, f);
        s->scene_flash = 0;
    }
    fe2_args_clear(&args);
    fe2_args_object(&args, P_FRAME, f);
    flight_select_root(c, &args);
    g->render_scene(g->opaque, &args);
    target_colour(c);
    s->view_owner = 0;
    return 1;
}
