/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "objects.h"

static fe2_session_overlay *enter(fe2_session *s, unsigned slot)
{
    if (slot >= FE2_SESSION_OVERLAYS) { s->error = 1; return NULL; }
    if (!s->loaded[slot]) {
        s->metadata[slot] = s->overlays[slot].metadata;
        s->event_masks[slot] = s->metadata[slot].events;
        s->loaded[slot] = 1;
    }
    ++s->locks[slot];
    return &s->overlays[slot];
}

/* Session-owned globals in and out of the objects state, around every call
 * and every callback out of it (callbacks may reenter the objects layer). */
static void load(fe2_session_objects *b)
{
    fe2_session *s = b->session;
    b->state.random[0] = s->noise[0]; b->state.random[1] = s->noise[1];
    b->state.time_step = s->time_step;
    b->state.simulation_step = s->simulation_step;
    b->state.selected_overlay = (uint16_t)s->selected_overlay;
    b->state.free_capacity = s->pools.player_capacity;
}
static void store(fe2_session_objects *b)
{
    fe2_session *s = b->session;
    s->noise[0] = b->state.random[0]; s->noise[1] = b->state.random[1];
    s->time_step = b->state.time_step;
    s->selected_overlay = b->state.selected_overlay;
    s->pools.player_capacity = b->state.free_capacity;
}

static void message(void *context, unsigned slot, const fe2_objects_message *m)
{
    fe2_session_objects *b = context;
    fe2_session_overlay *o = enter(b->session, slot);
    if (!o) return;
    store(b);
    if (o->message) o->message(o->context, b->session, m); else b->session->error = 1;
    load(b);
    --b->session->locks[slot];
}
static void damage_callbacks(void *context, fe2_object *record)
{
    fe2_session_objects *b = context;
    fe2_session *s = b->session;
    unsigned slot;
    for (slot = 0; slot <= s->last_overlay && slot < FE2_SESSION_OVERLAYS; ++slot) {
        fe2_session_overlay *o;
        if (!(s->metadata[slot].flags & 4u)) continue;
        o = enter(s, slot);
        store(b);
        if (o->record_event) o->record_event(o->context, s, record, 0xffff);
        load(b);
        --s->locks[slot];
    }
}
static void broadcast(void *context, uint16_t event)
{
    fe2_session_objects *b = context;
    store(b);
    if (fe2_session_broadcast(b->session, event)) b->session->error = 1;
    load(b);
}
static fe2_object *allocate(void *context, const fe2_objects_request *r, int upper)
{
    fe2_session_objects *b = context;
    fe2_session *s = b->session;
    fe2_pool_services pool;
    fe2_new_record request;
    fe2_object *record = 0;
    fe2_session_pool_services(s, &pool);
    request.flags = (uint8_t)r->flags; request.template_record = r->source; request.model = r->model;
    if (upper) fe2_pool_allocate_upper(&s->pools, &pool, &request, &record);
    else fe2_pool_allocate(&s->pools, &pool, &request, &record);
    return record;
}
static void release(void *context, fe2_object *record, uint16_t request)
{
    fe2_session_objects *b = context;
    fe2_pool_services pool;
    fe2_session_pool_services(b->session, &pool);
    store(b);
    fe2_pool_release(&b->session->pools, &pool, record, request);
    load(b);
}
static fe2_object *lookup(void *context, uint8_t handle)
{
    fe2_session_objects *b = context;
    b->session->pools.current = b->session->pools.root;
    return fe2_pool_record(b->session->pools.root, handle);
}
static uint8_t status(void *context, uint8_t handle)
{
    fe2_session_objects *b = context;
    return handle < FE2_POOL_SLOTS ? b->session->pools.saved->status[handle] : 0;
}

/* Host providers. */
#define HOST(b) (&(b)->host)
static void missing(fe2_session_objects *b) { b->session->error = 1; }
static void probe(void *context, fe2_object *source, fe2_objects_probe *p)
{
    fe2_session_objects *b = context;
    if (HOST(b)->probe) HOST(b)->probe(HOST(b)->context, source, p); else missing(b);
}
static const uint8_t *model(void *context, uint16_t number)
{
    fe2_session_objects *b = context;
    if (HOST(b)->model) return HOST(b)->model(HOST(b)->context, number);
    if (b->session->services.model) return b->session->services.model(b->session->services.context, number);
    missing(b); return NULL;
}
static void sound(void *context, uint16_t effect, uint8_t level)
{
    fe2_session_objects *b = context;
    if (HOST(b)->sound) HOST(b)->sound(HOST(b)->context, effect, level);
    else if (b->session->services.play_level) b->session->services.play_level(b->session->services.context, effect, level);
}
static void sound_plain(void *context, uint16_t effect)
{
    fe2_session_objects *b = context;
    if (HOST(b)->sound_plain) HOST(b)->sound_plain(HOST(b)->context, effect);
}
static void basis(void *context, fe2_object *record, const uint16_t seed[2])
{
    fe2_session_objects *b = context;
    if (HOST(b)->basis) HOST(b)->basis(HOST(b)->context, record, seed); else missing(b);
}
static void move(void *context, fe2_object *record, const int32_t delta[3])
{
    fe2_session_objects *b = context;
    if (HOST(b)->move) HOST(b)->move(HOST(b)->context, record, delta); else missing(b);
}
static void text(void *context, uint16_t selector, char *out, size_t capacity)
{
    fe2_session_objects *b = context;
    if (HOST(b)->text) HOST(b)->text(HOST(b)->context, selector, out, capacity);
    else { if (capacity) out[0] = 0; missing(b); }
}
static int key(void *context)
{
    fe2_session_objects *b = context;
    if (HOST(b)->key) return HOST(b)->key(HOST(b)->context);
    missing(b); return 13; /* Enter: the editor returns */
}
static void draw_text(void *context, const char *value, uint8_t color, uint16_t argument)
{
    fe2_session_objects *b = context;
    if (HOST(b)->draw_text) HOST(b)->draw_text(HOST(b)->context, value, color, argument);
}

static void nearby_damage(void *context, fe2_session *s, fe2_object *record, uint16_t amount, uint16_t attacker)
{
    (void)s;
    (void)fe2_session_objects_damage(context, record, amount, attacker);
}

int fe2_session_bind_objects(fe2_session *s, fe2_session_objects *b)
{
    fe2_objects_services *v;
    if (!s || !b || s->busy) return -1;
    b->session = s; v = &b->services; v->context = b;
    v->message = message; v->probe = probe; v->damage_callbacks = damage_callbacks; v->broadcast = broadcast;
    v->model = model; v->allocate = allocate; v->release = release; v->lookup = lookup; v->status = status;
    v->sound = sound; v->sound_plain = sound_plain; v->basis = basis; v->move = move; v->text = text;
    v->key = key; v->draw_text = draw_text;
    s->damage = nearby_damage; s->damage_context = b;
    return 0;
}

static void deselect(void *context, uint16_t previous)
{
    fe2_session_objects *b = context;
    fe2_session_overlay *o = enter(b->session, previous);
    if (!o) return;
    store(b);
    if (o->deselect) o->deselect(o->context, b->session);
    load(b);
    --b->session->locks[previous];
}

uint32_t fe2_session_objects_random(fe2_session_objects *b, uint32_t *mix)
{
    uint32_t value;
    load(b); value = fe2_objects_random(&b->state, mix); store(b);
    return value;
}
uint16_t fe2_session_objects_random_below(fe2_session_objects *b, uint16_t range)
{
    uint16_t value;
    load(b); value = fe2_objects_random_below(&b->state, range); store(b);
    return value;
}
void fe2_session_objects_select_overlay(fe2_session_objects *b, uint16_t slot)
{
    load(b);
    fe2_objects_select_overlay(&b->state, slot, deselect, b);
    store(b);
}
uint32_t fe2_session_objects_update(fe2_session_objects *b, fe2_object *o, uint16_t quantum)
{
    uint32_t amount;
    load(b); amount = fe2_objects_update(&b->state, &b->services, o, quantum); store(b);
    return amount;
}
void fe2_session_objects_consume(fe2_session_objects *b, fe2_object *o, uint32_t amount)
{
    load(b); fe2_objects_consume(&b->state, &b->services, o, amount); store(b);
}
void fe2_session_objects_spawn_effect(fe2_session_objects *b, fe2_object *source, const uint32_t position[6],
                                      uint16_t flags)
{
    load(b); fe2_objects_spawn_effect(&b->state, &b->services, source, position, flags); store(b);
}
fe2_object *fe2_session_objects_deploy(fe2_session_objects *b, fe2_object *source, uint16_t slot, uint8_t target)
{
    fe2_object *o;
    load(b); o = fe2_objects_deploy(&b->state, &b->services, source, slot, target); store(b);
    return o;
}
void fe2_session_objects_service(fe2_session_objects *b, fe2_object *o, uint32_t amount)
{
    load(b); fe2_objects_service(&b->state, &b->services, o, amount); store(b);
}
void fe2_session_objects_activate(fe2_session_objects *b, fe2_object *o, int8_t index)
{
    load(b); fe2_objects_activate(&b->state, &b->services, o, index); store(b);
}
int fe2_session_objects_damage(fe2_session_objects *b, fe2_object *o, uint16_t amount, uint16_t attacker)
{
    int alive;
    load(b); alive = fe2_objects_damage(&b->state, &b->services, o, amount, attacker); store(b);
    return alive;
}
void fe2_session_objects_edit_text(fe2_session_objects *b, char *value, uint16_t capacity, uint16_t argument,
                                   uint16_t mode)
{
    fe2_objects_edit_text(&b->services, value, capacity, argument, mode);
}
