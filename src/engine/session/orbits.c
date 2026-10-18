/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "orbits.h"
#include "math/math3.h"

static void product(void *context, const fe2_call_args *args, uint16_t out[4])
{
    uint64_t a = (uint64_t)args->arg[1].value << 32 | args->arg[0].value;
    uint64_t b = (uint64_t)args->arg[3].value << 32 | args->arg[2].value;
    uint64_t value = (uint64_t)fe2_math_scaled_difference(a, b, args->arg[4].value);
    unsigned i;
    (void)context;
    for (i = 0; i < 4; ++i) out[i] = (uint16_t)(value >> (i * 16));
}
static void interpolate(void *context, fe2_object *o, const fe2_call_args *args)
{
    uint16_t raw = (uint16_t)args->arg[6].value;
    int16_t factor = raw < 32768u ? (int16_t)raw : (int16_t)((int)raw - 65536);
    (void)context;
    fe2_math_place_on_arc(o, (uint16_t)args->arg[0].value, args->arg[1].value,
        factor, (uint16_t)args->arg[7].value);
}
static void scale(void *context, fe2_object *o, uint16_t factor)
{
    (void)context; fe2_math_step_object(o, factor);
}
static void basis(void *context, fe2_object *o)
{
    fe2_session_orbits *b = context;
    fe2_math_basis_b(b->state.basis_angle[0], b->state.basis_angle[1], o->orientation);
}
static fe2_object *lookup(void *context, uint8_t handle)
{
    fe2_session_orbits *b = context;
    fe2_object *o = fe2_pool_record(b->session->pools.saved, handle);
    if (!o) b->session->error = 1;
    return o;
}
static void message(void *context, uint16_t text, const fe2_call_args *args)
{
    fe2_session_orbits *b = context;
    if (b->message) b->message(b->context, text, args); else b->session->error = 1;
}
static void reference(void *context, uint16_t request, uint8_t handle, const fe2_call_args *args)
{
    fe2_session_orbits *b = context;
    if (b->reference) b->reference(b->context, request, handle, args); else b->session->error = 1;
}
static void update(void *context, fe2_session *s, fe2_object *o, uint64_t time)
{
    fe2_session_orbits *b = context;
    b->state.frame_step = s->simulation_step;
    fe2_orbit_update(&b->state, &b->services, o, time);
}
static void timers(void *context, fe2_session *s, fe2_object *o, uint32_t interval)
{
    fe2_session_orbits *b = context;
    (void)s;
    /* Fixed-body reference comparisons require an existing reference. */
    if ((o->flags & FE2_ORBIT_FIXED) && !lookup(b, b->state.reference_index)) return;
    fe2_orbit_count_down(&b->state, &b->services, o, (uint16_t)interval);
}
int fe2_session_bind_orbits(fe2_session *s, fe2_session_orbits *b)
{
    fe2_session_overlay *o;
    if (!s || !b || s->busy || s->locks[1]) return -1;
    b->session = s; b->services.opaque = b;
    b->services.wide_product = product; b->services.interpolate = interpolate;
    b->services.scale_vector = scale; b->services.basis_b = basis;
    b->services.lookup_object = lookup; b->services.show_message = message; b->services.reference_service = reference;
    o = &s->overlays[1]; o->context = b;
    o->periodic_record = update; o->second_record = timers;
    return 0;
}
