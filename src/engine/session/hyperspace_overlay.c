/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "hyperspace_overlay.h"
#include <string.h>

enum { HYPERSPACE_SLOT = 16, P_SYSTEM = 6, P_SHIP = 8 };

/* ---- The shared globals ------------------------------------------------ */
#include "hyperspace_resident.h"
typedef fe2_session_hyperspace_overlay hyperspace_binding;
static void load(hyperspace_binding *b) { hyperspace_resident_load(b->session, &b->objects->state, &b->state); }
static void store(hyperspace_binding *b) { hyperspace_resident_store(b->session, &b->objects->state, &b->state); }
/* Around native services and host calls: the state goes out first when
 * drives is running and comes back after. */
static void hyperspace_out(hyperspace_binding *b) { if (b->depth) store(b); }
static void hyperspace_back(hyperspace_binding *b) { load(b); }

/* Host providers: a missing void provider does nothing; one with a result
 * fails the frame. */
static void hyperspace_missing(hyperspace_binding *b) { b->session->error = 1; }
#include "hyperspace_forward.h"

/* ---- Native services --------------------------------------------------- */
static void post_event(void *opaque, uint16_t event)
{
    hyperspace_binding *b = opaque;
    hyperspace_out(b);
    if (fe2_session_broadcast(b->session, event)) b->session->error = 1;
    hyperspace_back(b);
}
static void set_object_flags(void *opaque, fe2_object *object, uint8_t flags)
{
    hyperspace_binding *b = opaque;
    fe2_pool_set_slot_flags(&b->session->pools, object, flags);
}
static uint16_t random_below(void *opaque, uint16_t bound)
{
    hyperspace_binding *b = opaque;
    uint16_t value;
    hyperspace_out(b); value = fe2_session_objects_random_below(b->objects, bound); hyperspace_back(b);
    return value;
}
static uint32_t random_word(void *opaque, uint32_t *high)
{
    hyperspace_binding *b = opaque;
    uint32_t value;
    hyperspace_out(b); value = fe2_session_objects_random(b->objects, high); hyperspace_back(b);
    return value;
}

/* ---- Other overlays (overlay_calls.h) ---------------------------------- */
/* Through the session's table when the provider is registered, the host's
 * provider otherwise. */
static void route_message(void *opaque, uint16_t text, const fe2_call_args *args)
{
    hyperspace_binding *b = opaque;
    fe2_session *s = b->session;
    if (!s->calls.message) { hyperspace_host_show_message(opaque, text, args); return; }
    hyperspace_out(b); s->calls.message(FE2_OVERLAY_CONTEXT(s, 12), text, args); hyperspace_back(b);
}
/* chart (overlay 0x10) request 0: the target system's seeds and name, and
 * its reference in 0x7d4 (parameter 6). */
static fe2_jump_target route_target_system(void *opaque)
{
    hyperspace_binding *b = opaque;
    fe2_session *s = b->session;
    fe2_overlay_system system;
    fe2_jump_target target;
    if (!s->calls.map) {
        fe2_args_clear(&b->system_reply);
        return hyperspace_host_target_system(opaque);
    }
    memset(&system, 0, sizeof system);
    fe2_args_clear(&b->system_reply);
    hyperspace_out(b);
    (void)s->calls.map(FE2_OVERLAY_CONTEXT(s, 2), 0, &b->system_reply, &system);
    hyperspace_back(b);
    target.seed_a = system.seed_a; target.seed_b = system.seed_b; target.name = system.name;
    target.system = b->system_reply.arg[P_SYSTEM].value;
    return target;
}

/* ---- Entries ------------------------------------------------------------ */
#define ENTER(context) hyperspace_binding *b = context; (void)s; load(b); ++b->depth
#define LEAVE() do { --b->depth; store(b); } while (0)

/* 0x8a on `query`, `model` and `subject` (inside an entry). */
static void dispatch(hyperspace_binding *b)
{
    switch (b->query.mode) {
    case FE2_DRIVE_PREPARE: fe2_hyperspace_prepare(&b->state, &b->services); b->result = FE2_HYPERSPACE_OK; break;
    case FE2_DRIVE_CLASS_RANGES:
        b->result = fe2_hyperspace_class_ranges(&b->state, &b->services, b->model, b->ranges);
        break;
    case FE2_DRIVE_NO_ACTION: b->result = FE2_HYPERSPACE_OK; break;
    default:
        b->result = fe2_hyperspace_query(&b->state, &b->services, b->subject ? b->subject : b->state.ship,
                                         &b->query);
        break;
    }
}
static void jump(void *context, fe2_session *s, uint16_t input)
{
    ENTER(context); (void)input; b->result = fe2_hyperspace_jump(&b->state, &b->services, &b->query); LEAVE();
}
static void drive_query(void *context, fe2_session *s, const fe2_objects_message *m)
{
    ENTER(context); (void)m; dispatch(b); LEAVE();
}

/* The session's overlay calls into drives (overlay_calls.h, context 16).
 * `drive` is 0x8a: `request` the mode (AX), `value` the model (BX, mode
 * 3); the query's inputs and results are the parameters (0x7bc mode out,
 * 0x7c0 distance in / jump time out, 0x7c4 range, 0x7c8 name text and
 * drive mass, 0x7cc fuel, 0x7d0 range left) and the ship is parameter 8
 * (0x7dc; the player's ship without one). Mode 3 leaves the ranges of
 * classes 2..8 in parameters 1..7; modes 0 and 4 leave the parameters.
 * The result is the query's mode (0xffff refused) or the request. */
static void query_in(fe2_drive_query *q, const fe2_call_args *args)
{
    q->request = args->arg[1].value;
    q->range = args->arg[2].value;
    q->name_text = (uint16_t)args->arg[3].value;
    q->mass = (uint16_t)(args->arg[3].value >> 16);
    q->fuel_needed = args->arg[4].value;
    q->range_left = (uint16_t)args->arg[5].value;
}
static void query_out(const fe2_drive_query *q, fe2_call_args *args)
{
    fe2_args_word(args, 0, q->mode);
    fe2_args_dword(args, 1, q->request);
    fe2_args_dword(args, 2, q->range);
    fe2_args_dword(args, 3, (uint32_t)q->name_text | (uint32_t)q->mass << 16);
    fe2_args_dword(args, 4, q->fuel_needed);
    fe2_args_word(args, 5, q->range_left);
}
static uint16_t call_drive(void *context, uint16_t request, uint16_t value, fe2_call_args *args)
{
    hyperspace_binding *b = context;
    fe2_object *saved = b->subject;
    uint16_t result = request;
    unsigned i;
    load(b); ++b->depth;
    b->query.mode = request; b->model = value;
    if (args) {
        query_in(&b->query, args);
        if (args->arg[P_SHIP].ref) b->subject = (fe2_object *)args->arg[P_SHIP].ref;
    }
    dispatch(b);
    if (request == FE2_DRIVE_CLASS_RANGES) {
        if (args) for (i = 0; i < FE2_RANGED_CLASSES; ++i) fe2_args_dword(args, 1 + i, b->ranges[i]);
    } else if (request != FE2_DRIVE_PREPARE && request != FE2_DRIVE_NO_ACTION) {
        if (args) query_out(&b->query, args);
        result = b->query.mode;
    }
    b->subject = saved;
    --b->depth; store(b);
    return result;
}
/* (0x72): the jump. */
static void call_jump(void *context)
{
    jump(context, ((hyperspace_binding *)context)->session, 0);
}

int fe2_session_bind_hyperspace_overlay(fe2_session *s, fe2_session_objects *objects, hyperspace_binding *b)
{
    fe2_hyperspace_services *v;
    fe2_session_overlay *o;
    if (!s || !objects || objects->session != s || !b || s->busy || s->locks[HYPERSPACE_SLOT]) return -1;
    b->session = s; b->objects = objects;
    if (!b->state.drive_classes) b->state.drive_classes = fe2_hyperspace_drive_classes;
    if (!b->state.outcomes) b->state.outcomes = fe2_hyperspace_outcomes;
    v = &b->services; v->opaque = b;
    hyperspace_forward_all(v);
    v->post_event = post_event; v->set_object_flags = set_object_flags;
    v->random_below = random_below; v->random = random_word;
    v->show_message = route_message; v->target_system = route_target_system;
    o = &s->overlays[HYPERSPACE_SLOT];
    o->context = b; o->activate = jump; o->message = drive_query;
    s->calls.context[HYPERSPACE_SLOT] = b; s->calls.drive = call_drive; s->calls.jump = call_jump;
    return 0;
}
