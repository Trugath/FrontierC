/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The intro and its selection  and the exit scene. */
#include "session_internal.h"
#include "math/math3.h"
#include <string.h>

enum {
    INTRO_OVERLAY = 8, INTRO_MUSIC = 8, INTRO_TIME = 0x400, INTRO_END = 0x9c2334, KEY_SPACE = 0x39,
    EXIT_MODEL = 6, EXIT_START = 250000, EXIT_NEAREST = 0x88b8, EXIT_END = -700000, LIGHT = 0x5a82,
    DRAIN_BUDGET = 4096
};

static void present(fe2_session *s)
{
    fe2_session_update_pointer(s, NULL, NULL);
    if (s->services.present) s->services.present(s->services.context, s);
}
static void clear_screen(fe2_session *s)
{
    s->drawing_flag = 0;
    if (s->services.clear_screen) s->services.clear_screen(s->services.context);
}
static void assign_palette(fe2_session *s)
{
    if (s->services.assign_palette) s->services.assign_palette(s->services.context, s);
}
static void force_detail(fe2_session *s)
{
    s->saved_detail = s->detail;
    if (s->detail < 0) s->detail = 0;
}

/* the intro's state. */
static void intro_state(fe2_session *s)
{
    fe2_session_reset_overlays(s);
    if (s->services.restore_banks) s->services.restore_banks(s->services.context, s);
    s->pools.game_flags = 0;
    s->pools.root = s->pools.current = &s->objects;
    s->pools.reusable_count = 0;
    fe2_pool_clear(&s->pools);
    s->generation = 1;
    if (s->services.set_drawing_color) s->services.set_drawing_color(s->services.context, 0x303, 0x17);
    if (s->services.restore_drawing_color) s->services.restore_drawing_color(s->services.context);
    s->pools.game_flags |= 0x20;
    clear_screen(s); present(s); clear_screen(s);
    s->time_step = 994; s->simulation_step = 0; s->pending_simulation = 0;
    s->pools.clock[0] = INTRO_TIME;
    (void)fe2_pointer_regions_append(&s->regions, fe2_pointer_missile_regions, FE2_POINTER_MISSILE_REGIONS);
    if (!s->music_off && s->services.select_music) s->services.select_music(s->services.context, INTRO_MUSIC);
    force_detail(s);
    s->frontend = FE2_SESSION_INTRO; s->initialized = 0;
}

int fe2_session_intro(fe2_session *s)
{
    if (!s || s->busy) return -1;
    intro_state(s);
    return 0;
}

int fe2_session_begin_exit_scene(fe2_session *s, uint16_t mode)
{
    unsigned budget = DRAIN_BUDGET;
    fe2_object *r = &s->exit_record;
    while (fe2_session_poll(s)) if (!budget--) break;
    s->simulation_step = s->pending_simulation = s->render_step = s->pending_render = 0;
    s->panel_selection = 0;
    clear_screen(s); present(s); clear_screen(s); assign_palette(s);
    if (!mode) { intro_state(s); return 0; }
    if (s->services.check_music) s->services.check_music(s->services.context);
    force_detail(s);
    s->pools.game_flags |= 0x20;
    s->light[0] = LIGHT; s->light[1] = 0; s->light[2] = LIGHT;
    /* The original's record is on its stack: only these fields are set. */
    memset(r, 0, sizeof(*r));
    r->model = EXIT_MODEL;
    memcpy(r->name, s->commander, sizeof(r->name));
    r->name[sizeof(r->name) - 1] = 0;
    s->exit_distance = EXIT_START;
    s->exit_mode = mode;
    s->frontend = FE2_SESSION_EXIT_SCENE; s->initialized = 0;
    return s->error ? -1 : 0;
}

int fe2_session_exit_scene(fe2_session *s, uint16_t mode)
{
    int result;
    if (!s) return -1;
    if (s->busy) { s->restart = FE2_SESSION_PENDING_EXIT_SCENE; s->exit_mode = mode; return 0; }
    s->busy = 1; s->error = 0;
    result = fe2_session_begin_exit_scene(s, mode);
    s->busy = 0;
    return result;
}

static fe2_session_result exit_frame(fe2_session *s)
{
    fe2_object *r = &s->exit_record;
    uint32_t elapsed;
    r->camera_offset[2] = (uint32_t)(s->exit_distance < EXIT_NEAREST ? EXIT_NEAREST : s->exit_distance);
    if (s->services.synchronize_pointer) s->services.synchronize_pointer(s->services.context, s);
    if (!fe2_session_poll(s)) {
        fe2_math_basis_b(0, (uint16_t)((uint32_t)s->exit_distance >> 1), r->orientation);
        if (s->services.begin_frame) s->services.begin_frame(s->services.context, s);
        if (s->services.render_record) s->services.render_record(s->services.context, s, r);
        assign_palette(s);
        if (s->services.clear_view) s->services.clear_view(s->services.context);
        if (s->services.execute_frame) s->services.execute_frame(s->services.context, s);
        present(s);
        elapsed = s->pending_render; s->pending_render = 0; s->render_step = elapsed;
        s->exit_distance = (int32_t)((uint32_t)s->exit_distance - elapsed);
        if (s->exit_distance > EXIT_END) return FE2_SESSION_FRAME;
    }
    s->detail = s->saved_detail;
    intro_state(s);
    return FE2_SESSION_FRAME;
}

static uint16_t selection(uint8_t code, fe2_session *s, int *redraw)
{
    *redraw = 0;
    switch (code) {
    case '1': return 0x001;
    case '2': return 0x101;
    case '3': return 0x201;
    case '4': return 0x501;
    case '5': return 0x401;
    default:
        if (s->pools.clock[0] >= INTRO_END) return 0x001;
        s->pools.clock[0] = INTRO_END; *redraw = 1;
        return 0;
    }
}

static fe2_session_result intro_frame(fe2_session *s)
{
    fe2_session_overlay *o;
    uint64_t time;
    uint8_t code;
    int redraw = 1;
    if (s->frame_ticks < 2) return FE2_SESSION_WAITING;
    s->frame_ticks = 0;
    if (s->services.begin_frame) s->services.begin_frame(s->services.context, s);
    s->simulation_step = s->pending_simulation; s->pending_simulation = 0;
    time = fe2_session_time(s) + s->simulation_step; fe2_session_set_time(s, time);
    o = fe2_session_lock(s, INTRO_OVERLAY); if (!o) return FE2_SESSION_ERROR;
    if (o->message) o->message(o->context, s, NULL);
    --s->locks[INTRO_OVERLAY];
    if (s->restart) return FE2_SESSION_RESTARTED;
    code = (uint8_t)(fe2_session_poll(s) & s->input_mask);
    if (code) {
        uint16_t chosen = selection(code, s, &redraw);
        if (!redraw) { s->selection = chosen; s->frontend = FE2_SESSION_SELECTED; return FE2_SESSION_WAITING; }
    }
    if (s->services.synchronize_pointer) s->services.synchronize_pointer(s->services.context, s);
    assign_palette(s);
    if (s->services.restore_color) s->services.restore_color(s->services.context, s);
    if (s->services.execute_frame) s->services.execute_frame(s->services.context, s);
    o = fe2_session_lock(s, INTRO_OVERLAY); if (!o) return FE2_SESSION_ERROR;
    if (o->draw) o->draw(o->context, s);
    --s->locks[INTRO_OVERLAY];
    if (s->restart) return FE2_SESSION_RESTARTED;
    present(s);
    return FE2_SESSION_FRAME;
}

/* The selection waits for Space's release, then starts the session. */
static fe2_session_result selected(fe2_session *s)
{
    if (s->input.keys[KEY_SPACE] == 0) return FE2_SESSION_WAITING;
    s->detail = s->saved_detail;
    (void)fe2_session_poll(s);
    if (s->services.check_music) s->services.check_music(s->services.context);
    s->pools.game_flags &= (uint8_t)~0x20u;
    s->frontend = FE2_SESSION_PLAYING;
    return fe2_session_start(s, s->selection) ? FE2_SESSION_ERROR : FE2_SESSION_STARTED;
}

fe2_session_result fe2_session_frontend_step(fe2_session *s)
{
    fe2_session_result result;
    if (s->frontend == FE2_SESSION_SELECTED) return selected(s);
    s->busy = 1; s->error = 0;
    result = s->frontend == FE2_SESSION_INTRO ? intro_frame(s) : exit_frame(s);
    if (result == FE2_SESSION_RESTARTED || s->restart) {
        /* A restart or exit scene requested by the intro's callbacks. */
        result = fe2_session_unwind(s) ? FE2_SESSION_ERROR : FE2_SESSION_RESTARTED;
    }
    if (s->error) result = FE2_SESSION_ERROR;
    s->busy = 0;
    if (result == FE2_SESSION_WAITING && s->frontend == FE2_SESSION_SELECTED) return selected(s);
    return result;
}
