/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "model.h"

static uint16_t read_word(const uint8_t *data)
{
    return (uint16_t)((unsigned)data[0] | (unsigned)data[1] << 8);
}

static int signed_word(uint16_t value)
{
    return value < 32768u ? (int)value : (int)value - 65536;
}

static int16_t shift_word(int16_t value, unsigned count)
{
    unsigned bits = (uint16_t)value;
    count &= 31u;
    if (!count) return value;
    if (count >= 16) return value < 0 ? -1 : 0;
    bits = (bits >> count) | (value < 0 ? 65535u << (16 - count) : 0u);
    return (int16_t)signed_word((uint16_t)bits);
}

int fe2_render_model_open(fe2_render_bytes resource, size_t base, fe2_render_model *out)
{
    fe2_render_model result;
    const uint8_t *header;
    size_t program, context, geometry, available;
    if (!out || !resource.data || base > resource.count || resource.count - base < 10) return -1;
    header = resource.data + base;
    available = resource.count - base;
    program = read_word(header);
    context = read_word(header + 2);
    geometry = read_word(header + 6);
    if (program > available || available - program < 2 || context > available || geometry > available) return -1;
    result.vertex_count = read_word(header + 4) >> 5;
    if (!result.vertex_count) return -1;
    result.parameter_bytes = read_word(header + 8);
    result.program.data = header + program; result.program.count = available - program;
    result.context.data = header + context; result.context.count = available - context;
    result.geometry.data = header + geometry; result.geometry.count = available - geometry;
    *out = result;
    return 0;
}

int fe2_render_model_begin(const fe2_render_model *model, const fe2_render_view *view,
                             uint16_t exponent, uint32_t header,
                             const fe2_render_model_workspace *work, fe2_render_model_context *out)
{
    fe2_render_model_context result;
    fe2_face_frame face = {0};
    size_t count, i;
    uint16_t target;
    if (!model || !view || !work || !out || !work->vertices || !work->parameters ||
        !model->vertex_count || work->vertex_capacity < FE2_RENDER_PREFIX_VERTICES ||
        model->vertex_count > work->vertex_capacity - FE2_RENDER_PREFIX_VERTICES) return -1;
    count = model->parameter_bytes >> 1;
    if (count < 2) count = 2;
    if (work->parameter_capacity < count) return -1;
    result.model = *model;
    result.view = *view;
    target = (uint16_t)(exponent - 8u);
    if (signed_word(target) > signed_word(view->shift)) {
        unsigned delta = (uint16_t)(target - view->shift);
        result.view.shift = target;
        for (i = 0; i < 3; ++i) result.view.position[i] = shift_word(view->position[i], delta);
    }
    result.vertices = work->vertices + FE2_RENDER_PREFIX_VERTICES;
    result.parameters = work->parameters;
    result.parameter_count = count;
    result.stamp = 2;
    for (i = 0; i < FE2_RENDER_PREFIX_VERTICES + model->vertex_count; ++i) work->vertices[i].state = 0;
    face.header = header;
    fe2_math_init_faces(&face, work->parameters, model->parameter_bytes);
    *out = result;
    return 0;
}
