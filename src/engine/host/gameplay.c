/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "gameplay.h"
#include "commander/commander_internal.h"
#include "game/object_data.h"
#include "session/resident_image.h"
#include "status_image.h"
#include "game/model.h"
#include "render/render.h"
#include "render/view.h"
#include "world/system_tables.h"
#include "world/names.h"
#include "world/density_grid.h"
#include "flight/panel_tables.h"
#include <stdio.h>
#include <string.h>

static fe2_gameplay *game(void *p) { return p; }
static void fatal(void *p, uint16_t code);
static const uint8_t *model(void *p, uint16_t number)
{
    fe2_gameplay *g = game(p);
    fe2_render_bytes b;
    if ((number & 1u) || number / 2 >= 240) { g->session->error = 1; return NULL; }
    b = g->host->resources->models[0][number / 2];
    if (!b.data || b.count < 0x1e) { g->session->error = 1; return NULL; }
    return b.data;
}
static const fe2_flight_model *flight_model(void *p, uint16_t number)
{ return model(p, number) ? &game(p)->flight_models[number / 2] : NULL; }
static const fe2_model_header *system_model(void *p, uint16_t number)
{ return model(p, number) ? &game(p)->system_models[number / 2] : NULL; }

/* All generation arithmetic goes through the recovered integer math. */
static fe2_scalar from_dword(void *p, uint32_t v) { (void)p; return fe2_math_from_unsigned32(v); }
static fe2_scalar normalize(void *p, fe2_scalar v) { (void)p; return fe2_math_normalize_unsigned(v); }
static fe2_scalar add(void *p, fe2_scalar a, fe2_scalar b) { (void)p; return fe2_math_add(a, b); }
static fe2_scalar divide(void *p, fe2_scalar a, fe2_scalar b) { (void)p; return fe2_math_divide(a, b); }
static fe2_scalar root(void *p, fe2_scalar v) { (void)p; return fe2_math_square_root_scalar(v); }
static fe2_scalar multiply(void *p, fe2_scalar a, fe2_scalar b)
{
    fe2_scalar32 wide = fe2_math_multiply(a, b);
    fe2_scalar result;
    (void)p; result.mantissa = (uint16_t)((uint32_t)wide.mantissa >> 16); result.exponent = wide.exponent;
    return result;
}
static uint32_t to_signed(void *p, fe2_scalar v) { (void)p; return (uint32_t)fe2_math_to_signed32(v); }
static uint32_t to_unsigned(void *p, fe2_scalar v) { (void)p; return fe2_math_to_unsigned32(v); }
static void wide_product(void *p, const fe2_call_args *a, uint16_t out[4])
{
    uint64_t value = (uint64_t)fe2_math_scaled_difference(
        (uint64_t)a->arg[1].value << 32 | a->arg[0].value,
        (uint64_t)a->arg[3].value << 32 | a->arg[2].value, a->arg[4].value);
    unsigned i; (void)p;
    for (i = 0; i < 4; ++i) out[i] = (uint16_t)(value >> (16 * i));
}
static void interpolate(void *p, fe2_object *o, const fe2_call_args *a)
{
    (void)p; fe2_math_place_on_arc(o, (uint16_t)a->arg[0].value, a->arg[1].value,
        (int16_t)a->arg[6].value, (uint16_t)a->arg[7].value);
}
static void rescale(void *p, uint32_t v[3], const fe2_call_args *a)
{
    int32_t values[3]; unsigned i; (void)p;
    for (i = 0; i < 3; ++i) values[i] = (int32_t)v[i];
    fe2_math_rescale32(values, a->arg[6].value);
    for (i = 0; i < 3; ++i) v[i] = (uint32_t)values[i];
}
static uint32_t object_reference(void *p, const fe2_object *o) { (void)p; return o->handle + 1u; }
static fe2_object *from_reference(void *p, uint32_t id)
{ return id && id <= FE2_POOL_SLOTS ? game(p)->session->objects.records[id - 1] : NULL; }
static fe2_object *allocate_body(void *p, const fe2_call_args *a)
{
    fe2_gameplay *g = game(p);
    fe2_object decoded, *result = NULL;
    fe2_new_record request;
    fe2_pool_services services;
    request.flags = (uint8_t)a->arg[0].value; request.model = (uint16_t)a->arg[1].value;
    if (a->arg[8].kind == FE2_ARG_TEMPLATE) {
        fe2_object_read_template(&decoded, a->arg[8].ref, 1); request.template_record = &decoded;
    } else request.template_record = a->arg[8].ref;
    if (!request.template_record || !model(g, request.model)) return NULL;
    fe2_session_pool_services(g->session, &services);
    if (fe2_pool_allocate(&g->session->pools, &services, &request, &result)) ++g->bodies;
    return result;
}
static void absolute(void *p, fe2_object *o, int64_t out[3])
{ fe2_gameplay *g = game(p); g->position.generation = g->session->generation; fe2_position_absolute(&g->position, o, out); }
static void relative(void *p, fe2_object *o, fe2_object *target, int64_t out[3])
{
    int64_t ignored[3]; absolute(p, target, ignored);
    fe2_position_relative(&game(p)->position, o, target, out);
}
static void secondary(void *p, fe2_object *o)
{ fe2_position_secondary_basis(&game(p)->position, o); }
static void rebuild_secondary(void *p, fe2_object *o, const fe2_call_args *a)
{ (void)a; secondary(p, o); }
static void notify(void *p, uint16_t event, fe2_object *o)
{ (void)o; (void)fe2_session_broadcast(game(p)->session, event); }
static int16_t distance(void *p, const fe2_object *a, const fe2_object *b)
{
    fe2_scaled v = fe2_motion_scaled_difference(&game(p)->motion_services, (fe2_object *)a, (fe2_object *)b);
    return (int16_t)fe2_math_length3(v.v[0], v.v[1], v.v[2]);
}
static const uint8_t *session_model(void *p, uint16_t number)
{ return model(((fe2_game_host *)p)->gameplay, number); }
static int16_t session_distance(void *p, const fe2_object *a, const fe2_object *b)
{ return distance(((fe2_game_host *)p)->gameplay, a, b); }
static void integrate(void *p, fe2_object *o, uint32_t step) { (void)p; fe2_motion_integrate(o, step); }
static void integrate_limited(void *p, fe2_object *o, uint32_t step) { (void)p; fe2_motion_integrate_limited(o, step); }
static void turn(void *p, fe2_object *o, const fe2_call_args *a)
{
    fe2_turn t = {0}; (void)p;
    t.integrate = (uint16_t)a->arg[5].value; t.rate_scale = a->arg[6].value; t.step_limit = a->arg[7].value;
    fe2_motion_turn(o, &t);
}
static void linked_force(void *p, fe2_object *o, uint32_t step)
{ fe2_motion_linked_force(&game(p)->motion, o, (int32_t)step); }
static void radial_rate(void *p, fe2_object *o) { fe2_motion_radial_rate(&game(p)->motion, o); }
static void select_frame(void *p, fe2_object *o)
{ fe2_gameplay *g = game(p); fe2_motion_select_frame(&g->motion, &g->motion_services, o); }
static void clamp_controls(void *p, fe2_object *o, const fe2_call_args *a)
{
    int32_t values[3]; unsigned i; (void)p;
    for (i = 0; i < 3; ++i) values[i] = (int32_t)a->arg[i].value;
    fe2_motion_set_controls(o, values);
}
static fe2_flight_feedback feedback(void *p, fe2_object *o, const fe2_call_args *a)
{
    fe2_feedback native = fe2_motion_axis_feedback(o, (int32_t)a->arg[6].value);
    fe2_flight_feedback out; (void)p; memcpy(out.v, native.v, sizeof(out.v)); out.shift = native.shift; return out;
}
static fe2_flight_scaled scaled_difference(void *p, fe2_object *o, const fe2_call_args *a)
{
    fe2_scaled native = fe2_motion_scaled_difference(&game(p)->motion_services, o, (fe2_object *)a->arg[9].ref);
    fe2_flight_scaled out; memcpy(out.v, native.v, sizeof(out.v)); out.shift = native.shift; return out;
}
static void difference(void *p, fe2_object *o, const fe2_call_args *a, int32_t out[3])
{
    int64_t v[3]; unsigned i; relative(p, o, (fe2_object *)a->arg[9].ref, v);
    for (i = 0; i < 3; ++i) out[i] = (int32_t)(uint32_t)v[i];
}
static void transform_feedback(void *p, fe2_object *o, const fe2_call_args *a, int16_t out[3])
{
    int32_t v[3]; fe2_feedback r; unsigned i; (void)p;
    for (i = 0; i < 3; ++i) v[i] = (int32_t)a->arg[3 + i].value;
    r = fe2_motion_transform_feedback(o, v);
    for (i = 0; i < 3; ++i) out[i] = (int16_t)r.v[i];
}
static void move_object(void *p, fe2_object *o, const fe2_call_args *a)
{ int32_t v[3]; unsigned i; (void)p; for (i = 0; i < 3; ++i) v[i] = (int32_t)a->arg[i].value; fe2_math_move_object(o, v); }
static void transform_vector(void *p, fe2_object *o, const fe2_call_args *a)
{ (void)p; fe2_math_advance_object(o, (int64_t)((uint64_t)a->arg[3].value << 32 | a->arg[2].value)); }
static void rotate_pair(void *p, fe2_object *o, unsigned a, unsigned b, const fe2_call_args *args)
{ (void)p; fe2_math_rotate_pair(o->orientation + a, o->orientation + b, (uint16_t)args->arg[7].value); }
static void rebuild(void *p, fe2_object *o) { (void)p; fe2_motion_rebuild_basis(o); }
static void rates_toward(void *p, fe2_object *o, fe2_object *r, const fe2_call_args *a)
{ (void)p; fe2_motion_rates_toward(o, r, a->arg[7].value != 0); }
static void normalize_vector(void *p, const fe2_call_args *a, int16_t out[3])
{ unsigned i; (void)p; for (i = 0; i < 3; ++i) out[i] = (int16_t)a->arg[i].value; fe2_motion_normalize(out); }
static uint16_t length(void *p, int16_t x, int16_t y, int16_t z) { (void)p; return fe2_math_length3(x, y, z); }
static void project(void *p, int32_t x, int32_t y, int32_t z, uint16_t *sx, uint16_t *sy)
{ fe2_point out; (void)p; out = fe2_math_project(x, y, z); *sx = (uint16_t)out.x; *sy = (uint16_t)out.y; }

/* Contact and ray services use the native geometry implementations. */
static uint16_t condition(void *p, const fe2_object *o, uint8_t index)
{
    uint16_t values[86];
    if (index >= 86) { game(p)->session->error = 1; return 0; }
    fe2_gameplay_model_variables(o, values); return values[index];
}
static void collision(void *p, fe2_object *o, fe2_object *target, uint16_t amount, uint8_t other)
{ (void)target; (void)fe2_session_objects_damage(&game(p)->objects, o, amount, other); }
/* is 0x112a4 (the scan that applies a motion response). A fast parent
 * impact collides there; 0x64 is 0x1129d and only refreshes the classification. */
static fe2_flight_contacts scan_contacts(void *p, fe2_object *o)
{
    fe2_gameplay *g = game(p);
    fe2_contact_report r;
    fe2_flight_contacts out;
    g->contact.frame_step = g->session->simulation_step;
    r = fe2_contact_scan(&g->contact, &g->contact_services, o, 1);
    out.flags = r.flags; out.record = r.record; out.distance = r.distance; out.classification = r.classification;
    return out;
}
/* people's views of the same flight-engine services. */
static fe2_people_contacts population_contacts(void *p, fe2_object *o)
{
    fe2_gameplay *g = game(p);
    fe2_contact_report r;
    fe2_people_contacts out;
    g->contact.frame_step = g->session->simulation_step;
    r = fe2_contact_scan(&g->contact, &g->contact_services, o, 1);
    out.flags = r.flags; out.handle = r.handle; out.contact = r.record;
    return out;
}
/* the scaled difference in AX, BX, CX and its shift in DX. */
static fe2_people_metric population_scaled(void *p, fe2_object *o, const fe2_call_args *a)
{
    fe2_scaled native = fe2_motion_scaled_difference(&game(p)->motion_services, o, (fe2_object *)a->arg[9].ref);
    fe2_people_metric out;
    out.x = native.v[0]; out.y = native.v[1]; out.z = native.v[2]; out.distance = (int16_t)native.shift;
    return out;
}
static void population_difference(void *p, fe2_object *o, const fe2_call_args *a, uint32_t low[3])
{
    int64_t v[3]; unsigned i; relative(p, o, (fe2_object *)a->arg[9].ref, v);
    for (i = 0; i < 3; ++i) low[i] = (uint32_t)v[i];
}
/* Steering's frame change reads the selected acceleration at DS:0x4627.
 * Probes read the published one at DS:0x570d. Both live with the menu. */
static void sync_motion_time(fe2_gameplay *g)
{
    g->motion.time_exponent = g->menu.state.time_exponent;
    g->motion.game_time_exponent = (uint16_t)g->session->resident.game_time_exponent;
}
/* steer toward parameter 9 with parameter 7's exponent; the
 * feedback goes to parameters 0..2 and the exponent it leaves is returned. */
static uint16_t steer_result(fe2_call_args *a, fe2_feedback f)
{
    unsigned i;
    for (i = 0; i < 3; ++i) fe2_args_dword(a, i, (uint32_t)f.v[i]);
    return f.shift;
}
static uint16_t population_relative(void *p, fe2_object *o, fe2_call_args *a)
{
    fe2_gameplay *g = game(p);
    sync_motion_time(g);
    return steer_result(a, fe2_motion_steer(&g->motion, &g->motion_services, o, (fe2_object *)a->arg[9].ref,
                                            (uint16_t)a->arg[7].value));
}
static uint16_t population_probed(void *p, fe2_object *o, fe2_call_args *a)
{
    fe2_gameplay *g = game(p);
    sync_motion_time(g);
    return steer_result(a, fe2_motion_steer_probed(&g->motion, &g->motion_services, o,
                                                   (fe2_object *)a->arg[9].ref, (uint16_t)a->arg[7].value));
}
/* the rotation rates from the steering, the reversed
 * steering and the controls. */
static void population_apply(void *p, fe2_object *o, const fe2_call_args *a)
{ (void)a; fe2_motion_rates_from_steering(&game(p)->motion, o); }
static void population_hold(void *p, fe2_object *o, const fe2_call_args *a)
{ (void)p; (void)a; fe2_motion_rates_from_reversed_steering(o); }
static void population_steer(void *p, fe2_object *o, const fe2_call_args *a)
{ (void)p; (void)a; fe2_motion_rates_from_controls(o); }
/* AL, the capacity used, is the low byte of the fuel consumed. */
static uint8_t population_capacity(void *p, fe2_object *o, uint32_t step)
{ return (uint8_t)fe2_session_objects_update(&game(p)->objects, o, (uint16_t)step); }
/* release with a zero request. */
static void population_remove(void *p, fe2_object *o)
{
    fe2_session *s = game(p)->session;
    fe2_pool_services pool;
    fe2_session_pool_services(s, &pool);
    fe2_pool_release(&s->pools, &pool, o, 0);
}
/* Motion's 0x130: the scene probe from the record along the steering. */
static void motion_probe(void *p, fe2_object *o, fe2_object *target, const fe2_motion_probe *request,
                         fe2_motion_hit *hit)
{
    fe2_gameplay *g = game(p);
    fe2_scene_probe_request r;
    fe2_scene_probe_result result;
    unsigned i;
    (void)target;
    memset(&r, 0, sizeof(r));
    r.source = o; r.excluded = request->handle; r.extent = request->extent;
    r.extent_high = request->extent_high; r.mode = request->mode;
    for (i = 0; i < 3; ++i) r.direction[i] = request->direction[i];
    hit->hit = fe2_scene_probe_query(&g->probe, &g->probe_services, &r, &result);
    hit->distance = (int64_t)result.distance;
    for (i = 0; i < 3; ++i) hit->direction[i] = g->probe.normal[i];
}
/* Motion's estimate: orbits's update (+0x18) on a record copy at `time`. The
 * original also raises DS:0x597 around it, which orbits does not read. */
static void motion_estimate(void *p, fe2_object *copy, const uint16_t time[4])
{
    fe2_session *s = game(p)->session;
    fe2_session_overlay *o = &s->overlays[1];
    uint64_t at = 0;
    unsigned i;
    for (i = 0; i < 4; ++i) at |= (uint64_t)time[i] << (16 * i);
    if (o->periodic_record) o->periodic_record(o->context, s, copy, at);
}
static void refresh_contacts(void *p, fe2_object *o)
{ fe2_gameplay *g = game(p); g->contact.frame_step = g->session->simulation_step; (void)fe2_contact_scan(&g->contact, &g->contact_services, o, 0); }
static int probe(void *p, const fe2_call_args *a, fe2_flight_probe *out)
{
    fe2_gameplay *g = game(p);
    fe2_scene_probe_request r = {0}; fe2_scene_probe_result hit; unsigned i; int found;
    r.source = (fe2_object *)a->arg[8].ref; r.excluded = (uint8_t)a->arg[6].value;
    r.extent = a->arg[7].value; r.extent_high = a->arg[11].value; r.mode = (uint16_t)a->arg[10].value;
    for (i = 0; i < 3; ++i) { r.offset[i] = (int32_t)a->arg[i].value; r.direction[i] = (int16_t)a->arg[3 + i].value; }
    found = fe2_scene_probe_query(&g->probe, &g->probe_services, &r, &hit);
    memset(out, 0, sizeof(*out)); out->hit = found; out->distance = (uint32_t)hit.distance; out->record = hit.record;
    memcpy(out->point, g->probe.point, sizeof(out->point)); memcpy(out->normal, g->probe.normal, sizeof(out->normal));
    return found;
}
static void object_probe(void *p, fe2_object *source, fe2_objects_probe *out)
{
    fe2_gameplay *g = game(p); fe2_scene_probe_request r = {0}; fe2_scene_probe_result hit; unsigned i;
    r.source = source; r.excluded = out->owner; r.extent = out->range; r.mode = 0x1a80;
    memcpy(r.offset, out->offset, sizeof(r.offset)); memcpy(r.direction, out->direction, sizeof(r.direction));
    out->hit = fe2_scene_probe_query(&g->probe, &g->probe_services, &r, &hit);
    out->target = hit.record; out->reach = (uint32_t)hit.distance; out->distance = (uint32_t)hit.distance;
    for (i = 0; i < 3; ++i) { out->position[i] = (uint32_t)g->probe.point[i]; out->position[i + 3] = (uint32_t)((uint64_t)g->probe.point[i] >> 32); }
}
/* -style text into a buffer: a debris fragment's name (0x8e00 + kind). */
static void object_text(void *p, uint16_t selector, char *out, size_t capacity)
{
    size_t length = 0;
    if (!capacity) return;
    if (fe2_text_expand(&game(p)->host->resources->text, selector, (uint8_t *)out, capacity - 1, &length)) length = 0;
    out[length] = 0;
}
static void object_basis(void *p, fe2_object *o, const uint16_t seed[2])
{ (void)p; fe2_math_basis_b(seed[0], seed[1], o->orientation); }
static void object_move(void *p, fe2_object *o, const int32_t delta[3])
{ (void)p; fe2_math_move_object(o, delta); }

static void scene(void *p, const fe2_call_args *a)
{ fe2_gameplay *g = game(p); if (fe2_gameplay_render_scene(g, (fe2_object *)a->arg[11].ref)) g->session->error = 1; }
static void render_explicit(void *p, fe2_object *o)
{
    fe2_gameplay *g = game(p); fe2_render_model_transform t = {0};
    fe2_render_program_result result; const uint8_t *descriptor = model(p, o->model);
    uint16_t values[86]; unsigned i;
    if (!descriptor) return;
    memcpy(t.basis, o->orientation, sizeof(t.basis));
    for (i = 0; i < 3; ++i) t.position[i] = (int32_t)o->camera_offset[i];
    t.exponent = fe2_model_word(descriptor, 0x0a); t.material = 0x111;
    if (!fe2_render_model_visible(t.position, fe2_model_word(descriptor, 0x0e), t.exponent)) return;
    memcpy(t.direction, g->session->light, sizeof(t.direction));
    {
        const fe2_resident *r = &g->session->resident;
        const uint32_t shades[4] = {r->classification_0, r->classification_1, r->classification_2, r->classification_3};
        for (i = 0; i < 8; ++i) t.shades[i] = (uint16_t)(shades[i / 2] >> (16 * (i % 2)));
    }
    fe2_gameplay_model_variables(o, values);
    result = fe2_render_runtime_submit(g->host->runtime, o->model, &t,
        (fe2_render_words){values, 86}, (fe2_render_words){(const uint16_t *)fe2_math_sine, 2048});
    if (result.status != FE2_RENDER_PROGRAM_COMPLETE) {
        fprintf(stderr, "Explicit model %04x failed: %u at %zu, position %d,%d,%d\n",
            o->model, result.status, result.cursor, t.position[0], t.position[1], t.position[2]);
        fatal(p, 0xf00e);
    }
}
static void render_frame_model(void *p, fe2_object *frame)
{
    fe2_object placed;
    fe2_gameplay *g = game(p);
    if (frame->station_slot) fe2_position_secondary_basis(&g->position, frame);
    if (fe2_render_basis_attachment(frame, &placed)) { fatal(p, 0xf00e); return; }
    render_explicit(p, &placed);
    ++g->frame_models;
}
/* UI previews can be requested after the flight command queue was drawn.
 * Start a new layer without clearing the page; keep raster writes within
 * the preview's rectangle, including the original unchecked raster paths. */
static void preview(void *p, fe2_object *o, const fe2_rect *bounds)
{
    fe2_gameplay *g = game(p);
    fe2_video *v = fe2_render_runtime_video(g->host->runtime);
    uint8_t saved[FE2_VIDEO_PAGE_BYTES];
    unsigned at;
    if (!o || fe2_render_runtime_layer(g->host->runtime)) { g->session->error = 1; return; }
    memcpy(saved, v->pages[v->drawing], sizeof saved);
    render_explicit(p, o);
    if (fe2_render_runtime_draw(g->host->runtime)) { g->session->error = 1; return; }
    ++g->previews;
    for (at = 0; at < sizeof saved; ++at) {
        unsigned x = at % 320u, y = at / 320u;
        if (x < bounds->left || x > bounds->right || y < bounds->top || y > bounds->bottom)
            v->pages[v->drawing][at] = saved[at];
        else if (v->pages[v->drawing][at] != saved[at]) ++g->preview_pixels;
    }
    /* Activation can draw a preview before flight's update submits its frame
     * model. Leave a fresh queue so that later resident renders remain valid. */
    if (fe2_render_runtime_layer(g->host->runtime)) g->session->error = 1;
}
/* reads the graphics work pointer. The screen cursor is
 * separate: only a held secondary button adds mouse motion to this stick. */
static uint16_t pointer(void *p, uint16_t *y)
{
    fe2_pointer_state *stick = &game(p)->session->pointer;
    *y = (uint16_t)stick->redirected[1];
    return (uint16_t)stick->redirected[0];
}
static void sample_pointer(void *p, fe2_session *s, fe2_pointer_sample *out)
{
    fe2_gameplay *g = ((fe2_game_host *)p)->gameplay;
    /* The window supplies absolute coordinates. Native secondary-button
     * motion rotates the view while leaving its cursor stationary; align
     * that cursor with the window again when the button is released. */
    int released = s->pointer.secondary && !(g->mouse_buttons & 2u);
    out->motion[0] = (int16_t)((int)g->mouse_x - (int)(released ? s->pointer.x : g->sampled_x));
    out->motion[1] = (int16_t)((int)g->mouse_y - (int)(released ? s->pointer.y : g->sampled_y));
    g->sampled_x = g->mouse_x;
    g->sampled_y = g->mouse_y;
    /* A click can be released before the frame samples the button. */
    out->primary = (g->mouse_buttons & 1u) || g->mouse_click ? 0xff : 0;
    g->mouse_click = 0;
    out->secondary = g->mouse_buttons & 2u ? 0xff : 0;
}
static void set_pointer(void *p, uint16_t x, uint16_t y)
{
    fe2_pointer_state *stick = &game(p)->session->pointer;
    stick->redirected[0] = (int16_t)x;
    stick->redirected[1] = (int16_t)y;
}
static void controls(void *p, uint16_t layout, uint8_t buttons) { (void)p; (void)layout; (void)buttons; }
/* AX is the effect number. 0x344 and 0x348 add a level or a duration. */
static void play(void *p, uint16_t effect) { if (game(p)->audio) fe2_intro_audio_play_effect(game(p)->audio, effect); }
static void duration(void *p, uint16_t effect, uint16_t ticks) { if (game(p)->audio) fe2_intro_audio_play_duration(game(p)->audio, effect, ticks); }
static void sound(void *p, uint16_t effect, uint8_t level) { if (game(p)->audio) fe2_intro_audio_play_level(game(p)->audio, effect, level); }
static void ambient(void *p, uint16_t a, uint16_t b, uint16_t c) { (void)c; if (game(p)->audio) fe2_intro_audio_ambient(game(p)->audio, a, b); }
static void fatal(void *p, uint16_t code) { fprintf(stderr, "Game error %04x\n", code); game(p)->session->error = 1; }
static void name(void *p, uint16_t type, uint32_t seed, char out[32])
{ fe2_name_result result; if (fe2_name_generate(type, (uint16_t)seed, (uint16_t)(seed >> 16),
    (uint8_t *)out, 32, &result)) fatal(p, 0xf009); }
static void sprite(void *p, uint16_t selector, uint16_t x, uint16_t y);
static void panel(void *p, uint16_t request, const fe2_call_args *a);
static void text(void *p, uint16_t selector, uint8_t colour, uint16_t x, uint16_t y, const fe2_call_args *a);
static void draw_expanded(void *p, uint16_t selector, uint8_t colour, uint16_t x, uint16_t y,
                          const fe2_call_args *a, int shadowed);
/* A 0x1 control indexes the map at DS:0x9e8. List text uses 0x9f3..0x9f5
 * (0xb..0xd). A message copies 0x8936..0x8938 into 0x9ea..0x9ec (2..4):
 * the reply quotes the question in 3 and the answer in 4. */
static void station_colours(fe2_gameplay *g)
{
    unsigned i;
    for (i = 0; i < 3; ++i) {
        g->host->colour_map[2u + i] = g->station.state.colours[1u + i];
        g->host->colour_map[0x0bu + i] = g->station.state.list_colours[i];
    }
}
static void station_line(void *p, uint16_t selector, uint8_t colour, uint16_t x, uint16_t y,
                         const fe2_call_args *a, int shadowed)
{
    /* is the named-subject wrapper. The subject far pointer is not a
     * native string yet, so draw the message id carried in parameter 1. */
    if (selector == 0x9900 && a && a->arg[1].kind != FE2_ARG_UNSET)
        selector = (uint16_t)a->arg[1].value;
    station_colours(game(p));
    draw_expanded(p, selector, colour, x, y, a, shadowed);
}
static void station_text(void *p, uint16_t selector, uint8_t colour, uint16_t x, uint16_t y,
                         const fe2_call_args *a)
{ station_line(p, selector, colour, x, y, a, 1); }
static void station_plain(void *p, uint16_t selector, uint8_t colour, uint16_t x, uint16_t y,
                          const fe2_call_args *a)
{ station_line(p, selector, colour, x, y, a, 0); }
static void station_text_both(void *p, uint16_t selector, uint8_t colour, uint16_t x, uint16_t y,
                              const fe2_call_args *a)
{
    fe2_video *v = fe2_render_runtime_video(game(p)->host->runtime);
    if (selector == 0x9900 && a && a->arg[1].kind != FE2_ARG_UNSET)
        selector = (uint16_t)a->arg[1].value;
    station_colours(game(p));
    draw_expanded(p, selector, colour, x, y, a, 0);          /* calls 0x104, both pages */
    v->drawing ^= 1u; draw_expanded(p, selector, colour, x, y, a, 0); v->drawing ^= 1u;
}
/* station's references (see session/station_overlay.h): pool handles, the
 * second pool's records, and the preview work record. A 64-bit pointer
 * does not fit the 32-bit reference the station keeps. */
static fe2_object *station_object(void *p, fe2_ref ref)
{
    fe2_gameplay *g = game(p);
    if (ref >= FE2_STATION_REF_WORK)
        return ref == FE2_STATION_REF_WORK * (FE2_STATION_WORK_PREVIEW + 1u) ? &g->station_preview : NULL;
    if (ref & FE2_STATION_REF_MAP)
        return (uint8_t)ref < FE2_POOL_SLOTS ? g->session->map_objects.records[(uint8_t)ref] : NULL;
    return g->session->objects.records[(uint8_t)ref < FE2_POOL_SLOTS ? (uint8_t)ref : 0];
}
/* The pool a list reference (parameter 12) names. */
static fe2_object_pool *station_pool(void *p, uint32_t list)
{
    fe2_session *s = game(p)->session;
    return list == FE2_STATION_REF_MAP_LIST ? &s->map_objects : &s->objects;
}
static fe2_ref station_ref(fe2_object_pool *pool, fe2_session *s, uint8_t handle)
{
    return pool == &s->map_objects ? (fe2_ref)(FE2_STATION_REF_MAP | handle) : handle;
}
/* the record of `handle` in the list of parameter 12. */
static fe2_ref station_lookup(void *p, uint8_t handle, const fe2_call_args *a)
{
    return station_ref(station_pool(p, a ? a->arg[12].value : 0), game(p)->session, handle);
}
/* the list of parameter 12 becomes the pool records are made in,
 * emptied. */
static void station_clear(void *p, const fe2_call_args *a)
{
    fe2_session *s = game(p)->session;
    s->pools.current = station_pool(p, a ? a->arg[12].value : 0);
    fe2_pool_clear(&s->pools);
}
/* the template (parameter 8) copied into the record parameter 9
 * names, initialized from the model in parameter 1 when there is one. */
static void station_prepare(void *p, const fe2_call_args *a)
{
    fe2_gameplay *g = game(p);
    fe2_object decoded, *record = station_object(p, a->arg[9].value);
    fe2_new_record request;
    fe2_pool_services services;
    if (!record) { g->session->error = 1; return; }
    if (a->arg[8].kind == FE2_ARG_TEMPLATE) {
        fe2_object_read_template(&decoded, a->arg[8].ref, 0); request.template_record = &decoded;
    } else if (a->arg[8].ref) request.template_record = a->arg[8].ref;
    else request.template_record = station_object(p, a->arg[8].value);   /* a record as its template */
    if (!request.template_record) { g->session->error = 1; return; }
    request.flags = 0; request.model = (uint16_t)a->arg[1].value;
    fe2_session_pool_services(g->session, &services);
    (void)fe2_pool_copy_template(&g->session->pools, &services, &request, record);
}
/* the record parameter 9 names moves by parameters 0..2. */
static void station_move(void *p, const fe2_call_args *a)
{
    fe2_object *o = station_object(p, a->arg[9].value);
    int32_t delta[3];
    unsigned i;
    if (!o) return;
    for (i = 0; i < 3; ++i) delta[i] = (int32_t)a->arg[i].value;
    fe2_math_move_object(o, delta);
}
/* the basis from the angles at 0x1f35. */
static void station_orient(void *p, fe2_ref object)
{
    fe2_object *o = station_object(p, object);
    const uint16_t *angle = game(p)->station.state.basis_angle;
    if (o) fe2_math_basis_b(angle[0], angle[1], o->orientation);
}
static void station_preview(void *p, fe2_ref object, const fe2_call_args *a)
{
    fe2_gameplay *g = game(p);
    fe2_video *v = fe2_render_runtime_video(g->host->runtime);
    const fe2_rect bounds = {1, 0x76, 0x16, 0x88};
    unsigned y;
    /* The offer's statistics are drawn once, then kept on the second
     * page. The host clears each world frame, so restore them beside the
     * animated preview before the native input pass submits its model. */
    for (y = 22; y <= 136; ++y)
        memcpy(v->pages[v->drawing] + y*320 + 119, v->pages[v->drawing ^ 1u] + y*320 + 119, 201);
    (void)a; preview(p, station_object(p, object), &bounds);
    memcpy(g->station_ship_pixels, v->pages[v->drawing] + 22*320, sizeof g->station_ship_pixels);
    g->station_ship_valid = 1;
}
/* takes inclusive AX/BX/CL/DL bounds and BP's palette index. */
static void station_rectangle(void *p, uint16_t left, uint16_t right, uint8_t top, uint8_t bottom, uint16_t colour)
{
    fe2_video_rectangle(fe2_render_runtime_video(game(p)->host->runtime), left, right, top, bottom, (uint8_t)colour, 0);
}
/* the record in parameter 9 rescans its contacts. */
static void station_attach(void *p, const fe2_call_args *a)
{
    fe2_object *o = station_object(p, a->arg[9].value);
    if (o) refresh_contacts(p, o);
}
static fe2_ref station_work(void *p, enum fe2_station_work work)
{
    (void)p; return FE2_STATION_REF_WORK * ((fe2_ref)work + 1u);
}
/* systems through the session's call table, with a work record's system
 * records in parameter 8 (station names them by reference). */
static void station_systems(void *p, uint32_t *seed_a, uint32_t *seed_b, const fe2_call_args *args)
{
    fe2_gameplay *g = game(p);
    fe2_session *s = g->session;
    fe2_overlay_system system = {0};
    fe2_call_args block;
    uint32_t ref;
    if (!args || !s->calls.system) { s->error = 1; return; }
    block = *args;
    ref = block.arg[8].value;
    if (ref == station_work(p, FE2_STATION_WORK_MISSION)) block.arg[8].ref = g->station_systems[0];
    else if (ref == station_work(p, FE2_STATION_WORK_OFFER)) block.arg[8].ref = g->station_systems[1];
    else { s->error = 1; return; }
    block.arg[8].value = 0;
    system.seed_a = *seed_a; system.seed_b = *seed_b;
    s->calls.system(s->calls.context[0], &system, &block);
    *seed_a = system.seed_a; *seed_b = system.seed_b;
}
/* a model's flight parameter words (its statistics record, the
 * descriptor's word at +0x1c). The parameters' address is named by the
 * model. Models without a record read as zeros. */
static const fe2_station_model *station_model(void *p, uint16_t model)
{
    fe2_gameplay *g = game(p);
    unsigned index = (model / 2u) % 240u, n, at;
    fe2_station_model *m = &g->station_models[index];
    fe2_render_bytes bytes = g->host->resources->models[0][index];
    memset(m, 0, sizeof *m);
    m->ref = 0x4d000000u | model;
    if ((model & 1u) || !bytes.data || bytes.count < 0x1e) return m;
    at = fe2_model_word(bytes.data, 0x1c);
    if (at + 2u * 11u > bytes.count) return m;
    for (n = 0; n < 11; ++n) m->word[n] = fe2_model_word(bytes.data + at, 2 * n);
    return m;
}
/* each live record of the list. */
static void station_visit(void *p, fe2_ref list, fe2_station_visitor visit, void *context,
                          const fe2_call_args *a)
{
    fe2_session *s = game(p)->session;
    fe2_object_pool *pool = station_pool(p, list);
    unsigned slot;
    (void)a;
    for (slot = 1; slot < FE2_POOL_SLOTS; ++slot)
        if (pool->status[slot] && pool->records[slot]) visit(context, station_ref(pool, s, (uint8_t)slot));
}

static void station_slot_flags(void *p, fe2_ref object, uint8_t flags)
{
    fe2_object *o = station_object(p, object);
    if (o) fe2_pool_set_slot_flags(&game(p)->session->pools, o, flags);
}
/* the seeds advance as 0x66a leaves them (CX:BX swapped back). */
static fe2_station_sample station_sample(void *p, uint16_t bound, uint32_t seed_a, uint32_t seed_b)
{
    fe2_station_sample r;
    (void)p;
    r.result = (uint16_t)(fe2_objects_scale_pair(bound, &seed_a, &seed_b) >> 16);
    r.seed_a = seed_a >> 16 | seed_a << 16;
    r.seed_b = seed_b;
    return r;
}
/* Commodity names are 0x8e00 plus the index. Doubled and masked, index 31
 * (selector 0x8e1f) is the two-column price list, not a 32nd name. */
enum { MARKET_TEXT_MASK = 0x03fe, MARKET_PRICE_LIST = 0x3e, MARKET_BLOCK = 0x224d };

typedef struct market_text_ctx {
    const fe2_text_resources *resources;
    size_t capacity;
} market_text_ctx;

static void market_text_append(fe2_text_out *out, size_t capacity, const uint8_t *bytes, size_t count)
{
    if ((size_t)out->at + count + 1 > capacity) return;
    if (count) memcpy(out->buffer + out->at, bytes, count);
    out->at = (uint16_t)(out->at + count);
    out->buffer[out->at++] = 0;
}

static void market_format_text(void *opaque, uint16_t selector, const fe2_text_table *table,
                               fe2_text_out *out)
{
    market_text_ctx *ctx = opaque;
    uint8_t line[64];
    size_t length = 0;
    (void)table;
    if (fe2_text_expand(ctx->resources, (uint16_t)(0x8e00u + (selector >> 1)), line, sizeof line, &length))
        length = 0;
    else if (length) --length; /* expand counts its terminator; the list overwrites one */
    market_text_append(out, ctx->capacity, line, length);
}

static void market_format_number(void *opaque, uint16_t format, const fe2_call_args *args,
                                 fe2_text_out *out)
{
    market_text_ctx *ctx = opaque;
    fe2_text_resources resources = *ctx->resources;
    uint8_t line[32];
    size_t length = 0;
    /* Format 0x3004 reads value 0, which draw maps from parameter 1. */
    resources.value[0] = args->arg[1].value;
    if (fe2_text_expand(&resources, format, line, sizeof line, &length)) length = 0;
    else if (length) --length;
    market_text_append(out, ctx->capacity, line, length);
}

/* The overlays' own text entries (+0x28) the formatter reaches: chart's
 * packed system references (0x3fc the sector pair, 0x3fe the name of the
 * system in parameter 1), and market's price list (0x8e1f). Other selectors
 * use the text directories. */
static int overlay_text(void *p, const fe2_text_resources *r, unsigned slot, uint16_t doubled,
                        char *out, size_t capacity)
{
    fe2_gameplay *g = p;
    if (slot == 7 && (doubled & MARKET_TEXT_MASK) == MARKET_PRICE_LIST) {
        market_text_ctx ctx;
        fe2_market_services services;
        fe2_text_out produced;
        uint32_t at = (uint16_t)r->pointer_value[0];
        if (!out || !capacity || at < MARKET_BLOCK || at - MARKET_BLOCK >= FE2_MARKET_BLOCK_BYTES) {
            if (out && capacity) out[0] = 0;
            return -1;
        }
        ctx.resources = r;
        ctx.capacity = capacity;
        memset(&services, 0, sizeof services);
        services.opaque = &ctx;
        services.format_text = market_format_text;
        services.format_number = market_format_number;
        produced.buffer = (uint8_t *)out;
        produced.at = 0;
        fe2_market_format(&g->market.state, &services, doubled, at - MARKET_BLOCK, &produced);
        return 0;
    }
    /* names +0x28 : the doubled selector is the name mode. The seed is
     * the dword at 0x7e4, which is pointer 0. */
    if (slot == 9) {
        fe2_name_result result;
        uint32_t seed = r->pointer_value[0];
        if (!out || !capacity || fe2_name_display(doubled, (uint16_t)seed, (uint16_t)(seed >> 16),
                                                  (uint8_t *)out, capacity, &result) != FE2_NAME_OK) {
            if (out && capacity) out[0] = 0;
            return -1;
        }
        return 0;
    }
    if (slot != 2) return -1;
    return fe2_map_format_text(&g->map.services, doubled, (uint16_t)r->value[2],
        (uint16_t)r->value[3], (uint16_t)r->value[4], r->value[0], NULL, out, capacity);
}
/* The commander file's image. The object block keeps the original
 * directory: the slot status bytes, slot 0's first 0x60 bytes at 0x74, then
 * 0x11e-byte records from 0xd4. A record pointer in the resident image is its
 * record's offset in the block. */
enum { IMAGE_SLOT_0 = 0x74, IMAGE_SLOT_0_BYTES = 0x60, IMAGE_SLOT_1 = 0xd4 };
static unsigned image_record_at(unsigned slot)
{ return slot ? IMAGE_SLOT_1 + (slot - 1u) * FE2_OBJECT_RECORD_BYTES : IMAGE_SLOT_0; }
static uint32_t image_ref(void *p, const fe2_object *o)
{
    (void)p; return o && o->handle < FE2_POOL_SLOTS ? image_record_at(o->handle) : 0;
}
static fe2_object *image_object(void *p, uint32_t ref)
{
    fe2_session *s = game(p)->session;
    uint16_t at = (uint16_t)ref;
    if (at == IMAGE_SLOT_0) return &s->records[0];
    if (at < IMAGE_SLOT_1 || (at - IMAGE_SLOT_1) % FE2_OBJECT_RECORD_BYTES) return NULL;
    at = (uint16_t)((at - IMAGE_SLOT_1) / FE2_OBJECT_RECORD_BYTES + 1u);
    return at < FE2_POOL_SLOTS ? &s->records[at] : NULL;
}
/* The overlays' states, as each annotates its DS cells. */
#define IMAGE_STATES(X) X(orbits, orbits) X(map, map) X(flight, flight) X(panels, panels) X(navigation, navigation) \
    X(market, market) X(commander, menu) X(people, population) X(station, station) X(offences, offences) \
    X(hyperspace, hyperspace) X(mining, mining)
/* The image of the game now (the session's cells last: they are the
 * shared ones' own copies). */
static fe2_save_image image_save(void *p)
{
    fe2_gameplay *g = game(p);
    fe2_session *s = g->session;
    uint8_t record[FE2_OBJECT_RECORD_BYTES];
    fe2_save_image image;
    unsigned slot;
    for (slot = 0; slot < FE2_POOL_SLOTS; ++slot) {
        g->image.objects[slot] = s->objects.status[slot];
        fe2_object_write_record(&s->records[slot], record);
        memcpy(g->image.objects + image_record_at(slot), record, slot ? FE2_OBJECT_RECORD_BYTES : IMAGE_SLOT_0_BYTES);
    }
    for (slot = 0; slot < FE2_SAVE_OVERLAY_RECORDS; ++slot) {
        uint8_t *r = g->image.overlay_records + 8 * slot + 4;
        r[0] = s->metadata[slot].activation; r[1] = s->metadata[slot].flags;
        r[2] = s->metadata[slot].update; r[3] = s->metadata[slot].draw;
        image_put(g->image.table, 4 * slot, 4, s->event_masks[slot]);
    }
    status_image(&g->status.state, g->image.resident, 0);
#define PACK(prefix, binding) prefix##_image_pack(&g->binding.state, g->image.resident, image_ref, g);
    IMAGE_STATES(PACK)
#undef PACK
    fe2_image_pack_session(s, &g->objects.state, g->image.resident, image_ref, g);
    /* These native byte-record spaces include overlapping records and
     * sentinels; scalar annotations cannot reconstruct them. */
    memcpy(g->image.resident + (0x224d - FE2_SAVE_RESIDENT_START),
           g->market.state.markets.bytes, sizeof g->market.state.markets.bytes);
    memcpy(g->image.resident + (0x5193 - FE2_SAVE_RESIDENT_START),
           g->mining.state.records.bytes, sizeof g->mining.state.records.bytes);
    image.objects = g->image.objects; image.overlay_records = g->image.overlay_records;
    image.table = g->image.table; image.resident = g->image.resident; image.stack = g->image.stack;
    g->image_retake = 1;
    return image;
}
/*, as the save uses it: the record of `handle` in the image. The
 * original's image is its memory, so the save event's effects (posted just
 * before the save clears the free records) are taken into the image again
 * at the first record asked for. */
static uint8_t *image_record(void *p, uint8_t handle)
{
    fe2_gameplay *g = game(p);
    if (handle >= FE2_POOL_SLOTS) return NULL;
    if (g->image_retake) { (void)image_save(p); g->image_retake = 0; }
    return g->image.objects + image_record_at(handle);
}
/* A loaded image becomes the game: the records, the overlays' metadata and
 * every annotated cell (the session's last). */
static void image_restore(void *p)
{
    fe2_gameplay *g = game(p);
    fe2_session *s = g->session;
    uint8_t record[FE2_OBJECT_RECORD_BYTES];
    unsigned slot;
    for (slot = 0; slot < FE2_POOL_SLOTS; ++slot) {
        s->objects.status[slot] = g->image.objects[slot];
        fe2_object_write_record(&s->records[slot], record);
        memcpy(record, g->image.objects + image_record_at(slot), slot ? FE2_OBJECT_RECORD_BYTES : IMAGE_SLOT_0_BYTES);
        fe2_object_read_record(&s->records[slot], record);
        s->records[slot].handle = (uint8_t)slot;
    }
    for (slot = 0; slot < FE2_SAVE_OVERLAY_RECORDS; ++slot) {
        const uint8_t *r = g->image.overlay_records + 8 * slot + 4;
        s->metadata[slot].activation = r[0]; s->metadata[slot].flags = r[1];
        s->metadata[slot].update = r[2]; s->metadata[slot].draw = r[3];
        s->event_masks[slot] = (uint32_t)image_get(g->image.table, 4 * slot, 4);
    }
#define UNPACK(prefix, binding) prefix##_image_unpack(&g->binding.state, g->image.resident, image_object, g);
    IMAGE_STATES(UNPACK)
#undef UNPACK
    fe2_image_unpack_session(s, &g->objects.state, g->image.resident, image_object, g);
    memcpy(g->market.state.markets.bytes, g->image.resident + (0x224d - FE2_SAVE_RESIDENT_START),
           sizeof g->market.state.markets.bytes);
    memcpy(g->mining.state.records.bytes, g->image.resident + (0x5193 - FE2_SAVE_RESIDENT_START),
           sizeof g->mining.state.records.bytes);
    status_image(&g->status.state, g->image.resident, 1);
}
static void station_sprite(void *p, uint16_t id, uint16_t x, uint16_t y, const fe2_call_args *a)
{
    fe2_video *v = fe2_render_runtime_video(game(p)->host->runtime);
    (void)a; sprite(p, id, x, y);
    v->drawing ^= 1u; sprite(p, id, x, y); v->drawing ^= 1u;
}
static void station_copy(void *p)
{ fe2_video_copy_frame(fe2_render_runtime_video(game(p)->host->runtime)); }
static void station_present(void *p)
{
    fe2_gameplay *g = game(p);
    fe2_video *v = fe2_render_runtime_video(g->host->runtime);
    memcpy(v->pages[v->drawing ^ 1u], v->pages[v->drawing], FE2_VIDEO_FRAME_BYTES);
    /* waits inside the frame. Show the arrest notice and read the
     * window, or the launch request never returns. */
    if (!g->station.state.notice_wait) return;
    if (g->host->present)
        (void)fe2_video_present(v, g->host->present, g->host->present_context);
    if (g->host->pump && g->host->pump(g->host->present_context))
        fe2_input_restore(&g->session->pointer, 0x39);
}
static void station_draw(void *context, fe2_session *s)
{
    fe2_session_station_overlay *b = context;
    fe2_gameplay *g = b->host.opaque;
    fe2_video *v = fe2_render_runtime_video(g->host->runtime);
    g->station_draw_native(context, s);
    if (b->state.screen_active && b->state.action == FE2_STATION_ACTION_SHIP && g->station_ship_valid) {
        /* The generic station refresh paints the list and balances after
         * the input pass. Restore the offer pane after that refresh. */
        memcpy(v->pages[v->drawing] + 22*320, g->station_ship_pixels, sizeof g->station_ship_pixels);
        station_present(g);
    } else g->station_ship_valid = 0;
}
static void station_open_view(void *p)
{
    fe2_game_host_restore_color(game(p)->host, 0);
}
static void restore_view(void *p) { fe2_game_host_restore_color(game(p)->host, 1); }
static void station_clear_drawing(void *p)
{
    fe2_game_host *h = game(p)->host;
    fe2_render_disable_drawing(&h->colour, &h->colour_services);
    game(p)->session->drawing_flag = 0;
}
static void station_panel(void *p, uint16_t request, uint16_t view, const fe2_call_args *a)
{ (void)view; panel(p, request, a); }
static void station_restore_time(void *p)
{
    fe2_gameplay *g = game(p);
    fe2_session_overlay *o = &g->session->overlays[10];
    if (o->message) o->message(o->context, g->session, NULL);
}
static void message(void *p, uint16_t selector, const fe2_call_args *a)
{
    fe2_gameplay *g = game(p);
    fprintf(stderr, "Game message %04x\n", selector);
    fe2_session_station_notify(&g->station, selector, a);
}
/* orbits's references go to market's service through the session's routing. */
static void orbit_reference(void *p, uint16_t request, uint8_t handle, const fe2_call_args *a)
{
    fe2_session *s = game(p)->session;
    fe2_call_args block;
    if (!s->calls.market) return;
    if (a) block = *a; else fe2_args_clear(&block);
    (void)s->calls.market(s->calls.context[7], request, handle, &block, NULL);
}
static void population_name(void *p, uint16_t type, uint32_t seed, char out[FE2_PEOPLE_NAME_BYTES])
{ name(p, type, seed, out); }
static void panel(void *p, uint16_t request, const fe2_call_args *a)
{
    fe2_gameplay *g = game(p); fe2_objects_message m = {0}; fe2_session_overlay *o = &g->session->overlays[4];
    m.selector = request; g->panels.argument = a ? (uint16_t)a->arg[2].value : 0; g->panels.overlay = 3;
    o->message(o->context, g->session, &m);
}
static void widgets(void *p, uint16_t request, uint16_t value, const fe2_call_args *a) { (void)value; panel(p, request, a); }

/* draws a black glyph at (x+1, y+1) first. 0x118 and 0x11c do not. */
static void draw_expanded(void *p, uint16_t selector, uint8_t colour, uint16_t x, uint16_t y,
                          const fe2_call_args *a, int shadowed)
{
    fe2_gameplay *g = game(p);
    fe2_text_resources resources = g->host->resources->text;
    fe2_render_text_state state;
    fe2_video *video = fe2_render_runtime_video(g->host->runtime);
    uint8_t buffer[1024]; size_t size; unsigned i;
    for (i = 0; i < 5; ++i) resources.value[i] = a->arg[i + 1].value;
    resources.current_day = g->session->pools.clock[1];
    for (i = 0; i < 2; ++i) {
        const fe2_call_arg *arg = &a->arg[10 + i];
        resources.pointer_value[i] = arg->value;
        const char *str = NULL;
        if (arg->kind == FE2_ARG_STRING || arg->kind == FE2_ARG_SUBJECT) str = arg->ref;
        if (arg->kind == FE2_ARG_OBJECT && arg->ref) str = ((const fe2_object *)arg->ref)->name;
        if (!str && arg->value == FE2_STATION_REF_PREVIEW_NAME) str = (const char *)g->station.state.preview.name;
        /* station's named work records. */
        if (!str && arg->value == FE2_STATION_REF_WORK * (FE2_STATION_WORK_REGISTRATION + 1u))
            str = g->station.names[0];
        if (!str && arg->value == FE2_STATION_REF_WORK * (FE2_STATION_WORK_SHIP_NAME + 1u))
            str = g->station.names[1];
        /* A name reference is the object's id plus 0xf6, the name field.
         * The native record keeps that name on the object, or the system. */
        if (!str && (uint16_t)arg->value >= 0xf6) {
            fe2_ref named_ref = (arg->value & 0xffff0000u) | (uint16_t)((uint16_t)arg->value - 0xf6);
            const fe2_object *named = (uint16_t)named_ref < FE2_STATION_REF_MAP_LIST ||
                                      named_ref >= FE2_STATION_REF_WORK ? station_object(p, named_ref) : 0;
            if (named && named->name[0]) str = named->name;
            else if (g->place_name[0]) str = g->place_name;
        }
        if (str) { resources.pointer[i].data = (const uint8_t *)str; resources.pointer[i].count = strlen(str) + 1; }
    }
    if (fe2_text_expand(&resources, selector, buffer, sizeof(buffer) - 1, &size)) {
        /* A missing line must not take down the screen that asked for it. */
        fprintf(stderr, "Text expansion failed %04x\n", selector);
        ++g->text_failures;
        size = (size_t)snprintf((char *)buffer, sizeof(buffer), "%04x", selector);
    }
    buffer[size] = 0;
    /* is the wrap width. Zero draws the line as expanded. */
    if (a->arg[7].value && resources.widths)
        (void)fe2_text_wrap(buffer, size + 1, resources.widths, (unsigned)(uint16_t)a->arg[7].value);
    state.x = x; state.y = y; state.origin_x = x; state.color = colour;
    if (fe2_render_draw_text(video->pages[video->drawing], g->host->resources->font,
        g->host->resources->glyphs, g->host->colour_map, buffer, size + 1, shadowed, &state)) {
        fprintf(stderr, "Text drawing failed %04x size=%zu\n", selector, size); fatal(g, selector);
    }
}
static void text(void *p, uint16_t selector, uint8_t colour, uint16_t x, uint16_t y, const fe2_call_args *a)
{ draw_expanded(p, selector, colour, x, y, a, 1); }
/* are the formatter slots a 0x1 control indexes from 0x9e8. */
static void menu_colours(fe2_gameplay *g)
{
    unsigned i;
    for (i = 0; i < sizeof g->menu.state.format_colours; ++i)
        g->host->colour_map[1u + i] = g->menu.state.format_colours[i];
}
static void menu_text(void *p, uint16_t selector, uint8_t colour, uint16_t x, uint16_t y, const fe2_call_args *a)
{
    menu_colours(game(p));
    draw_expanded(p, selector, colour, x, y, a, 1);
}
/* a shadowed literal, the directory names. */
static void menu_string(void *p, const char *literal, uint8_t colour, uint16_t x, uint16_t y)
{
    fe2_gameplay *g = game(p);
    fe2_render_text_state state;
    fe2_video *video;
    if (!literal) return;
    menu_colours(g);
    video = fe2_render_runtime_video(g->host->runtime);
    state.x = x; state.y = y; state.origin_x = x; state.color = colour;
    (void)fe2_render_draw_text(video->pages[video->drawing], g->host->resources->font,
        g->host->resources->glyphs, g->host->colour_map, (const uint8_t *)literal, strlen(literal) + 1, 1, &state);
}
/* fills the name box. The bottom is the fourth vertex, kept in 0x57ed. */
static void menu_box(void *p, uint16_t left, uint16_t top, uint16_t right)
{
    fe2_gameplay *g = game(p);
    fe2_video_rectangle(fe2_render_runtime_video(g->host->runtime), left, right, (uint8_t)top,
        (uint8_t)g->menu.state.edit_bottom, g->menu.state.line_colour, 0);
}
static void menu_redraw_name(fe2_gameplay *g, const char *literal, uint8_t colour, uint16_t x, uint16_t y)
{
    fe2_video *v = fe2_render_runtime_video(g->host->runtime);
    fe2_render_text_state state;
    unsigned page;
    state.x = x; state.y = y; state.origin_x = x; state.color = colour;
    for (page = 0; page < 2; ++page) {
        state.x = x;
        (void)fe2_render_draw_text(v->pages[page], g->host->resources->font, g->host->resources->glyphs,
            g->host->colour_map, (const uint8_t *)literal, strlen(literal) + 1, 0, &state);
    }
}
/*, BP bit 15 set: letters, digits and underscore. Enter returns.
 * Backspace paints the old name in black, then the shorter one. */
/* A file error waits out one timer tick with the message on screen. The
 * frame loop is inside this wait, so the tick and the window pump run
 * here. A held suppression would otherwise swallow the dismissing key,
 * and a click is a key once the pointer is sampled. */
static void menu_wait(void *p)
{
    fe2_gameplay *g = game(p);
    fe2_session *s = g->session;
    uint8_t suppressed = s->input.suppress;
    if (g->host->present)
        (void)fe2_video_present(fe2_render_runtime_video(g->host->runtime),
            g->host->present, g->host->present_context);
    s->input.suppress = 0;
    if (g->host->pump) (void)g->host->pump(g->host->present_context);
    s->input.suppress = suppressed;
    fe2_input_tick(&s->input, &s->pointer);
    fe2_session_update_pointer(s, NULL, NULL);
}
static void menu_edit(void *p, char *literal, uint16_t length, uint8_t colour, uint16_t x, uint16_t y,
                      const fe2_call_args *a)
{
    fe2_gameplay *g = game(p);
    size_t n;
    int room;
    (void)a;
    if (!literal || !length) return;
    n = strlen(literal);
    room = (int)length - (int)n;
    for (;;) {
        uint16_t key;
        menu_redraw_name(g, literal, colour, x, y);
        if (g->host->present)
            (void)fe2_video_present(fe2_render_runtime_video(g->host->runtime), g->host->present, g->host->present_context);
        for (;;) {
            key = fe2_session_poll(g->session);
            if (key) break;
            if (!g->host->pump || g->host->pump(g->host->present_context)) return;
        }
        key = (uint8_t)key;
        if (key == 0x0d) return;
        if (key == 8) {
            if (!n) continue;
            menu_redraw_name(g, literal, 0, x, y);
            literal[--n] = 0;
            ++room;
            continue;
        }
        if (!((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z') ||
              (key >= 'a' && key <= 'z') || key == '_')) continue;
        if (room <= 0) continue;
        literal[n++] = (char)key;
        literal[n] = 0;
        --room;
    }
}
static void text_word(void *p, uint16_t selector, uint16_t colour, uint16_t x, uint16_t y, const fe2_call_args *a)
{ text(p, selector, (uint8_t)colour, x, y, a); }
/* trusts the caller's rows; 0x90 clips both endpoints first.
 * Both preserve the original run rounding and directional endpoint bias. */
static void stroke(void *p, fe2_screen_point first, fe2_screen_point second, uint16_t colour)
{
    fe2_gameplay *g = game(p);
    fe2_video *v = fe2_render_runtime_video(g->host->runtime);
    (void)fe2_render_solid_line_unchecked(first, second, (uint8_t)colour, fe2_video_span, v);
}
static void line(void *p, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    fe2_screen_point first = {(int16_t)x0, (int16_t)y0}, second = {(int16_t)x1, (int16_t)y1};
    stroke(p, first, second, game(p)->session->resident.line_colour);
}
static void clipped_line(void *p, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    fe2_screen_point first = {(int16_t)x0, (int16_t)y0}, second = {(int16_t)x1, (int16_t)y1};
    (void)fe2_render_draw_line(first, second, game(p)->session->resident.line_colour, stroke, p);
}
/* returns the palette slot for an RGB12 colour. 0x200 turns that
 * slot into the DAC index the line and the range label are drawn with. */
static uint16_t palette_rgb(void *p, uint16_t rgb)
{
    fe2_palette_cache *cache = fe2_render_runtime_palette(game(p)->host->runtime);
    int slot = cache ? fe2_palette_lookup(cache, rgb) : -1;
    return slot < 0 ? 0 : (uint16_t)slot;
}
static uint16_t palette_index(void *p, uint8_t handle, uint16_t colour)
{
    fe2_palette_cache *cache = fe2_render_runtime_palette(game(p)->host->runtime);
    int dac;
    (void)handle;
    dac = cache ? fe2_palette_index(cache, colour) : -1;
    return dac < 0 ? 0 : (uint16_t)dac;
}
static void sprite(void *p, uint16_t selector, uint16_t x, uint16_t y)
{
    fe2_gameplay *g = game(p); fe2_render_bytes data;
    if (fe2_graphics_cache_get(fe2_render_runtime_graphics(g->host->runtime), selector, &data) ||
        fe2_video_blit(fe2_render_runtime_video(g->host->runtime), data, (int16_t)x, (int16_t)y,
            g->host->sprite_work, sizeof(g->host->sprite_work))) fatal(g, selector);
}
/* composites draws the station portrait into the left-hand well: a filled
 * rectangle, palette uploads from 0x80, then the face sprites. */
static int portrait_rectangle(void *p, uint16_t left, uint16_t right, uint16_t top, uint16_t bottom, uint8_t color)
{
    return fe2_video_rectangle(fe2_render_runtime_video(game(p)->host->runtime),
        left, right, (uint8_t)top, (uint8_t)bottom, color, 0);
}
static int portrait_palette(void *p, uint8_t start, uint8_t count, const uint8_t *rgb6)
{
    fe2_gameplay *g = game(p);
    fe2_video *v = fe2_render_runtime_video(g->host->runtime);
    unsigned i;
    /* These entries are the scene's dynamic colours. Present keeps them
     * for this frame and restores the scene on the next one. */
    g->host->dac_clobbered = 1;
    g->host->palette_frame = 1;
    for (i = 0; i < count; ++i)
        fe2_video_palette(v, (uint8_t)(start + i), rgb6[i * 3], rgb6[i * 3 + 1], rgb6[i * 3 + 2]);
    return 0;
}
static int portrait_sprite(void *p, uint16_t resource, int16_t x, int16_t y)
{
    fe2_gameplay *g = game(p); fe2_render_bytes data;
    if (fe2_graphics_cache_get(fe2_render_runtime_graphics(g->host->runtime), resource, &data)) return -1;
    return fe2_video_blit(fe2_render_runtime_video(g->host->runtime), data, x, y,
        g->host->sprite_work, sizeof(g->host->sprite_work));
}
static void panel_present(void *p) { (void)p; /* The session presents after scene and cockpit drawing. */ }
static void install_gauges(void *p, fe2_gauge_callback callback) { (void)p; (void)callback; }
static uint8_t notify_panel(void *p, uint16_t overlay, uint16_t request)
{
    fe2_gameplay *g = game(p);
    if (overlay == 12) return (uint8_t)fe2_session_station_resume(&g->station, (int16_t)request);
    if (overlay == 2) {
        int changed = 0;
        /* panels's +0x24 notification carries AX=1/0xffff and consumes AL.
         * It is not the resident selection-away call with AX=0. */
        ++g->map.depth;
        g->map.status = fe2_map_enter_view(&g->map.state, &g->map.services, (int16_t)request, &changed);
        --g->map.depth;
        if (g->map.status) g->session->error = 1;
        return (uint8_t)changed;
    }
    /* Flight's +0x24 is the same entry. AX=0 hides the cockpit; a panel
     * notice is 1 or 0xffff and must not. */
    if (overlay == 3) {
        fe2_flight_change_view(&g->flight.state, &g->flight.services, request);
        return 0;
    }
    if (overlay < FE2_SESSION_OVERLAYS && g->session->overlays[overlay].deselect)
        g->session->overlays[overlay].deselect(g->session->overlays[overlay].context, g->session);
    return 0;
}
static fe2_object *linked_object(void *p, fe2_object *o)
{ int64_t out[3]; absolute(p, o, out); return game(p)->session->objects.records[o->parent_handle]; }
static void panel_relative(void *p, fe2_object *o, const fe2_object *viewer, fe2_wide_vector *out)
{
    int64_t v[3]; unsigned i; relative(p, o, (fe2_object *)viewer, v);
    for (i = 0; i < 3; ++i) { out->low[i] = (uint32_t)v[i]; out->high[i] = (uint32_t)((uint64_t)v[i] >> 32); }
}
static fe2_model_colour model_colour(void *p, uint16_t number)
{
    const fe2_flight_model *m = flight_model(p, number); fe2_model_colour out = {0, 0};
    if (m) { out.present = m->stats_offset != 0; out.value = m->mass; } return out;
}
static void panel_marker(void *p, uint16_t x, uint16_t y, uint16_t height, const fe2_call_args *args)
{
    /* panels supplies DS:0x7c8 and DS:0x7dc as numbered parameters 3 and 8.
     * Marker colour is independent of the current line colour. */
    fe2_video_scanner_marker(fe2_render_runtime_video(game(p)->host->runtime),
        x, (uint8_t)y, (int16_t)height, (uint8_t)args->arg[8].value,
        (uint16_t)args->arg[3].value != 0);
}

/* is clear_viewport_index_127: station, map and preferences fill
 * the view with 0x7f, the published background, rather than black. */
static void menu_clear(void *p)
{
    fe2_video_clear_view(fe2_render_runtime_video(game(p)->host->runtime), 0x7f);
}
/* BX packs red and green; the third argument is blue. AX nonzero asks
 * service 0x248 to prepare first. The following 0x1dc clear is index 127, so
 * this triple is the panel. The station screen passes (0x17, 0x13, 0x27). */
static void menu_controls(void *p, uint16_t ax, uint16_t bx, uint8_t buttons)
{
    fe2_gameplay *g = game(p);
    fe2_render_rgb rgb;
    rgb.red = (uint8_t)bx;
    rgb.green = (uint8_t)(bx >> 8);
    rgb.blue = buttons;
    /* The frame draw publishes space or the atmosphere into DAC 127 before
     * the panel. The updater skips an unchanged triple, so publish this one
     * again or the clear would keep that earlier colour. */
    g->host->colour.last_127.red = (uint8_t)~rgb.red;
    fe2_render_set_color(&g->host->colour, &g->host->colour_services, ax != 0, rgb);
}
static void menu_select(void *p, uint16_t request, const fe2_call_args *args)
{
    fe2_gameplay *g = game(p); fe2_objects_message m = {0};
    g->panels.overlay = 10; g->panels.argument = (uint16_t)args->arg[2].value;
    m.selector = request;
    g->session->overlays[4].message(g->session->overlays[4].context, g->session, &m);
}
static void menu_refresh(void *p)
{
    fe2_gameplay *g = game(p);
    g->session->overlays[3].activate(g->session->overlays[3].context, g->session, 0xf1);
}
static void menu_draw(void *p, fe2_session *s)
{
    fe2_session_commander_overlay *b = p;
    (void)s;
    /* The native input entry draws changed widgets. This host clears the
     * viewport for each world frame, so repaint an open preferences page. */
    if (b->state.menu_mode == FE2_MENU_PREFERENCES)
        fe2_commander_draw_preferences(&b->state, &b->services);
    else if (b->state.menu_mode != FE2_MENU_CLOSED)
        fe2_commander_draw_dialog(&b->state, &b->services);
}

/* Slot 0's start: the empty pool, the root record and the starting system's
 * key. flight's start asks drives (mode 0) to generate and instantiate the system
 * chart selects, as the original does. */
static void initialize_world(void *p, fe2_session *s, uint16_t selection)
{
    fe2_gameplay *g = game(p);
    const char *name;
    if ((selection & 255u) != 1) return;
    fe2_pool_clear(&s->pools); g->bodies = 0;
    fe2_object_read_template(&s->records[0], fe2_systems_static_object, 1);
    s->objects.status[0] = 0x10;
    s->resident.current_system = (selection >> 8) == 1 ? UINT32_C(0xa925c600) :
        (selection >> 8) == 2 ? UINT32_C(0xa8f5c540) : UINT32_C(0xa925c5c1);
    name = (selection >> 8) == 1 ? "Sol" : (selection >> 8) == 2 ? "Lave" : "Ross 154";
    s->render_mode = 128; s->light[0] = 0; s->light[1] = 0; s->light[2] = (uint16_t)-32768;
    snprintf(g->place_name, sizeof g->place_name, "%s", name);
}

static void map_string(fe2_gameplay *g, const char *text, uint8_t colour, uint16_t x, uint16_t y)
{
    fe2_render_text_state state;
    fe2_video *video = fe2_render_runtime_video(g->host->runtime);
    if (!text) return;
    if (!colour) colour = 15;
    state.x = x; state.y = y; state.origin_x = x; state.color = colour;
    (void)fe2_render_draw_text(video->pages[video->drawing], g->host->resources->font,
        g->host->resources->glyphs, g->host->colour_map, (const uint8_t *)text, strlen(text) + 1, 1, &state);
}
static int map_expand(void *p, const char *source, const fe2_call_args *a, char *out, size_t capacity)
{
    fe2_text_resources r = game(p)->host->resources->text;
    uint8_t directory[FE2_MAP_TEXT_CAPACITY + 2];
    size_t n, length;
    unsigned i;
    if (!source) source = "";
    n = strlen(source);
    if (!out || !capacity || n + 3 > sizeof directory) return -1;
    directory[0] = 2; directory[1] = 0; memcpy(directory + 2, source, n + 1);
    r.module = (fe2_text_directory){{directory, n + 3}, 1};
    for (i = 0; i < 5; ++i) r.value[i] = a ? a->arg[i + 1].value : 0;
    r.pointer_value[0] = a ? a->arg[10].value : 0; r.pointer_value[1] = a ? a->arg[11].value : 0;
    r.current_day = game(p)->session->pools.clock[1];
    if (fe2_text_expand(&r, 0x4000, (uint8_t *)out, capacity - 1, &length)) return -1;
    out[length] = 0; return 0;
}
static int map_number(void *p, uint16_t selector, const fe2_call_args *a, char *out, size_t capacity)
{
    fe2_text_resources r = game(p)->host->resources->text;
    size_t length;
    unsigned i;
    if (!out || !capacity) return -1;
    for (i = 0; i < 5; ++i) r.value[i] = a ? a->arg[i + 1].value : 0;
    r.pointer_value[0] = a ? a->arg[10].value : 0; r.pointer_value[1] = a ? a->arg[11].value : 0;
    if (fe2_text_expand(&r, selector, (uint8_t *)out, capacity - 1, &length)) return -1;
    out[length] = 0; return 0;
}
static int map_length(void *p, const int16_t components[3], uint16_t *value)
{
    (void)p;
    *value = fe2_math_length3(components[0], components[1], components[2]);
    return 0;
}
static int map_details(void *p, uint32_t reference, unsigned mode, fe2_map_details *out)
{
    const fe2_catalogue_record *record = fe2_system_find(game(p)->catalogue.catalogue, reference);
    memset(out, 0, sizeof(*out));
    if (!record) return -1;
    if (mode == 1) {
        fe2_catalogue_trade trade = fe2_system_trade_of(record);
        out->population_text = trade.population_band;
        out->result_a = trade.economic_factor_index;
        out->government = trade.government;
        if (trade.trade_codes) memcpy(out->trade, trade.trade_codes, sizeof out->trade);
    } else {
        fe2_system_profile profile = fe2_system_profile_of(record);
        out->status = (uint8_t)profile.resource_richness;
        out->result_a = profile.map_text; out->result_b = profile.view_text;
        out->government = profile.government;
    }
    return 0;
}
static int map_travel(void *p, const fe2_object *ship, uint32_t distance, uint16_t *result, int *negative)
{
    (void)p; (void)ship;
    *negative = 0;
    *result = distance > 0xffffu ? 0xffffu : (uint16_t)distance;
    return 0;
}
static int map_density(void *p, uint16_t y, uint16_t x, fe2_map_samples *out)
{
    fe2_density_samples samples;
    (void)p;
    /* The map passes the horizontal sector first. */
    if (fe2_density_neighbors(y, x, &samples)) return -1;
    out->a = samples.a; out->b = samples.b; out->c = samples.c; out->d = samples.d;
    return 0;
}
static int map_basis(void *p, uint16_t angles[2], int16_t matrix[9])
{
    uint16_t y;
    angles[0] = (uint16_t)(pointer(p, &y) << 7);
    angles[1] = (uint16_t)(y << 7);
    fe2_math_basis_b(angles[1], angles[0], matrix);
    return 0;
}
static int map_project(void *p, const int16_t matrix[9], const int16_t vector[3], uint32_t projected[3])
{
    (void)p; fe2_map_project_vector(matrix, vector, projected); return 0;
}
static int map_button(void *p, uint16_t button, uint16_t x, uint16_t y)
{
    /* chart passes the original sprite selector directly to 0xf8. */
    sprite(p, button, x, y);
    return game(p)->session->error ? -1 : 0;
}
static int map_panel(void *p, unsigned request, uint16_t banner)
{
    fe2_session *s = game(p)->session;
    fe2_call_args args;
    fe2_args_clear(&args); fe2_args_word(&args, 2, banner);
    /* chart's primary-panel calls carry BX=2, not the flight overlay. */
    s->calls.panel(s->calls.context[4], (uint16_t)request, 2, &args);
    return s->error ? -1 : 0;
}
static int map_text(void *p, const char *text, uint8_t colour, uint16_t x, uint16_t y, int centered)
{
    (void)centered; map_string(game(p), text, colour, x, y); return 0;
}
static int map_resident_text(void *p, uint16_t selector, uint8_t colour, uint16_t x, uint16_t y, const fe2_call_args *a)
{
    fe2_gameplay *g = game(p);
    fe2_text_resources resources = g->host->resources->text;
    uint8_t buffer[128]; size_t size; unsigned i;
    char fallback[32];
    for (i = 0; i < 5; ++i) resources.value[i] = a ? a->arg[i + 1].value : 0;
    resources.current_day = g->session->pools.clock[1];
    if (!fe2_text_expand(&resources, selector, buffer, sizeof(buffer) - 1, &size)) {
        buffer[size] = 0; map_string(g, (char *)buffer, colour, x, y); return 0;
    }
    snprintf(fallback, sizeof(fallback), "%d %d", a ? (int)(int16_t)a->arg[1].value : 0,
        a ? (int)(int16_t)a->arg[2].value : 0);
    map_string(g, fallback, colour, x, y);
    return 0;
}
static void map_plot(fe2_gameplay *g, fe2_video *video, int x, int y, uint8_t colour)
{
    if (x < 0 || x >= 320 || y < 0 || y >= 168) return;
    fe2_video_pixel(video, (uint16_t)x, (uint8_t)y, colour);
    ++g->map_pixels;
}
static int map_scene(void *p, const fe2_map_scene *scene, int extra)
{
    fe2_gameplay *g = game(p);
    fe2_render_model_transform t = {0};
    fe2_render_program_result result;
    fe2_render_bytes descriptor, context;
    uint8_t records[4 * (FE2_MAP_MAX_SYSTEMS + 1)];
    uint16_t values[86] = {0};
    unsigned i, count = scene->count + (scene->has_origin ? 1u : 0u);
    if (scene->draw_kind & 1u || scene->draw_kind / 2 >= 240) return -1;
    descriptor = g->host->resources->models[1][scene->draw_kind / 2];
    if (!descriptor.data || scene->count > FE2_MAP_MAX_SYSTEMS) return -1;
    memcpy(t.basis, scene->basis, sizeof t.basis);
    for (i = 0; i < 3; ++i) t.position[i] = (int32_t)scene->position[i];
    t.exponent = fe2_model_word(descriptor.data, 0x0a); t.material = 0x111;
    memcpy(t.direction, g->session->light, sizeof t.direction);
    values[1] = scene->flags; values[2] = scene->sector_y; values[3] = scene->sector_x;
    values[4] = scene->draw_kind == 22 || scene->draw_kind == 24 ?
        scene->marker_range : (uint16_t)count;
    for (i = 0; i < scene->count; ++i) values[5 + i] = scene->identifiers[i];
    for (i = 0; i < count; ++i) {
        const fe2_map_record *r = scene->has_origin && !i ? &scene->origin :
            &scene->records[i - (scene->has_origin ? 1u : 0u)];
        records[4*i] = r->depth; records[4*i+1] = r->style;
        records[4*i+2] = r->y; records[4*i+3] = r->x;
    }
    context.data = records; context.count = 4u * count;
    result = fe2_render_runtime_submit_alternate(g->host->runtime, scene->draw_kind, &t,
        (fe2_render_words){values, 86}, extra ? NULL : &context, count * 2u);
    if (result.status != FE2_RENDER_PROGRAM_COMPLETE) {
        fprintf(stderr, "Map model %u failed: %u at %zu\n", scene->draw_kind, result.status, result.cursor);
        return -1;
    }
    g->map_pixels += scene->count;
    return 0;
}

static int map_pixel(void *p, uint16_t x, uint8_t y, uint8_t colour)
{
    map_plot(game(p), fe2_render_runtime_video(game(p)->host->runtime), (int)x, (int)y, colour); return 0;
}
/* with DX cleared: the size-0 disc is the single centre pixel. */
static int map_crosshair(void *p, uint16_t x, uint16_t y, uint8_t colour)
{
    fe2_gameplay *g = game(p);
    map_plot(g, fe2_render_runtime_video(g->host->runtime), (int)x, (int)y, colour);
    return 0;
}
/* the galactic view's scale bar. The colour is the argument, which the
 * chart stored in 0x57fd before the call. */
static int map_chart_line(void *p, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t colour)
{
    fe2_gameplay *g = game(p);
    uint16_t saved = g->session->resident.line_colour;
    g->session->resident.line_colour = (uint16_t)(colour | ((uint16_t)colour << 8));
    line(g, x0, y0, x1, y1);
    g->session->resident.line_colour = saved;
    return 0;
}
static int map_block(void *p, uint16_t left, uint16_t right, uint8_t top, uint8_t bottom, uint8_t colour)
{
    fe2_gameplay *g = game(p);
    if (left > right || top > bottom || left >= 320 || top >= 168) return 0;
    if (right >= 320) right = 319; if (bottom >= 168) bottom = 167;
    fe2_video_rectangle(fe2_render_runtime_video(g->host->runtime), left, right, top, bottom, colour, 0);
    g->map_pixels += (unsigned)(right - left + 1) * (unsigned)(bottom - top + 1);
    return 0;
}
/* Activation and the recenter key pass AX=BX=0 to 0x14c. The chart builds
 * its basis from that stick, so it opens on the galactic plane. */
static int map_clear_projection(void *p)
{
    set_pointer(p, 0, 0);
    return 0;
}
static int map_generate(void *p, const fe2_map_query *q, fe2_record_space *out, uint8_t *named)
{
    fe2_gameplay *g = game(p);
    fe2_system_generation generation = {0};
    fe2_system_request request = {0};
    out->records = g->map_records; out->count = sizeof g->map_records / sizeof g->map_records[0];
    memset(g->map_records, 0, sizeof g->map_records);
    request.seed_a = q->coordinate_mix; request.seed_b = q->table_value;
    request.key = q->reference; request.limit = q->classification;
    request.root_name = q->name; request.danger = q->status;
    fe2_systems_action(&generation, &g->systems.services, *out, &request);
    *named = generation.named_bodies;
    return g->session->error ? -1 : 0;
}
static int map_clear(void *p) { menu_clear(p); return 0; }
static int map_show(void *p) { fe2_pointer_regions_reset(&game(p)->session->regions); return 0; }
static int map_palette(void *p, const uint8_t *rgb, unsigned count)
{
    fe2_gameplay *g = game(p);
    fe2_video *v = fe2_render_runtime_video(g->host->runtime);
    unsigned i;
    if (!rgb || count > 128) return -1;
    if (!g->map_palette_saved) {
        memcpy(g->map_saved_palette, v->palette + 128, sizeof g->map_saved_palette);
        g->map_palette_saved = 1;
    }
    for (i = 0; i < count; ++i)
        fe2_video_palette(v, (uint8_t)(128+i), rgb[3*i], rgb[3*i+1], rgb[3*i+2]);
    return 0;
}
static int map_reset(void *p)
{
    fe2_gameplay *g = game(p);
    if (g->map_palette_saved)
        memcpy(fe2_render_runtime_video(g->host->runtime)->palette + 128, g->map_saved_palette, sizeof g->map_saved_palette);
    g->map_palette_saved = g->map_page_valid = 0; return 0;
}
static int map_present(void *p)
{
    fe2_gameplay *g = game(p);
    fe2_video *v = fe2_render_runtime_video(g->host->runtime);
    /* The view is cleared every frame; the panel strip is not. Keep the
     * presented page in the view and leave the strip to panels and navigation. */
    memcpy(g->map_page, v->pages[v->drawing], 320u * 168u);
    memcpy(g->map_palette, v->palette, sizeof g->map_palette);
    g->map_page_valid = 1; return 0;
}
static int map_flow(void *p, const char *text, uint8_t colour, uint16_t *x, uint16_t *y)
{
    fe2_gameplay *g = game(p);
    fe2_render_text_state state = {*x, *y, *x, colour};
    fe2_video *v = fe2_render_runtime_video(g->host->runtime);
    int result = fe2_render_draw_text(v->pages[v->drawing], g->host->resources->font,
        g->host->resources->glyphs, g->host->colour_map, (const uint8_t *)text, strlen(text) + 1, 1, &state);
    *x = state.x; *y = state.y; return result;
}
static int map_node(void *p, uint16_t selection, uint16_t spacing, uint16_t *x, uint16_t *y)
{
    return fe2_pointer_regions_add(&game(p)->session->regions, *x, *y, spacing, spacing, selection);
}
static int map_body_sprite(void *p, uint16_t id, uint16_t x, uint16_t y)
{ sprite(p, id, x, y); return game(p)->session->error ? -1 : 0; }
static int map_body_scale(void *p, uint16_t ma, uint16_t ex, uint16_t factor, uint16_t exponent,
                          uint16_t *out_ma, uint16_t *out_ex)
{
    fe2_scalar result = multiply(p, (fe2_scalar){ma, ex}, (fe2_scalar){factor, exponent});
    *out_ma = result.mantissa; *out_ex = result.exponent; return 0;
}
static int map_body_number(void *p, uint16_t ma, uint16_t ex, int sign, uint32_t *out)
{
    fe2_scalar value = {ma, ex}; (void)p;
    *out = sign ? (uint32_t)fe2_math_to_signed32(value) : fe2_math_to_unsigned32(value); return 0;
}
static void map_draw(void *context, fe2_session *s)
{
    fe2_session_map_overlay *b = context;
    fe2_gameplay *g = b->host.opaque;
    fe2_video *v = fe2_render_runtime_video(g->host->runtime);
    (void)s;
    if (b->state.mode > 0 && g->map_page_valid) {
        memcpy(v->pages[v->drawing], g->map_page, 320u * 168u);
        memcpy(v->palette, g->map_palette, sizeof g->map_palette);
    } else {
        g->map_page_valid = 0;
        if (fe2_render_runtime_layer(g->host->runtime)) {
            fprintf(stderr, "Map layer failed mode %d\n", b->state.mode);
            s->error = 1; return;
        }
        g->map_draw_native(context, s);
        if (fe2_render_runtime_draw(g->host->runtime)) {
            fprintf(stderr, "Map draw failed mode %d status %d\n", b->state.mode, b->status);
            s->error = 1;
        }
    }
}
static void flight_draw(void *context, fe2_session *s)
{
    fe2_session_flight_overlay *b = context;
    fe2_gameplay *g = b->host.opaque;
    /* The map draws before slot 3. Cockpit reticles must not overwrite its
     * information pages after the cockpit has been deselected. */
    if (g->map.state.mode == 0 && !s->metadata[5].draw) g->flight_draw_native(context, s);
}
static const fe2_ship_type *status_ship(void *p, uint16_t model_id)
{
    static fe2_ship_type type;
    fe2_render_bytes bytes;
    memset(&type, 0, sizeof type);
    type.size = 0x40; type.hull_mass = 100; type.name_text = 0x9c02;
    if (model_id & 1u || model_id / 2 >= 240) return &type;
    bytes = game(p)->host->resources->models[0][model_id / 2];
    if (bytes.data && bytes.count >= 0x1e) {
        unsigned stats = fe2_model_word(bytes.data, 0x1c);
        type.radius[0] = fe2_model_word(bytes.data, 0x0a); type.radius[1] = fe2_model_word(bytes.data, 0x0c);
        type.scale = fe2_model_word(bytes.data, 0x16);
        if (stats + 0x10 <= bytes.count) {
            uint16_t mass = fe2_model_word(bytes.data + stats, 6);
            uint16_t size = fe2_model_word(bytes.data + stats, 0x0c);
            uint16_t name = fe2_model_word(bytes.data + stats, 0x0e);
            if (mass) type.hull_mass = mass;
            if (size) type.size = size;
            if (name) type.name_text = name;
        }
    }
    return &type;
}
/* drives's ship statistics: the model's statistics record (descriptor + [0x1c]). */
static fe2_ship_stats drive_stats(void *p, uint16_t model)
{
    fe2_ship_stats stats = {0, 0};
    fe2_render_bytes bytes;
    unsigned at;
    if ((model & 1u) || model / 2 >= 240) return stats;
    bytes = game(p)->host->resources->models[0][model / 2];
    if (!bytes.data || bytes.count < 0x1e) return stats;
    at = fe2_model_word(bytes.data, 0x1c);
    if (at + 0x0a > bytes.count) return stats;
    stats.hull_mass = fe2_model_word(bytes.data + at, 6);
    stats.drive_limit = fe2_model_word(bytes.data + at, 8);
    return stats;
}
/* drives's systems system action, through the session's call table. */
static void drive_system(void *p, const fe2_jump_target *target, const fe2_call_args *args)
{
    /* chart request 0 leaves these in the shared block: 0x7c8 root kind,
     * 0x7d4 key, 0x7d8 danger. Arrival only writes the mode. */
    static const unsigned shared[] = {3, 6, 7};
    fe2_gameplay *g = game(p);
    fe2_session *s = g->session;
    fe2_overlay_system system = {0, 0, 0, 0};
    fe2_call_args block;
    unsigned i;
    if (!s->calls.system || !args) return;
    if (target) {
        system.seed_a = target->seed_a; system.seed_b = target->seed_b;
        system.name = target->name; system.reference = target->system;
        /* The system being entered names the place. */
        if (target->name) snprintf(g->place_name, sizeof g->place_name, "%s", target->name);
    }
    block = *args;
    for (i = 0; i < sizeof shared / sizeof shared[0]; ++i)
        if (block.arg[shared[i]].kind == FE2_ARG_UNSET)
            block.arg[shared[i]] = g->hyperspace.system_reply.arg[shared[i]];
    s->calls.system(s->calls.context[0], &system, &block);
}
/* the current pool becomes the saved one, emptied. */
/* The misjump elsewhere (selector 6): resident 0x84 releases the ship with
 * a zero request; the tag in AL is not read. */
static void drive_misjump(void *p, fe2_object *ship, uint8_t player_handle)
{ (void)player_handle; population_remove(p, ship); }
static void drive_clear(void *p, const fe2_call_args *args)
{ (void)args; fe2_pool_clear(&game(p)->session->pools); }
static void status_clear(void *p)
{
    fe2_video_clear_view(fe2_render_runtime_video(game(p)->host->runtime), 0x7f);
}
static void status_frame(void *p, const fe2_rect *frame, uint16_t style)
{
    fe2_video *video;
    (void)style;
    if (!frame || frame->left >= frame->right || frame->top >= frame->bottom) return;
    video = fe2_render_runtime_video(game(p)->host->runtime);
    fe2_video_rectangle(video, frame->left, frame->right, frame->top, frame->bottom, 0, 0);
}
static void status_prepare(void *p, fe2_object *camera, const fe2_call_args *a)
{
    const fe2_object *ship = a->arg[8].ref;
    (void)p;
    if (ship) *camera = *ship;
}
static void status_preview(void *p, fe2_object *camera)
{
    const fe2_rect bounds = {0xbd, 0x13d, 2, 0x82};
    preview(p, camera, &bounds);
}
static void status_text(void *p, uint16_t selector, uint8_t colour, uint16_t x, uint16_t y, const fe2_call_args *a)
{
    text(p, selector, colour ? colour : 15, x, y, a);
}
static void status_drive(void *p, uint16_t request, const fe2_call_args *a, uint16_t *first, uint16_t *second)
{
    fe2_session *s = game(p)->session;
    fe2_call_args args;
    if (a) args = *a; else fe2_args_clear(&args);
    if (!s->calls.drive) { s->error = 1; return; }
    (void)s->calls.drive(s->calls.context[16], request, 0, &args);
    if (first) *first = (uint16_t)args.arg[3].value;
    if (second) *second = (uint16_t)args.arg[5].value;
}
static int map_controls(void *p, uint16_t layout, uint8_t buttons) { (void)p; (void)layout; (void)buttons; return 0; }

#include "navigation_host.h"

int fe2_gameplay_start(fe2_gameplay *g, fe2_game_host *host, fe2_intro_audio *audio, uint16_t selection)
{
    fe2_session *s;
    fe2_system_services *v;
    fe2_flight_services *f;
    unsigned i;
    if (!g || !host || !host->session || host->session->busy ||
        (selection & 255u) != 1 || selection >> 8 > 5 || selection >> 8 == 3) return -1;
    s = host->session;
    if (host->in_frame) {
        if (fe2_render_runtime_abort(host->runtime)) return -1;
        host->in_frame = 0;
    }
    memset(g, 0, sizeof(*g)); g->host = host; g->session = s; g->audio = audio; host->gameplay = g;
    for (i = 0; i < 240; ++i) {
        fe2_render_bytes b = host->resources->models[0][i]; const uint8_t *p = b.data; unsigned stats, j;
        if (!p || b.count < 0x1e) continue;
        g->system_models[i].radius[0] = fe2_model_word(p, 0x0a); g->system_models[i].radius[1] = fe2_model_word(p, 0x0c);
        g->system_models[i].scale = fe2_model_word(p, 0x16);
        g->flight_models[i].scale_shift = fe2_model_word(p, 0x0a); g->flight_models[i].extent = fe2_model_word(p, 0x0e);
        stats = fe2_model_word(p, 0x1c); g->flight_models[i].stats_offset = (uint16_t)stats;
        if (stats + 0x10 > b.count) continue;
        g->flight_models[i].mass = fe2_model_word(p + stats, 6); g->flight_models[i].ship_name_text = fe2_model_word(p + stats, 0xe);
        /* Small craft omit turret pairs. Their shorter statistics records
         * still contain the front/rear eye positions and flight mass. */
        for (j = 0; j < 12 && stats + 0x1a + j * 2 <= b.count; ++j)
            g->flight_models[i].views[j] = (int16_t)fe2_model_word(p + stats, 0x18 + j * 2);
    }
    if (fe2_session_bind_objects(s, &g->objects) || fe2_session_bind_systems_overlay(s, &g->systems) ||
        fe2_session_bind_orbits(s, &g->orbits) || fe2_session_bind_flight_overlay(s, &g->objects, &g->flight) ||
        fe2_session_bind_panels_overlay(s, &g->objects, &g->panels) ||
        fe2_session_bind_offences_overlay(s, &g->objects, &g->offences) ||
        fe2_session_bind_people_overlay(s, &g->objects, &g->population) ||
        fe2_session_bind_commander_overlay(s, &g->objects, &g->menu) ||
        fe2_session_bind_map_overlay(s, &g->objects, &g->map) ||
        fe2_session_bind_status_overlay(s, &g->objects, &g->status) ||
        fe2_session_bind_station_overlay(s, &g->objects, &g->station) ||
        fe2_session_bind_navigation_overlay(s, &g->objects, &g->navigation) ||
        fe2_session_bind_market_overlay(s, &g->objects, &g->market) ||
        fe2_session_bind_hyperspace_overlay(s, &g->objects, &g->hyperspace) ||
        fe2_session_bind_mining_overlay(s, &g->objects, &g->mining) ||
        fe2_session_bind_catalogue(s, &g->catalogue) || fe2_session_bind_names(s, &g->names) ||
        fe2_session_bind_portraits(s, &g->portraits) || fe2_session_bind_interaction(s, &g->interaction)) return -1;
    g->portraits.host.opaque = g;
    g->portraits.host.rectangle = portrait_rectangle;
    g->portraits.host.palette = portrait_palette;
    g->portraits.host.sprite = portrait_sprite;
    if (fe2_dos_host_init(&g->files, ".", 'C')) return -1;
    g->file_services = fe2_dos_host_platform(&g->files);
    g->menu.host.opaque = g; g->menu.host.dos = &g->file_services;
    g->menu.host.save_image = image_save; g->menu.host.restart_game = image_restore;
    g->menu.host.object_record = image_record;
    g->menu.services.dos = &g->file_services; g->menu.state.drive = 'C';
    /* writes the available drive letters at 0x8921. This host maps one. */
    g->menu.state.drives[0] = 'C'; g->menu.state.drives[1] = 0;
    g->menu.host.clear_panel = menu_clear; g->menu.host.restore_view = restore_view;
    g->menu.host.set_controls = menu_controls; g->menu.host.present = panel_present;
    g->menu.host.draw_sprite = sprite; g->menu.host.draw_icon = sprite;
    g->menu.host.draw_text = menu_text; g->menu.host.draw_string = menu_string;
    g->menu.host.draw_edit_box = menu_box; g->menu.host.edit_text = menu_edit;
    g->menu.host.wait_tick = menu_wait;
    g->menu.host.play_effect = play;
    g->menu.host.select_panel = menu_select; g->menu.host.refresh_cockpit = menu_refresh;
    g->menu.host.show_message = message;
    s->overlays[10].initialize(s->overlays[10].context, s, 0);
    s->overlays[12].initialize(s->overlays[12].context, s, 0);
    g->position.saved = g->position.current = &s->objects;
    g->reference.position = &g->position;
    g->motion.pool = &s->objects;
    g->motion_services.opaque = g; g->motion_services.absolute_position = absolute; g->motion_services.relative_position = relative;
    g->motion_services.rebuild_secondary_basis = secondary; g->motion_services.notify = notify;
    g->motion_services.probe = motion_probe; g->motion_services.estimate_update = motion_estimate;
    g->contact.pool = &s->objects; g->contact_services.opaque = g; g->contact_services.motion = &g->motion_services;
    g->contact_services.model = model; g->contact_services.model_variable = condition;
    g->contact_services.frame_difference = relative; g->contact_services.collision = collision;
    g->probe.pool = &s->objects; g->probe_services.opaque = g; g->probe_services.model = model;
    g->probe_services.rebuild_secondary_basis = secondary; g->probe_services.condition_word = condition;
    g->objects.host.context = g; g->objects.host.model = model; g->objects.host.sound = sound;
    g->objects.host.sound_plain = play;
    g->objects.host.text = object_text; g->objects.host.probe = object_probe; g->objects.host.basis = object_basis; g->objects.host.move = object_move;
    g->offences.host.opaque = g; g->offences.host.show_message = message;
    g->offences.host.play_effect = play;
    g->population.host.opaque = g;
    g->population.host.model = model;
    g->population.host.names_name = population_name;
    /* is already the flight host's advance. Wrecks and missiles use it too. */
    g->population.host.transform_vector = transform_vector;
    g->population.host.scan_contacts = population_contacts;
    g->population.host.integrate_limited = integrate_limited;
    g->population.host.scaled_difference = population_scaled;
    g->population.host.difference = population_difference;
    g->population.host.linked_force = linked_force;
    g->population.host.rates_toward = rates_toward;
    g->population.host.select_frame = select_frame;
    g->population.host.clamp_controls = clamp_controls;
    g->population.host.turn = turn;
    g->population.host.play_effect = play;
    g->population.host.play_sound = sound;
    g->population.host.relative_motion = population_relative;
    g->population.host.relative_probe = population_probed;
    g->population.host.apply_controls = population_apply;
    g->population.host.hold_controls = population_hold;
    g->population.host.steer_controls = population_steer;
    g->population.host.capacity_used = population_capacity;
    g->population.host.remove = population_remove;
    g->population.host.attach = refresh_contacts;
    g->population.host.setup = interpolate;
    g->host->resources->text.overlay_text = overlay_text;
    g->host->resources->text.overlay_context = g;
    g->station.host.opaque = g;
    g->station.host.draw_text = station_text;
    g->station.host.draw_text_both = station_text_both;
    g->station.host.draw_text_shadowed = station_plain;
    g->station.host.draw_sprite = station_sprite;
    g->station.host.draw_icon = sprite;
    g->station.host.draw_line = line;
    g->station.host.present = station_present;
    g->station_draw_native = s->overlays[12].draw; s->overlays[12].draw = station_draw;
    g->station.host.play_effect = play;
    g->station.host.panels_panel = station_panel;
    g->station.host.commander_restore_time = station_restore_time;
    g->station.host.object = station_object;
    g->station.host.lookup = station_lookup;
    g->station.host.model = station_model;
    g->station.host.clear_objects = station_clear;
    g->station.host.work_record = station_work;
    g->station.host.prepare_object = station_prepare;
    g->station.host.move_object = station_move;
    g->station.host.orient_object = station_orient;
    g->station.host.render_object = station_preview;
    g->station.host.draw_frame = station_rectangle;
    g->station.host.clear_picture = station_copy;
    g->station.host.open_view = station_open_view;
    g->station.host.clear_drawing = station_clear_drawing;
    g->station.host.attach_object = station_attach;
    g->station.host.systems_system = station_systems;
    g->station.host.visit_objects = station_visit;
    g->station.host.set_slot_flags = station_slot_flags;
    g->station.host.bounded_sample = station_sample;
    g->station.host.clear_panel = menu_clear;
    g->station.host.set_controls = menu_controls;
    g->station.host.restore_view = restore_view;
    g->map.host.opaque = g;
    g->map.host.expand_text = map_expand;
    g->map.host.format_number = map_number;
    g->map.host.vector_length = map_length;
    g->map.host.system_details = map_details;
    g->map.host.travel = map_travel;
    g->map.host.density_samples = map_density;
    g->map.host.basis = map_basis;
    g->map.host.project = map_project;
    g->map.host.draw_button = map_button;
    g->map.host.primary_panel = map_panel;
    g->map.host.draw_text = map_text;
    g->map.host.draw_resident_text = map_resident_text;
    g->map.host.draw_scene = map_scene;
    g->map.host.draw_pixel = map_pixel;
    g->map.host.draw_block = map_block;
    g->map.host.draw_crosshair = map_crosshair;
    g->map.host.draw_line = map_chart_line;
    g->map.host.clear_projection = map_clear_projection;
    g->map.host.reset_view = map_reset;
    g->map.host.set_controls = map_controls;
    g->map.host.generate_system = map_generate;
    g->map.host.clear_panel = map_clear;
    g->map.host.show_panel = map_show;
    g->map.host.present = map_present;
    g->map.host.set_palette = map_palette;
    g->map.host.draw_text_flow = map_flow;
    g->map.host.hierarchy_node = map_node;
    g->map.host.draw_body_sprite = map_body_sprite;
    g->map.host.body_scale = map_body_scale;
    g->map.host.body_number = map_body_number;
    g->map_draw_native = s->overlays[2].draw; s->overlays[2].draw = map_draw;
    g->hyperspace.host.opaque = g;
    g->hyperspace.host.ship_stats = drive_stats;
    g->hyperspace.host.system_action = drive_system;
    g->hyperspace.host.clear_objects = drive_clear;
    g->hyperspace.host.interpolate = interpolate;
    g->hyperspace.host.play_effect = play;
    g->hyperspace.host.misjump = drive_misjump;
    g->status.host.opaque = g;
    g->status.host.ship_type = status_ship;
    g->status.host.draw_text = status_text;
    g->status.host.clear_panel = status_clear;
    g->status.host.draw_frame = status_frame;
    g->status.host.prepare_camera = status_prepare;
    g->status.host.render_scene = status_preview;
    g->status.host.draw_sprite = sprite;
    g->status.host.draw_icon = sprite;
    g->status.host.open_panel = panel;
    g->status.host.drive_summary = status_drive;
    navigation_bind(g);
    g->orbits.context = g; g->orbits.message = message; g->orbits.reference = orbit_reference;
    v = &g->systems.host; v->opaque = g;
    v->normalize_dword = from_dword; v->normalize_word = normalize; v->multiply = multiply; v->divide = divide;
    v->square_root = root; v->add = add; v->to_signed_dword = to_signed; v->to_unsigned_dword = to_unsigned;
    v->wide_product = wide_product; v->interpolate = interpolate; v->rescale_vector = rescale;
    v->allocate_object = allocate_body; v->object_reference = object_reference; v->object_from_reference = from_reference;
    v->model = system_model;
    f = &g->flight.host; f->opaque = g; f->model = flight_model; f->names_name = name; f->fatal_error = fatal;
    f->panels_panel = panel; f->panels_widgets = widgets;
    f->station_message = message; f->scan_contacts = scan_contacts; f->refresh_contacts = refresh_contacts;
    f->probe = probe; f->pointer = pointer; f->set_pointer = set_pointer; f->set_controls = controls;
    f->integrate = integrate; f->integrate_limited = integrate_limited; f->turn = turn; f->linked_force = linked_force;
    f->radial_rate = radial_rate; f->select_frame = select_frame; f->clamp_controls = clamp_controls; f->axis_feedback = feedback;
    f->scaled_difference = scaled_difference; f->difference = difference; f->transform_feedback = transform_feedback;
    f->rebuild_secondary_basis = rebuild_secondary; f->move_object = move_object; f->transform_vector = transform_vector;
    f->rotate_pair = rotate_pair; f->rebuild_basis = rebuild; f->rates_toward = rates_toward; f->normalize = normalize_vector;
    f->normalize_unsigned = normalize; f->vector_length = length; f->project = project; f->render_scene = scene;
    f->render_object = render_explicit;
    f->render_frame_model = render_frame_model;
    f->draw_line = line; f->draw_clipped_line = clipped_line; f->draw_text = text_word;
    f->draw_text_shadowed = text; f->draw_text_both = text; f->palette_rgb = palette_rgb; f->palette_index = palette_index;
    f->play_effect = play; f->play_duration = duration; f->ambient = ambient;
    f->draw_sprite = sprite;
    g->flight_draw_native = s->overlays[3].draw; s->overlays[3].draw = flight_draw;
    g->panels.host.opaque = g; g->panels.host.draw_sprite = sprite; g->panels.host.draw_sprite_over = sprite;
    g->panels.host.draw_text = text; g->panels.host.draw_text_over = text;
    g->panels.host.draw_line = line; g->panels.host.present = panel_present;
    g->panels.host.install_gauges = install_gauges; g->panels.host.notify_panel = notify_panel;
    g->panels.host.secondary_basis = rebuild_secondary; g->panels.host.linked_object = linked_object;
    g->panels.host.relative_position = panel_relative; g->panels.host.model_colour = model_colour;
    g->panels.host.draw_marker = panel_marker;
    s->overlays[0].context = g; s->overlays[0].initialize = initialize_world;
    s->overlays[3].metadata.update = 0xff; s->overlays[3].metadata.draw = 0xff;
    s->overlays[3].metadata.activation = 0xf1;
    s->overlays[3].metadata.flags = 0x67; s->overlays[3].metadata.events = UINT32_C(0x00e60028);
    /* market's header: events on. Arrival (bit 2) rebuilds every station's
     * ships and missions; the new day (bit 15) refreshes them. */
    s->overlays[7].metadata.flags = 0x0a; s->overlays[7].metadata.events = UINT32_C(0x00008004);
    /* people's header: events on, and object releases (flag bit 2). The jump
     * (bit 1), the pass before arrival (bit 19), arrival (bit 2) and the
     * populate request (bit 16) are what fill a system with ships. */
    s->overlays[11].metadata.flags = 0x16; s->overlays[11].metadata.events = UINT32_C(0x00090006);
    /* panels's header is update on, draw off (dw 0x0b00, 0x00ff). The plates,
     * mode icons, time strip and navigation marks are drawn when they
     * change and left in place. The update entry redraws the scanner and
     * the gauges; the coarse tick (flags bit 0) refreshes the clock. */
    s->overlays[4].metadata.flags = 0x0b; s->overlays[4].metadata.events = fe2_panels_event_mask;
    s->overlays[4].metadata.update = 0xff; s->overlays[4].metadata.draw = 0;
    /* offences's header is dw 0x0200, 0x0000: events on, update off. 0x413 is
     * that flag byte, so an alert (bit 0) is what arms the launch tick. */
    s->resident.police_flags = 0x02;
    s->overlays[15].metadata.flags = 0x02; s->overlays[15].metadata.events = UINT32_C(0x00020022);
    s->overlays[15].metadata.update = 0;
    s->overlays[10].metadata.flags = 0; s->overlays[10].metadata.activation = 7;
    s->overlays[10].metadata.update = 0xff; s->overlays[10].metadata.draw = 1;
    s->overlays[10].draw = menu_draw;
    /* F2 opens the galactic map. Its header also takes the jump and a misjump. */
    s->overlays[2].metadata.activation = 0xf2;
    s->overlays[2].metadata.flags = 0x02; s->overlays[2].metadata.events = UINT32_C(0x00100002);
    /* Status header: military arrival, entering a system, the new day, crew
     * pay, and the three redraws. */
    s->overlays[14].metadata.activation = 0xf3;
    s->overlays[14].metadata.flags = 0x3a; s->overlays[14].metadata.events = UINT32_C(0x02c18024);
    /* Both map overlays use F2. chart delegates to navigation while already open
     * (map_activation.inc, 0x2758); the resident scan reaches chart first. */
    s->overlays[5].metadata.activation = 0xf2;
    /* Station header: landing fee, berthing fee, and a refresh of an open
     * screen. Draw stays on so this loop runs the select routine; the header
     * itself leaves that byte clear. */
    s->overlays[12].metadata.activation = 0xf4;
    s->overlays[12].metadata.flags = 0x0a; s->overlays[12].metadata.events = UINT32_C(0x02008020);
    s->overlays[12].metadata.draw = 1;
    s->overlays[16].metadata.activation = 0x68;
    s->overlays[17].metadata.events = UINT32_C(0x00010004);
    s->pointer_shown = 1; s->services.sample_pointer = sample_pointer;
    s->services.model = session_model; s->services.distance = session_distance;
    /* Model/distance callbacks share the session service context (the game host). */
    if (fe2_session_start(s, selection) || !g->objects.state.player_record) return -1;
    fprintf(stderr, "World %s: %u bodies\n", g->place_name, g->bodies);
    g->active = 1; g->mouse_x = g->sampled_x = s->pointer.x; g->mouse_y = g->sampled_y = s->pointer.y;
    fprintf(stderr, "Player: handle %u, model %04x, mode %02x\n", g->objects.state.player,
        g->objects.state.player_record->model, s->resident.flight_mode);
    return 0;
}
void fe2_gameplay_key(fe2_gameplay *g, uint8_t scan, int extended, int released)
{
    if (!g || !g->active) return;
    if (extended) fe2_session_key(g->session, 0xe0);
    fe2_session_key(g->session, (uint8_t)(scan | (released ? 0x80u : 0u)));
    if (scan == 1 && !released && !extended) fe2_input_restore(&g->session->pointer, 7);
}

void fe2_gameplay_mouse(fe2_gameplay *g, unsigned x, unsigned y, unsigned buttons)
{
    if (!g || !g->active) return;
    g->mouse_x = (uint16_t)(x < 320 ? x : 319);
    g->mouse_y = (uint16_t)(y < 199 ? y : 198);
    if ((buttons & 1u) && !(g->mouse_buttons & 1u)) g->mouse_click = 1;
    g->mouse_buttons = (uint8_t)(buttons & 3u);
}
