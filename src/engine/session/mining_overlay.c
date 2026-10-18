/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "mining_overlay.h"
#include <string.h>

enum { MINING_SLOT = 17, CATALOGUE_PROFILE = 0 };

/* ---- The shared globals ------------------------------------------------ */
#include "mining_resident.h"
static void load(fe2_session_mining_overlay *b) { mining_resident_load(b->session, &b->objects->state, &b->state); }
static void store(fe2_session_mining_overlay *b) { mining_resident_store(b->session, &b->objects->state, &b->state); }
/* Around native services and host calls: the mining state goes out first
 * when mining is running and comes back after. */
static void mining_out(fe2_session_mining_overlay *b) { if (b->depth) store(b); }
static void mining_back(fe2_session_mining_overlay *b) { load(b); }

/* Host providers: a missing void provider does nothing; one with a result
 * fails the frame. */
static void mining_missing(fe2_session_mining_overlay *b) { b->session->error = 1; }
#include "mining_forward.h"

#define OUT(b) mining_out(b)
#define BACK(b) mining_back(b)

/* ---- Native services --------------------------------------------------- */
/* (0x9e9, any nonzero slot): parameter 0 the kind as slot flags, 1
 * the model, 8 the template (the ship). */
static fe2_object *create_object(void *opaque, const fe2_call_args *args)
{
    fe2_session_mining_overlay *b = opaque;
    fe2_pool_services pool;
    fe2_new_record request;
    fe2_object *record = 0;
    request.flags = (uint8_t)args->arg[0].value; request.model = (uint16_t)args->arg[1].value;
    request.template_record = (const fe2_object *)args->arg[8].ref;
    if (!request.template_record) { b->session->error = 1; return 0; }
    OUT(b);
    fe2_session_pool_services(b->session, &pool);
    (void)fe2_pool_allocate(&b->session->pools, &pool, &request, &record);
    BACK(b);
    return record;
}
/* the record of `handle` in the list of parameter 12. */
static fe2_object *lookup_object(void *opaque, uint8_t handle, const fe2_call_args *args)
{
    fe2_session_mining_overlay *b = opaque;
    fe2_object_pool *pool = args && args->arg[12].ref ? (fe2_object_pool *)args->arg[12].ref : b->session->pools.current;
    if (handle >= FE2_POOL_SLOTS) { b->session->error = 1; return 0; }
    b->session->pools.current = pool;
    return fe2_pool_record(pool, handle);
}
/* the records that are neither free nor reusable, highest first. */
typedef struct visit_context { fe2_mining_visitor visit; fe2_mining_pass *pass; } visit_context;
static void visit_record(void *context, fe2_object *record, uint16_t status_pair)
{
    visit_context *v = context;
    (void)status_pair;
    v->visit(v->pass, record);
}
static void visit_objects(void *opaque, fe2_object_list *list, fe2_mining_visitor visit, fe2_mining_pass *pass)
{
    fe2_session_mining_overlay *b = opaque;
    visit_context v;
    v.visit = visit; v.pass = pass;
    /* The visitor  works on the mining state: no carrying around it. */
    fe2_pool_visit(&b->session->pools, (fe2_object_pool *)list, visit_record, &v);
}
static uint16_t random_word(void *opaque)
{
    fe2_session_mining_overlay *b = opaque;
    uint32_t edx, value;
    OUT(b); value = fe2_session_objects_random(b->objects, &edx); BACK(b);
    return (uint16_t)value;
}
static uint16_t random_below(void *opaque, uint16_t bound)
{
    fe2_session_mining_overlay *b = opaque;
    uint16_t value;
    OUT(b); value = fe2_session_objects_random_below(b->objects, bound); BACK(b);
    return value;
}
static void post_event(void *opaque, uint16_t event)
{
    fe2_session_mining_overlay *b = opaque;
    OUT(b);
    if (fe2_session_broadcast(b->session, event)) b->session->error = 1;
    BACK(b);
}

/* ---- Other overlays (overlay_calls.h) ---------------------------------- */
/* Through the session's table when the provider is registered, the host's
 * provider otherwise. */
/* catalogue (overlay 0x30) AL = 0, the profile of the system in parameter 1:
 * its field 0x5 in AX. */
static uint16_t route_system_profile(void *opaque, const fe2_call_args *args)
{
    fe2_session_mining_overlay *b = opaque;
    fe2_session *s = b->session;
    fe2_call_args work = *args;
    uint16_t field;
    if (!s->calls.catalogue) return mining_host_system_profile(opaque, args);
    OUT(b); field = s->calls.catalogue(FE2_OVERLAY_CONTEXT(s, 6), CATALOGUE_PROFILE, &work, NULL); BACK(b);
    return field;
}
/* offences (overlay 0x78, +0x20): commit offence AX. */
static void route_commit_offence(void *opaque, uint16_t offence)
{
    fe2_session_mining_overlay *b = opaque;
    fe2_session *s = b->session;
    if (!s->calls.commit_offence) { mining_host_commit_offence(opaque, offence); return; }
    OUT(b); s->calls.commit_offence(FE2_OVERLAY_CONTEXT(s, 15), offence); BACK(b);
}

/* ---- Entries ------------------------------------------------------------ */
#define ENTER(context) fe2_session_mining_overlay *b = context; (void)s; load(b); ++b->depth
#define LEAVE() do { --b->depth; store(b); } while (0)

static void initialize(void *context, fe2_session *s, uint16_t selection)
{
    ENTER(context); (void)selection; fe2_mining_reset(&b->state); LEAVE();
}
static uint32_t event(void *context, fe2_session *s, uint32_t value)
{
    ENTER(context); fe2_mining_event(&b->state, &b->services, (uint8_t)value); LEAVE();
    return value;
}
static void progress(void *context, fe2_session *s, fe2_object *record, uint64_t time)
{
    ENTER(context); (void)time; fe2_mining_progress(&b->state, &b->services, record); LEAVE();
}
static void action(void *context, fe2_session *s, const fe2_objects_message *m)
{
    ENTER(context);
    if (m) b->action_result = fe2_mining_action(&b->state, &b->services, m->selector);
    else b->session->error = 1;
    LEAVE();
}
static void remove_record(void *context, fe2_session *s, fe2_object *record, uint32_t request)
{
    ENTER(context); fe2_mining_remove(&b->state, &b->services, (uint16_t)request, record); LEAVE();
}

void fe2_session_mining_text(fe2_session_mining_overlay *b, uint16_t selector, fe2_text_out *out)
{
    load(b); ++b->depth;
    fe2_mining_text(&b->services, selector, out);
    --b->depth; store(b);
}

/* 0x8a (+0x20) as the session's `mining` call (overlay_calls.h, context
 * 17): action `request` (AX: 0 deploy, 1 load, 2 add a slot, 3 remove a
 * slot, others remove the free slots); the parameters are not read. The
 * result is AX: 0xffff when removing found a free slot, else 0. */
static uint16_t call_mining(void *context, uint16_t request, fe2_call_args *args)
{
    fe2_session_mining_overlay *b = context;
    (void)args;
    load(b); ++b->depth;
    b->action_result = fe2_mining_action(&b->state, &b->services, request);
    --b->depth; store(b);
    return b->action_result ? 0xffff : 0;
}

int fe2_session_bind_mining_overlay(fe2_session *s, fe2_session_objects *objects, fe2_session_mining_overlay *b)
{
    fe2_mining_services *v;
    fe2_session_overlay *o;
    if (!s || !objects || objects->session != s || !b || s->busy || s->locks[MINING_SLOT]) return -1;
    b->session = s; b->objects = objects;
    v = &b->services; v->opaque = b;
    mining_forward_all(v);
    v->create_object = create_object; v->lookup_object = lookup_object; v->visit_objects = visit_objects;
    v->random = random_word; v->random_below = random_below; v->post_event = post_event;
    v->system_profile = route_system_profile; v->commit_offence = route_commit_offence;
    o = &s->overlays[MINING_SLOT];
    o->context = b;
    o->initialize = initialize; o->event = event; o->periodic_record = progress; o->message = action;
    o->record_event = remove_record;
    s->calls.context[MINING_SLOT] = b; s->calls.mining = call_mining;
    return 0;
}
