/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "intro_overlay.h"
#include "math/math3.h"

enum { INTRO_SLOT = 8 };

#include "intro_resident.h"
static void load(fe2_session_intro_overlay *b) { intro_resident_load(b->session, NULL, &b->state); }
static void store(fe2_session_intro_overlay *b) { intro_resident_store(b->session, NULL, &b->state); }

/* Session-side services: each call out carries the state back first. */
static void clear_overlay(void *opaque)
{
    fe2_session_intro_overlay *b = opaque;
    store(b);
    b->session->drawing_flag = 0;
    if (b->session->services.clear_screen) b->session->services.clear_screen(b->session->services.context);
    load(b);
}
static void restart_video(void *opaque)
{
    fe2_session_intro_overlay *b = opaque;
    fe2_session *s = b->session;
    store(b);
    fe2_session_update_pointer(s, NULL, NULL);
    if (s->services.present) s->services.present(s->services.context, s);
    load(b);
}
static void check_music(void *opaque)
{
    fe2_session_intro_overlay *b = opaque;
    store(b);
    if (b->session->services.check_music) b->session->services.check_music(b->session->services.context);
    load(b);
}
static void select_music(void *opaque, uint16_t track)
{
    fe2_session_intro_overlay *b = opaque;
    store(b);
    if (b->session->services.select_music)
        b->session->services.select_music(b->session->services.context, (int16_t)track);
    load(b);
}

/* Host providers: the state goes out first (the host reads the session's
 * light, shades and drawing flag) and comes back after. */
#define HOST(b) (&(b)->host)
static void register_bank(void *opaque, const uint8_t *models, const fe2_text_table *texts, uint16_t bank)
{
    fe2_session_intro_overlay *b = opaque;
    store(b);
    if (HOST(b)->register_bank) HOST(b)->register_bank(HOST(b)->opaque, models, texts, bank);
    load(b);
}
static void release_bank(void *opaque)
{
    fe2_session_intro_overlay *b = opaque;
    if (HOST(b)->release_bank) HOST(b)->release_bank(HOST(b)->opaque);
}
static void render_object(void *opaque, uint8_t *record, const fe2_call_args *args)
{
    fe2_session_intro_overlay *b = opaque;
    store(b);
    if (HOST(b)->render_object) HOST(b)->render_object(HOST(b)->opaque, record, args);
    load(b);
}
static void begin_overlay(void *opaque)
{
    fe2_session_intro_overlay *b = opaque;
    /* sets the drawing flag. */
    b->state.overlay_mode = 0xff;
    if (HOST(b)->begin_overlay) HOST(b)->begin_overlay(HOST(b)->opaque);
}
static void draw_sprite(void *opaque, uint16_t sprite, uint16_t x, uint16_t y)
{
    fe2_session_intro_overlay *b = opaque;
    if (HOST(b)->draw_sprite) HOST(b)->draw_sprite(HOST(b)->opaque, sprite, x, y);
}
static void draw_text(void *opaque, uint16_t text, uint8_t colour, uint16_t x, uint16_t y, const fe2_call_args *args)
{
    fe2_session_intro_overlay *b = opaque;
    store(b);
    if (HOST(b)->draw_text) HOST(b)->draw_text(HOST(b)->opaque, text, colour, x, y, args);
    load(b);
}

static void render(void *context, fe2_session *s, const fe2_objects_message *m)
{
    fe2_session_intro_overlay *b = context;
    (void)s; (void)m;
    load(b);
    b->return_mode = fe2_intro_render(&b->state, &b->services);
    store(b);
}
static void overlay_text(void *context, fe2_session *s)
{
    fe2_session_intro_overlay *b = context;
    (void)s;
    load(b);
    fe2_intro_timed_overlay(&b->state, &b->services);
    store(b);
}

int fe2_session_bind_intro_overlay(fe2_session *s, fe2_session_intro_overlay *b)
{
    fe2_intro_services *v;
    fe2_session_overlay *o;
    if (!s || !b || s->busy || s->locks[INTRO_SLOT]) return -1;
    b->session = s;
    fe2_intro_load(&b->state);
    b->state.sine = fe2_math_sine;
    v = &b->services; v->opaque = b;
    v->register_bank = register_bank; v->release_bank = release_bank; v->render_object = render_object;
    v->clear_overlay = clear_overlay; v->restart_video = restart_video; v->begin_overlay = begin_overlay;
    v->draw_sprite = draw_sprite; v->draw_text = draw_text; v->check_music = check_music;
    v->select_music = select_music;
    o = &s->overlays[INTRO_SLOT];
    o->context = b; o->message = render; o->draw = overlay_text;
    return 0;
}
