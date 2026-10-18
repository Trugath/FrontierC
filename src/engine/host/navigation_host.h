/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Host providers for navigation. The session owns the independent map pool. */
#include "flight/navigation_tables.h"
static void navigation_clear(void *p, const fe2_call_args *args)
{
    fe2_session *s = game(p)->session;
    s->pools.current = (fe2_object_pool *)args->arg[12].ref;
    fe2_pool_clear(&s->pools);
}
static void navigation_copy(void *p, fe2_object_list *destination, const fe2_object_list *source)
{
    fe2_object_pool *to = (fe2_object_pool *)destination;
    const fe2_object_pool *from = (const fe2_object_pool *)source;
    unsigned i;
    (void)p;
    memcpy(to->status, from->status, sizeof to->status);
    for (i = 0; i < FE2_POOL_SLOTS; ++i) {
        *to->records[i] = *from->records[i];
        to->records[i]->render_state = to->records[i]->basis_generation = 0;
    }
}
static void navigation_refresh(void *p, fe2_object_list *list)
{
    fe2_session *s = game(p)->session;
    fe2_object_pool *pool = (fe2_object_pool *)list;
    unsigned i;
    fe2_pool_reset_countdowns(&s->pools, pool);
    for (i = 0; i < FE2_POOL_SLOTS; ++i)
        pool->records[i]->render_state = pool->records[i]->basis_generation = 0;
}
static void navigation_time(void *p, fe2_object_list *list, uint64_t time)
{
    fe2_gameplay *g = game(p);
    fe2_pool_state saved = g->session->pools;
    fe2_orbit_state orbit = g->orbits.state;
    fe2_motion motion = g->motion;
    fe2_position position = g->position;
    fe2_object_pool *pool = (fe2_object_pool *)list;
    fe2_pool_services services;
    uint16_t words[4];
    unsigned i;
    g->session->pools.current = g->session->pools.saved = pool;
    g->motion.pool = pool;
    g->position.current = g->position.saved = pool;
    /* Resident 0xa0 dispatches periodic owners highest handle first, honoring
     * the original status/countdown flags (orbits bodies and navigation markers). */
    for (i = 0; i < 4; ++i) words[i] = (uint16_t)(time >> (16*i));
    fe2_session_pool_services(g->session, &services);
    fe2_pool_periodic_updates(&g->session->pools, &services, pool, words);
    g->session->pools = saved; g->orbits.state = orbit;
    g->motion = motion; g->position = position;
    navigation_refresh(p, list);
}
static fe2_navigation_model navigation_model(void *p, uint16_t id)
{
    const uint8_t *bytes = model(p, id);
    fe2_navigation_model result = {{0, 0}, 0, 0};
    if (bytes) {
        uint16_t at = fe2_model_word(bytes, 0x1c);
        result.radius[0] = fe2_model_word(bytes, 0x0a);
        result.radius[1] = fe2_model_word(bytes, 0x0c);
        result.has_size = at != 0;
        if (at && at + 8u <= game(p)->host->resources->models[0][id/2].count)
            result.size = fe2_model_word(bytes + at, 6);
    }
    return result;
}
static void navigation_string(void *p, const uint8_t *value, uint8_t colour, uint16_t x, uint16_t y)
{ map_string(game(p), (const char *)value, colour, x, y); }
static void navigation_name(void *p, const fe2_object *o, uint8_t colour, uint16_t x, uint16_t y)
{
    char value[FE2_OBJECT_NAME_BYTES + 1];
    memcpy(value, o->name, FE2_OBJECT_NAME_BYTES); value[FE2_OBJECT_NAME_BYTES] = 0;
    map_string(game(p), value, colour, x, y);
}
static void navigation_text(void *p, uint16_t selector, const fe2_text_table *table,
                            const fe2_call_args *args, fe2_text_out *out)
{
    fe2_text_resources r = game(p)->host->resources->text;
    size_t length;
    unsigned i;
    r.module = (fe2_text_directory){{(const uint8_t *)table, FE2_NAVIGATION_TEXT_BYTES}, 2};
    for (i = 0; i < 5; ++i) r.value[i] = args ? args->arg[i+1].value : 0;
    if (!out || !out->buffer || fe2_text_expand(&r, (uint16_t)(0x4000 | selector/2),
            out->buffer + out->at, 65536u - out->at, &length)) { fatal(p, 0xf005); return; }
    out->at = (uint16_t)(out->at + length);
}
static void navigation_map(void *p, const fe2_call_args *args)
{
    fe2_gameplay *g = game(p);
    if (fe2_render_runtime_layer(g->host->runtime)) { fatal(p, 0xf005); return; }
    menu_clear(p);
    if (fe2_gameplay_render_navigation(g, (fe2_object *)args->arg[11].ref, NULL)) fatal(p, 0xf005);
}
static void navigation_object(void *p, fe2_object *o, const fe2_call_args *args)
{
    if (fe2_gameplay_render_navigation(game(p), (fe2_object *)args->arg[11].ref, o)) fatal(p, 0xf005);
}
static void navigation_force(void *p, fe2_object *o, const fe2_call_args *args)
{ fe2_motion_linked_force(&game(p)->motion, o, (int32_t)args->arg[2].value); }
static void navigation_reference(void *p, fe2_object *o)
{ fe2_motion_select_frame(&game(p)->motion, &game(p)->motion_services, o); }
/* navigation's temporary 0xf44 block is a serialized protocol in the original.
 * Resolve it to bounded native systems records instead of casting that block. */
static void navigation_system(void *p, const fe2_system_seeds *seeds, const fe2_call_args *args)
{
    fe2_gameplay *g = ((fe2_session_navigation_overlay *)p)->host.opaque;
    fe2_pool_state saved = g->session->pools;
    fe2_system_request request = {0};
    unsigned bodies = g->bodies;
    request.mode = (uint16_t)args->arg[2].value;
    request.limit = g->navigation.system_reply.arg[3].value;
    request.key = g->navigation.system_reply.arg[6].value;
    request.danger = (uint8_t)g->navigation.system_reply.arg[7].value;
    request.orbit[0] = args->arg[4].value; request.orbit[1] = args->arg[5].value;
    request.root_name = seeds ? seeds->name : NULL;
    if (seeds) { request.seed_a = seeds->seed_a; request.seed_b = seeds->seed_b; }
    if (!request.mode) {
        memset(g->navigation_records, 0, sizeof g->navigation_records);
        memset(&g->navigation_generation, 0, sizeof g->navigation_generation);
    }
    g->session->pools.current = g->session->pools.saved = &g->session->map_objects;
    fe2_systems_action(&g->navigation_generation, &g->systems.services,
        (fe2_record_space){g->navigation_records, 256}, &request);
    g->bodies = bodies; g->session->pools = saved;
}
static void navigation_draw(void *context, fe2_session *s)
{
    fe2_session_navigation_overlay *b = context;
    fe2_gameplay *g = b->host.opaque;
    uint8_t flags = b->state.flags;
    g->navigation_draw_native(context, s);
    /* Preserve chart's deselection button (map_activation.inc) after the
     * host's per-frame cockpit repaint. It points back to the galaxy. */
    sprite(g, 10, 16, 188);
    sprite(g, 0x56, 256, 178);
    if (flags & FE2_NAV_OPTIONS_OPEN) {
        sprite(g, 0x58, 256, 188);
        if (flags & FE2_NAV_HIDE_PROJECTIONS) sprite(g, 0x59, 288, 188);
        if (flags & FE2_NAV_HIDE_LABELS) sprite(g, 0x5a, 304, 188);
    } else if (flags & FE2_NAV_COPIED_LIST) sprite(g, 0x57, 304, 188);
}
static int navigation_secondary(void *p, int activate)
{
    fe2_session *s = game(p)->session;
    fe2_session_overlay *o = &s->overlays[5];
    if (activate) o->activate(o->context, s, 0);
    else o->message(o->context, s, NULL);
    return s->error ? -1 : 0;
}
static void navigation_bind(fe2_gameplay *g)
{
    fe2_navigation_services *v = &g->navigation.host;
    v->opaque = g; v->restore_view = station_open_view;
    v->set_controls = controls; v->set_pointer = set_pointer;
    v->draw_sprite = sprite; v->draw_string = navigation_string; v->draw_name = navigation_name;
    v->format_table_text = navigation_text; v->play_effect = play;
    v->copy_objects = navigation_copy; v->clear_objects = navigation_clear;
    v->refresh_objects = navigation_refresh; v->set_objects_time = navigation_time;
    v->model = navigation_model; v->render_map = navigation_map; v->draw_object = navigation_object;
    v->view_angles = pointer; v->motion_update = navigation_force; v->reference_update = navigation_reference;
    v->normalize_word = normalize; v->multiply = multiply; v->transform_vector = transform_vector;
    g->navigation.state.stack_segment = g->navigation_text;
    g->host->resources->text.overlay[5] =
        (fe2_text_directory){{fe2_navigation_text_directory, FE2_NAVIGATION_TEXT_BYTES}, 2};
    g->navigation.services.system_action = navigation_system;
    /* Native current_system preserves chart's reply for the two systems actions;
     * the adapter receives the navigation binding as its service context. */
    g->navigation_draw_native = g->session->overlays[5].draw;
    g->session->overlays[5].draw = navigation_draw;
    g->map.host.secondary_view = navigation_secondary;
}
