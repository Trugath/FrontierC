/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "menu_overlays.h"
#include "systems_overlay.h"
#include "status_overlay.h"
#include <string.h>

enum { COMMANDER_SLOT = 10, STATION_SLOT = 12 };

/* station and status view the same structured commander records. Their native
 * structures differ, so copy named fields rather than sharing byte layouts. */
static void station_commander(fe2_session_station_overlay *b, int store)
{
    fe2_commander_status *st;
    unsigned i;
    if (!b->session->calls.contract || !b->session->calls.context[14] ||
        b->session->overlays[14].context != b->session->calls.context[14] || !b->session->overlays[14].initialize) return;
    st = &((fe2_session_status_overlay *)b->session->calls.context[14])->state;
    for (i = 0; i < FE2_STATION_CONTRACTS; ++i) {
        fe2_station_contract *a = &b->state.contracts[i];
        fe2_contract *c = &st->contracts[i];
        if (store) {
            c->fee = a->text; c->group = (uint8_t)a->detail; c->kind = (uint8_t)(a->detail >> 8);
            c->destination = a->value[0]; c->deadline = a->value[1]; c->client = a->seed;
        } else {
            a->text = c->fee; a->detail = (uint16_t)(c->group | (uint16_t)c->kind << 8);
            a->value[0] = c->destination; a->value[1] = c->deadline; a->seed = c->client;
        }
    }
    for (i = 0; i < FE2_STATION_MISSIONS; ++i) {
        fe2_station_mission *a = &b->state.missions[i];
        fe2_military_contract *c = &st->military[i];
        if (store) {
            c->type = a->kind; c->outcome = (int8_t)a->seen;
            c->target_system = a->system; c->target_seed = a->seed; c->deadline = a->due;
            c->target_model = (uint8_t)a->model; c->quality = (uint8_t)(a->model >> 8);
            c->reward = a->reward; memcpy(c->target_name, a->name, sizeof c->target_name); c->home_system = a->origin;
        } else {
            a->kind = c->type; a->seen = (uint8_t)c->outcome;
            a->system = c->target_system; a->seed = c->target_seed; a->due = c->deadline;
            a->model = (uint16_t)(c->target_model | (uint16_t)c->quality << 8);
            a->reward = c->reward; memcpy(a->name, c->target_name, sizeof a->name); a->origin = c->home_system;
        }
    }
    for (i = 0; i < FE2_STATION_PASSENGERS; ++i) {
        if (store) {
            st->crew[i].name_seed = b->state.passengers[i].seed;
            st->crew[i].salary = (int16_t)b->state.passengers[i].fare;
        } else {
            b->state.passengers[i].seed = st->crew[i].name_seed;
            b->state.passengers[i].fare = (uint16_t)st->crew[i].salary;
        }
    }
}

/* ---- commander --------------------------------------------------------------- */
#include "commander_resident.h"
static void commander_load(fe2_session_commander_overlay *b) { commander_resident_load(b->session, &b->objects->state, &b->state); }
static void commander_store(fe2_session_commander_overlay *b) { commander_resident_store(b->session, &b->objects->state, &b->state); }
static void commander_out(fe2_session_commander_overlay *b) { if (b->depth) commander_store(b); }
static void commander_back(fe2_session_commander_overlay *b) { commander_load(b); }
#define COMMANDER_OUT(b) commander_out(b)
#define COMMANDER_BACK(b) commander_back(b)

static void commander_missing(fe2_session_commander_overlay *b) { b->session->error = 1; }
#include "commander_forward.h"

/* Calls into other overlays: through the session's table when the
 * provider is registered, the host's provider otherwise. */
static void commander_select_panel(void *opaque, uint16_t request, const fe2_call_args *args)
{
    fe2_session_commander_overlay *b = opaque;
    fe2_overlay_calls *calls = &b->session->calls;
    fe2_call_args block;
    if (!calls->panel) { commander_host_select_panel(opaque, request, args); return; }
    if (args) block = *args;
    else fe2_args_clear(&block);
    /* BX (the calling overlay) only matters below request 2. */
    COMMANDER_OUT(b); calls->panel(calls->context[4], request, COMMANDER_SLOT, &block); COMMANDER_BACK(b);
}
static void commander_refresh_cockpit(void *opaque)
{
    fe2_session_commander_overlay *b = opaque;
    fe2_overlay_calls *calls = &b->session->calls;
    if (!calls->flight_view) { commander_host_refresh_cockpit(opaque); return; }
    COMMANDER_OUT(b); calls->flight_view(calls->context[3]); COMMANDER_BACK(b);
}
static void commander_show_message(void *opaque, uint16_t text, const fe2_call_args *args)
{
    fe2_session_commander_overlay *b = opaque;
    fe2_overlay_calls *calls = &b->session->calls;
    if (!calls->message) { commander_host_show_message(opaque, text, args); return; }
    COMMANDER_OUT(b); calls->message(calls->context[STATION_SLOT], text, args); COMMANDER_BACK(b);
}

/* commander's entries for the other overlays: the time-step reset (0x8a) for
 * flight, restoring the time step for station (0x50) and people's check (AL 0xff when
 * the step changed, which sounds the alarm). */
static int commander_call_restore(fe2_session_commander_overlay *b)
{
    int changed;
    commander_load(b); ++b->depth;
    changed = fe2_commander_restore_time_step(&b->state, &b->services);
    --b->depth; commander_store(b);
    return changed;
}
static void commander_call_reset(void *context) { (void)commander_call_restore(context); }
static uint8_t commander_call_check(void *context) { return commander_call_restore(context) ? 0xff : 0; }

static void commander_set_panel_mode(void *opaque, uint16_t mode)
{
    fe2_session_commander_overlay *b = opaque;
    COMMANDER_OUT(b); fe2_session_objects_select_overlay(b->objects, mode); COMMANDER_BACK(b);
}
static int commander_poll_key(void *opaque, uint8_t *key)
{
    fe2_session_commander_overlay *b = opaque;
    uint16_t code;
    COMMANDER_OUT(b); code = fe2_session_poll(b->session); COMMANDER_BACK(b);
    *key = (uint8_t)code;
    return code != 0;
}
static void commander_forward_key(void *opaque, uint8_t key)
{
    fe2_session_commander_overlay *b = opaque;
    fe2_input_restore(&b->session->pointer, key);
}
static void commander_show_panel(void *opaque)
{
    fe2_session_commander_overlay *b = opaque;
    fe2_pointer_regions_reset(&b->session->regions);
}
static void append_words(fe2_session *s, const uint16_t *words)
{
    fe2_pointer_region stream[FE2_POINTER_REGIONS + 1];
    size_t n = 0;
    do {
        const uint16_t *w = words + 5 * n;
        stream[n].left = w[0]; stream[n].top = w[1]; stream[n].right = w[2]; stream[n].bottom = w[3];
        stream[n].id = w[4];
        ++n;
    } while (n <= FE2_POINTER_REGIONS && words[5 * n] != FE2_POINTER_REGION_END);
    if (fe2_pointer_regions_append(&s->regions, stream, n) < 0) s->error = 1;
}
static void commander_set_widgets(void *opaque, const uint16_t *words)
{
    fe2_session_commander_overlay *b = opaque;
    append_words(b->session, words);
}
/* AX, BX the center, CX and DI the extents, DX the ID. */
static void commander_add_hotspot(void *opaque, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t event)
{
    fe2_session_commander_overlay *b = opaque;
    if (fe2_pointer_regions_add(&b->session->regions, x, y, width, height, event)) b->session->error = 1;
}
static void commander_post_event(void *opaque, uint16_t event)
{
    fe2_session_commander_overlay *b = opaque;
    COMMANDER_OUT(b);
    if (fe2_session_broadcast(b->session, event)) b->session->error = 1;
    COMMANDER_BACK(b);
}
/* JMP FAR [0x44] after a load: the commander's image is the game's memory
 * in the original. The host's provider makes the native game the image
 * first, then the session restarts. */
static void commander_restart_game(void *opaque)
{
    fe2_session_commander_overlay *b = opaque;
    COMMANDER_OUT(b);
    if (b->host.restart_game) b->host.restart_game(b->host.opaque);
    if (fe2_session_restart(b->session)) b->session->error = 1;
    COMMANDER_BACK(b);
}
static void commander_quit_game(void *opaque)
{
    fe2_session_commander_overlay *b = opaque;
    COMMANDER_OUT(b);
    if (fe2_session_exit_scene(b->session, 0)) b->session->error = 1;
    COMMANDER_BACK(b);
}
static void commander_check_music(void *opaque)
{
    fe2_session_commander_overlay *b = opaque;
    if (b->session->services.check_music) b->session->services.check_music(b->session->services.context);
}
static void commander_select_music(void *opaque, uint16_t track)
{
    fe2_session_commander_overlay *b = opaque;
    COMMANDER_OUT(b);
    if (b->session->services.select_music)
        b->session->services.select_music(b->session->services.context, (int16_t)track);
    COMMANDER_BACK(b);
}
static void commander_stop_effects(void *opaque)
{
    fe2_session_commander_overlay *b = opaque;
    if (b->session->services.stop_effects) b->session->services.stop_effects(b->session->services.context);
}
static void commander_reset_music(void *opaque)
{
    fe2_session_commander_overlay *b = opaque;
    if (b->session->services.reset_music) b->session->services.reset_music(b->session->services.context);
}

#define COMMANDER_ENTER(context) fe2_session_commander_overlay *b = context; (void)s; commander_load(b); ++b->depth
#define COMMANDER_LEAVE() do { --b->depth; commander_store(b); } while (0)
static void commander_initialize(void *context, fe2_session *s, uint16_t selection)
{
    COMMANDER_ENTER(context); (void)fe2_commander_initialize(&b->state, &b->services, selection); COMMANDER_LEAVE();
}
static void commander_activate(void *context, fe2_session *s, uint16_t input)
{
    COMMANDER_ENTER(context); (void)input; (void)fe2_commander_draw_if_paused(&b->state, &b->services); COMMANDER_LEAVE();
}
static void commander_update(void *context, fe2_session *s)
{
    COMMANDER_ENTER(context); (void)fe2_commander_time_input(&b->state, &b->services); COMMANDER_LEAVE();
}
static void commander_draw(void *context, fe2_session *s)
{
    COMMANDER_ENTER(context); (void)fe2_commander_menu_input(&b->state, &b->services); COMMANDER_LEAVE();
}
static void commander_message(void *context, fe2_session *s, const fe2_objects_message *m)
{
    COMMANDER_ENTER(context); (void)m; (void)fe2_commander_restore_time_step(&b->state, &b->services); COMMANDER_LEAVE();
}
static void commander_deselect(void *context, fe2_session *s)
{
    COMMANDER_ENTER(context); fe2_commander_resume(&b->state, &b->services); COMMANDER_LEAVE();
}

int fe2_session_bind_commander_overlay(fe2_session *s, fe2_session_objects *objects, fe2_session_commander_overlay *b)
{
    fe2_commander_services *v;
    fe2_session_overlay *o;
    if (!s || !objects || objects->session != s || !b || s->busy || s->locks[COMMANDER_SLOT]) return -1;
    b->session = s; b->objects = objects;
    v = &b->services; v->opaque = b; v->dos = b->host.dos;
    commander_forward_all(v);
    v->set_panel_mode = commander_set_panel_mode; v->poll_key = commander_poll_key; v->forward_key = commander_forward_key;
    v->show_panel = commander_show_panel; v->set_widgets = commander_set_widgets; v->add_hotspot = commander_add_hotspot;
    v->post_event = commander_post_event; v->restart_game = commander_restart_game; v->quit_game = commander_quit_game;
    v->check_music = commander_check_music; v->select_music = commander_select_music; v->stop_effects = commander_stop_effects;
    v->reset_music = commander_reset_music;
    v->select_panel = commander_select_panel; v->refresh_cockpit = commander_refresh_cockpit; v->show_message = commander_show_message;
    s->calls.context[COMMANDER_SLOT] = b;
    s->calls.menu_reset = commander_call_reset; s->calls.menu_restore_time = commander_call_reset;
    s->calls.menu_check = commander_call_check;
    o = &s->overlays[COMMANDER_SLOT];
    o->context = b; o->initialize = commander_initialize; o->activate = commander_activate; o->update = commander_update;
    o->draw = commander_draw; o->message = commander_message; o->deselect = commander_deselect;
    return 0;
}

/* ---- station --------------------------------------------------------------- */
#include "station_resident.h"
static void station_load(fe2_session_station_overlay *b)
{
    station_resident_load(b->session, &b->objects->state, &b->state);
    station_commander(b, 0);
    /* is the player's ship. The generated sync leaves the reference
     * empty, so the in-flight list named no ship. The reference is the
     * pool handle: a native pointer does not fit in the 32-bit record id. */
    b->state.ship = b->objects->state.player;
    /*, systems's station count, is station's search counter. A registered
     * system call with no systems binding (a test stand-in) has no generation. */
    if (b->session->calls.system && b->session->calls.context[0])
        b->state.search_counter = (int8_t)((fe2_session_systems_overlay *)b->session->calls.context[0])
                                      ->generation.station_count;
    /*, the second pool, as station names it. */
    b->state.parked_ref = FE2_STATION_REF_MAP_LIST;
    b->state.refs.preview_name = FE2_STATION_REF_PREVIEW_NAME;
    /* is the station (flight's flight reference), named by its handle. */
    if (b->session->resident.reference_object) b->state.station = b->session->resident.reference_object->handle;
}
static void station_store(fe2_session_station_overlay *b)
{
    fe2_object *station;
    station_resident_store(b->session, &b->objects->state, &b->state);
    station_commander(b, 1);
    if (b->session->calls.system && b->session->calls.context[0])
        ((fe2_session_systems_overlay *)b->session->calls.context[0])->generation.station_count =
            (uint8_t)b->state.search_counter;
    /* takes a record as the station. */
    station = b->state.station < FE2_POOL_SLOTS ? b->session->objects.records[b->state.station] : NULL;
    if (station && station != b->session->resident.reference_object) b->session->resident.reference_object = station;
}
static void station_out(fe2_session_station_overlay *b) { if (b->depth) station_store(b); }
static void station_back(fe2_session_station_overlay *b) { station_load(b); }
#define ST_OUT(b) station_out(b)
#define ST_BACK(b) station_back(b)

static void station_missing(fe2_session_station_overlay *b) { b->session->error = 1; }
#include "station_forward.h"

/* Calls into other overlays: through the session's table when the
 * provider is registered, the host's provider otherwise. The entry sees
 * a copy of the parameter block and leaves its results in it; when the
 * block is station's own, the results go back into it. */
#define ST_CALLS(b) (&(b)->session->calls)
static void st_block_in(fe2_call_args *block, const fe2_call_args *args)
{
    if (args) *block = *args;
    else fe2_args_clear(block);
}
static void st_block_out(fe2_session_station_overlay *b, const fe2_call_args *block, const fe2_call_args *args)
{
    if (args == &b->args) b->args = *block;
}
static void st_systems_system(void *opaque, uint32_t *seed_a, uint32_t *seed_b, const fe2_call_args *args)
{
    fe2_session_station_overlay *b = opaque;
    fe2_overlay_calls *calls = ST_CALLS(b);
    fe2_overlay_system system = {0};
    fe2_call_args block;
    /* station's work records are references the host resolves (work_record);
     * systems needs the records themselves in parameter 8, so a reference
     * stays with the host. */
    if (!calls->system || !args || !args->arg[8].ref) {
        station_host_systems_system(opaque, seed_a, seed_b, args);
        return;
    }
    system.seed_a = *seed_a; system.seed_b = *seed_b;
    st_block_in(&block, args);
    ST_OUT(b); calls->system(calls->context[0], &system, &block); ST_BACK(b);
    st_block_out(b, &block, args);
    *seed_a = system.seed_a; *seed_b = system.seed_b;
}
static uint16_t st_galactic_map_system(void *opaque, uint16_t request, const fe2_call_args *args)
{
    fe2_session_station_overlay *b = opaque;
    fe2_overlay_calls *calls = ST_CALLS(b);
    fe2_call_args block;
    uint16_t result;
    if (!calls->map) return station_host_galactic_map_system(opaque, request, args);
    st_block_in(&block, args);
    ST_OUT(b); result = calls->map(calls->context[2], request, &block, NULL); ST_BACK(b);
    st_block_out(b, &block, args);
    return result;
}
static void st_flight_activate_view(void *opaque)
{
    fe2_session_station_overlay *b = opaque;
    fe2_overlay_calls *calls = ST_CALLS(b);
    if (!calls->flight_view) { station_host_flight_activate_view(opaque); return; }
    ST_OUT(b); calls->flight_view(calls->context[3]); ST_BACK(b);
}
static void st_flight_reset_capacity(void *opaque)
{
    fe2_session_station_overlay *b = opaque;
    fe2_overlay_calls *calls = ST_CALLS(b);
    if (!calls->flight_reset_capacity) { station_host_flight_reset_capacity(opaque); return; }
    ST_OUT(b); calls->flight_reset_capacity(calls->context[3]); ST_BACK(b);
}
static void st_panels_panel(void *opaque, uint16_t request, uint16_t view, const fe2_call_args *args)
{
    fe2_session_station_overlay *b = opaque;
    fe2_overlay_calls *calls = ST_CALLS(b);
    fe2_call_args block;
    if (!calls->panel) { station_host_panels_panel(opaque, request, view, args); return; }
    st_block_in(&block, args);
    ST_OUT(b); calls->panel(calls->context[4], request, view, &block); ST_BACK(b);
    st_block_out(b, &block, args);
}
/* market's reply: AX, the record at DS:DI, and 0x7d8 (parameter 7). */
static fe2_station_market_reply st_market_market(void *opaque, uint16_t request, uint8_t handle,
                                              const fe2_call_args *args)
{
    fe2_session_station_overlay *b = opaque;
    fe2_overlay_calls *calls = ST_CALLS(b);
    fe2_station_market_reply reply;
    fe2_overlay_record record = {0};
    fe2_call_args block;
    unsigned n;
    if (!calls->market) return station_host_market_market(opaque, request, handle, args);
    st_block_in(&block, args);
    /* station names the actor and target (parameters 8, 9) by reference; market
     * reads the records. */
    for (n = 8; n <= 9; ++n)
        if (!block.arg[n].ref && block.arg[n].kind != FE2_ARG_UNSET)
            block.arg[n].ref = station_host_object(opaque, block.arg[n].value);
    ST_OUT(b); reply.result = calls->market(calls->context[7], request, handle, &block, &record); ST_BACK(b);
    st_block_out(b, &block, args);
    reply.market = record.record; reply.ref = record.ref;
    reply.value = block.arg[7].value;
    return reply;
}
/* The name into `out`, with the work record the original keeps it in. */
static void st_names_name(void *opaque, uint16_t type, uint32_t seed, enum fe2_station_work buffer,
                        fe2_station_name *out)
{
    fe2_session_station_overlay *b = opaque;
    fe2_overlay_calls *calls = ST_CALLS(b);
    if (!calls->name) { station_host_names_name(opaque, type, seed, buffer, out); return; }
    memset(out, 0, sizeof(*out));
    ST_OUT(b); calls->name(calls->context[9], type, seed, out->text, sizeof(out->text)); ST_BACK(b);
    out->text[sizeof(out->text) - 1] = 0;
    if (buffer == FE2_STATION_WORK_REGISTRATION || buffer == FE2_STATION_WORK_SHIP_NAME)
        memcpy(b->names[buffer == FE2_STATION_WORK_SHIP_NAME], out->text, sizeof(out->text));
    if (b->host.work_record) out->ref = station_host_work_record(opaque, buffer);
}
static void st_commander_restore_time(void *opaque)
{
    fe2_session_station_overlay *b = opaque;
    fe2_overlay_calls *calls = ST_CALLS(b);
    if (!calls->menu_restore_time) { station_host_commander_restore_time(opaque); return; }
    ST_OUT(b); calls->menu_restore_time(calls->context[10]); ST_BACK(b);
}
static void st_composite_portrait(void *opaque, uint32_t identity, const fe2_call_args *args)
{
    fe2_session_station_overlay *b = opaque;
    fe2_overlay_calls *calls = ST_CALLS(b);
    if (!calls->portrait) { station_host_composite_portrait(opaque, identity, args); return; }
    ST_OUT(b); calls->portrait(calls->context[13], identity, args); ST_BACK(b);
}
static void st_status_contract(void *opaque, const fe2_call_args *args)
{
    fe2_session_station_overlay *b = opaque;
    fe2_overlay_calls *calls = ST_CALLS(b);
    if (!calls->contract) { station_host_status_contract(opaque, args); return; }
    ST_OUT(b); calls->contract(calls->context[14], args); ST_BACK(b);
}
static void st_offences_police(void *opaque, uint16_t request, const fe2_call_args *args)
{
    fe2_session_station_overlay *b = opaque;
    fe2_overlay_calls *calls = ST_CALLS(b);
    if (!calls->police) { station_host_offences_police(opaque, request, args); return; }
    ST_OUT(b); calls->police(calls->context[15], request, args); ST_BACK(b);
}
static void st_hyperspace_ship(void *opaque, uint16_t request, uint16_t model, const fe2_call_args *args)
{
    fe2_session_station_overlay *b = opaque;
    fe2_overlay_calls *calls = ST_CALLS(b);
    fe2_call_args block;
    if (!calls->drive) { station_host_hyperspace_ship(opaque, request, model, args); return; }
    st_block_in(&block, args);
    ST_OUT(b); (void)calls->drive(calls->context[16], request, model, &block); ST_BACK(b);
    st_block_out(b, &block, args);
}
static uint16_t st_mining_service(void *opaque, uint16_t request, const fe2_call_args *args)
{
    fe2_session_station_overlay *b = opaque;
    fe2_overlay_calls *calls = ST_CALLS(b);
    fe2_call_args block;
    uint16_t result;
    if (!calls->mining) return station_host_mining_service(opaque, request, args);
    st_block_in(&block, args);
    ST_OUT(b); result = calls->mining(calls->context[17], request, &block); ST_BACK(b);
    st_block_out(b, &block, args);
    return result;
}
static void st_interaction_text_text(void *opaque, uint16_t selector, fe2_text_out *out)
{
    fe2_session_station_overlay *b = opaque;
    fe2_overlay_calls *calls = ST_CALLS(b);
    if (!calls->interaction_text) { station_host_interaction_text_text(opaque, selector, out); return; }
    ST_OUT(b); calls->interaction_text(calls->context[18], selector, out); ST_BACK(b);
}
/* station's own entry for the others: show message `text`. */
static void st_call_message(void *context, uint16_t text, const fe2_call_args *args)
{
    fe2_session_station_notify(context, text, args);
}

static void st_select_controls(void *opaque, uint16_t mode)
{
    fe2_session_station_overlay *b = opaque;
    ST_OUT(b); fe2_session_objects_select_overlay(b->objects, mode); ST_BACK(b);
}
static void st_set_widgets(void *opaque, const uint16_t *words)
{
    fe2_session_station_overlay *b = opaque;
    append_words(b->session, words);
}
static void st_show_panel(void *opaque)
{
    fe2_session_station_overlay *b = opaque;
    fe2_pointer_regions_reset(&b->session->regions);
}
static void st_clear_regions(void *opaque)
{
    fe2_session_station_overlay *b = opaque;
    fe2_pointer_regions_clear(&b->session->regions);
}
static int st_poll_key(void *opaque, uint16_t *key)
{
    fe2_session_station_overlay *b = opaque;
    ST_OUT(b); *key = fe2_session_poll(b->session); ST_BACK(b);
    return *key != 0;
}
static void st_forward_key(void *opaque, uint16_t key)
{
    fe2_session_station_overlay *b = opaque;
    fe2_input_restore(&b->session->pointer, (uint8_t)key);
}
/* remove the region with this ID. */
static void st_unregister_key(void *opaque, uint16_t key)
{
    fe2_session_station_overlay *b = opaque;
    fe2_pointer_regions_remove(&b->session->regions, key);
}
static void st_post_event(void *opaque, uint16_t event)
{
    fe2_session_station_overlay *b = opaque;
    ST_OUT(b);
    if (fe2_session_broadcast(b->session, event)) b->session->error = 1;
    ST_BACK(b);
}
static void st_restart_session(void *opaque, uint16_t mode)
{
    fe2_session_station_overlay *b = opaque;
    ST_OUT(b);
    if (fe2_session_exit_scene(b->session, mode)) b->session->error = 1;
    ST_BACK(b);
}
static uint16_t st_random_below(void *opaque, uint16_t bound)
{
    fe2_session_station_overlay *b = opaque;
    uint16_t value;
    ST_OUT(b); value = fe2_session_objects_random_below(b->objects, bound); ST_BACK(b);
    return value;
}

#define ST_ENTER(context) fe2_session_station_overlay *b = context; (void)s; station_load(b); ++b->depth
#define ST_LEAVE() do { --b->depth; station_store(b); } while (0)
static void st_initialize(void *context, fe2_session *s, uint16_t selection)
{
    ST_ENTER(context); (void)selection; fe2_station_initialize(&b->state, &b->args); ST_LEAVE();
}
static void st_activate(void *context, fe2_session *s, uint16_t input)
{
    ST_ENTER(context);
    (void)input;
    /* The in-flight list is drawn only while the cockpit flag is negative.
     * Commander status deselects the flight overlay, which sets that flag
     * to 0x7f, so the list is built and then discarded and the status page
     * keeps painting. The flight view entry releases that page and brings
     * the cockpit back. A docked phone selects its own overlay instead. */
    if (b->state.view_mode >= 0 &&
        b->state.flight_state != FE2_STATION_MODE_BERTH_A &&
        b->state.flight_state != FE2_STATION_MODE_BERTH_B) {
        fe2_overlay_calls *calls = ST_CALLS(b);
        if (calls->flight_view) {
            ST_OUT(b); calls->flight_view(calls->context[3]); ST_BACK(b);
        }
    }
    fe2_station_draw(&b->state, &b->services, &b->args);
    if (b->state.screen_active || (b->state.ui_flags & 1u)) s->metadata[STATION_SLOT].update = 1;
    ST_LEAVE();
}
static uint32_t st_event(void *context, fe2_session *s, uint32_t value)
{
    ST_ENTER(context); fe2_station_event(&b->state, &b->services, &b->args, (uint16_t)value); ST_LEAVE();
    return value;
}
static void st_update(void *context, fe2_session *s)
{
    ST_ENTER(context); fe2_station_input(&b->state, &b->services, &b->args); ST_LEAVE();
}
static void st_draw(void *context, fe2_session *s)
{
    ST_ENTER(context);
    fe2_station_select(&b->state, &b->services, &b->args);
    fe2_station_refresh(&b->state, &b->services, &b->args);
    if (!b->state.screen_active && !(b->state.ui_flags & 1u)) s->metadata[STATION_SLOT].update = 0;
    ST_LEAVE();
}
/* The message's selector in AX; its parameter, value  and
 * amount  are parameters 7, 1 and 3. */
static void st_message(void *context, fe2_session *s, const fe2_objects_message *m)
{
    ST_ENTER(context);
    if (m) {
        fe2_args_clear(&b->args);
        if (m->uses & FE2_OBJECTS_USES_PARAMETER) fe2_args_word(&b->args, 7, m->parameter);
        if (m->uses & FE2_OBJECTS_USES_VALUE) fe2_args_word(&b->args, 1, m->value);
        if (m->uses & FE2_OBJECTS_USES_AMOUNT) fe2_args_dword(&b->args, 3, m->amount);
        fe2_station_show_message(&b->state, &b->services, &b->args, m->selector);
    }
    ST_LEAVE();
}
static void st_deselect(void *context, fe2_session *s)
{
    ST_ENTER(context); (void)fe2_station_resume(&b->state, &b->services, &b->args, 0); ST_LEAVE();
}

uint16_t fe2_session_station_resume(fe2_session_station_overlay *b, int16_t request)
{
    uint16_t result;
    station_load(b); ++b->depth;
    result = fe2_station_resume(&b->state, &b->services, &b->args, request);
    --b->depth; station_store(b);
    return result;
}

void fe2_session_station_notify(fe2_session_station_overlay *b, uint16_t text,
                                const fe2_call_args *args)
{
    if (!b || !b->session) return;
    station_load(b);
    ++b->depth;
    if (args) b->args = *args;
    else fe2_args_clear(&b->args);
    fe2_station_show_message(&b->state, &b->services, &b->args, text);
    --b->depth;
    station_store(b);
}

int fe2_session_bind_station_overlay(fe2_session *s, fe2_session_objects *objects, fe2_session_station_overlay *b)
{
    fe2_station_services *v;
    fe2_session_overlay *o;
    if (!s || !objects || objects->session != s || !b || s->busy || s->locks[STATION_SLOT]) return -1;
    b->session = s; b->objects = objects;
    v = &b->services; v->opaque = b;
    station_forward_all(v);
    v->select_controls = st_select_controls; v->set_widgets = st_set_widgets; v->show_panel = st_show_panel;
    v->clear_regions = st_clear_regions; v->poll_key = st_poll_key; v->forward_key = st_forward_key;
    v->unregister_key = st_unregister_key; v->post_event = st_post_event; v->restart_session = st_restart_session;
    v->random_below = st_random_below;
    v->systems_system = st_systems_system; v->galactic_map_system = st_galactic_map_system; v->flight_activate_view = st_flight_activate_view;
    v->flight_restore_capacity = st_flight_reset_capacity; v->panels_panel = st_panels_panel; v->market_market = st_market_market;
    v->names_name = st_names_name; v->commander_restore_time = st_commander_restore_time; v->composite_portrait = st_composite_portrait;
    v->status_contract = st_status_contract; v->offences_police = st_offences_police; v->hyperspace_ship = st_hyperspace_ship;
    v->mining_service = st_mining_service; v->interaction_text_text = st_interaction_text_text;
    s->calls.context[STATION_SLOT] = b; s->calls.message = st_call_message;
    o = &s->overlays[STATION_SLOT];
    /* is 0x993b in the image: the arrest notice for an unpaid fine. */
    b->state.selected_text = 0x993b;
    o->context = b; o->initialize = st_initialize; o->activate = st_activate; o->event = st_event;
    o->update = st_update; o->draw = st_draw; o->message = st_message; o->deselect = st_deselect;
    return 0;
}
