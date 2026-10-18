/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "session_internal.h"
#include <string.h>

enum { COARSE_DIVISOR = 49710, TIME_SUBTICK = 994, CALLBACK_BUDGET = 4096 };

uint64_t fe2_session_time(const fe2_session *s)
{
    return s ? (uint64_t)s->pools.clock[1] << 32 | s->pools.clock[0] : 0;
}
void fe2_session_set_time(fe2_session *s, uint64_t time)
{
    if (s) { s->pools.clock[0] = (uint32_t)time; s->pools.clock[1] = (uint32_t)(time >> 32); }
}
void fe2_session_create(fe2_session *s, const fe2_session_services *services)
{
    unsigned i;
    fe2_session_services copy = {0};
    if (!s) return;
    if (services) copy = *services;
    memset(s, 0, sizeof(*s));
    s->services = copy;
    for (i = 0; i < FE2_POOL_SLOTS; ++i) { s->objects.records[i] = &s->records[i]; s->records[i].handle = (uint8_t)i; }
    for (i = 0; i < FE2_POOL_SLOTS; ++i) {
        s->map_objects.records[i] = &s->map_records[i]; s->map_records[i].handle = (uint8_t)i;
    }
    s->pools.root = s->pools.current = s->pools.saved = &s->objects;
    s->last_overlay = FE2_SESSION_DEFAULT_LAST; s->generation = 1;
    s->time_step = TIME_SUBTICK; s->subtick_countdown = 4;
    s->scheduled_time = UINT64_MAX;
    s->noise[0] = UINT32_C(0x12345678); s->noise[1] = UINT32_C(0x89abcdef);
    fe2_pointer_regions_init(&s->regions);
    fe2_input_init(&s->input);
    fe2_resident_init(&s->resident);
    s->resident.map_objects = (fe2_object_list *)&s->map_objects;
}
/* An error, or a restart requested from a callback, ends the scans. */
#define unwind fe2_session_unwind
static int stopped(const fe2_session *s) { return s->error || s->restart; }
static int valid(fe2_session *s)
{
    if (!s || s->last_overlay >= FE2_SESSION_OVERLAYS || !s->pools.root) return 0;
    return 1;
}
fe2_session_overlay *fe2_session_lock(fe2_session *s, unsigned slot)
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
#define lock fe2_session_lock
void fe2_session_reset_overlays(fe2_session *s)
{
    if (s->services.reset_resources) s->services.reset_resources(s->services.context);
    memset(s->metadata, 0, sizeof(s->metadata));
    memset(s->locks, 0, sizeof(s->locks));
    memset(s->loaded, 0, sizeof(s->loaded));
    /* The original cache reset retains the mask cache until a slot loads. */
}
static int initialize(fe2_session *s, uint16_t selection)
{
    unsigned slot = 0, budget = CALLBACK_BUDGET;
    while (slot <= s->last_overlay) {
        fe2_session_overlay *o;
        if (!budget-- || s->last_overlay >= FE2_SESSION_OVERLAYS) return -1;
        s->current_overlay = slot; o = lock(s, slot);
        if (!o) return -1;
        if (o->initialize) o->initialize(o->context, s, selection);
        --s->locks[slot];
        if (s->restart) return 0;
        if (s->current_overlay >= FE2_SESSION_OVERLAYS) return -1;
        slot = s->current_overlay + 1;
    }
    return s->error ? -1 : 0;
}
int fe2_session_startup(fe2_session *s)
{
    int result;
    if (!valid(s) || s->busy) return -1;
    s->busy = 1; s->error = 0; s->initialized = 0; fe2_session_reset_overlays(s);
    s->selected_overlay = 0; s->last_overlay = FE2_SESSION_DEFAULT_LAST;
    s->noise[0] = UINT32_C(0x12345678); s->noise[1] = UINT32_C(0x89abcdef);
    result = initialize(s, 0);
    if (!result && s->restart) result = unwind(s);
    s->busy = 0; return result;
}
int fe2_session_start(fe2_session *s, uint16_t selection)
{
    unsigned phase;
    if (!valid(s) || s->busy || !(selection & 255u) || (selection & 255u) > 4) return -1;
    s->busy = 1; s->error = 0; s->initialized = 0; fe2_session_reset_overlays(s);
    if (selection < 0x400) { s->noise[0] = UINT32_C(0x12345678); s->noise[1] = UINT32_C(0x89abcdef); }
    s->selected_overlay = 0;
    fe2_session_set_time(s, UINT64_C(0x0011d5a080000000));
    s->scheduled_time |= UINT64_C(0xffffffff00000000); s->coarse_time = 0;
    for (phase = selection & 255u; phase <= 4; ++phase)
    {
        if (initialize(s, (uint16_t)((selection & 0xff00u) | phase))) { s->busy = 0; s->restart = 0; return -1; }
        if (s->restart) { int result = unwind(s); s->busy = 0; return result; }
    }
    s->frontend = FE2_SESSION_PLAYING; s->initialized = 1; s->busy = 0; return 0;
}
void fe2_session_ticks(fe2_session *s, uint32_t ticks)
{
    uint64_t phase, subticks;
    if (!s || !s->subtick_countdown || s->subtick_countdown > 4) return;
    s->frame_ticks = (uint16_t)(s->frame_ticks + ticks);
    phase = (uint64_t)ticks + 4u - s->subtick_countdown;
    subticks = phase / 4;
    s->subtick_countdown = (uint8_t)(4 - phase % 4);
    for (; subticks && !s->exit_requested; --subticks) {
        if (fe2_input_tick(&s->input, &s->pointer)) { s->exit_requested = 1; break; }
        s->pending_simulation += s->time_step;
        s->pending_render += TIME_SUBTICK;
        s->event_time += TIME_SUBTICK;
    }
}
void fe2_session_key(fe2_session *s, uint8_t byte)
{
    if (s) fe2_input_scancode(&s->input, &s->pointer, byte);
}
void fe2_session_update_pointer(fe2_session *s, uint16_t *x, uint16_t *y)
{
    fe2_pointer_sample sample = {{0, 0}, 0, 0};
    uint16_t qx, qy;
    if (!s) return;
    if (s->services.sample_pointer) s->services.sample_pointer(s->services.context, s, &sample);
    fe2_pointer_query(&s->pointer, &s->regions, &sample, &qx, &qy);
    if (s->pointer_shown) { s->pointer.x = qx; s->pointer.y = qy; }
    if (x) *x = qx;
    if (y) *y = qy;
}
void fe2_session_microseconds(fe2_session *s, uint32_t elapsed)
{
    uint64_t phase;
    if (!s) return;
    phase = s->timer_phase + (uint64_t)elapsed * UINT32_C(1193182);
    s->timer_phase = phase % UINT64_C(5899000000);
    fe2_session_ticks(s, (uint32_t)(phase / UINT64_C(5899000000)));
}
int fe2_session_broadcast(fe2_session *s, uint32_t event)
{
    unsigned slot;
    if (!valid(s)) return -1;
    for (slot = 0; slot <= s->last_overlay; ++slot) {
        unsigned index;
        fe2_session_overlay *o;
        if (s->last_overlay >= FE2_SESSION_OVERLAYS) { s->error = 1; return -1; }
        if (!(s->metadata[slot].flags & 2u)) continue;
        event &= 255u; index = slot + event / 32u;
        if (index >= FE2_SESSION_OVERLAYS) { s->error = 1; return -1; }
        if (!(s->event_masks[index] & (UINT32_C(1) << (event & 31u)))) continue;
        o = lock(s, slot); if (!o) return -1;
        if (o->event) event = o->event(o->context, s, event);
        --s->locks[slot];
        if (s->restart) return 0;
    }
    return s->error ? -1 : 0;
}
static void periodic(void *context, uint8_t owner, fe2_object *record, const uint16_t words[4])
{
    fe2_session *s = context;
    fe2_session_overlay *o = stopped(s) ? NULL : lock(s, owner);
    uint64_t time = (uint64_t)words[3] << 48 | (uint64_t)words[2] << 32 | (uint32_t)words[1] << 16 | words[0];
    if (!o) return;
    if (o->periodic_record) o->periodic_record(o->context, s, record, time);
    --s->locks[owner];
}
static void second(void *context, uint8_t owner, fe2_object *record, const uint16_t words[2])
{
    fe2_session *s = context;
    fe2_session_overlay *o = stopped(s) ? NULL : lock(s, owner);
    if (!o) return;
    if (o->second_record) o->second_record(o->context, s, record, (uint32_t)words[1] << 16 | words[0]);
    --s->locks[owner];
}
uint16_t fe2_session_poll(fe2_session *s)
{
    int screenshot;
    uint16_t code;
    if (s->services.poll_input) return s->services.poll_input(s->services.context);
    code = fe2_input_poll(&s->pointer, &screenshot);
    if (screenshot && s->services.screenshot) s->services.screenshot(s->services.context, s);
    return code;
}
static int input(fe2_session *s)
{
    unsigned slot;
    uint16_t code;
    if (s->pools.game_flags & 1u) return 0;
    code = fe2_session_poll(s);
    if (!code) return 0;
    for (slot = 0; slot <= s->last_overlay; ++slot) {
        fe2_session_overlay *o;
        if (s->last_overlay >= FE2_SESSION_OVERLAYS) return -1;
        if ((uint8_t)code != s->metadata[slot].activation) continue;
        if (s->services.activation_sound) s->services.activation_sound(s->services.context);
        s->current_overlay = slot; o = lock(s, slot); if (!o) return -1;
        if (o->activate) o->activate(o->context, s, code);
        --s->locks[slot]; return s->error ? -1 : 0;
    }
    if (s->services.restore_input) s->services.restore_input(s->services.context, code);
    else if (!s->services.poll_input) fe2_input_restore(&s->pointer, (uint8_t)code);
    return 0;
}
static int phase(fe2_session *s, unsigned kind, uint32_t interval)
{
    unsigned slot = 0, budget = CALLBACK_BUDGET;
    while (slot <= s->last_overlay) {
        fe2_session_overlay *o;
        int enabled;
        if (!budget-- || s->last_overlay >= FE2_SESSION_OVERLAYS) return -1;
        enabled = kind == 0 ? s->metadata[slot].update : kind == 1 ? s->metadata[slot].draw : s->metadata[slot].flags & 1u;
        if (!enabled) { ++slot; continue; }
        s->current_overlay = slot; o = lock(s, slot); if (!o) return -1;
        if (kind == 0 && o->update) o->update(o->context, s);
        else if (kind == 1 && o->draw) o->draw(o->context, s);
        else if (kind == 2 && o->coarse_tick) o->coarse_tick(o->context, s, interval);
        --s->locks[slot]; slot = s->current_overlay + 1;
        if (s->restart) return 0;
        if (s->current_overlay >= FE2_SESSION_OVERLAYS) return -1;
    }
    return s->error ? -1 : 0;
}
static int updates(fe2_session *s, const fe2_pool_services *pool_services)
{
    uint32_t elapsed, interval;
    uint16_t words[2];
    s->pools.game_flags &= 0xe3u;
    if (phase(s, 0, 0)) return -1;
    elapsed = s->pools.clock[0] - s->coarse_time;
    if (elapsed < COARSE_DIVISOR) return 0;
    /* DIV overflow resumes with the low dividend word still in AX. */
    interval = elapsed / COARSE_DIVISOR;
    if (interval > 65535u) interval = (uint16_t)elapsed;
    s->coarse_time += interval * COARSE_DIVISOR;
    if (phase(s, 2, interval)) return -1;
    if (s->restart) return 0;
    words[0] = (uint16_t)interval; words[1] = 0;
    s->pools.current = s->pools.root;
    fe2_pool_second_updates(&s->pools, pool_services, s->pools.root, words);
    return s->error ? -1 : 0;
}
/* Owner overlays for the pool: releases go to entry +0x30 of the slots whose
 * metadata has flag bit 2. */
static unsigned overlay_count(void *context)
{
    fe2_session *s = context;
    return s->last_overlay < FE2_SESSION_OVERLAYS ? s->last_overlay + 1 : 0;
}
static int receives_releases(void *context, unsigned slot)
{
    fe2_session *s = context;
    return slot < FE2_SESSION_OVERLAYS && (s->metadata[slot].flags & 4u);
}
static void released(void *context, unsigned slot, fe2_object *record, uint8_t handle, uint32_t request)
{
    fe2_session *s = context;
    fe2_session_overlay *o = lock(s, slot);
    (void)handle;
    if (!o) return;
    if (o->record_event) o->record_event(o->context, s, record, request);
    --s->locks[slot];
}
static const uint8_t *model(void *context, uint16_t number)
{
    fe2_session *s = context;
    if (s->services.model) return s->services.model(s->services.context, number);
    s->error = 1; return NULL;
}
static void play_level(void *context, uint16_t effect, uint8_t level)
{
    fe2_session *s = context;
    if (s->services.play_level) s->services.play_level(s->services.context, effect, level);
}
static int16_t distance(void *context, const fe2_object *from, const fe2_object *to)
{
    fe2_session *s = context;
    if (s->services.distance) return s->services.distance(s->services.context, from, to);
    s->error = 1; return INT16_MAX;
}
/* The nearby effect's damage: AX = 0x105, DS:0x7d8's low byte the source
 * (its high byte is left from earlier calls; zero here). */
static void damage(void *context, fe2_object *record, uint16_t amount, uint8_t source)
{
    fe2_session *s = context;
    if (stopped(s)) return;
    if (s->damage) s->damage(s->damage_context, s, record, amount, source); else s->error = 1;
}
void fe2_session_pool_services(fe2_session *s, fe2_pool_services *v)
{
    memset(v, 0, sizeof(*v));
    v->opaque = s; v->overlay_count = overlay_count; v->receives_releases = receives_releases;
    v->released = released; v->periodic_update = periodic; v->second_update = second;
    v->model = model; v->play_level = play_level; v->distance = distance; v->damage = damage;
}
/* One restart pass; a restart or exit scene requested inside it is left
 * for unwind. */
static int restart(fe2_session *s)
{
    {
        fe2_session_overlay *o;
        s->restart = 0;
        if (s->services.assign_palette) s->services.assign_palette(s->services.context, s);
        s->pools.game_flags = 0;
        s->simulation_step = s->pending_simulation = s->render_step = s->pending_render = 0;
        s->panel_selection = 0; s->next_page = 0;
        s->scheduled_time |= UINT64_C(0xffffffff00000000);
        memset(s->locks, 0, sizeof(s->locks));
        if (fe2_session_broadcast(s, 3)) return -1;
        if (s->restart) return 0;
        if (s->services.reset_music) s->services.reset_music(s->services.context);
        if (s->services.stop_effects) s->services.stop_effects(s->services.context);
        if (s->current_music >= 0 && s->services.select_music)
            s->services.select_music(s->services.context, s->current_music);
        o = lock(s, 3); if (!o) return -1;
        if (o->activate) o->activate(o->context, s, (uint16_t)s->current_music);
        --s->locks[3];
    }
    s->frontend = FE2_SESSION_PLAYING; s->initialized = 1;
    return s->error ? -1 : 0;
}
/* Deferred restarts (1) and exit scenes (2), until none is requested. */
int fe2_session_unwind(fe2_session *s)
{
    unsigned budget = CALLBACK_BUDGET;
    while (s->restart) {
        if (!budget--) return -1;
        if (s->restart == FE2_SESSION_PENDING_EXIT_SCENE) {
            s->restart = 0;
            if (fe2_session_begin_exit_scene(s, s->exit_mode)) return -1;
        } else if (restart(s)) return -1;
    }
    return s->error ? -1 : 0;
}
int fe2_session_restart(fe2_session *s)
{
    int result;
    if (!valid(s)) return -1;
    if (s->busy) { s->restart = FE2_SESSION_PENDING_RESTART; return 0; }
    s->busy = 1; s->error = 0; result = restart(s);
    if (!result) result = unwind(s);
    s->busy = 0;
    if (result) s->error = 1;
    return result;
}
fe2_session_result fe2_session_step(fe2_session *s)
{
    fe2_pool_services pool_services;
    fe2_session_result result = FE2_SESSION_FRAME;
    uint32_t previous_high, compare_high;
    uint64_t time;
    uint16_t words[4];
    unsigned i;
    if (!valid(s) || s->busy) return FE2_SESSION_ERROR;
    if (s->exit_requested) return FE2_SESSION_EXIT;
    if (s->frontend != FE2_SESSION_PLAYING) return fe2_session_frontend_step(s);
    if (!s->initialized) return FE2_SESSION_ERROR;
    if (s->frame_ticks < 2) return FE2_SESSION_WAITING;
    s->busy = 1; s->error = 0; s->frame_ticks = 0;
    if (s->services.synchronize_pointer) s->services.synchronize_pointer(s->services.context, s);
    if (s->services.begin_frame) s->services.begin_frame(s->services.context, s);
    do { ++s->generation; } while (!s->generation);
    previous_high = s->pools.clock[1];
    s->simulation_step = s->pending_simulation; s->pending_simulation = 0;
    time = fe2_session_time(s) + s->simulation_step; fe2_session_set_time(s, time);
    compare_high = s->pools.clock[1];
    if (time >= s->scheduled_time) {
        fe2_session_set_time(s, s->scheduled_time);
        if (fe2_session_broadcast(s, 0x15)) goto fail;
        if (s->restart) goto restart;
        compare_high = (uint32_t)(s->scheduled_time >> 32);
        s->scheduled_time |= UINT64_C(0xffffffff00000000);
    }
    if (previous_high != compare_high) {
        if ((uint16_t)previous_high % 7u == 0 && fe2_session_broadcast(s, 0x10)) goto fail;
        if (s->restart) goto restart;
        if (fe2_session_broadcast(s, 0x0f)) goto fail;
        if (s->restart) goto restart;
    }
    s->render_step = s->pending_render; s->pending_render = 0;
    time = fe2_session_time(s);
    for (i = 0; i < 4; ++i) words[i] = (uint16_t)(time >> (i * 16));
    fe2_session_pool_services(s, &pool_services);
    fe2_pool_periodic_updates(&s->pools, &pool_services, s->pools.root, words);
    if (s->restart) goto restart;
    if (s->error || input(s)) goto fail;
    if (s->restart) goto restart;
    if (updates(s, &pool_services)) goto fail;
    if (s->restart) goto restart;
    fe2_pool_reset_frame(&s->pools, s->pools.root);
    if (!(s->render_mode & 128u)) {
        if (phase(s, 1, 0)) goto fail;
    } else {
        if (s->services.assign_palette) s->services.assign_palette(s->services.context, s);
        if ((s->render_mode & 1u) && s->services.restore_color) s->services.restore_color(s->services.context, s);
        if (s->services.execute_frame) s->services.execute_frame(s->services.context, s);
        if (phase(s, 1, 0)) goto fail;
        if (s->restart) goto restart;
        fe2_session_update_pointer(s, NULL, NULL);
        if (s->services.present) s->services.present(s->services.context, s);
    }
    if (s->restart) goto restart;
    s->busy = 0; return s->error ? FE2_SESSION_ERROR : result;
restart:
    if (unwind(s)) goto fail;
    result = FE2_SESSION_RESTARTED;
    s->busy = 0; return result;
fail:
    s->busy = 0; s->error = 1; s->restart = 0; return FE2_SESSION_ERROR;
}
