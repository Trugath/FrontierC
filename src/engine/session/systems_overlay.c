/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "systems_overlay.h"
#include "math/math3.h"
#include <string.h>

enum {
    SYSTEMS_SLOT = 0, NAMES_MODULE = 9,
    /* The overlay call's parameters (0x7bc + 4n). */
    MODE_PARAMETER = 2, KIND_PARAMETER = 3, ORBIT_PARAMETER = 4, KEY_PARAMETER = 6,
    DANGER_PARAMETER = 7, RECORDS_PARAMETER = 8, NAME_PARAMETER = 10,
    /* Records when parameter 8 does not count them: the 0xf44-byte stack
     * context navigation and drives generate into. */
    DEFAULT_RECORDS = 0xf44 / FE2_SYSTEM_RECORD_BYTES
};

/* No resident globals: going out and coming back carry nothing. */
static void systems_out(fe2_session_systems_overlay *b) { (void)b; }
static void systems_back(fe2_session_systems_overlay *b) { (void)b; }

/* Host providers: a missing void provider does nothing; one with a result
 * fails the frame. */
static void systems_missing(fe2_session_systems_overlay *b) { b->session->error = 1; }
#include "systems_forward.h"

/* ---- Native services --------------------------------------------------- */
/* the orientation from the angles systems left at 0x1f35/0x1f37. */
static void basis_a(void *opaque, fe2_object *record)
{
    fe2_session_systems_overlay *b = opaque;
    fe2_math_basis_a(b->generation.basis_angle[0], b->generation.basis_angle[1], record->orientation);
}
static void basis_b(void *opaque, fe2_object *record)
{
    fe2_session_systems_overlay *b = opaque;
    fe2_math_basis_b(b->generation.basis_angle[0], b->generation.basis_angle[1], record->orientation);
}

static void set_slot_flags(void *opaque, fe2_object *record, uint8_t flags)
{
    fe2_session_systems_overlay *b = opaque;
    fe2_pool_set_slot_flags(&b->session->pools, record, flags);
}

/* names: the name through the overlay call when names provides it. */
static void generate_name(void *opaque, uint16_t type, uint32_t seed, char out[FE2_NAME_TEXT_BYTES])
{
    fe2_session_systems_overlay *b = opaque;
    fe2_session *s = b->session;
    if (!s->calls.name) { systems_host_generate_name(opaque, type, seed, out); return; }
    systems_out(b);
    s->calls.name(FE2_OVERLAY_CONTEXT(s, NAMES_MODULE), type, seed, out, FE2_NAME_TEXT_BYTES);
    systems_back(b);
}

/* ---- Entries ------------------------------------------------------------ */
void fe2_session_systems_action(fe2_session_systems_overlay *b, fe2_record_space space,
                                const fe2_system_request *request)
{
    ++b->depth; fe2_systems_action(&b->generation, &b->services, space, request); --b->depth;
}
void fe2_session_systems_format_text(fe2_session_systems_overlay *b, uint16_t selector)
{
    ++b->depth; fe2_systems_format_text(&b->services, selector); --b->depth;
}

/* The overlay call (+0x20): the request from the parameters as 0x8a reads
 * them (0x7c4 the mode, 0x7c8 the root kind, 0x7cc/0x7d0 the orbit, 0x7d4
 * the handcrafted key, 0x7d8 the danger, 0x7dc the records, 0x7e4 the
 * name). The records start at parameter 8 (its value, when set, counts
 * them); a key or name the parameters leave unset comes from `system`. */
static void provide_system(void *context, fe2_overlay_system *system, fe2_call_args *args)
{
    fe2_session_systems_overlay *b = context;
    const fe2_call_arg *records = &args->arg[RECORDS_PARAMETER];
    const fe2_call_arg *key = &args->arg[KEY_PARAMETER], *name = &args->arg[NAME_PARAMETER];
    fe2_system_request r;
    fe2_record_space space;
    if (!records->ref) { b->session->error = 1; return; }
    space.records = (fe2_system_record *)records->ref;
    space.count = records->value ? records->value : DEFAULT_RECORDS;
    memset(&r, 0, sizeof(r));
    if (system) { r.seed_a = system->seed_a; r.seed_b = system->seed_b; }
    r.mode = (uint16_t)args->arg[MODE_PARAMETER].value;
    r.limit = args->arg[KIND_PARAMETER].value;
    r.orbit[0] = args->arg[ORBIT_PARAMETER].value; r.orbit[1] = args->arg[ORBIT_PARAMETER + 1].value;
    r.key = key->kind != FE2_ARG_UNSET ? key->value : system ? system->reference : 0;
    r.danger = (uint8_t)args->arg[DANGER_PARAMETER].value;
    r.root_name = name->kind == FE2_ARG_STRING ? (const char *)name->ref : system ? system->name : 0;
    fe2_session_systems_action(b, space, &r);
}

int fe2_session_bind_systems_overlay(fe2_session *s, fe2_session_systems_overlay *b)
{
    fe2_system_services *v;
    if (!s || !b || s->busy || s->locks[SYSTEMS_SLOT]) return -1;
    b->session = s; b->depth = 0;
    memset(&b->generation, 0, sizeof(b->generation));
    v = &b->services; v->opaque = b;
    systems_forward_all(v);
    v->basis_a = basis_a; v->basis_b = basis_b; v->set_object_flags = set_slot_flags;
    v->generate_name = generate_name;
    s->overlays[SYSTEMS_SLOT].context = b;
    s->calls.system = provide_system; s->calls.context[SYSTEMS_SLOT] = b;
    return 0;
}
