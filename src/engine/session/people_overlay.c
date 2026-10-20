/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "people_overlay.h"
#include "math/math3.h"
#include <string.h>

enum { PEOPLE_SLOT = 11, P_MODE = 7, P_OBJECT = 8, P_CREATED = 9, P_LIST = 12 };

/* ---- The shared globals ------------------------------------------------ */
#include "people_resident.h"
static void load(fe2_session_people_overlay *b) { people_resident_load(b->session, &b->objects->state, &b->state); }
static void store(fe2_session_people_overlay *b)
{
    people_resident_store(b->session, &b->objects->state, &b->state);
    /* Shared with slot 15: 0x413 flags, 0x414 update. */
    b->session->metadata[15].flags = b->state.police_flags;
    b->session->metadata[15].update = b->state.launch_phase;
}
/* Around native services and host calls: the people state goes out first
 * when people is running (inside an entry; otherwise the session's copy is
 * current) and comes back after. */
static void people_out(fe2_session_people_overlay *b) { if (b->depth) store(b); }
static void people_back(fe2_session_people_overlay *b) { load(b); }

/* Host providers: a missing void provider does nothing; one with a result
 * fails the frame. */
static void people_missing(fe2_session_people_overlay *b) { b->session->error = 1; }
#include "people_forward.h"

#define OUT(b) people_out(b)
#define BACK(b) people_back(b)

/* ---- Native services --------------------------------------------------- */
static int damage(void *opaque, fe2_object *object, uint16_t amount, const fe2_call_args *args)
{
    fe2_session_people_overlay *b = opaque;
    int destroyed;
    OUT(b);
    destroyed = fe2_session_objects_damage(b->objects, object, amount, (uint16_t)args->arg[P_MODE].value);
    BACK(b);
    return destroyed;
}
static void activate_effect(void *opaque, fe2_object *object)
{
    fe2_session_people_overlay *b = opaque;
    fe2_pool_services pool;
    OUT(b);
    fe2_session_pool_services(b->session, &pool);
    fe2_pool_equipment_effect(&b->session->pools, &pool, object);
    BACK(b);
}
static void consume_effect(void *opaque, fe2_object *object)
{
    fe2_session_people_overlay *b = opaque;
    fe2_pool_services pool;
    OUT(b);
    fe2_session_pool_services(b->session, &pool);
    fe2_pool_nearby_effect(&b->session->pools, &pool, object);
    BACK(b);
}
static void activate_slot(void *opaque, fe2_object *object, uint8_t slot)
{
    fe2_session_people_overlay *b = opaque;
    OUT(b); fe2_session_objects_activate(b->objects, object, (int8_t)slot); BACK(b);
}
/* parameter 7 the target. */
static int deploy_slot(void *opaque, fe2_object *object, uint16_t slot, const fe2_call_args *args)
{
    fe2_session_people_overlay *b = opaque;
    fe2_object *launched;
    OUT(b);
    launched = fe2_session_objects_deploy(b->objects, object, slot, (uint8_t)args->arg[P_MODE].value);
    BACK(b);
    return launched != 0;
}
/* parameters 0..5 the position, 7 the kind, 8 the source. */
static void spawn_effect(void *opaque, const fe2_call_args *args)
{
    fe2_session_people_overlay *b = opaque;
    uint32_t position[6];
    unsigned i;
    for (i = 0; i < 6; ++i) position[i] = args->arg[i].value;
    OUT(b);
    fe2_session_objects_spawn_effect(b->objects, (fe2_object *)args->arg[P_OBJECT].ref, position,
                                     (uint16_t)args->arg[P_MODE].value);
    BACK(b);
}
static uint32_t random_word(void *opaque, uint32_t *second)
{
    fe2_session_people_overlay *b = opaque;
    uint32_t value;
    OUT(b); value = fe2_session_objects_random(b->objects, second); BACK(b);
    return value;
}
static uint16_t random_below(void *opaque, uint16_t bound)
{
    fe2_session_people_overlay *b = opaque;
    uint16_t value;
    OUT(b); value = fe2_session_objects_random_below(b->objects, bound); BACK(b);
    return value;
}
static void basis(void *opaque, fe2_object *object)
{
    fe2_session_people_overlay *b = opaque;
    fe2_math_basis_b(b->state.basis_angle[0], b->state.basis_angle[1], object->orientation);
}
static void set_slot_flags(void *opaque, fe2_object *object, uint8_t flags)
{
    fe2_session_people_overlay *b = opaque;
    fe2_pool_set_slot_flags(&b->session->pools, object, flags);
}
/* the session's broadcast carries AL only; DX is not delivered. */
static void post_event(void *opaque, uint16_t event, uint16_t value)
{
    fe2_session_people_overlay *b = opaque;
    (void)value;
    OUT(b);
    if (fe2_session_broadcast(b->session, event)) b->session->error = 1;
    BACK(b);
}
static void check_music(void *opaque)
{
    fe2_session_people_overlay *b = opaque;
    if (b->session->services.check_music) b->session->services.check_music(b->session->services.context);
}
static void select_music(void *opaque, uint16_t track)
{
    fe2_session_people_overlay *b = opaque;
    OUT(b);
    if (b->session->services.select_music)
        b->session->services.select_music(b->session->services.context, (int16_t)track);
    BACK(b);
}

/* Pool records. Both 0xa8 and 0xac are 0x9e9 (any nonzero slot):
 * parameter 0 the slot flags, 1 the model, 8 the template. */
static int allocate(void *opaque, const fe2_call_args *args, fe2_object **created)
{
    fe2_session_people_overlay *b = opaque;
    fe2_pool_services pool;
    fe2_new_record request;
    fe2_object *record = 0;
    request.flags = (uint8_t)args->arg[0].value; request.model = (uint16_t)args->arg[1].value;
    request.template_record = (const fe2_object *)args->arg[P_OBJECT].ref;
    if (!request.template_record) { b->session->error = 1; if (created) *created = 0; return 0; }
    OUT(b);
    fe2_session_pool_services(b->session, &pool);
    (void)fe2_pool_allocate(&b->session->pools, &pool, &request, &record);
    BACK(b);
    if (created) *created = record;
    return record != 0;
}
/* release without notifying the owners. */
static void release(void *opaque, fe2_object *object)
{
    fe2_session_people_overlay *b = opaque;
    fe2_pool_release_quietly(&b->session->pools, object);
}
/* the record of `handle` in the list of parameter 12. */
static fe2_object *lookup(void *opaque, uint8_t handle, const fe2_call_args *args)
{
    fe2_session_people_overlay *b = opaque;
    fe2_object_pool *pool = args && args->arg[P_LIST].ref ? (fe2_object_pool *)args->arg[P_LIST].ref
                                                          : b->session->pools.current;
    if (handle >= FE2_POOL_SLOTS || !pool) { b->session->error = 1; return 0; }
    b->session->pools.current = pool;
    return fe2_pool_record(pool, handle);
}
/* the records that are neither free nor reusable, highest first. */
typedef struct visit_context { fe2_people_visitor visit; void *pass; } visit_context;
static void visit_record(void *context, fe2_object *record, uint16_t status_pair)
{
    visit_context *v = context;
    (void)status_pair;
    v->visit(v->pass, record);
}
static void visit(void *opaque, fe2_object_list *list, fe2_people_visitor visitor, void *pass)
{
    fe2_session_people_overlay *b = opaque;
    visit_context v;
    v.visit = visitor; v.pass = pass;
    /* The visitors work on the people state: no carrying around them. */
    fe2_pool_visit(&b->session->pools, (fe2_object_pool *)list, visit_record, &v);
}

/* ---- Other overlays (overlay_calls.h) ---------------------------------- */
/* A registered entry gets a copy of people's parameters and leaves its
 * results in them: the reply is the result and parameters 0..5. Without
 * one, the host's provider answers. */
static fe2_people_reply reply_of(uint16_t result, const fe2_call_args *work)
{
    fe2_people_reply reply;
    unsigned i;
    reply.result = result;
    for (i = 0; i < 6; ++i) reply.value[i] = work->arg[i].value;
    return reply;
}
static fe2_people_reply route_galactic_map(void *opaque, uint16_t request, const fe2_call_args *args)
{
    fe2_session_people_overlay *b = opaque;
    fe2_session *s = b->session;
    fe2_call_args work = *args;
    uint16_t result;
    if (!s->calls.map) return people_host_galactic_map_relationship(opaque, request, args);
    OUT(b); result = s->calls.map(FE2_OVERLAY_CONTEXT(s, 2), request, &work, 0); BACK(b);
    return reply_of(result, &work);
}
static fe2_people_reply route_catalogue(void *opaque, uint8_t request, const fe2_call_args *args)
{
    fe2_session_people_overlay *b = opaque;
    fe2_session *s = b->session;
    fe2_call_args work = *args;
    uint16_t result;
    if (!s->calls.catalogue) return people_host_catalogue_query(opaque, request, args);
    OUT(b); result = s->calls.catalogue(FE2_OVERLAY_CONTEXT(s, 6), request, &work, NULL); BACK(b);
    return reply_of(result, &work);
}
static fe2_people_reply route_market(void *opaque, uint16_t service, uint8_t handle, const fe2_call_args *args)
{
    fe2_session_people_overlay *b = opaque;
    fe2_session *s = b->session;
    fe2_call_args work = *args;
    uint16_t result;
    if (!s->calls.market) return people_host_market_service(opaque, service, handle, args);
    OUT(b); result = s->calls.market(FE2_OVERLAY_CONTEXT(s, 7), service, handle, &work, 0); BACK(b);
    return reply_of(result, &work);
}
static void route_names(void *opaque, uint16_t type, uint32_t seed, char out[FE2_PEOPLE_NAME_BYTES])
{
    fe2_session_people_overlay *b = opaque;
    fe2_session *s = b->session;
    if (!s->calls.name) { people_host_names_name(opaque, type, seed, out); return; }
    OUT(b); s->calls.name(FE2_OVERLAY_CONTEXT(s, 9), type, seed, out, FE2_PEOPLE_NAME_BYTES); BACK(b);
}
static uint8_t route_commander(void *opaque)
{
    fe2_session_people_overlay *b = opaque;
    fe2_session *s = b->session;
    uint8_t alarm;
    if (!s->calls.menu_check) return people_host_commander_check(opaque);
    OUT(b); alarm = s->calls.menu_check(FE2_OVERLAY_CONTEXT(s, 10)); BACK(b);
    return alarm;
}
static void route_station(void *opaque, uint16_t text, const fe2_call_args *args)
{
    fe2_session_people_overlay *b = opaque;
    fe2_session *s = b->session;
    if (!s->calls.message) { people_host_station_message(opaque, text, args); return; }
    OUT(b); s->calls.message(FE2_OVERLAY_CONTEXT(s, 12), text, args); BACK(b);
}
static void route_offences(void *opaque, uint16_t event, const fe2_call_args *args)
{
    fe2_session_people_overlay *b = opaque;
    fe2_session *s = b->session;
    if (!s->calls.offence_event) { people_host_offences_event(opaque, event, args); return; }
    OUT(b); s->calls.offence_event(FE2_OVERLAY_CONTEXT(s, 15), event, args); BACK(b);
}
/* drives request 2, the arrival (parameter 1 the distance). */
static fe2_people_reply route_hyperspace(void *opaque, const fe2_call_args *args)
{
    fe2_session_people_overlay *b = opaque;
    fe2_session *s = b->session;
    fe2_call_args work = *args;
    uint16_t result;
    if (!s->calls.drive) return people_host_hyperspace_arrival(opaque, args);
    OUT(b); result = s->calls.drive(FE2_OVERLAY_CONTEXT(s, 16), 2, 0, &work); BACK(b);
    return reply_of(result, &work);
}

/* ---- Entries ------------------------------------------------------------ */
#define ENTER(context) fe2_session_people_overlay *b = context; (void)s; load(b); ++b->depth
#define LEAVE() do { --b->depth; store(b); } while (0)

static void initialize(void *context, fe2_session *s, uint16_t selection)
{
    ENTER(context); fe2_people_initialize(&b->state, &b->services, &b->args, (uint8_t)selection); LEAVE();
}
static uint32_t event(void *context, fe2_session *s, uint32_t value)
{
    ENTER(context); fe2_people_event(&b->state, &b->services, &b->args, (uint16_t)value); LEAVE();
    return value;
}
static void motion(void *context, fe2_session *s, fe2_object *record, uint64_t time)
{
    ENTER(context); fe2_people_motion(&b->state, &b->services, &b->args, record, time); LEAVE();
}
static void update(void *context, fe2_session *s, fe2_object *record, uint32_t interval)
{
    ENTER(context); fe2_people_update(&b->state, &b->services, &b->args, record, interval); LEAVE();
}
static void relationship_event(void *context, fe2_session *s, fe2_object *record, uint32_t request)
{
    ENTER(context);
    fe2_args_object(&b->args, P_OBJECT, record);
    fe2_people_relationship_event(&b->state, &b->services, &b->args, record, record->handle, (uint16_t)request);
    LEAVE();
}

uint16_t fe2_session_people_create(fe2_session_people_overlay *b, uint16_t parent)
{
    uint16_t result;
    load(b); ++b->depth;
    result = fe2_people_create(&b->state, &b->services, &b->args, parent);
    --b->depth; store(b);
    return result;
}

/* The session's population_create: 0x8a with BX (`parent`: the model for
 * mode 6, the behaviour parent for modes 1..4; modes 5, 7 and 8 do not
 * read it) and the caller's parameters. The parameters go back to the
 * caller, and the new object is parameter 9. */
static fe2_object *population_create(void *context, uint16_t parent, fe2_call_args *args)
{
    fe2_session_people_overlay *b = context;
    uint16_t made;
    if (!args) { b->session->error = 1; return 0; }
    b->args = *args;
    made = fe2_session_people_create(b, parent);
    *args = b->args;
    if (!made || args->arg[P_CREATED].kind != FE2_ARG_OBJECT) return 0;
    return (fe2_object *)args->arg[P_CREATED].ref;
}

/* The session's population_update: 0x86, the update of `object` over
 * `step`; the object it leaves in parameter 9. */
static fe2_object *population_update(void *context, fe2_object *object, uint32_t step)
{
    fe2_session_people_overlay *b = context;
    update(context, b->session, object, step);
    if (b->args.arg[P_CREATED].kind != FE2_ARG_OBJECT) return 0;
    return (fe2_object *)b->args.arg[P_CREATED].ref;
}

int fe2_session_bind_people_overlay(fe2_session *s, fe2_session_objects *objects, fe2_session_people_overlay *b)
{
    fe2_people_services *v;
    fe2_session_overlay *o;
    if (!s || !objects || objects->session != s || !b || s->busy || s->locks[PEOPLE_SLOT]) return -1;
    b->session = s; b->objects = objects;
    fe2_args_clear(&b->args);
    v = &b->services; v->opaque = b;
    people_forward_all(v);
    v->visit = visit; v->lookup = lookup; v->allocate = allocate; v->allocate_upper = allocate;
    v->release = release; v->set_slot_flags = set_slot_flags; v->damage = damage;
    v->activate_effect = activate_effect; v->consume_effect = consume_effect; v->activate_slot = activate_slot;
    v->deploy_slot = deploy_slot; v->spawn_effect = spawn_effect; v->random = random_word;
    v->random_below = random_below; v->basis = basis; v->post_event = post_event;
    v->check_music = check_music; v->select_music = select_music;
    v->galactic_map_relationship = route_galactic_map; v->catalogue_query = route_catalogue; v->market_overlay_service = route_market;
    v->names_name = route_names; v->commander_check = route_commander; v->station_message = route_station;
    v->offences_event = route_offences; v->hyperspace_arrival = route_hyperspace;
    s->calls.context[PEOPLE_SLOT] = b;
    s->calls.population_create = population_create;
    s->calls.population_update = population_update;
    o = &s->overlays[PEOPLE_SLOT];
    o->context = b;
    o->initialize = initialize; o->event = event; o->periodic_record = motion; o->second_record = update;
    o->record_event = relationship_event;
    return 0;
}
