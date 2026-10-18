/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "market_overlay.h"
#include <string.h>

enum { MARKET_SLOT = 7, P_POOL = 12 };
enum { REQUEST_FIND_MARKET = 0, REQUEST_CATEGORY = 8, MARKET_BLOCK = 0x224d };

typedef fe2_session_market_overlay market_binding;

/* ---- The shared globals ------------------------------------------------ */
#include "market_resident.h"
static void load(market_binding *b) { market_resident_load(b->session, &b->objects->state, &b->state); }
static void store(market_binding *b) { market_resident_store(b->session, &b->objects->state, &b->state); }
/* Around native services and host calls: the market state goes out first
 * when market is running and comes back after. */
static void market_out(market_binding *b) { if (b->depth) store(b); }
static void market_back(market_binding *b) { load(b); }

/* Host providers: a missing void provider does nothing; one with a result
 * fails the frame. */
static void market_missing(market_binding *b) { b->session->error = 1; }
#include "market_forward.h"

/* ---- Native services --------------------------------------------------- */
static uint32_t random_word(void *opaque, uint32_t *high)
{
    market_binding *b = opaque;
    uint32_t value, mix;
    market_out(b); value = fe2_session_objects_random(b->objects, &mix); market_back(b);
    if (high) *high = mix;
    return value;
}
static uint16_t random_below(void *opaque, uint16_t bound)
{
    market_binding *b = opaque;
    uint16_t value;
    market_out(b); value = fe2_session_objects_random_below(b->objects, bound); market_back(b);
    return value;
}
/* the records that are neither free nor reusable, highest first. */
typedef struct visit_context { fe2_market_visitor visit; void *context; } visit_context;
static void visit_record(void *context, fe2_object *record, uint16_t status_pair)
{
    visit_context *v = context;
    (void)status_pair;
    v->visit(v->context, record);
}
static void visit_objects(void *opaque, fe2_object_list *list, fe2_market_visitor visit, void *context)
{
    market_binding *b = opaque;
    visit_context v;
    v.visit = visit; v.context = context;
    /* The visitors (0x831, collection) work on the market state. */
    fe2_pool_visit(&b->session->pools, (fe2_object_pool *)list, visit_record, &v);
}
/* the record of `handle` in the list of parameter 12. */
static fe2_object *lookup_object(void *opaque, uint8_t handle, const fe2_call_args *args)
{
    market_binding *b = opaque;
    fe2_object_pool *pool = args && args->arg[P_POOL].ref ? (fe2_object_pool *)args->arg[P_POOL].ref
                                                         : b->session->pools.current;
    if (!pool || handle >= FE2_POOL_SLOTS) { b->session->error = 1; return 0; }
    b->session->pools.current = pool;
    return fe2_pool_record(pool, handle);
}
static void select_music(void *opaque, uint16_t track)
{
    market_binding *b = opaque;
    market_out(b);
    if (b->session->services.select_music)
        b->session->services.select_music(b->session->services.context, (int16_t)track);
    market_back(b);
}

/* ---- Other overlays (overlay_calls.h) ---------------------------------- */
/* The station message: through the session's table when station is registered,
 * the host's provider otherwise. */
static void route_message(void *opaque, uint16_t text, const fe2_call_args *args)
{
    market_binding *b = opaque;
    fe2_session *s = b->session;
    if (!s->calls.message) { market_host_show_message(opaque, text, args); return; }
    market_out(b); s->calls.message(FE2_OVERLAY_CONTEXT(s, 12), text, args); market_back(b);
}

/* ---- Entries ------------------------------------------------------------ */
#define ENTER(context) market_binding *b = context; (void)s; load(b); ++b->depth
#define LEAVE() do { --b->depth; store(b); } while (0)

static void initialize(void *context, fe2_session *s, uint16_t selection)
{
    ENTER(context); fe2_market_initialize(&b->state, &b->services, (uint8_t)selection); LEAVE();
}
static uint32_t event(void *context, fe2_session *s, uint32_t value)
{
    ENTER(context); fe2_market_event(&b->state, &b->services, (uint16_t)value); LEAVE();
    return value;
}

/* 0x8a (+0x20): reference service `request` (BX, FE2 request numbering of
 * fe2_market_reference: 0 find the market, 1 attach, 2 detach, 3
 * activate, 4 clear, 5 claim, 6 dock, 7 match, 8 category value, 9 and
 * above the slot vector) with the handle in AL. Parameters 8  and
 * 9  are the actor and the target; the draft is the scratch block
 * : the numeric parameters the caller set go in, and
 * parameters 0..7  come back. Request 0 points DS:DI at
 * the market record found (one past the counted markets when none). The
 * result is AX. */
static void draft_in(fe2_market_draft *d, const fe2_call_args *args)
{
    uint32_t *field[FE2_CALL_ARGS] = {
        &d->lookup, &d->system, &d->due, &d->reward, &d->detail, &d->kind, &d->counter,
        &d->threshold, &d->request_seed, 0, &d->seed, 0, 0};
    unsigned n;
    for (n = 0; n < FE2_CALL_ARGS; ++n) {
        fe2_arg_kind kind = args->arg[n].kind;
        if (field[n] && (kind == FE2_ARG_BYTE || kind == FE2_ARG_WORD || kind == FE2_ARG_DWORD))
            *field[n] = args->arg[n].value;
    }
}
static void draft_out(const fe2_market_draft *d, fe2_call_args *args)
{
    const uint32_t value[8] = {d->lookup, d->system, d->due, d->reward, d->detail, d->kind,
                               d->counter, d->threshold};
    unsigned n;
    for (n = 0; n < 8; ++n) fe2_args_dword(args, n, value[n]);
}
static uint16_t call_market(void *context, uint16_t request, uint16_t handle, fe2_call_args *args,
                            fe2_overlay_record *record)
{
    market_binding *b = context;
    fe2_reference_call call;
    memset(&call, 0, sizeof call);
    call.request = request; call.ax = handle;
    if (args) {
        call.actor = (fe2_object *)args->arg[8].ref;
        call.target = (fe2_object *)args->arg[9].ref;
    }
    if (record) { record->record = 0; record->ref = 0; }
    /* Every request but 0 and 8 works on the target. */
    if (request != REQUEST_FIND_MARKET && request != REQUEST_CATEGORY && !call.target) {
        b->session->error = 1;
        return 0;
    }
    load(b); ++b->depth;
    if (args) draft_in(&b->state.draft, args);
    fe2_market_reference(&b->state, &b->services, &call);
    if (args) draft_out(&b->state.draft, args);
    --b->depth; store(b);
    if (request == REQUEST_FIND_MARKET && record && call.market < FE2_MARKET_SLOTS) {
        uint32_t at = (uint32_t)call.market * FE2_MARKET_RECORD_BYTES;
        record->record = b->state.markets.bytes + at;
        record->ref = MARKET_BLOCK + at;
    }
    return call.ax;
}

/* catalogue (0x8a, mode AL) and chart (request 1) for the draft: the draft is the
 * parameter block, the system key in parameter 1; the queries'
 * results come back into it. */
static uint16_t query(market_binding *b, int map, uint16_t request, fe2_market_draft *d, fe2_overlay_trade *trade)
{
    fe2_session *s = b->session;
    fe2_call_args block;
    uint16_t result;
    fe2_args_clear(&block);
    draft_out(d, &block);
    fe2_args_dword(&block, 8, d->request_seed); fe2_args_dword(&block, 10, d->seed);
    market_out(b);
    result = map ? s->calls.map(FE2_OVERLAY_CONTEXT(s, 2), request, &block, NULL)
                 : s->calls.catalogue(FE2_OVERLAY_CONTEXT(s, 6), request, &block, trade);
    market_back(b);
    draft_in(d, &block);
    return result;
}
static void route_profile(void *opaque, fe2_market_draft *d)
{
    market_binding *b = opaque;
    if (!b->session->calls.catalogue) { market_host_system_profile(opaque, d); return; }
    (void)query(b, 0, 0, d, NULL);
}
static fe2_system_trade route_trade(void *opaque, fe2_market_draft *d)
{
    market_binding *b = opaque;
    fe2_system_trade trade;
    fe2_overlay_trade reply = {0, 0, 0};
    if (!b->session->calls.catalogue) return market_host_system_trade(opaque, d);
    trade.population = query(b, 0, 1, d, &reply);
    trade.metadata = reply.metadata; trade.codes = reply.codes;
    return trade;
}
static void route_summary(void *opaque, fe2_market_draft *d)
{
    market_binding *b = opaque;
    if (!b->session->calls.catalogue) { market_host_system_summary(opaque, d); return; }
    (void)query(b, 0, 2, d, NULL);
}
/* chart request 1: the candidate near parameter 1's system, table index in
 * parameter 2 (0x7c4, the draft's due), into parameter 0; AX nonzero when
 * found. */
static uint16_t route_candidate(void *opaque, fe2_market_draft *d)
{
    market_binding *b = opaque;
    if (!b->session->calls.map) return market_host_find_candidate(opaque, d);
    return query(b, 1, 1, d, NULL);
}

int fe2_session_bind_market_overlay(fe2_session *s, fe2_session_objects *objects, market_binding *b)
{
    fe2_market_services *v;
    fe2_session_overlay *o;
    if (!s || !objects || objects->session != s || !b || s->busy || s->locks[MARKET_SLOT]) return -1;
    b->session = s; b->objects = objects; b->depth = 0;
    v = &b->services; v->opaque = b;
    market_forward_all(v);
    v->random = random_word; v->random_below = random_below; v->visit_objects = visit_objects;
    v->lookup_object = lookup_object; v->select_music = select_music;
    v->show_message = route_message;
    v->system_profile = route_profile; v->system_trade = route_trade;
    v->system_summary = route_summary; v->find_candidate = route_candidate;
    o = &s->overlays[MARKET_SLOT];
    o->context = b;
    o->initialize = initialize; o->event = event;
    s->calls.context[MARKET_SLOT] = b; s->calls.market = call_market;
    return 0;
}
