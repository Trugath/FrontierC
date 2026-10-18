/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "flight_overlay.h"
#include "status_overlay.h"
#include "math/math3.h"
#include <string.h>

enum {
    FLIGHT_SLOT = 3, P_MODE = 7, KEYS_BEFORE_STATES = 8, /* precede the key states  */
    STATE_KEYS = FE2_FLIGHT_KEYS - KEYS_BEFORE_STATES
};

/* ---- The shared globals ------------------------------------------------ */
#include "flight_resident.h"
static fe2_commander_status *commander(fe2_session_flight_overlay *b)
{
    if (!b->session->calls.contract || !b->session->calls.context[14] ||
        b->session->overlays[14].context != b->session->calls.context[14] || !b->session->overlays[14].initialize) return NULL;
    return &((fe2_session_status_overlay *)b->session->calls.context[14])->state;
}
static void load(fe2_session_flight_overlay *b)
{
    fe2_commander_status *st = commander(b);
    unsigned i;
    flight_resident_load(b->session, &b->objects->state, &b->state);
    if (!st) return;
    b->state.records = b->mission_records;
    for (i = 0; i < FE2_MAX_MILITARY_CONTRACTS; ++i) {
        fe2_flight_record *r = &b->mission_records[i];
        const fe2_military_contract *c = &st->military[i];
        r->kind = c->type; r->seen = (uint8_t)c->outcome; r->system = c->target_system;
        r->handle = c->target_model; r->lowest_level = c->quality;
    }
    b->state.federal_points = st->federal_points; b->state.imperial_points = st->imperial_points;
}
static void store(fe2_session_flight_overlay *b)
{
    fe2_commander_status *st = commander(b);
    unsigned i;
    flight_resident_store(b->session, &b->objects->state, &b->state);
    if (st) {
        for (i = 0; i < FE2_MAX_MILITARY_CONTRACTS; ++i) {
            const fe2_flight_record *r = &b->mission_records[i];
            fe2_military_contract *c = &st->military[i];
            c->type = r->kind; c->outcome = (int8_t)r->seen; c->target_system = r->system;
            c->target_model = r->handle; c->quality = r->lowest_level;
        }
        st->federal_points = b->state.federal_points; st->imperial_points = b->state.imperial_points;
    }
    /* is shared: its low byte is the cockpit flag (negative while the
     * cockpit is shown) and the scanner's clip bit. The generated sync does
     * not carry view_mode, so the phone list never saw the cockpit. */
    b->session->resident.scanner_view_mode = (uint8_t)b->state.view_mode;
    /* Shared with slot 15: 0x413 flags, 0x414 update. */
    b->session->metadata[15].flags = b->state.police_flags;
    b->session->metadata[15].update = b->state.launch_phase;
}
/* Around native services and host calls: the flight state goes out first
 * when flight is running (inside an entry; otherwise the session's copy is
 * current) and comes back after. */
static void flight_out(fe2_session_flight_overlay *b) { if (b->depth) store(b); }
static void flight_back(fe2_session_flight_overlay *b) { load(b); }

/* Host providers: a missing void provider does nothing; one with a result
 * fails the frame. */
static void flight_missing(fe2_session_flight_overlay *b) { b->session->error = 1; }
#include "flight_forward.h"

#define OUT(b) flight_out(b)
#define BACK(b) flight_back(b)

/* ---- Native services --------------------------------------------------- */
static void set_panel_mode(void *opaque, uint16_t mode)
{
    fe2_session_flight_overlay *b = opaque;
    OUT(b); fe2_session_objects_select_overlay(b->objects, mode); BACK(b);
}
static void damage(void *opaque, fe2_object *record, uint16_t amount, const fe2_call_args *args)
{
    fe2_session_flight_overlay *b = opaque;
    OUT(b);
    (void)fe2_session_objects_damage(b->objects, record, amount, (uint16_t)args->arg[P_MODE].value);
    BACK(b);
}
static void activate_effect(void *opaque, fe2_object *record)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_pool_services pool;
    OUT(b);
    fe2_session_pool_services(b->session, &pool);
    fe2_pool_equipment_effect(&b->session->pools, &pool, record);
    BACK(b);
}
static void consume_effect(void *opaque, fe2_object *record)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_pool_services pool;
    OUT(b);
    fe2_session_pool_services(b->session, &pool);
    fe2_pool_nearby_effect(&b->session->pools, &pool, record);
    BACK(b);
}
/* takes AX: the interval flight passed 0x1cc just before. */
static void update_capacity(void *opaque, fe2_object *record)
{
    fe2_session_flight_overlay *b = opaque;
    OUT(b); (void)fe2_session_objects_update(b->objects, record, (uint16_t)b->interval); BACK(b);
}
static void advance_failures(void *opaque, fe2_object *record, uint32_t interval)
{
    fe2_session_flight_overlay *b = opaque;
    b->interval = interval;
    OUT(b); fe2_session_objects_service(b->objects, record, interval); BACK(b);
}
static void activate_slot(void *opaque, fe2_object *record, uint8_t view)
{
    fe2_session_flight_overlay *b = opaque;
    OUT(b); fe2_session_objects_activate(b->objects, record, (int8_t)view); BACK(b);
}
static void deploy_slot(void *opaque, fe2_object *record, uint16_t slot, uint8_t target, const fe2_call_args *args)
{
    fe2_session_flight_overlay *b = opaque;
    (void)args;
    OUT(b); (void)fe2_session_objects_deploy(b->objects, record, slot, target); BACK(b);
}
static void spawn_effect(void *opaque, fe2_object *record, const fe2_call_args *args)
{
    fe2_session_flight_overlay *b = opaque;
    uint32_t position[6];
    unsigned i;
    for (i = 0; i < 6; ++i) position[i] = args->arg[i].value;
    OUT(b);
    fe2_session_objects_spawn_effect(b->objects, record, position, (uint16_t)args->arg[P_MODE].value);
    BACK(b);
}
static uint32_t random_word(void *opaque, uint32_t *edx)
{
    fe2_session_flight_overlay *b = opaque;
    uint32_t value;
    OUT(b); value = fe2_session_objects_random(b->objects, edx); BACK(b);
    return value;
}
static void basis(void *opaque, fe2_object *record)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_math_basis_b(b->state.basis_angle[0], b->state.basis_angle[1], record->orientation);
}
static void set_slot_flags(void *opaque, fe2_object *record, uint8_t flags)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_pool_set_slot_flags(&b->session->pools, record, flags);
}
static int poll_key(void *opaque, uint8_t *key)
{
    fe2_session_flight_overlay *b = opaque;
    uint16_t code;
    OUT(b); code = fe2_session_poll(b->session); BACK(b);
    *key = (uint8_t)code;
    return code != 0;
}
static void forward_key(void *opaque, uint8_t key)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_input_restore(&b->session->pointer, key);
}
static void show_panel(void *opaque)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_pointer_regions_reset(&b->session->regions);
}
/* records of five words; the first unconditionally, then until a
 * left edge of 0xffff. */
static void set_widgets(void *opaque, const uint16_t *words)
{
    fe2_session_flight_overlay *b = opaque;
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
/* the X extent for both; the center stays in AX, BX. */
static void add_region(void *opaque, uint16_t x, uint16_t y, uint16_t extent, uint16_t id, uint16_t *px,
                       uint16_t *py)
{
    fe2_session_flight_overlay *b = opaque;
    if (fe2_pointer_regions_add(&b->session->regions, x, y, extent, extent, id)) b->session->error = 1;
    if (px) *px = x;
    if (py) *py = y;
}
static void remove_region(void *opaque, uint16_t id)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_pointer_regions_remove(&b->session->regions, id);
}
static void clear_regions(void *opaque)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_pointer_regions_clear(&b->session->regions);
}
static void post_event(void *opaque, uint16_t event)
{
    fe2_session_flight_overlay *b = opaque;
    OUT(b);
    if (fe2_session_broadcast(b->session, event)) b->session->error = 1;
    BACK(b);
}
static void check_music(void *opaque)
{
    fe2_session_flight_overlay *b = opaque;
    if (b->session->services.check_music) b->session->services.check_music(b->session->services.context);
}
static void select_music(void *opaque, uint16_t track)
{
    fe2_session_flight_overlay *b = opaque;
    OUT(b);
    if (b->session->services.select_music)
        b->session->services.select_music(b->session->services.context, (int16_t)track);
    BACK(b);
}
static void restart_session(void *opaque, uint16_t mode)
{
    fe2_session_flight_overlay *b = opaque;
    OUT(b);
    if (fe2_session_exit_scene(b->session, mode)) b->session->error = 1;
    BACK(b);
}

/* ---- Other overlays ------------------------------------------------------ */
/* flight reaches the others through the overlay call (+0x20, the target's 0x8a;
 * people's +0x1c and drives's +0x8). Each goes through the session's entry when
 * the target registered one, and to the host's provider otherwise. The
 * parameter block is the entry's to change, so it gets a copy. */
#define CALLS(b) (&(b)->session->calls)
#define CONTEXT(b, module) FE2_OVERLAY_CONTEXT((b)->session, module)
enum { PANELS_SLOT = 4, MARKET_SLOT = 7, NAMES_SLOT = 9, COMMANDER_SLOT = 10,
       PEOPLE_SLOT = 11, STATION_SLOT = 12, OFFENCES_SLOT = 15, DRIVES_SLOT = 16,
       MINING_SLOT = 17 };
static void copy_args(fe2_call_args *copy, const fe2_call_args *args)
{
    if (args) *copy = *args; else fe2_args_clear(copy);
}
/* panels 0x8a: AX the request (flight's are panel modes 2 and 4, which ignore
 * BX), BX the widgets or the overlay whose panel to select. */
static void panels_panel(void *opaque, uint16_t request, const fe2_call_args *args)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_call_args copy;
    if (!CALLS(b)->panel) { flight_host_panels_panel(opaque, request, args); return; }
    copy_args(&copy, args);
    OUT(b); CALLS(b)->panel(CONTEXT(b, PANELS_SLOT), request, FLIGHT_SLOT, &copy); BACK(b);
}
static void panels_widgets(void *opaque, uint16_t request, uint16_t widgets, const fe2_call_args *args)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_call_args copy;
    if (!CALLS(b)->panel) { flight_host_panels_widgets(opaque, request, widgets, args); return; }
    copy_args(&copy, args);
    OUT(b); CALLS(b)->panel(CONTEXT(b, PANELS_SLOT), request, widgets, &copy); BACK(b);
}
/* market 0x8a: service BX with handle AL; the result in AL, the value in
 * parameter 7  and a position change in parameters 0..5. */
static void market_overlay_service(void *opaque, uint16_t service, uint16_t handle, const fe2_call_args *args,
                        fe2_flight_market_reply *reply)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_call_args copy;
    uint16_t result;
    unsigned i;
    if (!CALLS(b)->market) { flight_host_market_service(opaque, service, handle, args, reply); return; }
    copy_args(&copy, args);
    OUT(b); result = CALLS(b)->market(CONTEXT(b, MARKET_SLOT), service, handle, &copy, 0); BACK(b);
    reply->result = (uint8_t)result;
    reply->value = (uint8_t)copy.arg[P_MODE].value;
    for (i = 0; i < 3; ++i) {
        reply->offset_low[i] = copy.arg[i].value;
        reply->offset_high[i] = copy.arg[3 + i].value;
    }
}
static void names_name(void *opaque, uint16_t type, uint32_t seed, char out[FE2_FLIGHT_NAME_BYTES])
{
    fe2_session_flight_overlay *b = opaque;
    if (!CALLS(b)->name) { flight_host_names_name(opaque, type, seed, out); return; }
    OUT(b); CALLS(b)->name(CONTEXT(b, NAMES_SLOT), type, seed, out, FE2_FLIGHT_NAME_BYTES); BACK(b);
    out[FE2_FLIGHT_NAME_BYTES - 1] = 0;
}
/* commander 0x8a: restore the normal time step. */
static void commander_reset(void *opaque)
{
    fe2_session_flight_overlay *b = opaque;
    if (!CALLS(b)->menu_restore_time) { flight_host_commander_reset(opaque); return; }
    OUT(b); CALLS(b)->menu_restore_time(CONTEXT(b, COMMANDER_SLOT)); BACK(b);
}
/* people +0x1c: update the record over the interval; the object it leaves in
 * parameter 9. */
static fe2_object *people_update(void *opaque, fe2_object *record, uint32_t interval)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_object *object;
    if (!CALLS(b)->population_update) return flight_host_people_update(opaque, record, interval);
    OUT(b); object = CALLS(b)->population_update(CONTEXT(b, PEOPLE_SLOT), record, interval); BACK(b);
    return object;
}
static void station_message(void *opaque, uint16_t text, const fe2_call_args *args)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_call_args copy;
    if (!CALLS(b)->message) { flight_host_station_message(opaque, text, args); return; }
    copy_args(&copy, args);
    OUT(b); CALLS(b)->message(CONTEXT(b, STATION_SLOT), text, &copy); BACK(b);
}
/* offences 0x8a: commit offence AX about the subject in parameter 9. */
static void offences_event(void *opaque, uint16_t offence, const fe2_call_args *args)
{
    fe2_session_flight_overlay *b = opaque;
    if (!CALLS(b)->offence_event) { flight_host_offences_event(opaque, offence, args); return; }
    OUT(b); CALLS(b)->offence_event(CONTEXT(b, OFFENCES_SLOT), offence, args); BACK(b);
}
/* drives 0x8a: drive query mode AX (BX unset); the mode comes back in
 * parameter 0 (0x7bc, negative when refused) and the range left in
 * parameter 5. */
static void hyperspace_query(void *opaque, uint16_t mode, const fe2_call_args *args, fe2_flight_drive_reply *reply)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_call_args copy;
    if (!CALLS(b)->drive) { flight_host_hyperspace_query(opaque, mode, args, reply); return; }
    copy_args(&copy, args);
    OUT(b); (void)CALLS(b)->drive(CONTEXT(b, DRIVES_SLOT), mode, 0, &copy); BACK(b);
    reply->mode = (uint16_t)copy.arg[0].value;
    reply->range_left = (uint16_t)copy.arg[5].value;
}
static void hyperspace_jump(void *opaque)
{
    fe2_session_flight_overlay *b = opaque;
    if (!CALLS(b)->jump) { flight_host_hyperspace_jump(opaque); return; }
    OUT(b); CALLS(b)->jump(CONTEXT(b, DRIVES_SLOT)); BACK(b);
}
/* mining 0x8a: mining action AX. */
static void mining_command(void *opaque, uint16_t request, const fe2_call_args *args)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_call_args copy;
    if (!CALLS(b)->mining) { flight_host_mining_command(opaque, request, args); return; }
    copy_args(&copy, args);
    OUT(b); (void)CALLS(b)->mining(CONTEXT(b, MINING_SLOT), request, &copy); BACK(b);
}

/* Pool records. Both 0xa8 and 0xac are 0x9e9 (any nonzero slot):
 * parameter 0 the slot flags, 1 the model, 8 the template; the slot word
 * is left in parameter 0. */
static fe2_object *allocate(void *opaque, const fe2_call_args *args, uint16_t *slot)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_pool_services pool;
    fe2_new_record request;
    fe2_object *record = 0;
    uint8_t taken;
    request.flags = (uint8_t)args->arg[0].value; request.model = (uint16_t)args->arg[1].value;
    request.template_record = (const fe2_object *)args->arg[8].ref;
    if (!request.template_record) { b->session->error = 1; return 0; }
    OUT(b);
    fe2_session_pool_services(b->session, &pool);
    taken = fe2_pool_allocate(&b->session->pools, &pool, &request, &record);
    BACK(b);
    if (slot) *slot = taken;
    return record;
}
static fe2_object *allocate_upper(void *opaque, const fe2_call_args *args) { return allocate(opaque, args, 0); }
/* release without notifying the owners. */
static void release(void *opaque, fe2_object *record)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_pool_release_quietly(&b->session->pools, record);
}
/* the record of `handle` in the list of parameter 12. */
static fe2_object *lookup(void *opaque, uint8_t handle, const fe2_call_args *args)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_object_pool *pool = args && args->arg[12].ref ? (fe2_object_pool *)args->arg[12].ref : b->session->pools.current;
    if (handle >= FE2_POOL_SLOTS) { b->session->error = 1; return 0; }
    b->session->pools.current = pool; b->state.current_objects = (fe2_object_list *)pool;
    return fe2_pool_record(pool, handle);
}
static uint8_t handle_flags(void *opaque, const fe2_object_list *list, uint8_t handle)
{
    (void)opaque;
    return handle < FE2_POOL_SLOTS ? ((const fe2_object_pool *)list)->status[handle] : 0;
}
/* the records that are neither free nor reusable, highest first. */
typedef struct visit_context { fe2_flight_visitor visit; void *context; } visit_context;
static void visit_record(void *context, fe2_object *record, uint16_t status_pair)
{
    visit_context *v = context;
    (void)status_pair;
    v->visit(v->context, record);
}
static void visit_objects(void *opaque, fe2_object_list *list, fe2_flight_visitor visit, void *context)
{
    fe2_session_flight_overlay *b = opaque;
    visit_context v;
    v.visit = visit; v.context = context;
    /* The visitors work on the flight state: no carrying around them. */
    fe2_pool_visit(&b->session->pools, (fe2_object_pool *)list, visit_record, &v);
    b->state.current_objects = (fe2_object_list *)b->session->pools.current;
}
/* projected (flag 8), not reusable records whose screen
 * position is inside the view, with AX = x + 3 and BX = y - 4. */
static void visit_projected(void *opaque, fe2_object_list *list, fe2_flight_screen_visitor visit, void *context)
{
    fe2_session_flight_overlay *b = opaque;
    fe2_object_pool *pool = (fe2_object_pool *)list;
    int slot;
    b->session->pools.current = b->session->pools.saved = pool;
    b->state.current_objects = (fe2_object_list *)pool;
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

/* ---- Entries ------------------------------------------------------------ */
#define ENTER(context) fe2_session_flight_overlay *b = context; (void)s; load(b); ++b->depth
#define LEAVE() do { --b->depth; store(b); } while (0)
static fe2_object *subject(fe2_session_flight_overlay *b) { return b->subject ? b->subject : b->state.ship; }

static void initialize(void *context, fe2_session *s, uint16_t selection)
{
    ENTER(context); fe2_flight_initialize(&b->state, &b->services, selection); LEAVE();
}
static void activate(void *context, fe2_session *s, uint16_t input)
{
    ENTER(context); (void)input; fe2_flight_activate_view(&b->state, &b->services); LEAVE();
}
static uint32_t event(void *context, fe2_session *s, uint32_t value)
{
    ENTER(context); fe2_flight_event(&b->state, &b->services, (uint16_t)value, b->selected); LEAVE();
    return value;
}
static void cockpit(void *context, fe2_session *s)
{
    ENTER(context); fe2_flight_cockpit(&b->state, &b->services, subject(b)); LEAVE();
}
static void status(void *context, fe2_session *s)
{
    ENTER(context); fe2_flight_status(&b->state, &b->services, subject(b)); LEAVE();
}
static void update(void *context, fe2_session *s, fe2_object *record, uint64_t time)
{
    ENTER(context); fe2_flight_update(&b->state, &b->services, record, time); LEAVE();
}
static void timers(void *context, fe2_session *s, fe2_object *record, uint32_t interval)
{
    ENTER(context); fe2_flight_timers(&b->state, &b->services, record, interval); LEAVE();
}
static void reset_capacity(void *context, fe2_session *s, const fe2_objects_message *m)
{
    ENTER(context); (void)m; fe2_flight_reset_capacity(&b->state); LEAVE();
}
static void deselect(void *context, fe2_session *s)
{
    ENTER(context); fe2_flight_change_view(&b->state, &b->services, 0); LEAVE();
}
static void countdown(void *context, fe2_session *s, uint32_t interval)
{
    ENTER(context); (void)interval; fe2_flight_countdown(&b->state, &b->services); LEAVE();
}
/* The session's table: the cockpit view (+0x8, 0x72) and the capacity
 * reset (+0x20, 0x8a) for the other overlays. */
static void call_view(void *context) { activate(context, ((fe2_session_flight_overlay *)context)->session, 0); }
static void call_reset_capacity(void *context)
{
    reset_capacity(context, ((fe2_session_flight_overlay *)context)->session, 0);
}
static void select_target(void *context, fe2_session *s, fe2_object *record, uint32_t request)
{
    ENTER(context);
    fe2_flight_select_target(&b->state, &b->services, record->handle, (uint16_t)request, record);
    LEAVE();
}

int fe2_session_bind_flight_overlay(fe2_session *s, fe2_session_objects *objects, fe2_session_flight_overlay *b)
{
    fe2_flight_services *v;
    fe2_session_overlay *o;
    if (!s || !objects || objects->session != s || !b || s->busy || s->locks[FLIGHT_SLOT]) return -1;
    b->session = s; b->objects = objects;
    v = &b->services; v->opaque = b;
    flight_forward_all(v);
    v->set_panel_mode = set_panel_mode; v->damage = damage; v->activate_effect = activate_effect;
    v->consume_effect = consume_effect; v->update_capacity = update_capacity; v->activate_slot = activate_slot;
    v->deploy_slot = deploy_slot; v->advance_failures = advance_failures; v->spawn_effect = spawn_effect;
    v->random = random_word; v->basis = basis; v->set_slot_flags = set_slot_flags; v->poll_key = poll_key;
    v->forward_key = forward_key; v->show_panel = show_panel; v->set_widgets = set_widgets;
    v->add_region = add_region; v->remove_region = remove_region; v->clear_regions = clear_regions;
    v->post_event = post_event; v->check_music = check_music; v->select_music = select_music;
    v->restart_session = restart_session;
    v->panels_panel = panels_panel; v->panels_widgets = panels_widgets; v->market_overlay_service = market_overlay_service; v->names_name = names_name;
    v->commander_reset = commander_reset; v->people_update = people_update; v->station_message = station_message; v->offences_event = offences_event;
    v->hyperspace_query = hyperspace_query; v->hyperspace_jump = hyperspace_jump; v->mining_command = mining_command;
    v->allocate = allocate; v->allocate_upper = allocate_upper; v->release = release; v->lookup = lookup;
    v->handle_flags = handle_flags; v->visit_objects = visit_objects; v->visit_projected = visit_projected;
    o = &s->overlays[FLIGHT_SLOT];
    o->context = b;
    o->initialize = initialize; o->activate = activate; o->event = event; o->update = cockpit; o->draw = status;
    o->periodic_record = update; o->second_record = timers; o->message = reset_capacity; o->deselect = deselect;
    o->coarse_tick = countdown; o->record_event = select_target;
    s->calls.context[FLIGHT_SLOT] = b;
    s->calls.flight_view = call_view; s->calls.flight_reset_capacity = call_reset_capacity;
    return 0;
}
