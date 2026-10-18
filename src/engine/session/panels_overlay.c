/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "panels_overlay.h"
#include <string.h>

enum { PANELS_SLOT = 4, P_ARGUMENT = 2 };
typedef fe2_session_panels_overlay panels_binding;

/* ---- The shared globals ------------------------------------------------ */
#include "panels_resident.h"
static void load(panels_binding *b) { panels_resident_load(b->session, &b->objects->state, &b->state); }
static void store(panels_binding *b) { panels_resident_store(b->session, &b->objects->state, &b->state); }
/* Around native services and host calls: the panel state goes out first
 * when panels is running and comes back after. */
static void panels_out(panels_binding *b) { if (b->depth) store(b); }
static void panels_back(panels_binding *b) { load(b); }

/* Host providers: a missing void provider does nothing; one with a result
 * fails the frame. */
static void panels_missing(panels_binding *b) { b->session->error = 1; }
#include "panels_forward.h"

/* ---- Native services --------------------------------------------------- */
/* sets the drawing flag  before the host's part. */
static void selection_changed(void *opaque)
{
    panels_binding *b = opaque;
    b->state.enabled = 0xff;
    panels_host_selection_changed(opaque);
}
/* AL back into the input latch (0xf5: no scanner). */
static void object_event(void *opaque, uint8_t event)
{
    panels_binding *b = opaque;
    fe2_input_restore(&b->session->pointer, event);
}
/* the records that are neither free nor reusable, highest first. */
typedef struct visit_context { fe2_panel_visitor visit; void *context; } visit_context;
static void visit_record(void *context, fe2_object *record, uint16_t status_pair)
{
    visit_context *v = context;
    (void)status_pair;
    v->visit(v->context, record);
}
static void visit_objects(void *opaque, fe2_object_list *list, fe2_panel_visitor visit, void *context)
{
    panels_binding *b = opaque;
    visit_context v;
    v.visit = visit; v.context = context;
    /* The visitor (the scanner marker) works on the panel state. */
    fe2_pool_visit(&b->session->pools, (fe2_object_pool *)list, visit_record, &v);
}

/* ---- Entries ------------------------------------------------------------ */
#define ENTER(context) panels_binding *b = context; (void)s; load(b); ++b->depth
#define LEAVE() do { --b->depth; store(b); } while (0)

static void initialize(void *context, fe2_session *s, uint16_t selection)
{
    ENTER(context); fe2_panels_initialize(&b->state, (uint8_t)selection); LEAVE();
}
static uint32_t event(void *context, fe2_session *s, uint32_t value)
{
    ENTER(context); fe2_panels_event(&b->state, &b->services, (uint16_t)value); LEAVE();
    return value;
}
static void render_objects(void *context, fe2_session *s)
{
    ENTER(context); fe2_panels_render_objects(&b->state, &b->services, b->view_flags, b->viewer); LEAVE();
}
static void select_panel(void *context, fe2_session *s, const fe2_objects_message *m)
{
    ENTER(context);
    fe2_panels_select(&b->state, &b->services, m ? (int16_t)m->selector : b->request, b->overlay, b->argument);
    LEAVE();
}
static void refresh(void *context, fe2_session *s, uint32_t interval)
{
    ENTER(context); (void)interval; fe2_panels_refresh(&b->state, &b->services); LEAVE();
}

/* The overlay call (+0x20, 0x8a): `request` is AX, `value` BX (the overlay
 * whose panel is selected) and parameter 2 the argument. */
static void provide_panel(void *context, uint16_t request, uint16_t value, fe2_call_args *args)
{
    panels_binding *b = context;
    load(b); ++b->depth;
    fe2_panels_select(&b->state, &b->services, (int16_t)request, value,
                      args ? (uint16_t)args->arg[P_ARGUMENT].value : 0);
    LEAVE();
}

int fe2_session_bind_panels_overlay(fe2_session *s, fe2_session_objects *objects, panels_binding *b)
{
    fe2_panel_services *v;
    fe2_session_overlay *o;
    if (!s || !objects || objects->session != s || !b || s->busy || s->locks[PANELS_SLOT]) return -1;
    b->session = s; b->objects = objects;
    v = &b->services; v->opaque = b;
    panels_forward_all(v);
    v->selection_changed = selection_changed; v->object_event = object_event; v->visit_objects = visit_objects;
    o = &s->overlays[PANELS_SLOT];
    o->context = b;
    o->initialize = initialize; o->event = event; o->update = render_objects; o->message = select_panel;
    o->coarse_tick = refresh;
    s->calls.panel = provide_panel; s->calls.context[PANELS_SLOT] = b;
    return 0;
}
