/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "runtime.h"
#include "points.h"
#include <stdlib.h>
#include <string.h>
enum { DEPTH = 8, VERTICES = 517, PARAMETERS = 512, COMMANDS = 512, ELEMENTS = 8192 };
struct fe2_render_runtime {
    const fe2_render_runtime_resources *resources;
    fe2_video video;
    fe2_graphics_cache graphics;
    fe2_palette_cache palette;
    fe2_render_backend backend;
    fe2_render_execution execution;
    fe2_render_execution_target target;
    fe2_render_program_drawing drawing;
    fe2_render_execution_frame frames[DEPTH];
    fe2_vertex vertices[DEPTH][VERTICES];
    uint16_t parameters[DEPTH][PARAMETERS];
    fe2_vertex_work work;
    uint32_t *shared_noise; /* session seed shared with gameplay random */
    fe2_render_queue queue;
    fe2_render_command_group groups[COMMANDS], batches[COMMANDS];
    fe2_render_command line_nodes[COMMANDS], surface_nodes[COMMANDS], texture_nodes[COMMANDS];
    fe2_render_command round_nodes[COMMANDS], text_nodes[COMMANDS], path_nodes[COMMANDS];
    fe2_render_command curve_nodes[COMMANDS], point_nodes[COMMANDS], sphere_nodes[COMMANDS];
    fe2_render_segment lines[COMMANDS];
    fe2_render_surface_command surfaces[COMMANDS];
    fe2_render_texture_command textures[COMMANDS];
    fe2_render_round_command rounds[COMMANDS];
    fe2_render_text_command texts[COMMANDS];
    fe2_render_path_command paths[COMMANDS];
    fe2_render_curve_command curves[COMMANDS];
    fe2_render_point_command points[COMMANDS];
    fe2_render_sphere_command spheres[COMMANDS];
    fe2_render_path_element path_elements[ELEMENTS];
    fe2_render_sphere_element sphere_elements[ELEMENTS];
    fe2_screen_point point_elements[ELEMENTS];
    fe2_render_path_output path_output;
    fe2_render_point_output point_output;
    fe2_render_sphere_output sphere_output;
    fe2_render_texture_output texture_output;
    uint8_t image[65536], text[DEPTH][256], color_map[256];
    uint8_t color_slots[4096], text_page[65536];
    int active, error, preparing, drawn;
};
static int model(void *context, uint16_t selector, int alternate, fe2_render_bytes *out)
{
    fe2_render_runtime *r = context;
    unsigned index = selector / 2u;
    if ((selector & 1u) || index >= 240 || !r->resources->models[!!alternate][index].data) return -1;
    *out = r->resources->models[!!alternate][index]; return 0;
}
static int text(void *context, uint16_t selector, fe2_render_bytes *out)
{
    fe2_render_runtime *r = context;
    fe2_text_resources resources;
    size_t depth = r->execution.depth, length;
    if (depth >= DEPTH) return -1;
    resources = r->resources->text;
    /* are the object's text (fe2-intro entityVars+66). Nested
     * selectors such as the obituary  expand 0x3016 themselves, so the
     * pointer is installed for every expansion while a model is open. */
    if (depth) {
        const fe2_render_words *vars = &r->execution.frames[depth - 1].state.model;
        if (vars->data && vars->count > 66) {
            resources.pointer[0].data = (const uint8_t *)(vars->data + 66);
            resources.pointer[0].count = (vars->count - 66) * 2u;
        }
    }
    if (fe2_text_expand(&resources, selector, r->text[depth], 256, &length)) return -1;
    out->data = r->text[depth]; out->count = length; return 0;
}
static void sound(void *context, uint16_t event, uint16_t duration)
{
    fe2_render_runtime *r = context;
    if (r->resources->sound) r->resources->sound(r->resources->sound_context, event, duration);
}
static void span(void *context, unsigned y, unsigned x, unsigned count, uint8_t color)
{
    fe2_render_runtime *r = context;
    if (!r->preparing) fe2_video_span(&r->video, y, x, count, color);
}
static uint8_t color(void *context, uint16_t material)
{
    fe2_render_runtime *r = context;
    int slot, index;
    material &= 4095u;
    if (r->preparing) {
        slot = fe2_palette_lookup(&r->palette, material);
        if (slot < 0) r->error = 1;
        else r->color_slots[material] = (uint8_t)slot;
        return 0;
    }
    slot = r->color_slots[material];
    index = fe2_palette_index(&r->palette, (unsigned)slot);
    if (index < 0) { r->error = 1; return 0; }
    return (uint8_t)index;
}
static int texture(void *context, unsigned selector, fe2_render_texture_image *out)
{
    fe2_render_runtime *r = context;
    fe2_render_bytes record;
    return fe2_graphics_cache_get(&r->graphics, selector, &record) ||
        fe2_graphics_decode(record, r->image, sizeof(r->image), out) ? -1 : 0;
}
static void round_command(void *context, const fe2_render_round_command *c)
{
    fe2_render_runtime *r = context; if (fe2_render_draw_round(c, &r->backend)) r->error = 1;
}
static void text_command(void *context, const fe2_render_text_command *c)
{
    fe2_render_runtime *r = context; if (fe2_render_draw_label(c, &r->backend)) r->error = 1;
}
static void path_command(void *context, const fe2_render_path_command *c)
{
    fe2_render_runtime *r = context; if (fe2_render_draw_path(c, &r->backend)) r->error = 1;
}
static void sphere_command(void *context, const fe2_render_sphere_command *c)
{
    fe2_render_runtime *r = context; if (fe2_render_draw_sphere(c, &r->backend)) r->error = 1;
}
static void points_command(void *context, const fe2_render_point_command *c)
{
    fe2_render_runtime *r = context;
    if (fe2_render_draw_disc_list(c->material, c->size, c->points, c->count, &r->backend)) r->error = 1;
}
static void curve_command(void *context, const fe2_render_curve_command *c)
{
    fe2_render_runtime *r = context;
    fe2_render_cubic_lines(c->points[0], c->points + 1, c->material, fe2_render_backend_line, &r->backend);
}
static void texture_command(void *context, const fe2_render_texture_command *c)
{
    fe2_render_backend *b = context;
    fe2_render_runtime *r = b->context;
    if (fe2_render_draw_texture(c, b) < 0) r->error = 1;
}
fe2_render_runtime *fe2_render_runtime_create(const fe2_render_runtime_resources *resources)
{
    fe2_render_runtime *r;
    unsigned i;
    if (!resources) return NULL;
    r = calloc(1, sizeof(*r)); if (!r) return NULL;
    r->resources = resources; fe2_video_init(&r->video); fe2_graphics_cache_init(&r->graphics);
    fe2_palette_initialize(&r->palette);
    for (i = 0; i < 16; ++i) fe2_video_palette(&r->video, (uint8_t)(16 + i),
        (uint8_t)(63 - i * 4), (uint8_t)(63 - i * 4), (uint8_t)(63 - i * 4));
    for (i = 0; i < 256; ++i) r->color_map[i] = (uint8_t)i;
    r->backend.color = color; r->backend.span = span; r->backend.context = r;
    r->backend.texture = texture; r->backend.text_resources = &resources->text;
    r->backend.font = resources->font; r->backend.font_glyphs = resources->glyphs;
    r->backend.color_map = r->color_map;
    r->target.drawing.line.draw = fe2_render_backend_line; r->target.drawing.line.context = &r->backend;
    r->target.drawing.surface = fe2_render_backend_surface; r->target.drawing.surface_context = &r->backend;
    r->target.drawing.texture = texture_command;
    r->target.round = round_command; r->target.text = text_command; r->target.path = path_command;
    r->target.sphere = sphere_command; r->target.points = points_command; r->target.curve = curve_command; r->target.context = r;
    r->execution.services.context = r; r->execution.services.model = model; r->execution.services.instance_text = text;
    r->execution.services.sound = sound;
    r->execution.detail_shift[0] = resources->detail_shift[0]; r->execution.detail_shift[1] = resources->detail_shift[1];
    r->sphere_output.layer_threshold = 0;
    r->execution.frames = r->frames; r->execution.frame_capacity = DEPTH; r->execution.vertex_work = &r->work;
    r->work.noise_a = 1; r->work.noise_b = 2;
    r->execution.groups = r->groups; r->execution.group_capacity = COMMANDS;
    r->execution.batches = r->batches; r->execution.batch_capacity = COMMANDS;
    r->execution.round_nodes = r->round_nodes; r->execution.rounds = r->rounds; r->execution.round_capacity = COMMANDS;
    r->execution.text_nodes = r->text_nodes; r->execution.texts = r->texts; r->execution.text_capacity = COMMANDS;
    for (i = 0; i < DEPTH; ++i) {
        r->frames[i].workspace.vertices = r->vertices[i]; r->frames[i].workspace.vertex_capacity = VERTICES;
        r->frames[i].workspace.parameters = r->parameters[i]; r->frames[i].workspace.parameter_capacity = PARAMETERS;
    }
    r->drawing.queue = &r->queue; r->drawing.nodes = r->line_nodes; r->drawing.segments = r->lines; r->drawing.capacity = COMMANDS;
    r->drawing.surface_nodes = r->surface_nodes; r->drawing.surfaces = r->surfaces; r->drawing.surface_capacity = COMMANDS;
    r->texture_output.nodes = r->texture_nodes; r->texture_output.commands = r->textures; r->texture_output.capacity = COMMANDS;
    r->drawing.textures = &r->texture_output;
    r->drawing.textured_surfaces = resources->textured_surfaces;
    r->path_output.nodes = r->path_nodes; r->path_output.paths = r->paths; r->path_output.capacity = COMMANDS;
    r->path_output.elements = r->path_elements; r->path_output.element_capacity = ELEMENTS;
    r->path_output.curve_nodes = r->curve_nodes; r->path_output.curves = r->curves; r->path_output.curve_capacity = COMMANDS;
    r->point_output.nodes = r->point_nodes; r->point_output.commands = r->points; r->point_output.capacity = COMMANDS;
    r->point_output.points = r->point_elements; r->point_output.point_capacity = ELEMENTS;
    r->sphere_output.nodes = r->sphere_nodes; r->sphere_output.commands = r->spheres; r->sphere_output.capacity = COMMANDS;
    r->sphere_output.elements = r->sphere_elements; r->sphere_output.element_capacity = ELEMENTS;
    r->execution.paths = &r->path_output; r->execution.points = &r->point_output; r->execution.spheres = &r->sphere_output;
    return r;
}
void fe2_render_runtime_destroy(fe2_render_runtime *r) { free(r); }
fe2_video *fe2_render_runtime_video(fe2_render_runtime *r) { return r ? &r->video : NULL; }
fe2_palette_cache *fe2_render_runtime_palette(fe2_render_runtime *r) { return r ? &r->palette : NULL; }
fe2_graphics_cache *fe2_render_runtime_graphics(fe2_render_runtime *r) { return r ? &r->graphics : NULL; }
fe2_vertex_work *fe2_render_runtime_vertex_work(fe2_render_runtime *r) { return r ? &r->work : NULL; }
void fe2_render_runtime_bind_noise(fe2_render_runtime *r, uint32_t noise[2])
{
    if (r) r->shared_noise = noise;
}
static void borrow_noise(fe2_render_runtime *r)
{
    if (!r->shared_noise) return;
    r->work.noise_a = r->shared_noise[0];
    r->work.noise_b = r->shared_noise[1];
}
static void return_noise(fe2_render_runtime *r)
{
    if (!r->shared_noise) return;
    r->shared_noise[0] = r->work.noise_a;
    r->shared_noise[1] = r->work.noise_b;
}
void fe2_render_runtime_set_detail(fe2_render_runtime *r, uint16_t solid, uint16_t surface, int16_t layers)
{
    if (!r) return;
    r->execution.detail_shift[0] = solid;
    r->execution.detail_shift[1] = surface;
    r->sphere_output.layer_threshold = layers;
}
void fe2_render_runtime_detail(const fe2_render_runtime *r, uint16_t *solid, uint16_t *surface, int16_t *layers)
{
    if (solid) *solid = r ? r->execution.detail_shift[0] : 0;
    if (surface) *surface = r ? r->execution.detail_shift[1] : 0;
    if (layers) *layers = r ? r->sphere_output.layer_threshold : 0;
}
int fe2_render_runtime_abort(fe2_render_runtime *r)
{
    if (!r || r->queue.executing || r->execution.depth) return -1;
    r->active = r->drawn = r->error = 0; return 0;
}
static void reset_commands(fe2_render_runtime *r)
{
    r->drawn = 0;
    fe2_render_queue_init(&r->queue);
    r->drawing.count = r->drawing.surface_count = r->texture_output.count = 0;
    r->execution.group_count = r->execution.batch_count = r->execution.round_count = r->execution.text_count = 0;
    r->execution.active_group = NULL; memset(r->execution.trees, 0, sizeof(r->execution.trees));
    memset(r->execution.attachments, 0, sizeof(r->execution.attachments));
    r->path_output.count = r->path_output.element_count = r->path_output.curve_count = 0;
    r->point_output.count = r->point_output.point_count = 0;
    r->sphere_output.count = r->sphere_output.element_count = 0; r->sphere_output.rgb_set = 0;
}
int fe2_render_runtime_begin(fe2_render_runtime *r, uint8_t background)
{
    if (!r || r->active || r->video.drawing > 1) return -1;
    r->active = 1; r->error = 0; r->backend.page = r->video.pages[r->video.drawing];
    memset(r->color_slots, 255, sizeof(r->color_slots));
    fe2_video_clear_view(&r->video, background); fe2_palette_age(&r->palette);
    reset_commands(r);
    return 0;
}
int fe2_render_runtime_layer(fe2_render_runtime *r)
{
    uint8_t rgb[3];
    int rgb_set;
    unsigned slot, count = 0;
    if (!r || !r->active || r->queue.executing || r->execution.depth || fe2_render_runtime_draw(r)) return -1;
    memcpy(rgb, r->sphere_output.rgb, sizeof rgb); rgb_set = r->sphere_output.rgb_set;
    reset_commands(r);
    memcpy(r->sphere_output.rgb, rgb, sizeof rgb); r->sphere_output.rgb_set = rgb_set;
    /* assign() reuses the cache's scratch list for DAC allocation. Rebuild
     * its slot list for the next lookup pass without aging visible colors. */
    for (slot = 0; slot < FE2_PALETTE_SLOTS; ++slot)
        if (r->palette.slots[slot].state == FE2_PALETTE_FREE)
            r->palette.free_list[count++] = (uint8_t)slot;
    r->palette.free_list[count] = 0xff;
    r->palette.cursor = 0;
    return 0;
}
fe2_render_program_result fe2_render_runtime_submit(fe2_render_runtime *r, uint16_t selector,
    const fe2_render_model_transform *transform, fe2_render_words variables, fe2_render_words scales)
{
    fe2_render_program_result result = {FE2_RENDER_PROGRAM_BAD_VALUE, 0, 0, 0};
    fe2_render_bytes descriptor;
    if (!r || !r->active || r->drawn) return result;
    if (model(r, selector, 0, &descriptor)) { r->error = 1; return result; }
    borrow_noise(r);
    result = fe2_render_execute_model(&r->execution, descriptor, transform, variables, scales, NULL, 0, &r->drawing, 65536);
    return_noise(r);
    if (result.status != FE2_RENDER_PROGRAM_COMPLETE) r->error = 1;
    return result;
}
fe2_render_program_result fe2_render_runtime_submit_alternate(fe2_render_runtime *r, uint16_t selector,
    const fe2_render_model_transform *transform, fe2_render_words variables,
    const fe2_render_bytes *context, size_t vertex_count)
{
    fe2_render_program_result result = {FE2_RENDER_PROGRAM_BAD_VALUE, 0, 0, 0};
    fe2_render_bytes descriptor;
    fe2_render_words scales = {(const uint16_t *)fe2_math_sine, 2048};
    if (!r || !r->active || r->drawn) return result;
    if (model(r, selector, 1, &descriptor)) { r->error = 1; return result; }
    borrow_noise(r);
    result = fe2_render_execute_model(&r->execution, descriptor, transform, variables, scales,
        context, vertex_count, &r->drawing, 65536);
    return_noise(r);
    if (result.status != FE2_RENDER_PROGRAM_COMPLETE) r->error = 1;
    return result;
}
static void queued_round(void *target, const void *payload)
{
    fe2_render_execution_target *out = target;
    if (out && out->round) out->round(out->context, payload);
}
int fe2_render_runtime_round(fe2_render_runtime *r, const fe2_render_round_command *command)
{
    size_t index;
    if (!r || !command || !r->active || r->drawn || r->queue.executing ||
        r->execution.depth || r->execution.round_count >= COMMANDS) return -1;
    index = r->execution.round_count;
    r->rounds[index] = *command;
    if (fe2_render_queue_insert(&r->queue, r->round_nodes + index, command->key,
        queued_round, r->rounds + index)) return -1;
    ++r->execution.round_count;
    return 0;
}
/* clear_viewport_index_127 fills with 0x7f. The current drawing colour
 * (DS:0x8931, written by a shaded atmosphere) is DAC entry 127. With no
 * atmosphere the entry is 0x5, the default space blue. */
static void apply_background(fe2_render_runtime *r)
{
    uint8_t red = 3, green = 3, blue = 23;
    if (r->sphere_output.rgb_set) {
        red = r->sphere_output.rgb[0];
        green = r->sphere_output.rgb[1];
        blue = r->sphere_output.rgb[2];
    }
    fe2_video_palette(&r->video, 0x7f, red, green, blue);
}
int fe2_render_runtime_atmosphere(const fe2_render_runtime *r)
{ return r && r->sphere_output.rgb_set; }
int fe2_render_runtime_draw(fe2_render_runtime *r)
{
    if (!r || !r->active) return -1;
    if (r->drawn) return r->error ? -1 : 0;
    /* Reserve the frame's colors before assigning DAC entries. Lookup after
     * assignment consumes a different free-list layout in the recovered cache. */
    r->preparing = 1; r->backend.page = r->text_page;
    if (fe2_render_queue_execute(&r->queue, &r->target)) r->error = 1;
    if (fe2_palette_assign(&r->palette, fe2_video_palette, &r->video) < 0) r->error = 1;
    r->preparing = 0; r->backend.page = r->video.pages[r->video.drawing];
    if (fe2_render_queue_execute(&r->queue, &r->target)) r->error = 1;
    apply_background(r);
    r->drawn = 1; return r->error ? -1 : 0;
}
int fe2_render_runtime_present(fe2_render_runtime *r, fe2_video_present_fn present, void *context)
{
    int error;
    if (!r || !r->active || !present) return -1;
    error = fe2_render_runtime_draw(r); r->active = 0;
    if (fe2_video_present(&r->video, present, context)) return -1;
    return error ? -1 : 0;
}
