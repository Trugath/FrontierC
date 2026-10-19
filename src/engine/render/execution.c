/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "execution.h"
#include <string.h>

static uint16_t word(const uint8_t *p)
{
    return (uint16_t)((unsigned)p[0] | (unsigned)p[1] << 8);
}

static int16_t signed_word(uint16_t value)
{
    return (int16_t)(value < 32768u ? (int)value : (int)value - 65536);
}

static int signed_byte(unsigned value)
{
    return value < 128u ? (int)value : (int)value - 256;
}

int fe2_render_inherit_model_transform(const fe2_render_model_transform *parent,
    uint16_t mode, const int32_t position[3], fe2_render_model_transform *out)
{
    static const unsigned destinations[6][3] = {
        {0, 1, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}, {1, 0, 2}, {0, 2, 1}
    };
    fe2_render_model_transform result;
    unsigned selector = (mode >> 8) & 7u, i, j;
    if (!parent || !position || !out || selector == 7) return -1;
    result = *parent;
    memcpy(result.position, position, sizeof(result.position));
    if (selector == 6) {
        memcpy(result.basis, parent->selected_basis, sizeof(result.basis));
        result.parity = parent->selected_parity;
    } else {
        result.parity = parent->parity ^ (selector >= 3 ? 65535u : 0u);
        for (i = 0; i < 3; ++i) {
            unsigned negate = mode & (0x2000u >> i);
            if (negate) result.parity ^= 65535u;
            for (j = 0; j < 3; ++j) {
                uint16_t value = (uint16_t)parent->basis[i * 3 + j];
                result.basis[destinations[selector][i] * 3 + j] =
                    signed_word(negate ? (uint16_t)(0u - value) : value);
            }
        }
    }
    *out = result;
    return 0;
}

static int available(const fe2_render_program_drawing *d, int index)
{
    const fe2_vertex_frame *f = d->vertex_frame;
    if (!f || !f->vertices || (index < 0 ? (size_t)-index > d->prefix_count : (size_t)index >= d->vertex_count)) return 0;
    return index >= 0 || f->vertices[index].state != 0 || (index >= -5 && f->inherited && f->inherited[index]);
}

static fe2_vertex *refresh(fe2_render_program_drawing *d, int index, int place)
{
    const fe2_vertex_frame *f = d->vertex_frame;
    fe2_vertex *v;
    if (!available(d, index)) return NULL;
    v = f->vertices + index;
    if (place) fe2_math_vertex_place(f, index);
    else if ((uint8_t)v->state != f->stamp) fe2_math_vertex_refresh(f, index);
    return v;
}

static void execute_round(void *target, const void *payload)
{
    fe2_render_execution_target *out = target;
    if (out && out->round) out->round(out->context, payload);
}
static void execute_text(void *target, const void *payload)
{
    fe2_render_execution_target *out = target;
    if (out && out->text) out->text(out->context, payload);
}

fe2_render_program_status fe2_render_submit_round(uint16_t token, uint16_t size, uint16_t control,
    fe2_render_program_state *state, fe2_render_program_drawing *d)
{
    fe2_render_execution *e = d->execution;
    fe2_render_execution_frame *f;
    fe2_render_round_command command;
    fe2_render_model_transform transform;
    fe2_vertex *vertex;
    fe2_render_vertex resolved;
    uint32_t radius;
    int delta, status;
    if (!e || !e->depth || !d->queue || d->queue->executing) return FE2_RENDER_PROGRAM_BAD_VALUE;
    f = e->frames + e->depth - 1;
    vertex = refresh(d, signed_byte(control >> 8), 0);
    if (!vertex) return FE2_RENDER_PROGRAM_BAD_VALUE;
    if (!(size & 0x80u)) {
        uint16_t swapped = (uint16_t)((size << 8) | (size >> 8));
        size = swapped & 0x7fffu;
    } else {
        unsigned offset = (uint16_t)(size << 1);
        fe2_render_words values;
        if (offset & 0x80u) { values.data = state->local; values.count = 64; offset &= 0x7fu; }
        else { values = state->model; offset &= 0xfe7fu; }
        if (!values.data || offset / 2 >= values.count) return FE2_RENDER_PROGRAM_BAD_VALUE;
        size = values.data[offset / 2] >> 1;
    }
    delta = signed_word((uint16_t)(f->transform.exponent - 7u));
    if (delta < 0) radius = ((unsigned)(-delta) & 31u) >= 16 ? 0 : (uint16_t)(size >> ((unsigned)(-delta) & 31u));
    else radius = (unsigned)delta % 64 >= 32 ? 0 : (uint32_t)size << ((unsigned)delta & 63u);
    memcpy(resolved.camera, vertex->view, sizeof(resolved.camera));
    resolved.screen.x = vertex->screen[0]; resolved.screen.y = vertex->screen[1];
    transform = f->transform; transform.material = d->inherited_material;
    status = fe2_render_build_round(&resolved, radius, token, control, &transform, &command);
    if (status < 0) return FE2_RENDER_PROGRAM_BAD_VALUE;
    if (!status) return FE2_RENDER_PROGRAM_COMPLETE;
    if (e->round_count >= e->round_capacity) return FE2_RENDER_PROGRAM_OUTPUT_FULL;
    if (!e->rounds || !e->round_nodes) return FE2_RENDER_PROGRAM_BAD_VALUE;
    if (fe2_render_execution_insert(d, e->round_nodes + e->round_count, command.key,
        execute_round, e->rounds + e->round_count)) return FE2_RENDER_PROGRAM_BAD_VALUE;
    e->rounds[e->round_count++] = command;
    return FE2_RENDER_PROGRAM_COMPLETE;
}

fe2_render_program_status fe2_render_submit_text(uint16_t token, uint16_t selector,
    fe2_render_program_state *state, fe2_render_program_drawing *d)
{
    fe2_render_execution *e = d->execution;
    fe2_render_text_command command;
    fe2_vertex *vertex;
    int16_t x, y;
    if (!e || !e->depth || !d->queue || d->queue->executing) return FE2_RENDER_PROGRAM_BAD_VALUE;
    vertex = refresh(d, (token & 0x1fe0u) >> 5, 0);
    if (!vertex) return FE2_RENDER_PROGRAM_BAD_VALUE;
    y = signed_word((uint16_t)(vertex->screen[1] - 4));
    x = signed_word((uint16_t)(vertex->screen[0] + 2));
    if (y < 0 || y > 159 || x > 315) return FE2_RENDER_PROGRAM_COMPLETE;
    if (!state->model.data || state->model.count < 4) return FE2_RENDER_PROGRAM_BAD_VALUE;
    command.key = fe2_render_depth_key(vertex->view, d->scale);
    command.selector = selector; command.position.x = x; command.position.y = y;
    command.parameter[0] = state->model.data[2]; command.parameter[1] = state->model.data[3];
    command.parameter[2] = state->local[4];
    if (e->text_count >= e->text_capacity) return FE2_RENDER_PROGRAM_OUTPUT_FULL;
    if (!e->texts || !e->text_nodes) return FE2_RENDER_PROGRAM_BAD_VALUE;
    if (fe2_render_execution_insert(d, e->text_nodes + e->text_count, command.key,
        execute_text, e->texts + e->text_count)) return FE2_RENDER_PROGRAM_BAD_VALUE;
    e->texts[e->text_count++] = command;
    return FE2_RENDER_PROGRAM_COMPLETE;
}

static void bind(fe2_render_execution *e, fe2_render_execution_frame *f, fe2_render_program_drawing *d)
{
    unsigned i;
    memset(&f->vertices, 0, sizeof(f->vertices));
    memset(&f->faces, 0, sizeof(f->faces));
    f->vertices.vertices = f->context.vertices;
    f->vertices.program = f->context.model.context.data;
    f->vertices.inherited = f->inherited + 5;
    f->vertices.object_words = f->state.model.data;
    f->vertices.frame_words = f->state.local;
    memcpy(f->vertices.matrix, f->transform.basis, sizeof(f->vertices.matrix));
    memcpy(f->vertices.offset, f->transform.position, sizeof(f->vertices.offset));
    f->vertices.scale = f->transform.exponent;
    f->vertices.object_size = word(f->descriptor.data + 0x16);
    f->vertices.stamp = f->context.stamp;
    f->vertices.work = e->vertex_work;
    f->faces.normals = f->context.model.geometry.data;
    f->faces.program = f->context.model.context.data;
    f->faces.scale = f->transform.exponent;
    f->faces.shift = f->context.view.shift;
    memcpy(f->faces.origin, f->context.view.position, sizeof(f->faces.origin));
    memcpy(f->faces.view, f->context.view.direction, sizeof(f->faces.view));
    memcpy(f->faces.shades, f->transform.shades, sizeof(f->faces.shades));
    for (i = 0; i < 5; ++i) f->inherited[i] = i < 4 ? e->attachments[i] : NULL;
    d->vertices = NULL; d->vertex_frame = &f->vertices;
    d->prefix_count = 5; d->vertex_count = f->context.model.vertex_count;
    d->scale = signed_word(f->transform.scale);
    d->inherited_material = f->transform.material;
    d->face_base = f->transform.shades[3];
    d->face_parameters = f->context.parameters;
    d->face_parameter_count = f->context.parameter_count;
    d->face_frame = &f->faces;
    d->execution = e;
}

static void restore_drawing(fe2_render_program_drawing *d, const fe2_render_program_drawing *saved)
{
    size_t lines = d->count, surfaces = d->surface_count;
    *d = *saved;
    d->count = lines; d->surface_count = surfaces;
}

static fe2_render_program_result execute_model(fe2_render_execution *e,
    fe2_render_bytes descriptor, const fe2_render_model_transform *transform,
    fe2_render_words variables, fe2_render_words scales,
    const fe2_render_bytes *external_context, size_t external_vertex_count,
    fe2_render_program_drawing *drawing, size_t budget, fe2_vertex *origin, int retain_view, int test_visibility)
{
    fe2_render_program_result result = {FE2_RENDER_PROGRAM_BAD_VALUE, 0, 0, 0};
    fe2_render_execution_frame *f;
    fe2_render_program_drawing saved;
    fe2_render_view view;
    fe2_render_model model;
    if (!e || !transform || !drawing || !descriptor.data || descriptor.count < 0x18 ||
        !e->frames || !e->vertex_work || (variables.count && !variables.data) ||
        (scales.count && !scales.data)) return result;
    if (test_visibility && !fe2_render_model_visible(transform->position, word(descriptor.data + 14), transform->exponent)) {
        result.status = FE2_RENDER_PROGRAM_COMPLETE;
        return result;
    }
    if (e->depth >= e->frame_capacity) { result.status = FE2_RENDER_PROGRAM_LIMIT; return result; }
    if (fe2_render_model_open(descriptor, 0, &model)) return result;
    if (external_context) {
        if (!external_context->data || !external_vertex_count ||
            external_vertex_count / 2 + external_vertex_count % 2 > external_context->count / 4) return result;
        model.context = *external_context;
        model.vertex_count = external_vertex_count;
    }
    f = e->frames + e->depth;
    if (retain_view) view = f->context.view;
    else fe2_render_prepare_view(transform->position, transform->direction, transform->basis, transform->exponent, &view);
    if (fe2_render_model_begin(&model, &view, transform->exponent,
        (uint32_t)transform->shades[3] | (uint32_t)transform->shades[4] << 16, &f->workspace, &f->context)) return result;
    f->descriptor = descriptor; f->transform = *transform;
    f->context_offset = f->vertex_offset = 0;
    memset(&f->state, 0, sizeof(f->state));
    f->state.model = variables; f->state.scale = scales;
    if (e->depth) {
        const fe2_render_program_state *parent = &e->frames[e->depth - 1].state;
        f->state.local[0] = (uint16_t)(parent->local[0] + 1u);
        f->state.local[1] = parent->local[1]; f->state.local[2] = parent->local[2];
    }
    saved = *drawing;
    bind(e, f, drawing);
    f->inherited[4] = origin;
    ++e->depth;
    result = fe2_render_program_execute_drawing(model.program.data, model.program.count, &f->state, drawing, budget);
    --e->depth;
    if (!e->depth) e->active_group = NULL;
    restore_drawing(drawing, &saved);
    return result;
}

fe2_render_program_result fe2_render_execute_model(fe2_render_execution *e,
    fe2_render_bytes descriptor, const fe2_render_model_transform *transform,
    fe2_render_words variables, fe2_render_words scales,
    const fe2_render_bytes *external_context, size_t external_vertex_count,
    fe2_render_program_drawing *drawing, size_t budget)
{
    return execute_model(e, descriptor, transform, variables, scales, external_context,
        external_vertex_count, drawing, budget, NULL, 0, 1);
}

static fe2_render_program_result nested(fe2_render_execution *e, uint16_t token,
    fe2_render_words operands, fe2_render_program_state *state,
    fe2_render_program_drawing *d, size_t budget)
{
    fe2_render_program_result result = {FE2_RENDER_PROGRAM_TRUNCATED, 0, 0, 0};
    fe2_render_execution_frame *parent = e->frames + e->depth - 1;
    fe2_render_model_transform transform;
    fe2_render_model model;
    fe2_render_bytes descriptor;
    fe2_vertex *origin;
    uint16_t mode, exponent, adjustment = 0;
    uint32_t material = d->inherited_material;
    unsigned opcode = token & 31u, i;
    size_t words = opcode == 27 ? 2 : 1;
    if (operands.count < words) return result;
    mode = operands.data[0];
    if (mode & 0x8000u) words += 2;
    if (operands.count < words) return result;
    result.status = FE2_RENDER_PROGRAM_BAD_VALUE;
    origin = refresh(d, signed_byte(mode & 255u), 0);
    if (!origin) return result;
    if (opcode == 27) {
        uint16_t control = operands.data[1], encoded = (uint16_t)(control << 1);
        uint32_t value = encoded & 0xfffu;
        if ((value & 0xf00u) == 0xe00u)
            value = (value & 0x10u) | ((value & 14u) << 15) | ((value & 0xe0u) << 14);
        value &= ~UINT32_C(0x101);
        if (value & 0x10u) value = (value & ~UINT32_C(0x10)) + material;
        material = value;
        if (!(control & 0x800u)) adjustment = (uint16_t)(signed_word(control) >> 12);
        else {
            int offset = signed_word(control & 0xf7ffu) >> 11;
            /* This encoding addresses a byte offset in the local word view. */
            if (offset < 0 || (offset & 1) || (unsigned)offset / 2 >= 64) return result;
            adjustment = state->local[(unsigned)offset / 2];
        }
    }
    if (!e->services.model || e->services.model(e->services.context, (token >> 4) & 0xffeu, 0, &descriptor) ||
        !descriptor.data || descriptor.count < 0x18 || fe2_render_model_open(descriptor, 0, &model)) return result;
    exponent = (uint16_t)(word(descriptor.data + 10) + word(descriptor.data + 12) + adjustment - parent->transform.scale);
    if (!fe2_render_model_visible(origin->view, word(descriptor.data + 14), exponent)) {
        result.status = FE2_RENDER_PROGRAM_COMPLETE; result.cursor = words;
        return result;
    }
    if (mode & 0x8000u) {
        uint16_t first = operands.data[opcode == 27 ? 2 : 1], second = operands.data[opcode == 27 ? 3 : 2];
        unsigned selectors[4] = {second & 255u, first & 255u, first >> 8, second >> 8};
        static const unsigned slots[4] = {3, 2, 1, 0};
        for (i = 0; i < 4; ++i) {
            fe2_vertex *v;
            if (i && selectors[i] == 127) break;
            v = refresh(d, signed_byte(selectors[i]), 0);
            if (!v) return result;
            e->attachments[slots[i]] = v;
        }
    }
    if (e->depth >= e->frame_capacity) { result.status = FE2_RENDER_PROGRAM_LIMIT; return result; }
    if (fe2_render_inherit_model_transform(&parent->transform, mode, origin->view, &transform)) return result;
    if ((mode & 0x4000u) && ((mode >> 8) & 7u) != 6)
        memcpy(transform.direction, e->frames[e->depth].transform.direction, sizeof(transform.direction));
    transform.exponent = exponent; transform.material = material;
    result = execute_model(e, descriptor, &transform, state->model, state->scale, NULL, 0, d,
        budget, origin, (mode & 0x4000u) != 0, 0);
    result.cursor = result.status == FE2_RENDER_PROGRAM_COMPLETE ? words : 0;
    return result;
}

static int classify(fe2_render_program_drawing *d, unsigned index, uint16_t *out)
{
    if (!d->face_parameters || index >= d->face_parameter_count) return -1;
    *out = d->face_parameters[index];
    if (*out == 0x8080u) {
        if (!d->face_frame) return -1;
        *out = fe2_math_classify_face(d->face_frame, (uint16_t)(index << 1));
        d->face_parameters[index] = *out;
    }
    return 0;
}

static int signed_key_less(uint32_t a, uint32_t b)
{
    return (a ^ UINT32_C(0x80000000)) < (b ^ UINT32_C(0x80000000));
}

static int32_t signed_dword(uint32_t value)
{
    return value < UINT32_C(0x80000000) ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}

static void execute_group(void *target, const void *payload)
{
    fe2_render_queue_execute((fe2_render_queue *)payload, target);
}

int fe2_render_execution_insert(fe2_render_program_drawing *d, fe2_render_command *node,
    uint32_t key, fe2_render_command_fn execute, const void *payload)
{
    if (d->execution && d->execution->active_group)
        return fe2_render_queue_insert(&d->execution->active_group->commands, node, 1, execute, payload);
    return fe2_render_queue_insert(fe2_render_execution_tree(d), node, key, execute, payload);
}

fe2_render_queue *fe2_render_execution_tree(fe2_render_program_drawing *d)
{
    return d->execution && d->execution->trees[0] ? d->execution->trees[0] : d->queue;
}

fe2_vertex *fe2_render_execution_place(fe2_render_program_drawing *d, int index)
{
    return refresh(d, index, 1);
}

fe2_vertex *fe2_render_execution_refresh(fe2_render_program_drawing *d, int index)
{
    return refresh(d, index, 0);
}

fe2_render_program_result fe2_render_execute_group_command(uint16_t token,
    fe2_render_bytes operands, fe2_render_program_drawing *d)
{
    fe2_render_program_result result = {FE2_RENDER_PROGRAM_BAD_VALUE, 0, 0, 0};
    fe2_render_execution *e;
    fe2_render_execution_frame *f;
    fe2_render_command_group *group;
    unsigned mode = token >> 5;
    uint32_t key;
    int32_t position[3] = {0, 0, 0};
    fe2_vertex *chosen;
    if (!d || !(e = d->execution) || !e->depth || !d->queue || d->queue->executing ||
        (operands.count && !operands.data)) return result;
    f = e->frames + e->depth - 1;
    e->active_group = NULL;
    result.status = FE2_RENDER_PROGRAM_COMPLETE;
    if (!mode) return result;
    if (mode == 0x7feu) key = UINT32_MAX;
    else if (mode == 0x7ffu) key = fe2_render_depth_key(f->vertices.offset, d->scale);
    else if (mode == 0x7fdu) {
        unsigned shift = f->transform.exponent & 63u;
        if (operands.count < 2) { result.status = FE2_RENDER_PROGRAM_TRUNCATED; return result; }
        position[2] = signed_dword(shift < 32 ? (uint32_t)(int32_t)signed_word(word(operands.data)) << shift : 0);
        result.cursor = 1;
        key = fe2_render_depth_key(position, d->scale);
    } else {
        chosen = refresh(d, signed_byte(mode & 255u), 1);
        if (!chosen) { result.status = FE2_RENDER_PROGRAM_BAD_VALUE; return result; }
        if (!(mode & 0x100u)) key = fe2_render_depth_key(chosen->view, d->scale);
        else if ((mode & 0x600u) == 0x600u) {
            uint32_t depth;
            unsigned shift = f->transform.exponent & 63u;
            if (operands.count < 2) { result.status = FE2_RENDER_PROGRAM_TRUNCATED; return result; }
            depth = shift < 32 ? (uint32_t)(int32_t)signed_word(word(operands.data)) << shift : 0;
            memcpy(position, chosen->view, sizeof(position));
            position[2] = signed_dword((uint32_t)position[2] + depth);
            result.cursor = 1;
            key = fe2_render_depth_key(position, d->scale);
        } else {
            uint32_t selected = fe2_render_depth_key(chosen->view, d->scale);
            uint16_t reference;
            do {
                fe2_vertex *candidate;
                uint32_t next;
                if (result.cursor >= operands.count / 2) { result.status = FE2_RENDER_PROGRAM_TRUNCATED; return result; }
                reference = word(operands.data + result.cursor++ * 2);
                candidate = refresh(d, signed_byte(reference & 255u), 1);
                if (!candidate) { result.status = FE2_RENDER_PROGRAM_BAD_VALUE; return result; }
                next = fe2_render_depth_key(candidate->view, d->scale);
                if ((mode & 0x200u) ? !signed_key_less(selected, next) : !signed_key_less(next, selected)) {
                    selected = next; chosen = candidate;
                }
            } while (reference & 0x8000u);
            key = fe2_render_depth_key(chosen->view, d->scale);
        }
    }
    if (e->group_count >= e->group_capacity) { result.status = FE2_RENDER_PROGRAM_OUTPUT_FULL; return result; }
    if (!e->groups) { result.status = FE2_RENDER_PROGRAM_BAD_VALUE; return result; }
    group = e->groups + e->group_count;
    fe2_render_queue_init(&group->commands);
    if (fe2_render_queue_insert(fe2_render_execution_tree(d), &group->node, key, execute_group, &group->commands)) {
        result.status = FE2_RENDER_PROGRAM_BAD_VALUE; return result;
    }
    ++e->group_count;
    e->active_group = group;
    return result;
}

static void rotate(int16_t basis[9], unsigned axis, int16_t cosine, int16_t sine)
{
    unsigned first = ((axis / 2u) + 1u) % 3u, second = (first + 1u) % 3u, i;
    for (i = 0; i < 3; ++i) {
        int16_t a = basis[first * 3 + i], b = basis[second * 3 + i];
        uint32_t u = (uint32_t)((int32_t)a * cosine) + (uint32_t)((int32_t)b * sine);
        uint32_t v = (uint32_t)((int32_t)b * cosine) - (uint32_t)((int32_t)a * sine);
        basis[first * 3 + i] = signed_word((uint16_t)((u * 2u) >> 16));
        basis[second * 3 + i] = signed_word((uint16_t)((v * 2u) >> 16));
    }
}

static fe2_render_program_status basis_control(fe2_render_execution_frame *f, uint16_t token,
    fe2_render_words operands, fe2_render_program_state *state)
{
    unsigned opcode = token & 31u, axis = (token >> 4) & 6u;
    int16_t cosine, sine;
    if (opcode == 25 && (token & 0x8000u)) {
        unsigned i;
        for (i = 0; i < 9; ++i) f->transform.selected_basis[i] = i % 4 == 0 ? 32767 : 0;
        return FE2_RENDER_PROGRAM_COMPLETE;
    }
    if (opcode == 28) {
        uint16_t phase = operands.data[0];
        if (!(token & 0x8000u)) {
            fe2_render_model_transform selected;
            uint16_t mode = (uint16_t)(((token >> 7) & 7u) << 8 | ((token << 1) & 0x3800u));
            if (fe2_render_inherit_model_transform(&f->transform, mode, f->transform.position, &selected))
                return FE2_RENDER_PROGRAM_BAD_VALUE;
            memcpy(f->transform.selected_basis, selected.basis, sizeof(selected.basis));
            f->transform.selected_parity = selected.parity;
            if (!phase) return FE2_RENDER_PROGRAM_COMPLETE;
        }
        if (axis == 6) return FE2_RENDER_PROGRAM_BAD_VALUE;
        if (!(phase & 0x80u)) phase = (uint16_t)((phase >> 7) | (phase << 9));
        else {
            unsigned offset = (uint16_t)(phase << 1);
            fe2_render_words values;
            if (offset & 0x80u) { values.data = state->local; values.count = 64; offset &= 0x7fu; }
            else { values = state->model; offset &= 0xfe7fu; }
            if (!values.data || offset / 2 >= values.count) return FE2_RENDER_PROGRAM_BAD_VALUE;
            phase = values.data[offset / 2];
        }
        cosine = fe2_math_cos(phase); sine = fe2_math_sin(phase);
    } else {
        static const unsigned components[3][2] = {{1, 2}, {2, 0}, {0, 1}};
        int16_t a, b, divisor;
        uint32_t square;
        uint16_t magnitude;
        int32_t u, v;
        if (axis == 6) return FE2_RENDER_PROGRAM_BAD_VALUE;
        a = f->context.view.direction[components[axis / 2][0]];
        b = f->context.view.direction[components[axis / 2][1]];
        square = (uint32_t)((int32_t)a * a) + (uint32_t)((int32_t)b * b);
        magnitude = (uint16_t)(((uint32_t)fe2_math_square_root32(square) * 0x8101u) >> 16);
        if (!magnitude) { cosine = fe2_math_sin((uint16_t)b); sine = fe2_math_cos((uint16_t)a); }
        else {
            divisor = signed_word(magnitude);
            u = ((int32_t)a * 16384) / divisor; v = ((int32_t)b * 16384) / divisor;
            cosine = signed_word(u < -32768 || u > 32767 ? (uint16_t)((int32_t)a * 16384) : (uint16_t)u);
            sine = signed_word(v < -32768 || v > 32767 ? (uint16_t)((int32_t)b * 16384) : (uint16_t)v);
        }
    }
    rotate(f->transform.selected_basis, axis, cosine, sine);
    return FE2_RENDER_PROGRAM_COMPLETE;
}

static fe2_render_program_status context_entries(fe2_render_execution_frame *f,
    fe2_render_program_state *state, fe2_render_program_drawing *d);

static fe2_render_program_status context_control(fe2_render_execution_frame *f, uint16_t token,
    fe2_render_program_state *state, fe2_render_program_drawing *d)
{
    uint16_t value;
    fe2_render_expression_sources sources;
    int64_t program, vertices;
    if (token & 0x8000u) {
        if ((uint16_t)(token | 15u) == 0x801fu) return context_entries(f, state, d);
        memcpy(f->transform.basis, f->transform.selected_basis, sizeof(f->transform.basis));
        memcpy(f->vertices.matrix, f->transform.basis, sizeof(f->vertices.matrix));
        return FE2_RENDER_PROGRAM_COMPLETE;
    }
    sources.model = state->model; sources.local.data = state->local; sources.local.count = 64; sources.scale = state->scale;
    if (fe2_render_expression_operand((uint8_t)(token >> 5), &sources, &value)) return FE2_RENDER_PROGRAM_BAD_VALUE;
    program = (int64_t)f->context_offset + signed_word((uint16_t)(value << 2));
    vertices = (int64_t)f->vertex_offset + signed_word((uint16_t)(value << 6)) / 32;
    if (program < 0 || (uint64_t)program > f->context.model.context.count || vertices < 0 ||
        (uint64_t)vertices >= f->context.model.vertex_count) return FE2_RENDER_PROGRAM_BAD_VALUE;
    f->context_offset = (size_t)program; f->vertex_offset = (size_t)vertices;
    f->vertices.program = f->context.model.context.data + f->context_offset;
    f->faces.program = f->vertices.program;
    f->vertices.vertices = f->context.vertices + f->vertex_offset;
    d->prefix_count = 5 + f->vertex_offset;
    d->vertex_count = f->context.model.vertex_count - f->vertex_offset;
    return FE2_RENDER_PROGRAM_COMPLETE;
}

static fe2_render_program_status context_entries(fe2_render_execution_frame *f,
    fe2_render_program_state *state, fe2_render_program_drawing *d)
{
    size_t count, i;
    if (!state->model.data || state->model.count < 5) return FE2_RENDER_PROGRAM_BAD_VALUE;
    count = (uint16_t)(state->model.data[4] - 1u);
    if (!count) count = 65536u;
    if (count > state->model.count - 5) return FE2_RENDER_PROGRAM_BAD_VALUE;
    state->local[3] = (uint16_t)count;
    state->local[1] = state->model.data[1];
    state->local[4] = 0;
    for (i = 0; i < count; ++i) {
        unsigned selector = (state->model.data[5 + i] >> 8) & 127u;
        size_t threshold = 0x54u + selector * 2u;
        uint16_t size, upper, lower;
        fe2_render_program_status status;
        const fe2_vertex *vertex;
        state->local[2] = (uint16_t)selector;
        if (threshold > fe2_render_context_entry_bytes || fe2_render_context_entry_bytes - threshold < 4)
            return FE2_RENDER_PROGRAM_BAD_VALUE;
        status = context_control(f, 0x3e, state, d);
        if (status != FE2_RENDER_PROGRAM_COMPLETE) return status;
        if (state->local[1] & 0x20u) {
            status = fe2_render_submit_model_line(d, word(fe2_render_context_entry_data + selector), 1);
            if (status != FE2_RENDER_PROGRAM_COMPLETE) return status;
        }
        size = word(fe2_render_context_entry_data + 0x38u + selector);
        size = (uint16_t)((size << 8) | (size >> 8));
        status = fe2_render_submit_round(word(fe2_render_context_entry_data + 0x1cu + selector), size, 0, state, d);
        if (status != FE2_RENDER_PROGRAM_COMPLETE) return status;
        vertex = f->vertices.vertices;
        lower = word(fe2_render_context_entry_data + threshold);
        upper = word(fe2_render_context_entry_data + threshold + 2);
        /* The recovered comparison uses signed halves, including the low
         * half when high halves match; preserve that ordering. */
        if (signed_word((uint16_t)((uint32_t)vertex->view[2] >> 16)) < signed_word(upper) ||
            (signed_word((uint16_t)((uint32_t)vertex->view[2] >> 16)) == signed_word(upper) &&
             signed_word((uint16_t)vertex->view[2]) <= signed_word(lower))) {
            status = fe2_render_submit_text(0x12, 0x8400, state, d);
            if (status != FE2_RENDER_PROGRAM_COMPLETE) return status;
        }
        ++state->local[4]; --state->local[3];
    }
    return FE2_RENDER_PROGRAM_COMPLETE;
}

static fe2_render_program_status parameter_control(fe2_render_execution_frame *f, uint16_t token,
    fe2_render_words operands, fe2_render_program_state *state, fe2_render_program_drawing *d)
{
    uint16_t mode = operands.data[0], index = (token >> 4) & 0xffeu, face;
    uint32_t value;
    unsigned inherit, no_base;
    if (mode >> 8) {
        fe2_render_expression_sources sources;
        uint16_t selector;
        sources.model = state->model; sources.local.data = state->local; sources.local.count = 64; sources.scale = state->scale;
        if (fe2_render_expression_operand((uint8_t)(mode >> 8), &sources, &selector)) return FE2_RENDER_PROGRAM_BAD_VALUE;
        if (selector & 7u) index = operands.data[selector & 7u];
    }
    value = index & 0xfffu;
    if ((value & 0xf00u) == 0xe00u)
        value = (value & 0x10u) | ((value & 14u) << 15) | ((value & 0xe0u) << 14);
    if (classify(d, mode & 127u, &face)) return FE2_RENDER_PROGRAM_BAD_VALUE;
    if (value >> 16) value |= 0x100u;
    inherit = value & 0x10u; no_base = value & 0x100u;
    value &= ~UINT32_C(0x110);
    if (!no_base) value = (value & UINT32_C(0xffff0000)) | (uint16_t)((uint16_t)value + (unsigned)face);
    if (inherit) value += d->inherited_material;
    if (!(mode & 0x80u)) { d->inherited_material = value; f->transform.material = value; }
    else {
        if (value >> 16) {
            uint16_t looked;
            if (fe2_render_material_word(value >> 12, &looked)) return FE2_RENDER_PROGRAM_BAD_VALUE;
            value = (uint16_t)((value & 0xfffu) + looked);
        }
        d->face_parameters[0] = (uint16_t)value;
    }
    return FE2_RENDER_PROGRAM_COMPLETE;
}

fe2_render_program_status fe2_render_model_threshold(uint16_t token, uint16_t threshold,
    fe2_render_program_drawing *d, int *skip)
{
    fe2_render_execution *e;
    fe2_render_execution_frame *f;
    uint16_t face, shifts;
    uint32_t value = threshold;
    unsigned inverse = (token & 31u) == 12;
    if (!d || !(e = d->execution) || !e->depth || !skip) return FE2_RENDER_PROGRAM_BAD_VALUE;
    f = e->frames + e->depth - 1;
    if (threshold & 0x8000u) {
        if (classify(d, threshold & 255u, &face)) return FE2_RENDER_PROGRAM_BAD_VALUE;
        *skip = inverse ? !(face & 0x8000u) : (face & 0x8000u) != 0;
        return FE2_RENDER_PROGRAM_COMPLETE;
    }
    if (f->vertices.offset[2] < 0) { *skip = (int)inverse; return FE2_RENDER_PROGRAM_COMPLETE; }
    shifts = (uint16_t)(e->detail_shift[threshold & 1u] + f->transform.exponent);
    if (signed_word(shifts) > 0) {
        while (shifts--) {
            uint32_t next = value << 1;
            if ((value ^ next) & UINT32_C(0x80000000)) { *skip = (int)inverse; return FE2_RENDER_PROGRAM_COMPLETE; }
            value = next;
        }
    }
    *skip = inverse ? value >= (uint32_t)f->vertices.offset[2] : value < (uint32_t)f->vertices.offset[2];
    return FE2_RENDER_PROGRAM_COMPLETE;
}

static fe2_render_program_result instance(fe2_render_execution *e, uint16_t token,
    fe2_render_words operands, fe2_render_program_state *state,
    fe2_render_program_drawing *d, size_t budget)
{
    fe2_render_program_result result = {FE2_RENDER_PROGRAM_BAD_VALUE, 0, 0, 0};
    fe2_render_execution_frame *parent = e->frames + e->depth - 1, *f;
    fe2_render_model_transform transform;
    fe2_render_model model;
    fe2_render_view view;
    fe2_render_program_drawing saved;
    fe2_render_bytes descriptor, text;
    fe2_vertex *origin;
    int32_t base_position[3];
    size_t cursor;
    uint16_t face, material, mode;
    unsigned i;
    if (operands.count < 3) { result.status = FE2_RENDER_PROGRAM_TRUNCATED; return result; }
    if (classify(d, operands.data[0] & 255u, &face)) return result;
    if (face & 0x8000u) { result.status = FE2_RENDER_PROGRAM_COMPLETE; result.cursor = 3; return result; }
    if (fe2_render_line_material(token, face, d->inherited_material, &material)) return result;
    mode = operands.data[1];
    origin = refresh(d, signed_byte(mode & 255u), 1);
    if (!origin || !e->services.model || !e->services.instance_text ||
        e->services.model(e->services.context, (operands.data[0] >> 11) & 0x1eu, 1, &descriptor) ||
        !descriptor.data || descriptor.count < 0x20 || fe2_render_model_open(descriptor, 0, &model)) return result;
    if (e->depth >= e->frame_capacity) { result.status = FE2_RENDER_PROGRAM_LIMIT; return result; }
    f = e->frames + e->depth;
    if (f->workspace.parameter_capacity < 0x84 || !f->workspace.parameters) return result;
    if (fe2_render_inherit_model_transform(&parent->transform, mode, origin->view, &transform)) return result;
    if ((mode & 0x4000u) && ((mode >> 8) & 7u) != 6)
        memcpy(transform.direction, f->transform.direction, sizeof(transform.direction));
    transform.material = material; transform.exponent = (operands.data[0] >> 8) & 15u;
    if (mode & 0x4000u) view = f->context.view;
    else fe2_render_prepare_view(transform.position, transform.direction, transform.basis, transform.exponent, &view);
    /* Instance face storage is a fixed 0x108-byte area. Only its first four
     * words are initialized: zero, zero, inherited face, inherited face. */
    model.parameter_bytes = 4;
    if (fe2_render_model_begin(&model, &view, (uint16_t)(view.shift + 8u), 0, &f->workspace, &f->context)) return result;
    f->context.parameter_count = 0x84;
    f->context.parameters[2] = f->context.parameters[3] = face;
    if (transform.parity & 0x8000u)
        for (i = 0; i < 3; ++i) transform.basis[i] = signed_word((uint16_t)(0u - (uint16_t)transform.basis[i]));
    if (e->services.instance_text(e->services.context, operands.data[2], &text) || !text.data || !text.count)
        return result;
    f->descriptor = descriptor; f->transform = transform;
    f->context_offset = f->vertex_offset = 0;
    memset(&f->state, 0, sizeof(f->state));
    f->state.model = state->model; f->state.scale = state->scale;
    memcpy(base_position, transform.position, sizeof(base_position));
    saved = *d;
    bind(e, f, d);
    ++e->depth;
    result.status = FE2_RENDER_PROGRAM_TRUNCATED;
    for (cursor = 0; cursor < text.count;) {
        unsigned character = text.data[cursor++];
        size_t end;
        uint16_t restore;
        f->faces.normals = text.data + cursor;
        if (!character) { result.status = FE2_RENDER_PROGRAM_COMPLETE; result.cursor = 3; break; }
        if (character < 32) {
            memcpy(f->vertices.offset, base_position, sizeof(base_position));
            ++f->vertices.stamp;
            origin = refresh(d, signed_byte(descriptor.data[0x1e]), 1);
            if (!origin) { result.status = FE2_RENDER_PROGRAM_BAD_VALUE; break; }
            memcpy(base_position, origin->view, sizeof(base_position));
            memcpy(f->vertices.offset, base_position, sizeof(base_position));
            memcpy(f->transform.position, base_position, sizeof(base_position));
            ++f->vertices.stamp;
            continue;
        }
        end = 0x20u + (character - 32u) * 2u;
        if (end > descriptor.count || descriptor.count - end < 4) { result.status = FE2_RENDER_PROGRAM_BAD_VALUE; break; }
        if (fe2_render_model_visible(f->vertices.offset, word(descriptor.data + 14), transform.exponent)) {
            size_t entry = word(descriptor.data + end);
            fe2_render_program_result child;
            if (entry > descriptor.count || descriptor.count - entry < 2) { result.status = FE2_RENDER_PROGRAM_BAD_VALUE; break; }
            child = fe2_render_program_execute_drawing(descriptor.data + entry, descriptor.count - entry,
                &f->state, d, budget - result.commands);
            result.commands += child.commands; result.divide_resumes += child.divide_resumes;
            if (child.status != FE2_RENDER_PROGRAM_COMPLETE) { result.status = child.status; break; }
            end = entry + child.cursor * 2;
        } else end = word(descriptor.data + end + 2);
        if (end < 2 || end > descriptor.count) { result.status = FE2_RENDER_PROGRAM_BAD_VALUE; break; }
        restore = word(descriptor.data + end - 2);
        if (restore) {
            origin = refresh(d, restore >> 6, 1);
            if (!origin) { result.status = FE2_RENDER_PROGRAM_BAD_VALUE; break; }
            memcpy(f->vertices.offset, origin->view, sizeof(f->vertices.offset));
            memcpy(f->transform.position, origin->view, sizeof(f->transform.position));
            ++f->vertices.stamp;
        }
    }
    --e->depth;
    restore_drawing(d, &saved);
    return result;
}

fe2_render_program_result fe2_render_execute_model_command(uint16_t token,
    fe2_render_words operands, fe2_render_program_state *state,
    fe2_render_program_drawing *drawing, size_t budget)
{
    fe2_render_program_result bad = {FE2_RENDER_PROGRAM_BAD_VALUE, 0, 0, 0};
    fe2_render_execution *e;
    if (!drawing || !state || !(e = drawing->execution) || !e->depth ||
        e->depth > e->frame_capacity || !e->frames || (operands.count && !operands.data)) return bad;
    if ((token & 31u) == 14 || (token & 31u) == 27) return nested(e, token, operands, state, drawing, budget);
    if ((token & 31u) == 10) return instance(e, token, operands, state, drawing, budget);
    if ((token & 31u) == 1 || (token & 31u) == 18) {
        size_t count = (token & 31u) == 1 ? 2 : 1;
        if (operands.count < count) { bad.status = FE2_RENDER_PROGRAM_TRUNCATED; return bad; }
        bad.status = count == 2 ? fe2_render_submit_round(token, operands.data[0], operands.data[1], state, drawing) :
            fe2_render_submit_text(token, operands.data[0], state, drawing);
        if (bad.status == FE2_RENDER_PROGRAM_COMPLETE) bad.cursor = count;
        return bad;
    }
    if ((token & 31u) == 21) {
        bad.status = fe2_render_execute_batch_command(token, drawing);
        return bad;
    }
    if ((token & 31u) == 15) {
        const fe2_render_execution_frame *f = e->frames + e->depth - 1;
        uint32_t distance = 0;
        unsigned i;
        for (i = 0; i < 3; ++i) {
            uint32_t value = (uint32_t)f->vertices.offset[i];
            distance += f->vertices.offset[i] < 0 ? 0u - value : value;
        }
        distance >>= 17;
        bad.status = FE2_RENDER_PROGRAM_COMPLETE;
        if (distance < 63) {
            if (!e->services.sound) bad.status = FE2_RENDER_PROGRAM_UNSUPPORTED;
            else e->services.sound(e->services.context, token >> 5, (uint16_t)(63u - distance));
        }
        return bad;
    }
    if ((token & 31u) == 25 || (token & 31u) == 28 || (token & 31u) == 30 || (token & 31u) == 26) {
        unsigned opcode = token & 31u;
        size_t count = opcode == 25 || opcode == 30 ? 0 : 1;
        fe2_render_execution_frame *f = e->frames + e->depth - 1;
        if (operands.count < count) { bad.status = FE2_RENDER_PROGRAM_TRUNCATED; return bad; }
        if (opcode == 26 && operands.data[0] >> 8) count = 8;
        if (operands.count < count) { bad.status = FE2_RENDER_PROGRAM_TRUNCATED; return bad; }
        bad.status = opcode == 30 ? context_control(f, token, state, drawing) : opcode == 26 ?
            parameter_control(f, token, operands, state, drawing) : basis_control(f, token, operands, state);
        if (bad.status == FE2_RENDER_PROGRAM_COMPLETE) bad.cursor = count;
        return bad;
    }
    bad.status = FE2_RENDER_PROGRAM_UNSUPPORTED;
    return bad;
}
