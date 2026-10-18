/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "status_overlay.h"
#include "math/math3.h"
#include <string.h>

enum { STATUS_SLOT = 14 };

/* ---- The shared globals ------------------------------------------------ */
/* By hand (status's state is not in the generator's list), with the
 * generator's owners: the session, its objects layer (o) and the shared
 * resident store. Not carried: the registration (0x2133, bytes where the
 * objects layer has the lost-control words), the palette ramp (0x1ff5,
 * words where the store has dwords), the fourth permit word (0x516b, a
 * record pointer in the store) and the globals nothing else shares. */
static void load(fe2_session_status_overlay *b)
{
    fe2_session *s = b->session;
    fe2_objects_state *o = &b->objects->state;
    fe2_commander_status *st = &b->state;
    unsigned i;
    st->next_page = s->next_page;
    st->contract_count = s->resident.contract_count;
    st->military_count = s->resident.mission_count;
    st->crew_needed = s->resident.passenger_limit;
    st->crew_count = s->resident.passenger_count;
    st->permits[0] = s->resident.mission_flags;
    st->criminal_record[0] = s->resident.criminal_record_0;
    st->criminal_record[1] = s->resident.criminal_record_1;
    st->criminal_record[2] = s->resident.criminal_record_2;
    st->mining_count = s->resident.mining_machine_count;
    st->cash = o->bounty;
    st->cargo_capacity = s->pools.player_capacity;
    st->cabin_capacity = s->resident.cabins;
    st->cargo_used = o->cargo_space;
    st->cabins_used = s->resident.cabins_used;
    for (i = 0; i < FE2_CARGO_TYPES; ++i) st->cargo[i] = o->cargo[i];
    st->elite_points = o->rating;
    st->reliability = (int16_t)s->resident.rating;
    st->legal_authority = s->resident.legal_allegiance;
    st->ship = (fe2_object *)o->player_record;
    st->time_of_day = s->pools.clock[0];
    st->day = s->pools.clock[1];
    st->frame_time = s->render_step;
    st->current_system = s->resident.current_system;
    st->local_authority = o->player;
    st->flight_state = s->resident.flight_mode;
    st->navigation_target = s->resident.navigation_target;
    st->redraw_flags = s->render_mode;
    st->ship_flags = o->warnings;
    st->text_colour = s->resident.display_colour_1;
    st->dim_colour = s->resident.display_colour_2;
    for (i = 0; i < 2; ++i) st->view_angle[i] = o->effect_seed[i];
    for (i = 0; i < 3; ++i) st->palette_top[i] = s->light[i];
    {
        const uint32_t ramp[4] = {s->resident.classification_0, s->resident.classification_1, s->resident.classification_2, s->resident.classification_3};
        for (i = 0; i < 8; ++i) st->palette_ramp[i] = (uint16_t)(ramp[i / 2] >> (16 * (i % 2)));
    }
    st->objects = (fe2_object_list *)s->pools.root;
}
static void store(fe2_session_status_overlay *b)
{
    fe2_session *s = b->session;
    fe2_objects_state *o = &b->objects->state;
    const fe2_commander_status *st = &b->state;
    unsigned i;
    s->next_page = st->next_page;
    s->resident.contract_count = st->contract_count;
    s->resident.mission_count = st->military_count;
    s->resident.passenger_limit = st->crew_needed;
    s->resident.passenger_count = st->crew_count;
    s->resident.mission_flags = st->permits[0];
    s->resident.criminal_record_0 = st->criminal_record[0];
    s->resident.criminal_record_1 = st->criminal_record[1];
    s->resident.criminal_record_2 = st->criminal_record[2];
    s->resident.mining_machine_count = st->mining_count;
    o->bounty = st->cash;
    s->pools.player_capacity = st->cargo_capacity;
    s->resident.cabins = st->cabin_capacity;
    o->cargo_space = st->cargo_used;
    s->resident.cabins_used = st->cabins_used;
    for (i = 0; i < FE2_CARGO_TYPES; ++i) o->cargo[i] = st->cargo[i];
    o->rating = st->elite_points;
    s->resident.rating = (uint16_t)st->reliability;
    s->resident.legal_allegiance = st->legal_authority;
    o->player_record = st->ship;
    s->pools.clock[0] = st->time_of_day;
    s->pools.clock[1] = st->day;
    s->render_step = st->frame_time;
    s->resident.current_system = st->current_system;
    o->player = st->local_authority;
    s->resident.flight_mode = st->flight_state;
    s->resident.navigation_target = st->navigation_target;
    s->render_mode = st->redraw_flags;
    o->warnings = st->ship_flags;
    s->resident.display_colour_1 = st->text_colour;
    s->resident.display_colour_2 = st->dim_colour;
    for (i = 0; i < 2; ++i) o->effect_seed[i] = st->view_angle[i];
    for (i = 0; i < 3; ++i) s->light[i] = st->palette_top[i];
    s->resident.classification_0 = st->palette_ramp[0] | (uint32_t)st->palette_ramp[1] << 16;
    s->resident.classification_1 = st->palette_ramp[2] | (uint32_t)st->palette_ramp[3] << 16;
    s->resident.classification_2 = st->palette_ramp[4] | (uint32_t)st->palette_ramp[5] << 16;
    s->resident.classification_3 = st->palette_ramp[6] | (uint32_t)st->palette_ramp[7] << 16;
    s->pools.root = (fe2_object_pool *)st->objects;
}
/* Around native services and host calls: the state goes out first when
 * status is running (inside an entry; otherwise the session's copy is
 * current) and comes back after. */
static void status_out(fe2_session_status_overlay *b) { if (b->depth) store(b); }
static void status_back(fe2_session_status_overlay *b) { load(b); }

/* Host providers: a missing void provider does nothing; one with a result
 * fails the frame. */
static void status_missing(fe2_session_status_overlay *b) { b->session->error = 1; }
#include "status_forward.h"

#define OUT(b) status_out(b)
#define BACK(b) status_back(b)

/* ---- Native services --------------------------------------------------- */
static void set_panel_mode(void *opaque, uint16_t mode)
{
    fe2_session_status_overlay *b = opaque;
    OUT(b); fe2_session_objects_select_overlay(b->objects, mode); BACK(b);
}
static void show_panel(void *opaque)
{
    fe2_session_status_overlay *b = opaque;
    fe2_pointer_regions_reset(&b->session->regions);
}
static int poll_key(void *opaque, uint8_t *key)
{
    fe2_session_status_overlay *b = opaque;
    uint16_t code;
    OUT(b); code = fe2_session_poll(b->session); BACK(b);
    b->key = (uint8_t)code;
    *key = b->key;
    return code != 0;
}
/* puts back the key 0x48 gave (status passes it in AL untouched). */
static void forward_key(void *opaque)
{
    fe2_session_status_overlay *b = opaque;
    fe2_input_restore(&b->session->pointer, b->key);
}
static void post_event(void *opaque, uint16_t event, const fe2_call_args *args)
{
    fe2_session_status_overlay *b = opaque;
    (void)args;
    OUT(b);
    if (fe2_session_broadcast(b->session, event)) b->session->error = 1;
    BACK(b);
}
static void select_music(void *opaque, uint16_t track)
{
    fe2_session_status_overlay *b = opaque;
    OUT(b);
    if (b->session->services.select_music)
        b->session->services.select_music(b->session->services.context, (int16_t)track);
    BACK(b);
}
static uint32_t random_word(void *opaque)
{
    fe2_session_status_overlay *b = opaque;
    uint32_t value;
    OUT(b); value = fe2_session_objects_random(b->objects, &b->mix); BACK(b);
    return value;
}
static uint16_t random_below(void *opaque, uint16_t bound)
{
    fe2_session_status_overlay *b = opaque;
    uint16_t value;
    OUT(b); value = fe2_session_objects_random_below(b->objects, bound); BACK(b);
    return value;
}
/* the camera's basis from the view angles (0x1f35, set just before). */
static void orient_camera(void *opaque, fe2_object *camera)
{
    fe2_session_status_overlay *b = opaque;
    fe2_math_basis_b(b->state.view_angle[0], b->state.view_angle[1], camera->orientation);
}
/* (0x9e9, any nonzero slot): parameter 0 the slot flags, 1 the
 * model, 8 the template. */
static fe2_object *spawn_object(void *opaque, const fe2_call_args *args)
{
    fe2_session_status_overlay *b = opaque;
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
/* status leaves 0x7ec as it is, so the current list. */
static fe2_object *lookup_object(void *opaque, uint8_t handle)
{
    fe2_session_status_overlay *b = opaque;
    if (handle >= FE2_POOL_SLOTS) { b->session->error = 1; return 0; }
    return fe2_pool_record(b->session->pools.current, handle);
}
/* the records that are neither free nor reusable, highest first. */
typedef struct visit_context { fe2_object_visitor visit; void *context; } visit_context;
static void visit_record(void *context, fe2_object *record, uint16_t status_pair)
{
    visit_context *v = context;
    (void)status_pair;
    v->visit(v->context, record);
}
static void visit_objects(void *opaque, fe2_object_list *list, fe2_object_visitor visit, void *context)
{
    fe2_session_status_overlay *b = opaque;
    visit_context v;
    v.visit = visit; v.context = context;
    /* The visitor (the target-name match) works on the state: no carrying. */
    fe2_pool_visit(&b->session->pools, (fe2_object_pool *)list, visit_record, &v);
}

/* ---- Other overlays (overlay_calls.h) ---------------------------------- */
/* Through the session's table when the provider is registered, the host's
 * provider otherwise. */
static void route_message(void *opaque, uint16_t text, const fe2_call_args *args)
{
    fe2_session_status_overlay *b = opaque;
    fe2_session *s = b->session;
    if (!s->calls.message) { status_host_show_message(opaque, text, args); return; }
    OUT(b); s->calls.message(FE2_OVERLAY_CONTEXT(s, 12), text, args); BACK(b);
}
/* panels (overlay 0x20, +0x20): panel request AX with status's slot as BX (only
 * requests below 2 read it). */
static void route_panel(void *opaque, uint16_t panel, const fe2_call_args *args)
{
    fe2_session_status_overlay *b = opaque;
    fe2_session *s = b->session;
    fe2_call_args block;
    if (!s->calls.panel) { status_host_open_panel(opaque, panel, args); return; }
    if (args) block = *args;
    else fe2_args_clear(&block);
    OUT(b); s->calls.panel(FE2_OVERLAY_CONTEXT(s, 4), panel, STATUS_SLOT, &block); BACK(b);
}

/* ---- Entries ------------------------------------------------------------ */
#define ENTER(context) fe2_session_status_overlay *b = context; (void)s; load(b); ++b->depth
#define LEAVE() do { --b->depth; store(b); } while (0)

static void initialize(void *context, fe2_session *s, uint16_t selection)
{
    ENTER(context); (void)selection; fe2_status_initialize(&b->state); LEAVE();
}
static void activate(void *context, fe2_session *s, uint16_t input)
{
    ENTER(context);
    (void)input;
    b->result = fe2_status_draw_screen(&b->state, &b->services);
    /* The header leaves update off. The idle path of that entry turns the
     * preview  and draws it; the view is cleared every frame, so the
     * draw entry repaints the page at the new angle. */
    if (b->state.screen_active) s->metadata[STATUS_SLOT].update = s->metadata[STATUS_SLOT].draw = 1;
    LEAVE();
}
static void draw(void *context, fe2_session *s)
{
    ENTER(context);
    if (b->state.screen_active) b->result = fe2_status_refresh(&b->state, &b->services);
    LEAVE();
}
static uint32_t event(void *context, fe2_session *s, uint32_t value)
{
    ENTER(context); b->result = fe2_status_event(&b->state, &b->services, (uint16_t)value); LEAVE();
    return value;
}
static void input(void *context, fe2_session *s)
{
    ENTER(context); fe2_status_input(&b->state, &b->services); LEAVE();
}
static void spawn_child(void *context, fe2_session *s, fe2_object *record, uint32_t interval)
{
    ENTER(context); (void)interval; fe2_status_spawn_child(&b->services, record); LEAVE();
}
static void resume(void *context, fe2_session *s)
{
    ENTER(context);
    fe2_status_resume(&b->state, &b->services);
    s->metadata[STATUS_SLOT].update = s->metadata[STATUS_SLOT].draw = 0;
    LEAVE();
}
static void scan(void *context, fe2_session *s, uint32_t interval)
{
    ENTER(context); (void)interval; fe2_status_scan_arrivals(&b->state, &b->services); LEAVE();
}

void fe2_session_status_add_contract(fe2_session_status_overlay *b, const fe2_contract *contract)
{
    load(b); ++b->depth;
    fe2_status_add_contract(&b->state, &b->services, contract);
    --b->depth; store(b);
}
void fe2_session_status_format_text(fe2_session_status_overlay *b, uint16_t selector)
{
    load(b); ++b->depth;
    fe2_status_format_text(&b->services, selector);
    --b->depth; store(b);
}

/* 0x8a (+0x20) as the session's `contract` call: the contract as the
 * argument block gives it (fee 0x7c8, group and kind 0x7cc, destination
 * 0x7c0, deadline 0x7c4, client 0x7e4). */
static void call_contract(void *context, const fe2_call_args *args)
{
    fe2_session_status_overlay *b = context;
    fe2_contract contract;
    if (!args) { b->session->error = 1; return; }
    contract.fee = (uint16_t)args->arg[3].value;
    contract.group = (uint8_t)args->arg[4].value;
    contract.kind = (uint8_t)(args->arg[4].value >> 8);
    contract.destination = args->arg[1].value;
    contract.deadline = args->arg[2].value;
    contract.client = args->arg[10].value;
    fe2_session_status_add_contract(b, &contract);
}

int fe2_session_bind_status_overlay(fe2_session *s, fe2_session_objects *objects, fe2_session_status_overlay *b)
{
    fe2_game_services *v;
    fe2_session_overlay *o;
    if (!s || !objects || objects->session != s || !b || s->busy || s->locks[STATUS_SLOT]) return -1;
    b->session = s; b->objects = objects;
    v = &b->services; v->opaque = b;
    status_forward_all(v);
    v->set_panel_mode = set_panel_mode; v->show_panel = show_panel; v->poll_key = poll_key;
    v->forward_key = forward_key; v->post_event = post_event; v->select_music = select_music;
    v->random = random_word; v->random_below = random_below; v->orient_camera = orient_camera;
    v->spawn_object = spawn_object; v->lookup_object = lookup_object; v->visit_objects = visit_objects;
    v->show_message = route_message; v->open_panel = route_panel;
    o = &s->overlays[STATUS_SLOT];
    o->context = b;
    o->initialize = initialize; o->activate = activate; o->event = event; o->update = input;
    o->draw = draw;
    o->second_record = spawn_child; o->deselect = resume; o->coarse_tick = scan;
    s->calls.context[STATUS_SLOT] = b; s->calls.contract = call_contract;
    return 0;
}
