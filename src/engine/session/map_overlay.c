/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "map_overlay.h"
#include <string.h>

enum {
    MAP_SLOT = 2, CATALOGUE_MODULE = 6, DRIVE_MODULE = 16, EVENT_PROMOTE = 1, EVENT_PERTURB = 0x14,
    /* The overlay call's parameters (0x7bc + 4n). */
    P_RESULT = 0, P_SYSTEM = 1, P_TABLE = 2, P_CLASS = 3, P_INDEX = 4, P_REFERENCE = 6,
    P_STATUS = 7, P_GOVERNMENT = 7, P_SHIP = 8, P_NAME = 10,
    /* drives's travel query (AX) and catalogue's profile request (AL). */
    DRIVE_TRAVEL = 1, CATALOGUE_PROFILE = 0,
    MAP_CACHED = 0, MAP_CANDIDATE = 1, MAP_DISTANCE = 2,
    KEY_LEFT_SHIFT = 0x2a, KEY_RIGHT_SHIFT = 0x36, KEY_ALT = 0x38
};

/* ---- The shared globals ------------------------------------------------ */
#include "map_resident.h"
static void load(fe2_session_map_overlay *b)
{
    map_resident_load(b->session, &b->objects->state, &b->state);
    b->state.ship = b->objects->state.player_record;
    b->state.current_system = b->session->resident.current_system;
}
static void store(fe2_session_map_overlay *b)
{
    map_resident_store(b->session, &b->objects->state, &b->state);
    b->objects->state.player_record = b->state.ship;
    b->session->resident.current_system = b->state.current_system;
}
/* Around native services and host calls: the map state goes out first
 * when chart is running and comes back after. */
static void map_out(fe2_session_map_overlay *b) { if (b->depth) store(b); }
static void map_back(fe2_session_map_overlay *b) { load(b); }

/* Host providers: every chart provider has a result, so a missing one fails
 * the frame. */
static void map_missing(fe2_session_map_overlay *b) { b->session->error = 1; }
#include "map_forward.h"

#define OUT(b) map_out(b)
#define BACK(b) map_back(b)

/* ---- Native services --------------------------------------------------- */

static int set_panel_mode(void *opaque, uint16_t mode)
{
    fe2_session_map_overlay *b = opaque;
    OUT(b); fe2_session_objects_select_overlay(b->objects, mode); BACK(b);
    return 0;
}
/* EAX. */
static int random_word(void *opaque, uint32_t *value)
{
    fe2_session_map_overlay *b = opaque;
    uint32_t mix;
    OUT(b); *value = fe2_session_objects_random(b->objects, &mix); BACK(b);
    return 0;
}

static int random_below(void *opaque, uint16_t bound, uint16_t *value)
{
    fe2_session_map_overlay *b = opaque;
    OUT(b); *value = fe2_session_objects_random_below(b->objects, bound); BACK(b);
    return 0;
}
/* The density passes stop on a pending input code. */
static int input_pending(void *opaque)
{
    fe2_session_map_overlay *b = opaque;
    return b->session->pointer.input_code != 0;
}
/* an unhandled command goes back to the input latch. */
static int forward_command(void *opaque, uint8_t command)
{
    fe2_session_map_overlay *b = opaque;
    fe2_input_restore(&b->session->pointer, command);
    return 0;
}

/* Other overlays through the session's overlay calls when registered. */
/* drives (AX=1, 0x7c0 the distance, 0x7dc the ship): the fuel in 0x7cc,
 * negative when 0x7bc (the mode) is. */
static int travel(void *opaque, const fe2_object *ship, uint32_t distance, uint16_t *result, int *negative)
{
    fe2_session_map_overlay *b = opaque;
    fe2_session *s = b->session;
    fe2_call_args args;
    if (!s->calls.drive) return map_host_travel(opaque, ship, distance, result, negative);
    fe2_args_clear(&args);
    fe2_args_dword(&args, P_SYSTEM, distance);
    fe2_args_object(&args, P_SHIP, ship);
    OUT(b); (void)s->calls.drive(FE2_OVERLAY_CONTEXT(s, DRIVE_MODULE), DRIVE_TRAVEL, 0, &args); BACK(b);
    *result = (uint16_t)args.arg[P_INDEX].value;
    *negative = (int16_t)args.arg[P_RESULT].value < 0;
    return 0;
}
/* catalogue AL=0 (0x7c0 the system): the status, the two texts (0x7c0,
 * 0x7c4) and the government. Mode 1 answers in registers the
 * overlay call does not carry, so it stays the host's. */
static int system_details(void *opaque, uint32_t reference, unsigned mode, fe2_map_details *details)
{
    fe2_session_map_overlay *b = opaque;
    fe2_session *s = b->session;
    fe2_call_args args;
    if (!s->calls.catalogue || mode != CATALOGUE_PROFILE)
        return map_host_system_details(opaque, reference, mode, details);
    fe2_args_clear(&args);
    fe2_args_dword(&args, P_SYSTEM, reference);
    OUT(b); (void)s->calls.catalogue(FE2_OVERLAY_CONTEXT(s, CATALOGUE_MODULE), CATALOGUE_PROFILE, &args, NULL); BACK(b);
    memset(details, 0, sizeof(*details));
    details->status = (uint8_t)args.arg[P_RESULT].value;
    details->result_a = (uint16_t)args.arg[P_SYSTEM].value;
    details->result_b = (uint16_t)args.arg[P_TABLE].value;
    details->government = (uint16_t)args.arg[P_GOVERNMENT].value;
    return 0;
}

/* ---- Input --------------------------------------------------------------- */
/* Key states are zero while pressed. Each pan direction has four keys
 * (0x8708 + scan code; E0 codes +0x80): any pressed one pans, unless Alt is
 * down; either Shift selects the slow step. 0x48 supplies the command. */
static int any_down(const uint8_t *keys, const uint8_t codes[4])
{
    return !(keys[codes[0]] & keys[codes[1]] & keys[codes[2]] & keys[codes[3]]);
}
static void read_input(fe2_session_map_overlay *b, fe2_map_input *input)
{
    static const uint8_t pans[4][4] = {
        {0x4b, 0x2b, 0xcb, 0xab}, {0x4d, 0x4e, 0xcd, 0xce},
        {0x48, 0x29, 0xc8, 0xa9}, {0x50, 0x4a, 0xd0, 0xca}
    };
    static const uint8_t bits[4] = {
        FE2_MAP_PAN_Y_MINUS, FE2_MAP_PAN_Y_PLUS, FE2_MAP_PAN_X_PLUS, FE2_MAP_PAN_X_MINUS
    };
    const uint8_t *keys = b->session->input.keys;
    uint16_t code;
    unsigned i;
    memset(input, 0, sizeof(*input));
    input->fast = !(keys[KEY_LEFT_SHIFT] & keys[KEY_RIGHT_SHIFT]);
    if (keys[KEY_ALT])
        for (i = 0; i < 4; ++i) if (any_down(keys, pans[i])) input->pan |= bits[i];
    OUT(b); code = fe2_session_poll(b->session); BACK(b);
    input->command = (uint8_t)code;
    input->has_command = code != 0;
}

/* ---- Entries ------------------------------------------------------------ */
#define ENTER(context) fe2_session_map_overlay *b = context; (void)s; load(b); ++b->depth
#define LEAVE() do { --b->depth; store(b); if (b->status < 0) b->session->error = 1; } while (0)

static void initialize(void *context, fe2_session *s, uint16_t selection)
{
    ENTER(context);
    b->status = (selection & 0xff) == 1 ? fe2_map_initialize(&b->state, &b->services, selection >> 8) : 0;
    LEAVE();
}
static void activate(void *context, fe2_session *s, uint16_t input)
{
    ENTER(context);
    (void)input;
    b->status = fe2_map_activate(&b->state, &b->services);
    /* The original leaves update and draw disabled. The session paints the
     * 3D view after overlay updates, so the starfield has to be drawn again
     * in the draw phase while the map is the current view. */
    if (!b->status && b->state.mode < 0) {
        s->metadata[MAP_SLOT].update = 1;
        s->metadata[MAP_SLOT].draw = 1;
    }
    LEAVE();
}
static uint32_t event(void *context, fe2_session *s, uint32_t value)
{
    ENTER(context);
    b->status = 0;
    if ((uint16_t)value == EVENT_PROMOTE) fe2_map_promote_selection(&b->state);
    else if ((uint16_t)value == EVENT_PERTURB) b->status = fe2_map_perturb_home(&b->state, &b->services, &b->range);
    LEAVE();
    return value;
}
static void update(void *context, fe2_session *s)
{
    fe2_map_input input;
    ENTER(context);
    read_input(b, &input);
    b->status = fe2_map_update(&b->state, &b->services, &input);
    LEAVE();
}
static void draw(void *context, fe2_session *s)
{
    ENTER(context);
    b->status = 0;
    if (b->state.mode < 0) {
        /* The home marker reads the live jump range (0x20e5) and scales it. */
        b->state.marker_range = s->resident.jump_range;
        b->status = fe2_map_draw(&b->state, &b->services);
        if (!b->status && b->state.selection_status)
            b->status = fe2_map_draw_selection(&b->state, &b->services);
        if (!b->status) b->status = fe2_map_draw_status(&b->state, &b->services);
    }
    LEAVE();
}
static void deselect(void *context, fe2_session *s)
{
    int changed;
    ENTER(context);
    b->status = fe2_map_enter_view(&b->state, &b->services, 0, &changed);
    s->metadata[MAP_SLOT].update = 0;
    s->metadata[MAP_SLOT].draw = 0;
    LEAVE();
}

/* ---- The overlay call (+0x20, 0x8a) ---------------------------------------- */
/* Request 0: the selected system, its seeds (DX:AX, CX:BX) and name (ES:SI)
 * into `system`, its classification, reference  and status
 *  into the parameters; AX the low seed word. */
static uint16_t cached_system(fe2_session_map_overlay *b, fe2_call_args *args, fe2_overlay_system *system)
{
    fe2_map_query q;
    fe2_map_cached_selection(&b->state, &q);
    if (system) {
        system->seed_a = q.coordinate_mix; system->seed_b = q.table_value;
        system->name = b->state.selected_name; system->reference = q.reference;
    }
    if (args) {
        fe2_args_word(args, P_CLASS, q.classification);
        fe2_args_dword(args, P_REFERENCE, q.reference);
        fe2_args_byte(args, P_STATUS, q.status);
    }
    return (uint16_t)q.coordinate_mix;
}
/* Request 1: a system near 0x7c0's (0x7c4: 0 its sector, 1 or other the
 * offset tables) into 0x7bc, zero for none; AX 1 when found. */
static uint16_t candidate_system(fe2_session_map_overlay *b, fe2_call_args *args)
{
    fe2_map_candidate c;
    uint16_t table = (uint16_t)args->arg[P_TABLE].value;
    b->status = fe2_map_find_candidate(&b->state, &b->services, args->arg[P_SYSTEM].value,
                                       table > 1 ? 2u : table, &c);
    if (b->status < 0) return 0;
    fe2_args_dword(args, P_RESULT, c.available ? c.reference : 0);
    return c.available ? 1 : 0;
}
/* Other requests: the system 0x7c0 refers to, with AX the mode. Not in its
 * sector: FFFFFFFFh in 0x7bc, AX 0xffff. Mode 2: its distance from home in
 * 0x7bc (AX the low word). Others: the coordinate mix, the table
 * value, the classification, the reference, the
 * status  and the name; AX 0. */
static uint16_t query_system(fe2_session_map_overlay *b, uint16_t mode, fe2_call_args *args)
{
    fe2_map_query q;
    b->status = fe2_map_query_system(&b->state, &b->services, args->arg[P_SYSTEM].value, mode, &q);
    if (b->status < 0) return 0;
    if (!q.available) { fe2_args_dword(args, P_RESULT, UINT32_C(0xffffffff)); return 0xffff; }
    if (mode == MAP_DISTANCE) { fe2_args_dword(args, P_RESULT, q.distance); return (uint16_t)q.distance; }
    memcpy(b->name, q.name, sizeof(b->name));
    fe2_args_dword(args, P_RESULT, q.coordinate_mix);
    fe2_args_dword(args, P_SYSTEM, q.table_value);
    fe2_args_word(args, P_CLASS, q.classification);
    fe2_args_dword(args, P_INDEX, 0);
    fe2_args_dword(args, P_REFERENCE, q.reference);
    fe2_args_byte(args, P_STATUS, q.status);
    fe2_args_string(args, P_NAME, b->name);
    return 0;
}
static uint16_t provide_map(void *context, uint16_t request, fe2_call_args *args, fe2_overlay_system *system)
{
    fe2_session_map_overlay *b = context;
    uint16_t result;
    load(b); ++b->depth;
    b->status = 0;
    if (request == MAP_CACHED) result = cached_system(b, args, system);
    else if (!args) { b->status = FE2_MAP_INVALID; result = 0; }
    else if (request == MAP_CANDIDATE) result = candidate_system(b, args);
    else result = query_system(b, request, args);
    LEAVE();
    return result;
}

int fe2_session_bind_map_overlay(fe2_session *s, fe2_session_objects *objects, fe2_session_map_overlay *b)
{
    fe2_map_services *v;
    fe2_session_overlay *o;
    if (!s || !objects || objects->session != s || !b || s->busy || s->locks[MAP_SLOT]) return -1;
    b->session = s; b->objects = objects;
    v = &b->services; v->opaque = b;
    map_forward_all(v);
    v->set_panel_mode = set_panel_mode; v->random = random_word; v->random_below = random_below;
    v->input_pending = input_pending; v->forward_command = forward_command;
    v->travel = travel; v->system_details = system_details;
    o = &s->overlays[MAP_SLOT];
    o->context = b;
    o->initialize = initialize; o->activate = activate; o->event = event; o->update = update; o->draw = draw;
    o->deselect = deselect;
    s->calls.map = provide_map; s->calls.context[MAP_SLOT] = b;
    return 0;
}
