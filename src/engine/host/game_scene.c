/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native record-to-camera bridge for the recovered model renderer.
 * The coordinate reduction and descriptor exponent adjustment follow
 * record_model_rendering_and_group_setup.inc (0x3808 onward). */
#include "gameplay.h"
#include "game/object_data.h"
#include "render/attachment.h"
#include "render/view.h"
#include "math/math3.h"
#include <string.h>

static int32_t sar(int32_t value, unsigned count)
{
    if (count >= 32) return value < 0 ? -1 : 0;
    return count ? (int32_t)(((uint32_t)value >> count) |
        (value < 0 ? UINT32_MAX << (32 - count) : 0u)) : value;
}

void fe2_gameplay_model_variables(const fe2_object *o, uint16_t out[86])
{
    unsigned i;
    memset(out, 0, 86 * sizeof(*out));
    out[0] = o->clock; out[1] = o->cargo_type;
    out[2] = (uint16_t)o->target_seed; out[3] = (uint16_t)(o->target_seed >> 16);
    out[4] = o->size_mantissa; out[5] = o->size_exponent;
    for (i = 0; i < 2; ++i) {
        out[6 + i * 2] = (uint16_t)o->orbit[i]; out[7 + i * 2] = (uint16_t)(o->orbit[i] >> 16);
    }
    out[10] = (uint16_t)o->scalar; out[11] = (uint16_t)(o->scalar >> 16);
    for (i = 0; i < 3; ++i) out[12 + i] = o->vector_delta[i];
    if (o->role == 1) {
        for (i = 0; i < 4; ++i) out[15 + i] = o->as.body.orbit_product[i];
        out[19] = (uint16_t)o->as.body.orbit_radius; out[20] = (uint16_t)(o->as.body.orbit_radius >> 16);
        out[21] = o->as.body.orbit_inclination;
        out[22] = (uint16_t)(o->as.body.time_shift | o->as.body.docking_state << 8);
        out[23] = (uint16_t)(o->as.body.docking_claim | o->as.body.generation << 8);
        out[24] = o->as.body.basis_seed; out[25] = o->as.body.shape;
        for (i = 0; i < 4; ++i) out[26 + i] = (uint16_t)(o->as.body.docked[i * 2] | o->as.body.docked[i * 2 + 1] << 8);
        out[30] = o->as.body.docking_timer; out[31] = o->as.body.scale_exponent;
        out[32] = o->as.body.radius_class; out[33] = o->as.body.progress; out[34] = o->as.body.temperature;
    } else {
        for (i = 0; i < 3; ++i) out[13 + i] = (uint16_t)o->controls[i];
        for (i = 0; i < 6; ++i) out[16 + i] = (uint16_t)o->as.flight.control_limits[i / 2][i % 2];
        out[22] = (uint16_t)o->as.flight.equipment; out[23] = (uint16_t)(o->as.flight.equipment >> 16);
        out[24] = (uint16_t)o->as.flight.equipment_damaged; out[25] = (uint16_t)(o->as.flight.equipment_damaged >> 16);
        out[26] = (uint16_t)(o->as.flight.drive | o->as.flight.capacity_class << 8);
        for (i = 0; i < 6; ++i) out[27 + i] = (uint16_t)(o->as.flight.installed[i * 2] | o->as.flight.installed[i * 2 + 1] << 8);
        out[33] = o->as.flight.shield_charge; out[34] = o->as.flight.shield_capacity;
    }
    out[35] = o->hull; out[36] = o->stored_model; out[37] = o->cooldown; out[38] = o->busy_remaining;
    for (i = 0; i < 3; ++i) out[39 + i] = o->velocity_fraction[i];
    for (i = 0; i < 3; ++i) { out[42 + i * 2] = (uint16_t)o->feedback[i]; out[43 + i * 2] = (uint16_t)((uint32_t)o->feedback[i] >> 16); }
    out[48] = (uint16_t)(o->behaviour_parent | o->behaviour << 8);
    out[49] = (uint16_t)(o->behaviour_target | o->control_class << 8);
    for (i = 0; i < 3; ++i) { out[50 + i] = (uint16_t)o->source_offset[i]; out[53 + i] = (uint16_t)o->steering[i]; out[57 + i] = (uint16_t)o->rotation_rates[i]; }
    out[56] = o->steering_shift; out[60] = o->capacity;
    out[61] = (uint16_t)(o->map_marker | o->drive_fuel << 8);
    out[62] = (uint16_t)o->reward; out[63] = (uint16_t)(o->reward >> 16);
    out[64] = (uint16_t)o->fuel; out[65] = (uint16_t)(o->fuel >> 16);
    for (i = 0; i < 10; ++i) out[66 + i] = (uint16_t)((uint8_t)o->name[i * 2] | (uint8_t)o->name[i * 2 + 1] << 8);
    for (i = 0; i < 3; ++i) { out[76 + i] = o->model_scale_a[i]; out[79 + i] = o->model_scale_b[i]; }
    out[82] = o->transition_countdown; out[83] = o->flags;
    out[84] = (uint16_t)(o->trail | o->effect << 8); out[85] = (uint16_t)(o->probe_countdown | o->frame_state << 8);
}

static int submit_record(fe2_gameplay *g, fe2_object *camera, fe2_object *o, int write_screen)
{
    fe2_render_model_transform t;
    fe2_render_bytes descriptor;
    fe2_render_program_result result;
    fe2_scaled reduced;
    int64_t difference[3];
    int32_t transformed[3];
    uint16_t values[86];
    const int16_t *basis = camera->orientation, *source = o->orientation;
    unsigned i, j, k;
    int level, shift;
    if ((o->model & 1u) || o->model / 2 >= 240) return -1;
    descriptor = g->host->resources->models[0][o->model / 2];
    if (!descriptor.data || descriptor.count < 0x18) return -1;
    fe2_position_relative(&g->position, o, camera, difference);
    reduced = fe2_motion_scale_wide(difference);
    level = (int)(int16_t)(reduced.shift - o->model_parameter);
    o->level = (uint8_t)level;
    {
        int cutoff = (int16_t)fe2_object_data_word(descriptor.data + 0x10);
        if (level >= cutoff) {
            unsigned delta = (unsigned)(level - cutoff) & 31u;
            unsigned period = delta < 8 ? 1u << delta : 0;
            o->update_period = (uint8_t)(period && period < 128 ? period : 127);
            /* a culled model invalidates last frame's projection. */
            if (write_screen) o->screen_x = o->screen_y = 0x8000;
            return 0;
        }
    }

    o->update_period = 0;
    if (camera->station_slot && o->station_slot == 0 && camera->parent_handle != o->handle)
        basis = camera->alternate_orientation;
    if (!camera->station_slot && o->station_slot) {
        fe2_position_secondary_basis(&g->position, o);
        source = o->alternate_orientation;
    }
    fe2_math_matrix_vector(basis, reduced.v, transformed);
    memset(&t, 0, sizeof(t));
    t.exponent = fe2_object_data_word(descriptor.data + 0x0a);
    t.scale = fe2_object_data_word(descriptor.data + 0x0c);
    shift = 16 - level + (int16_t)t.scale;
    if (shift < 0) {
        t.scale = (uint16_t)(t.scale - shift);
        t.exponent = (uint16_t)(t.exponent + shift); shift = 0;
    }
    for (i = 0; i < 3; ++i) t.position[i] = sar(transformed[i], (unsigned)shift & 31u);
    if (!fe2_render_model_visible(t.position, fe2_object_data_word(descriptor.data + 0x0e), t.exponent)) {
        if (write_screen) o->screen_x = o->screen_y = 0x8000;
        return 0;
    }
    for (i = 0; i < 3; ++i) for (j = 0; j < 3; ++j) {
        uint32_t sum = 0;
        for (k = 0; k < 3; ++k) sum += (uint32_t)((int32_t)source[k * 3 + i] * basis[k * 3 + j]);
        t.basis[i * 3 + j] = (int16_t)(uint16_t)((sum << 1) >> 16);
    }
    if (camera->station_slot && !o->station_slot && camera->parent_handle == o->handle)
        memcpy(t.basis, camera->orientation, sizeof(t.basis));
    memcpy(t.direction, camera->station_slot ? g->reference.secondary : g->reference.primary, sizeof(t.direction));
    for (i = 0; i < 8; ++i) t.shades[i] = (uint16_t)(g->reference.parameters[i / 2] >> (16 * (i % 2)));
    t.material = 0x111;
    {
        fe2_screen_point point;
        int projected = fe2_render_project(t.position[0], t.position[1], t.position[2], &point);
        if (write_screen) {
            o->screen_x = projected ? (uint16_t)point.x : 0x8000;
            o->screen_y = projected ? (uint16_t)point.y : 0x8000;
        }
        if (projected && level >= (int16_t)fe2_object_data_word(descriptor.data + 0x14)) {
            fe2_render_vertex vertex;
            fe2_render_round_command command;
            int status;
            memcpy(vertex.camera, t.position, sizeof(vertex.camera)); vertex.screen = point;
            status = fe2_render_build_distant(&vertex, fe2_object_data_word(descriptor.data + 0x0e),
                fe2_object_data_word(descriptor.data + 0x12), &t, &command);
            if (status < 0 || (status && fe2_render_runtime_round(g->host->runtime, &command))) return -1;
            if (status) { ++g->submitted; ++g->distant; }
            return 0;
        }
    }
    fe2_gameplay_model_variables(o, values);
    result = fe2_render_runtime_submit(g->host->runtime, o->model, &t,
        (fe2_render_words){values, 86}, (fe2_render_words){(const uint16_t *)fe2_math_sine, 2048});
    if (result.status != FE2_RENDER_PROGRAM_COMPLETE) return -1;
    ++g->submitted;
    if (o->model == 0xbe) ++g->beams;
    return 0;
}

typedef struct scene_pass {
    fe2_gameplay *game;
    fe2_object *camera;
} scene_pass;

/* A model that stops is omitted. Later records are still visited, so the
 * planet behind a beam is still drawn. */
static void scene_record(void *context, fe2_object *record)
{
    scene_pass *pass = context;
    (void)submit_record(pass->game, pass->camera, record, 1);
}
static uint32_t scene_random(void *context)
{
    scene_pass *pass = context;
    uint32_t mix = 0;
    return fe2_session_objects_random(&pass->game->objects, &mix);
}
static const uint8_t *scene_model(void *context, uint16_t selector)
{
    scene_pass *pass = context;
    return !(selector & 1u) && selector / 2 < 240 ?
        pass->game->host->resources->models[0][selector / 2].data : NULL;
}
/* Resident 0xfc consumes mount bits 2/3/4 in priority order. The scene
 * traversal clears bit 1 before calling it, including for the view owner. */
static void scene_attachment(void *context, fe2_object *o)
{
    scene_pass *pass = context;
    fe2_gameplay *g = pass->game;
    fe2_render_bytes bank;
    fe2_object placed;
    fe2_render_attachment_state state;
    size_t offset = 0;
    if ((o->model & 1u) || o->model / 2 >= 240) return;
    bank = g->host->resources->models[0][o->model / 2];
    if (g->host->resources->model_banks[0].data) {
        bank = g->host->resources->model_banks[0];
        offset = g->host->resources->model_offsets[0][o->model / 2];
    }
    state.angle[0] = (uint16_t)g->flight.state.turret_angle[0];
    state.angle[1] = (uint16_t)g->flight.state.turret_angle[1];
    state.size_high = (uint8_t)(o->size_mantissa >> 8);
    if (!bank.data || fe2_render_place_attachment(o, o, bank, offset, &state, &placed)) return;
    placed.render_state = 0;
    (void)submit_record(g, pass->camera, &placed, 0);
}

int fe2_gameplay_render_scene(fe2_gameplay *g, fe2_object *camera)
{
    fe2_session *s = g->session;
    int64_t absolute[3];
    scene_pass pass = {g, camera};
    fe2_reference_services services = {&pass, scene_record, scene_attachment, scene_random, scene_model};
    g->position.current = g->position.saved = &s->objects;
    g->position.generation = s->generation;
    fe2_position_absolute(&g->position, camera, absolute);
    g->reference.clock = s->pools.clock[0];
    g->reference.player = g->flight.state.view_owner;
    g->reference.game_flags = s->pools.game_flags;
    ++g->scenes;
    fe2_reference_render(&g->reference, &services, camera);
    s->pools.game_flags = g->reference.game_flags;
    memcpy(s->light, g->reference.primary, sizeof s->light);
    s->resident.classification_0 = g->reference.parameters[0]; s->resident.classification_1 = g->reference.parameters[1];
    s->resident.classification_2 = g->reference.parameters[2]; s->resident.classification_3 = g->reference.parameters[3];
    if (fe2_render_runtime_atmosphere(g->host->runtime)) g->flight.state.scene_flash = 0xffff;
    return 0;
}

/* Navigation resolves every parent in its own pool. Keep flight's cached
 * position/reference state intact while sharing the native model pipeline. */
int fe2_gameplay_render_navigation(fe2_gameplay *g, fe2_object *camera, fe2_object *object)
{
    fe2_position position = g->position;
    fe2_reference reference = g->reference;
    int64_t absolute[3];
    int result = 0;
    scene_pass pass = {g, camera};
    fe2_reference_services services = {&pass, scene_record, scene_attachment, scene_random, scene_model};
    g->position.current = g->position.saved = &g->session->map_objects;
    g->position.generation = g->session->generation;
    camera->render_state = 0;
    fe2_position_absolute(&g->position, camera, absolute);
    g->reference.clock = g->session->pools.clock[0];
    if (object) {
        (void)fe2_reference_update(&g->reference, camera);
        result = submit_record(g, camera, object, 0);
        ++g->navigation_projections;
    } else {
        ++g->navigation_scenes;
        fe2_reference_refresh(&g->reference, &services, camera);
    }
    g->position = position; g->reference = reference;
    return result;
}
