/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "offences_overlay.h"
#include <string.h>

enum { OFFENCES_SLOT = 15, SUBJECT_PARAMETER = 9, MARKET_SLOT_VECTOR = 9 };

/* ---- The shared globals ------------------------------------------------ */
#include "offences_resident.h"
static void load(fe2_session_offences_overlay *b) { offences_resident_load(b->session, &b->objects->state, &b->state); }
static void store(fe2_session_offences_overlay *b)
{
    offences_resident_store(b->session, &b->objects->state, &b->state);
    /* are slot 15's flag and update bytes. Bit 0 arms the
     * launch tick; a nonzero phase is the progress update. */
    b->session->metadata[OFFENCES_SLOT].flags = b->state.police_flags;
    b->session->metadata[OFFENCES_SLOT].update = b->state.launch_phase;
}
/* Around native services and host calls: the state goes out first when offences
 * is running and comes back after. */
static void offences_out(fe2_session_offences_overlay *b) { if (b->depth) store(b); }
static void offences_back(fe2_session_offences_overlay *b) { load(b); }

/* Host providers: a missing void provider does nothing; one with a result
 * fails the frame. */
static void offences_missing(fe2_session_offences_overlay *b) { b->session->error = 1; }
#include "offences_forward.h"

/* ---- Native services --------------------------------------------------- */
static uint32_t random_word(void *opaque)
{
    fe2_session_offences_overlay *b = opaque;
    uint32_t value;
    offences_out(b); value = fe2_session_objects_random(b->objects, &b->mix); offences_back(b);
    return value;
}
static uint16_t random_below(void *opaque, uint16_t bound)
{
    fe2_session_offences_overlay *b = opaque;
    uint16_t value;
    offences_out(b); value = fe2_session_objects_random_below(b->objects, bound); offences_back(b);
    return value;
}
/* the records that are neither free nor reusable, highest first. */
typedef struct visit_context { fe2_offence_visitor visit; void *context; } visit_context;
static void visit_record(void *context, fe2_object *record, uint16_t status_pair)
{
    visit_context *v = context;
    (void)status_pair;
    v->visit(v->context, record);
}
static void visit_objects(void *opaque, fe2_object_list *list, fe2_offence_visitor visit, void *context)
{
    fe2_session_offences_overlay *b = opaque;
    visit_context v;
    v.visit = visit; v.context = context;
    /* The police search works on the offence state: no carrying around it. */
    fe2_pool_visit(&b->session->pools, (fe2_object_pool *)list, visit_record, &v);
}
static void check_music(void *opaque)
{
    fe2_session_offences_overlay *b = opaque;
    fe2_session *s = b->session;
    offences_out(b);
    if (s->services.check_music) s->services.check_music(s->services.context);
    offences_back(b);
}
static void select_music(void *opaque, uint16_t track)
{
    fe2_session_offences_overlay *b = opaque;
    fe2_session *s = b->session;
    offences_out(b);
    if (s->services.select_music) s->services.select_music(s->services.context, (int16_t)track);
    offences_back(b);
}

/* ---- Other overlays (overlay_calls.h) ---------------------------------- */
/* Through the session's table when the provider is registered, the host's
 * provider otherwise. */
static void route_message(void *opaque, uint16_t text, const fe2_call_args *args)
{
    fe2_session_offences_overlay *b = opaque;
    fe2_session *s = b->session;
    if (!s->calls.message) { offences_host_show_message(opaque, text, args); return; }
    offences_out(b); s->calls.message(FE2_OVERLAY_CONTEXT(s, 12), text, args); offences_back(b);
}
/* market (overlay 0x38, +0x20) with BX = 9: the slot vector of the source in
 * parameter 9 for the attempt in parameter 7. AL is not set (0 here); the
 * slot vector does not read it. */
static void route_prepare_child(void *opaque, fe2_call_args *args)
{
    fe2_session_offences_overlay *b = opaque;
    fe2_session *s = b->session;
    fe2_call_args work;
    if (!args) return;
    work = *args;
    if (!s->calls.market) { offences_host_prepare_child(opaque, args); return; }
    offences_out(b); (void)s->calls.market(FE2_OVERLAY_CONTEXT(s, 7), MARKET_SLOT_VECTOR, 0, &work, 0); offences_back(b);
    /* The slot offset comes back in this block. Creation reads the caller's
     * arguments, so a copy that is thrown away leaves the ship at the kind
     * byte (inside the station). */
    *args = work;
}
/* people (overlay 0x58, +0x20): the creation with parameter 7 = 5. */
static fe2_object *route_create_object(void *opaque, const fe2_call_args *args)
{
    fe2_session_offences_overlay *b = opaque;
    fe2_session *s = b->session;
    fe2_call_args work = *args;
    fe2_object *object;
    if (!s->calls.population_create) return offences_host_create_object(opaque, args);
    offences_out(b); object = s->calls.population_create(FE2_OVERLAY_CONTEXT(s, 11), &work); offences_back(b);
    return object;
}

/* ---- Entries ------------------------------------------------------------ */
#define ENTER(context) fe2_session_offences_overlay *b = context; (void)s; load(b); ++b->depth
#define LEAVE() do { --b->depth; store(b); } while (0)
static fe2_object *subject(fe2_session_offences_overlay *b)
{
    const fe2_object_pool *pool = (const fe2_object_pool *)b->state.objects;
    if (b->subject) return b->subject;
    return pool && b->state.player_handle < FE2_POOL_SLOTS ? fe2_pool_record(pool, b->state.player_handle) : NULL;
}

static uint32_t event(void *context, fe2_session *s, uint32_t value)
{
    ENTER(context); fe2_offence_event(&b->state, &b->services, (uint8_t)value, subject(b)); LEAVE();
    return value;
}
static void progress(void *context, fe2_session *s)
{
    ENTER(context); fe2_offence_progress(&b->state); LEAVE();
}
/* with AX the offence (the panel messages' selector). */
static void commit(void *context, fe2_session *s, const fe2_objects_message *m)
{
    ENTER(context);
    if (m && m->selector < FE2_OFFENCES) fe2_offence_commit(&b->state, &b->services, m->selector, subject(b));
    else b->session->error = 1;
    LEAVE();
}
static void launch(void *context, fe2_session *s, uint32_t interval)
{
    ENTER(context); (void)interval; fe2_offence_launch(&b->state, &b->services); LEAVE();
}

/* The session's overlay calls into offences. The original has one entry for
 * them all, +0x20 (0x8a): commit offence AX (0..10) by the object in 0x7e0
 * (parameter 9); the other scratch is preserved. `offence_event` (flight,
 * people), `police` (station) and `commit_offence` (mining) differ only in their
 * callers' names; without parameter 9 (or without parameters) the
 * subject is the binding's, as for the session's message entry. */
static void commit_call(fe2_session_offences_overlay *b, uint16_t offence, const fe2_call_args *args)
{
    fe2_object *saved = b->subject;
    if (offence >= FE2_OFFENCES) { b->session->error = 1; return; }
    if (args && args->arg[SUBJECT_PARAMETER].ref) b->subject = (fe2_object *)args->arg[SUBJECT_PARAMETER].ref;
    load(b); ++b->depth;
    fe2_offence_commit(&b->state, &b->services, offence, subject(b));
    --b->depth; store(b);
    b->subject = saved;
}
static void call_offence_event(void *context, uint16_t event, const fe2_call_args *args)
{
    commit_call(context, event, args);
}
static void call_police(void *context, uint16_t request, const fe2_call_args *args)
{
    commit_call(context, request, args);
}
static void call_commit_offence(void *context, uint16_t offence)
{
    commit_call(context, offence, 0);
}

int fe2_session_bind_offences_overlay(fe2_session *s, fe2_session_objects *objects, fe2_session_offences_overlay *b)
{
    fe2_offence_services *v;
    fe2_session_overlay *o;
    if (!s || !objects || objects->session != s || !b || s->busy || s->locks[OFFENCES_SLOT]) return -1;
    b->session = s; b->objects = objects;
    v = &b->services; v->opaque = b;
    offences_forward_all(v);
    v->random = random_word; v->random_below = random_below; v->visit_objects = visit_objects;
    v->check_music = check_music; v->select_music = select_music;
    v->show_message = route_message; v->prepare_child = route_prepare_child;
    v->create_object = route_create_object;
    o = &s->overlays[OFFENCES_SLOT];
    o->context = b;
    o->event = event; o->update = progress; o->message = commit; o->coarse_tick = launch;
    s->calls.context[OFFENCES_SLOT] = b;
    s->calls.offence_event = call_offence_event; s->calls.police = call_police;
    s->calls.commit_offence = call_commit_offence;
    return 0;
}
