/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "navigation_overlay.h"
#include "math/math3.h"
#include <string.h>

enum {
    NAVIGATION_SLOT = 5, SYSTEMS_MODULE = 0, MAP_MODULE = 2, MESSAGES_MODULE = 12,
    MAP_CURRENT_SYSTEM = 0,
    /* What chart's request 0 leaves in the shared parameters for systems:
     * 0x7c8 the root kind, 0x7d4 the key, 0x7d8 the danger. */
    P_KIND = 3, P_REFERENCE = 6, P_STATUS = 7
};

/* ---- The shared globals ------------------------------------------------ */
typedef fe2_session_navigation_overlay navigation_binding;
#include "navigation_resident.h"
static void load(navigation_binding *b) { navigation_resident_load(b->session, &b->objects->state, &b->state); }
static void store(navigation_binding *b) { navigation_resident_store(b->session, &b->objects->state, &b->state); }
/* Around native services and host calls: the state goes out first when
 * navigation is running (inside an entry) and comes back after. */
static void navigation_out(navigation_binding *b) { if (b->depth > 0) store(b); }
static void navigation_back(navigation_binding *b) { load(b); }

/* Host providers: a missing void provider does nothing; one with a result
 * fails the frame. */
static void navigation_missing(navigation_binding *b) { b->session->error = 1; }
#include "navigation_forward.h"

#define OUT(b) navigation_out(b)
#define BACK(b) navigation_back(b)

/* ---- Native services --------------------------------------------------- */
static void set_panel_mode(void *opaque, uint16_t mode)
{
    navigation_binding *b = opaque;
    OUT(b); fe2_session_objects_select_overlay(b->objects, mode); BACK(b);
}
static int poll_key(void *opaque, uint8_t *key)
{
    navigation_binding *b = opaque;
    uint16_t code;
    OUT(b); code = fe2_session_poll(b->session); BACK(b);
    *key = (uint8_t)code;
    return code != 0;
}
static void forward_key(void *opaque, uint8_t key)
{
    navigation_binding *b = opaque;
    fe2_input_restore(&b->session->pointer, key);
}
static void show_panel(void *opaque)
{
    navigation_binding *b = opaque;
    fe2_pointer_regions_reset(&b->session->regions);
}
/* records of five words; the first unconditionally, then until a
 * left edge of 0xffff. */
static void set_widgets(void *opaque, const uint16_t *words)
{
    navigation_binding *b = opaque;
    fe2_pointer_region stream[FE2_POINTER_REGIONS + 1];
    size_t n = 0;
    do {
        const uint16_t *w = words + 5 * n;
        stream[n].left = w[0]; stream[n].top = w[1]; stream[n].right = w[2]; stream[n].bottom = w[3];
        stream[n].id = w[4];
        ++n;
    } while (n <= FE2_POINTER_REGIONS && words[5 * n] != FE2_POINTER_REGION_END);
    if (fe2_pointer_regions_append(&b->session->regions, stream, n) < 0) b->session->error = 1;
}
/* release without notifying the owners. */
static void remove_object(void *opaque, fe2_object *record)
{
    navigation_binding *b = opaque;
    fe2_pool_release_quietly(&b->session->pools, record);
}
/* the record of `handle` in the list of parameter 12. */
static fe2_object *lookup_object(void *opaque, uint8_t handle, const fe2_call_args *args)
{
    navigation_binding *b = opaque;
    fe2_object_pool *pool = args && args->arg[12].ref ? (fe2_object_pool *)args->arg[12].ref : b->session->pools.current;
    if (handle >= FE2_POOL_SLOTS) { b->session->error = 1; return 0; }
    b->session->pools.current = pool;
    return fe2_pool_record(pool, handle);
}
static uint8_t handle_flags(void *opaque, const fe2_object_list *list, uint8_t handle)
{
    (void)opaque;
    return handle < FE2_POOL_SLOTS ? ((const fe2_object_pool *)list)->status[handle] : 0;
}
/* the records that are neither free nor reusable, highest first. */
typedef struct visit_context { fe2_navigation_visitor visit; void *context; } visit_context;
static void visit_record(void *context, fe2_object *record, uint16_t status_pair)
{
    visit_context *v = context;
    (void)status_pair;
    v->visit(v->context, record);
}
static void visit_objects(void *opaque, fe2_object_list *list, fe2_navigation_visitor visit, void *context)
{
    navigation_binding *b = opaque;
    visit_context v;
    v.visit = visit; v.context = context;
    /* The visitors work on the navigation state: no carrying around them. */
    fe2_pool_visit(&b->session->pools, (fe2_object_pool *)list, visit_record, &v);
}
/* projected (flag 8), not reusable records whose screen
 * position is inside the view, with AX = x + 3 and BX = y - 4. */
static void visit_projected(void *opaque, fe2_object_list *list, fe2_navigation_screen_visitor visit, void *context)
{
    navigation_binding *b = opaque;
    fe2_object_pool *pool = (fe2_object_pool *)list;
    int slot;
    b->session->pools.current = b->session->pools.saved = pool;
    for (slot = FE2_POOL_LAST_SLOT; slot >= 0; --slot) {
        fe2_object *o;
        uint16_t x, y;
        if (!(pool->status[slot] & FE2_SLOT_PROJECTED) || (pool->status[slot] & FE2_SLOT_REUSABLE)) continue;
        o = pool->records[slot];
        y = (uint16_t)(o->screen_y - 4u);
        if (y >= 0x9f) continue;
        x = (uint16_t)(o->screen_x + 3u);
        if (x >= 0x13c) continue;
        visit(context, o, x, y);
    }
}
/* the angles are the shared 0x1f35/0x1f37 words. */
static void basis_b(void *opaque, int16_t basis[9])
{
    navigation_binding *b = opaque;
    fe2_math_basis_b(b->state.basis_angle[0], b->state.basis_angle[1], basis);
}

/* ---- Other overlays ------------------------------------------------------ */
/* Through the session's overlay calls when registered, otherwise the
 * host's. */
static void show_message(void *opaque, uint16_t text, const fe2_call_args *args)
{
    navigation_binding *b = opaque;
    fe2_session *s = b->session;
    if (!s->calls.message) { navigation_host_show_message(opaque, text, args); return; }
    OUT(b); s->calls.message(FE2_OVERLAY_CONTEXT(s, MESSAGES_MODULE), text, args); BACK(b);
}
/* chart AX=0. The original's parameter block is shared, so the systems call
 * after it sees what the query left: `system_reply` keeps it. */
static fe2_system_seeds current_system(void *opaque)
{
    navigation_binding *b = opaque;
    fe2_session *s = b->session;
    fe2_overlay_system system;
    fe2_system_seeds seeds;
    if (!s->calls.map) return navigation_host_current_system(opaque);
    memset(&system, 0, sizeof(system));
    fe2_args_clear(&b->system_reply);
    OUT(b); (void)s->calls.map(FE2_OVERLAY_CONTEXT(s, MAP_MODULE), MAP_CURRENT_SYSTEM, &b->system_reply, &system); BACK(b);
    seeds.seed_a = system.seed_a; seeds.seed_b = system.seed_b; seeds.name = system.name;
    return seeds;
}
/* systems 0x8a, with chart's reply in the parameters navigation leaves unset. */
static void system_action(void *opaque, const fe2_system_seeds *seeds, const fe2_call_args *args)
{
    static const unsigned shared[] = {P_KIND, P_REFERENCE, P_STATUS};
    navigation_binding *b = opaque;
    fe2_session *s = b->session;
    fe2_overlay_system system;
    fe2_call_args work;
    unsigned i;
    if (!s->calls.system) { navigation_host_system_action(opaque, seeds, args); return; }
    memset(&system, 0, sizeof(system));
    if (args) work = *args; else fe2_args_clear(&work);
    for (i = 0; i < sizeof(shared) / sizeof(shared[0]); ++i)
        if (work.arg[shared[i]].kind == FE2_ARG_UNSET) work.arg[shared[i]] = b->system_reply.arg[shared[i]];
    if (seeds) { system.seed_a = seeds->seed_a; system.seed_b = seeds->seed_b; system.name = seeds->name; }
    system.reference = work.arg[P_REFERENCE].value;
    OUT(b); s->calls.system(FE2_OVERLAY_CONTEXT(s, SYSTEMS_MODULE), &system, &work); BACK(b);
}

/* ---- Entries ------------------------------------------------------------ */
#define ENTER(context) navigation_binding *b = context; (void)s; load(b); ++b->depth
#define LEAVE() do { --b->depth; store(b); } while (0)

static void initialize(void *context, fe2_session *s, uint16_t selection)
{
    ENTER(context);
    fe2_navigation_initialize(&b->state, (uint8_t)selection, (uint8_t)(selection >> 8));
    LEAVE();
}
static void activate(void *context, fe2_session *s, uint16_t input)
{
    ENTER(context); (void)input; fe2_navigation_select(&b->state, &b->services); LEAVE();
    s->metadata[NAVIGATION_SLOT].update = s->metadata[NAVIGATION_SLOT].draw = 1;
}
static void render(void *context, fe2_session *s)
{
    ENTER(context); fe2_navigation_render(&b->state, &b->services); LEAVE();
}
static void targets(void *context, fe2_session *s)
{
    ENTER(context); fe2_navigation_render_targets(&b->state, &b->services); LEAVE();
}
static void motion(void *context, fe2_session *s, fe2_object *record, uint64_t time)
{
    ENTER(context); fe2_navigation_update_motion(&b->services, record, time, &b->samples); LEAVE();
}
static void generated(void *context, fe2_session *s, const fe2_objects_message *m)
{
    ENTER(context); (void)m; fe2_navigation_select_generated(&b->state, &b->services); LEAVE();
    s->metadata[NAVIGATION_SLOT].update = s->metadata[NAVIGATION_SLOT].draw = 1;
}
static void restore(void *context, fe2_session *s)
{
    ENTER(context); fe2_navigation_restore(&b->state, &b->services); LEAVE();
    s->metadata[NAVIGATION_SLOT].update = s->metadata[NAVIGATION_SLOT].draw = 0;
}

int fe2_session_bind_navigation_overlay(fe2_session *s, fe2_session_objects *objects, navigation_binding *b)
{
    fe2_navigation_services *v;
    fe2_session_overlay *o;
    if (!s || !objects || objects->session != s || !b || s->busy || s->locks[NAVIGATION_SLOT]) return -1;
    b->session = s; b->objects = objects;
    v = &b->services; v->opaque = b;
    navigation_forward_all(v);
    v->set_panel_mode = set_panel_mode; v->poll_key = poll_key; v->forward_key = forward_key;
    v->show_panel = show_panel; v->set_widgets = set_widgets; v->remove_object = remove_object;
    v->lookup_object = lookup_object; v->handle_flags = handle_flags; v->visit_objects = visit_objects;
    v->visit_projected = visit_projected; v->basis_b = basis_b;
    v->show_message = show_message; v->current_system = current_system; v->system_action = system_action;
    fe2_args_clear(&b->system_reply);
    o = &s->overlays[NAVIGATION_SLOT];
    o->context = b;
    o->initialize = initialize; o->activate = activate; o->update = render; o->draw = targets;
    o->periodic_record = motion; o->message = generated; o->deselect = restore;
    return 0;
}
