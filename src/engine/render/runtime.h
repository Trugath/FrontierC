/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_RUNTIME_H
#define FE2_RENDER_RUNTIME_H
#include "video.h"
#include "attachment.h"
typedef struct fe2_render_runtime fe2_render_runtime;
typedef struct fe2_render_runtime_resources {
    fe2_render_bytes models[2][240];
    /* Original resource-bank offsets are needed by the attachment service's
     * changed-base reload. These contain editable model data only. */
    fe2_render_bytes model_banks[2];
    uint16_t model_offsets[2][240];
    fe2_text_resources text;
    const uint8_t *font;
    size_t glyphs;
    uint16_t detail_shift[2];
    int textured_surfaces;
    void *sound_context;
    void (*sound)(void *context, uint16_t event, uint16_t duration);
} fe2_render_runtime_resources;
/* Owned native workspace for a complete renderer frame. Resources remain
 * caller-owned. No executable images, DOS services or CPU state are used. */
fe2_render_runtime *fe2_render_runtime_create(const fe2_render_runtime_resources *resources);
void fe2_render_runtime_destroy(fe2_render_runtime *runtime);
fe2_video *fe2_render_runtime_video(fe2_render_runtime *runtime);
fe2_palette_cache *fe2_render_runtime_palette(fe2_render_runtime *runtime);
fe2_graphics_cache *fe2_render_runtime_graphics(fe2_render_runtime *runtime);
fe2_vertex_work *fe2_render_runtime_vertex_work(fe2_render_runtime *runtime);
/* Model vertices and the object random service are one seed.
 * Submissions borrow it and put the advanced pair back. */
void fe2_render_runtime_bind_noise(fe2_render_runtime *runtime, uint32_t noise[2]);
/* surface may be negative (low detail). */
void fe2_render_runtime_set_detail(fe2_render_runtime *runtime, uint16_t solid, uint16_t surface, int16_t layers);
void fe2_render_runtime_detail(const fe2_render_runtime *runtime, uint16_t *solid, uint16_t *surface, int16_t *layers);
int fe2_render_runtime_begin(fe2_render_runtime *runtime, uint8_t background);
int fe2_render_runtime_abort(fe2_render_runtime *runtime);
fe2_render_program_result fe2_render_runtime_submit(fe2_render_runtime *runtime, uint16_t model,
    const fe2_render_model_transform *transform, fe2_render_words variables, fe2_render_words scales);
/* Alternate resource model, optionally replacing its encoded vertex context. */
fe2_render_program_result fe2_render_runtime_submit_alternate(fe2_render_runtime *runtime, uint16_t model,
    const fe2_render_model_transform *transform, fe2_render_words variables,
    const fe2_render_bytes *context, size_t vertex_count);
/* Execute the queue before overlay drawing. A second draw is harmless;
 * submissions after drawing are rejected. Present draws first if needed. */
int fe2_render_runtime_draw(fe2_render_runtime *runtime);
/* Queue an already prepared round with the same palette/depth pass as models. */
int fe2_render_runtime_round(fe2_render_runtime *runtime, const fe2_render_round_command *command);
/* The atmosphere flag is produced while preparing the scene, before drawing. */
int fe2_render_runtime_atmosphere(const fe2_render_runtime *runtime);
/* Flush pending commands and start another command layer on the same page.
 * Keeps pixels, palette reservations and the frame's atmosphere intact. */
int fe2_render_runtime_layer(fe2_render_runtime *runtime);
int fe2_render_runtime_present(fe2_render_runtime *runtime, fe2_video_present_fn present, void *context);
#endif
