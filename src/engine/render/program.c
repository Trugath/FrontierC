/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "program.h"
#include "execution.h"
#include "path.h"
#include "points.h"
#include "texture.h"
#include "math/math3.h"

int fe2_render_context_test(uint16_t operand, const fe2_render_expression_sources *sources, int *matches)
{
    uint16_t value;
    unsigned mode = operand >> 8;
    unsigned bit = (mode - 1u) & 255u;
    if (!matches || fe2_render_expression_operand((uint8_t)operand, sources, &value)) return -1;
    if (bit >= 128u) *matches = value != 0;
    else {
        unsigned count = bit & 31u;
        *matches = count < 16 && (value & (1u << count)) != 0;
    }
    return 0;
}

static uint16_t program_word(fe2_render_words words, const uint8_t *encoded, size_t index)
{
    if (!encoded) return words.data[index];
    return (uint16_t)((unsigned)encoded[index * 2] | (unsigned)encoded[index * 2 + 1] << 8);
}

static int vertex_available(const fe2_render_program_drawing *drawing, int index)
{
    const fe2_vertex_frame *frame = drawing->vertex_frame;
    if (frame && index < 0 && (size_t)-index <= drawing->prefix_count && frame->vertices &&
        frame->vertices[index].state == 0 && ((drawing->execution && index < -5) ||
        !frame->inherited || !frame->inherited[index])) return 0;
    return (drawing->vertex_frame ? drawing->vertex_frame->vertices != NULL : drawing->vertices != NULL) &&
           (index < 0 ? (size_t)-index <= drawing->prefix_count :
                                 (size_t)index < drawing->vertex_count);
}

static void cached_vertex(const fe2_vertex *source, fe2_render_vertex *out)
{
    unsigned i;
    for (i = 0; i < 3; ++i) out->camera[i] = source->view[i];
    out->screen.x = source->screen[0];
    out->screen.y = source->screen[1];
}

static void refresh_vertex(fe2_render_program_drawing *drawing, int index)
{
    const fe2_vertex_frame *frame = drawing->vertex_frame;
    if (frame && (uint8_t)frame->vertices[index].state != frame->stamp) fe2_math_vertex_refresh(frame, index);
}

static void read_vertex(fe2_render_program_drawing *drawing, int index, fe2_render_vertex *out)
{
    if (drawing->vertex_frame) cached_vertex(drawing->vertex_frame->vertices + index, out);
    else *out = drawing->vertices[index];
}

fe2_render_program_status fe2_render_submit_model_line(fe2_render_program_drawing *drawing, uint16_t token, uint16_t pair)
{
    int low = pair & 255u, high = pair >> 8;
    fe2_render_segment segment;
    fe2_render_vertex first, second;
    int status;
    if (low >= 128) low -= 256;
    if (high >= 128) high -= 256;
    if (!drawing->queue || drawing->queue->executing || !vertex_available(drawing, low) ||
        !vertex_available(drawing, high)) return FE2_RENDER_PROGRAM_BAD_VALUE;
    if (drawing->vertex_frame) {
        refresh_vertex(drawing, low);
        refresh_vertex(drawing, high);
        read_vertex(drawing, low, &first);
        read_vertex(drawing, high, &second);
    } else {
        first = drawing->vertices[low];
        second = drawing->vertices[high];
    }
    status = fe2_render_build_segment(&first, &second, token,
        drawing->face_base, drawing->inherited_material, drawing->scale, &segment);
    if (status < 0) return FE2_RENDER_PROGRAM_BAD_VALUE;
    if (!status) return FE2_RENDER_PROGRAM_COMPLETE;
    if (drawing->count >= drawing->capacity) return FE2_RENDER_PROGRAM_OUTPUT_FULL;
    if (!drawing->nodes || !drawing->segments) return FE2_RENDER_PROGRAM_BAD_VALUE;
    if (fe2_render_execution_insert(drawing, drawing->nodes + drawing->count, segment.key,
        fe2_render_segment_execute, drawing->segments + drawing->count)) return FE2_RENDER_PROGRAM_BAD_VALUE;
    drawing->segments[drawing->count++] = segment;
    return FE2_RENDER_PROGRAM_COMPLETE;
}

static int signed_byte(unsigned value)
{
    return value < 128u ? (int)value : (int)value - 256;
}

static fe2_render_program_status distance_filter(fe2_render_program_drawing *d,
                                                   uint16_t threshold, uint16_t pair, int *skip)
{
    int first = signed_byte(pair & 255u), second = signed_byte(pair >> 8);
    fe2_render_vertex a, b;
    int32_t x, y;
    uint16_t distance;
    *skip = 0;
    if (!vertex_available(d, first)) return FE2_RENDER_PROGRAM_BAD_VALUE;
    refresh_vertex(d, first);
    read_vertex(d, first, &a);
    if (a.camera[2] < 64) return FE2_RENDER_PROGRAM_COMPLETE;
    if (!vertex_available(d, second)) return FE2_RENDER_PROGRAM_BAD_VALUE;
    refresh_vertex(d, second);
    read_vertex(d, second, &b);
    if (b.camera[2] < 64) return FE2_RENDER_PROGRAM_COMPLETE;
    /* The second refresh may replace the first projected record. Its depth
     * was checked before that refresh; its coordinates are read afterwards. */
    read_vertex(d, first, &a);
    x = (int32_t)a.screen.x - b.screen.x;
    y = (int32_t)a.screen.y - b.screen.y;
    distance = (uint16_t)((x < 0 ? -x : x) + (y < 0 ? -y : y));
    *skip = threshold & 0x8000u ? distance > (threshold & 0x7fffu) : distance <= threshold;
    return FE2_RENDER_PROGRAM_COMPLETE;
}

static fe2_render_program_status surface_command(fe2_render_program_drawing *d, uint16_t token,
                                                   uint16_t first, uint16_t second, uint16_t face_index,
                                                   unsigned count)
{
    fe2_render_vertex records[4], sampled;
    fe2_screen_point cached[4];
    fe2_render_surface_command command;
    int indices[4];
    unsigned i;
    uint16_t face;
    int status;
    if (!d->face_parameters || face_index >= d->face_parameter_count || !d->queue || d->queue->executing)
        return FE2_RENDER_PROGRAM_BAD_VALUE;
    face = d->face_parameters[face_index];
    if (face == 0x8080u) {
        if (!d->face_frame) return FE2_RENDER_PROGRAM_BAD_VALUE;
        face = fe2_math_classify_face(d->face_frame, (uint16_t)(face_index << 1));
        d->face_parameters[face_index] = face;
    }
    if (face & 0x8000u) return FE2_RENDER_PROGRAM_COMPLETE;
    indices[1] = signed_byte(first & 255u);
    if (count == 3) {
        indices[0] = signed_byte(first >> 8);
        indices[2] = signed_byte(second >> 8);
    } else {
        indices[0] = signed_byte(second & 255u);
        indices[2] = signed_byte(second >> 8);
        indices[3] = signed_byte(first >> 8);
    }
    for (i = 0; i < count; ++i) if (!vertex_available(d, indices[i])) return FE2_RENDER_PROGRAM_BAD_VALUE;
    /* Captured projected coordinates and final record contents are distinct
     * inputs in the original producer. Preserve its per-reference timing. */
    for (i = 1; i <= count; ++i) {
        unsigned slot = i % count;
        unsigned screen_slot = count == 4 && slot >= 2 ? 5 - slot : slot;
        refresh_vertex(d, indices[slot]);
        read_vertex(d, indices[slot], &sampled);
        cached[screen_slot] = sampled.screen;
    }
    for (i = 0; i < count; ++i) read_vertex(d, indices[i], records + i);
    if (d->textured_surfaces) {
        fe2_render_texture_command texture;
        fe2_render_texture_output *o = d->textures;
        status = fe2_render_build_texture(cached, count, token, face, d->inherited_material,
            records[0].camera, d->scale, &texture);
        if (status < 0) return FE2_RENDER_PROGRAM_BAD_VALUE;
        if (status == FE2_RENDER_TEXTURE_REJECTED) return FE2_RENDER_PROGRAM_COMPLETE;
        if (status == FE2_RENDER_TEXTURE_EMITTED) {
            if (!o || !o->nodes || !o->commands) return FE2_RENDER_PROGRAM_BAD_VALUE;
            if (o->count >= o->capacity) return FE2_RENDER_PROGRAM_OUTPUT_FULL;
            o->commands[o->count] = texture;
            if (fe2_render_execution_insert(d, o->nodes + o->count, texture.key,
                fe2_render_texture_execute, o->commands + o->count)) return FE2_RENDER_PROGRAM_BAD_VALUE;
            ++o->count;
            return FE2_RENDER_PROGRAM_COMPLETE;
        }
    }
    status = fe2_render_build_surface(records, cached, count, token, face, d->inherited_material, d->scale, &command);
    if (status < 0) return FE2_RENDER_PROGRAM_BAD_VALUE;
    if (!status) return FE2_RENDER_PROGRAM_COMPLETE;
    if (d->surface_count >= d->surface_capacity) return FE2_RENDER_PROGRAM_OUTPUT_FULL;
    if (!d->surface_nodes || !d->surfaces) return FE2_RENDER_PROGRAM_BAD_VALUE;
    if (fe2_render_execution_insert(d, d->surface_nodes + d->surface_count, command.key,
        fe2_render_surface_execute, d->surfaces + d->surface_count)) return FE2_RENDER_PROGRAM_BAD_VALUE;
    d->surfaces[d->surface_count++] = command;
    return FE2_RENDER_PROGRAM_COMPLETE;
}

static fe2_render_program_result execute(fe2_render_words program, const uint8_t *encoded,
                                          fe2_render_program_state *state,
                                          fe2_render_program_drawing *drawing, size_t budget)
{
    fe2_render_program_result result = {FE2_RENDER_PROGRAM_TRUNCATED, 0, 0, 0};
    fe2_render_expression_sources sources;
    if (!state || (!program.data && !encoded && program.count)) {
        result.status = FE2_RENDER_PROGRAM_BAD_VALUE;
        return result;
    }
    sources.model = state->model;
    sources.scale = state->scale;
    sources.local.data = state->local;
    sources.local.count = 64;
    for (;;) {
        size_t start = result.cursor;
        uint16_t token, operand;
        unsigned command;
        int model_command;
        if (result.commands == budget) { result.status = FE2_RENDER_PROGRAM_LIMIT; return result; }
        if (start >= program.count) return result;
        token = program_word(program, encoded, start);
        command = token & 31u;
        model_command = drawing && drawing->execution && (command == 1 || command == 18 || command == 10 || command == 14 || command == 15 || command == 27 ||
            command == 25 || command == 26 || command == 28 || command == 30 || command == 21);
        if (!command) {
            ++result.cursor;
            ++result.commands;
            result.status = FE2_RENDER_PROGRAM_COMPLETE;
            return result;
        }
        if (command != 13 && command != 29 && command != 19 && command != 20 &&
            !(drawing && drawing->execution && (command == 5 || command == 9 || command == 16 || command == 17 || command == 22 || command == 31)) &&
            !(drawing && (command == 2 || command == 3 || command == 4 || command == 7 || command == 8 || command == 23 ||
               model_command || ((command == 6 || command == 11 || command == 12 || command == 24) && drawing->execution)))) {
            result.status = FE2_RENDER_PROGRAM_UNSUPPORTED;
            return result;
        }
        if (drawing && drawing->execution && (command == 5 || command == 9 || command == 16 || command == 17 || command == 22 || command == 31)) {
            /* Variable-length geometry producers (path.c). */
            fe2_render_bytes operands = {encoded + (start + 1) * 2, (program.count - start - 1) * 2};
            fe2_render_program_result geometry = fe2_render_execute_geometry_command(token, operands, state, drawing);
            result.divide_resumes += geometry.divide_resumes;
            if (geometry.status != FE2_RENDER_PROGRAM_COMPLETE) { result.status = geometry.status; return result; }
            result.cursor += 1 + geometry.cursor;
            ++result.commands;
            continue;
        }
        if (!model_command && command != 6 && program.count - start < 2) return result;
        operand = program.count - start >= 2 ? program_word(program, encoded, start + 1) : 0;
        if (command == 6 || command == 24) {
            fe2_render_bytes operands = {encoded + (start + 1) * 2, (program.count - start - 1) * 2};
            fe2_render_program_result group = command == 6 ? fe2_render_execute_group_command(token, operands, drawing) :
                fe2_render_submit_points(token, operands, state, drawing);
            result.divide_resumes += group.divide_resumes;
            if (group.status != FE2_RENDER_PROGRAM_COMPLETE) { result.status = group.status; return result; }
            result.cursor += 1 + group.cursor;
        } else if (model_command) {
            uint16_t values[8];
            size_t count = command == 15 || command == 21 || command == 25 || command == 30 ? 0 : command == 10 ? 3 :
                command == 27 || command == 1 ? 2 : 1, i;
            fe2_render_words operands;
            fe2_render_program_result child;
            if ((command == 14 || command == 27) && (operand & 0x8000u)) count += 2;
            if (command == 26 && operand >> 8) count = 8;
            if (program.count - start - 1 < count) return result;
            for (i = 0; i < count; ++i) values[i] = program_word(program, encoded, start + 1 + i);
            operands.data = values; operands.count = count;
            child = fe2_render_execute_model_command(token, operands, state, drawing, budget - result.commands - 1);
            result.commands += child.commands;
            result.divide_resumes += child.divide_resumes;
            if (child.status != FE2_RENDER_PROGRAM_COMPLETE) { result.status = child.status; return result; }
            result.cursor += 1 + child.cursor;
        } else if (command == 11 || command == 12) {
            int matches;
            size_t skip;
            result.status = fe2_render_model_threshold(token, operand, drawing, &matches);
            if (result.status != FE2_RENDER_PROGRAM_COMPLETE) return result;
            result.status = FE2_RENDER_PROGRAM_TRUNCATED;
            skip = matches ? token >> 5 : 0;
            if (matches && !skip) {
                result.cursor += 2; ++result.commands;
                result.status = FE2_RENDER_PROGRAM_COMPLETE;
                return result;
            }
            if (skip > program.count - start - 2) return result;
            result.cursor += 2 + skip;
        } else if (command == 23) {
            int matches;
            size_t skip;
            if (program.count - start < 3) return result;
            result.status = distance_filter(drawing, operand, program_word(program, encoded, start + 2), &matches);
            if (result.status != FE2_RENDER_PROGRAM_COMPLETE) return result;
            result.status = FE2_RENDER_PROGRAM_TRUNCATED;
            skip = matches ? token >> 5 : 0;
            if (matches && !skip) {
                result.cursor += 3;
                ++result.commands;
                result.status = FE2_RENDER_PROGRAM_COMPLETE;
                return result;
            }
            if (skip > program.count - start - 3) return result;
            result.cursor += 3 + skip;
        } else if (command == 3 || command == 4 || command == 7 || command == 8) {
            unsigned count = command == 3 || command == 7 ? 3 : 4;
            size_t words = count == 3 ? 3 : 4;
            uint16_t second, face;
            if (program.count - start < words) return result;
            second = program_word(program, encoded, start + 2);
            face = count == 3 ? second & 255u : program_word(program, encoded, start + 3) & 32767u;
            if (command == 7 || command == 8) {
                result.status = surface_command(drawing, token, operand ^ 0x0101u,
                    second ^ 0x0101u, face ^ 1u, count);
                if (result.status != FE2_RENDER_PROGRAM_COMPLETE) return result;
            }
            result.status = surface_command(drawing, token, operand, second, face, count);
            if (result.status != FE2_RENDER_PROGRAM_COMPLETE) return result;
            result.status = FE2_RENDER_PROGRAM_TRUNCATED;
            result.cursor += words;
        } else if (command == 2) {
            result.status = fe2_render_submit_model_line(drawing, token, operand);
            if (result.status != FE2_RENDER_PROGRAM_COMPLETE) return result;
            result.status = FE2_RENDER_PROGRAM_TRUNCATED;
            result.cursor += 2;
        } else if (command == 13 || command == 29) {
            int status = fe2_render_expression_evaluate(token, operand, &sources, state->local);
            if (status < 0) { result.status = FE2_RENDER_PROGRAM_BAD_VALUE; return result; }
            result.divide_resumes += status == 1;
            result.cursor += 2;
        } else {
            int matches;
            size_t skip = 0;
            if (fe2_render_context_test(operand, &sources, &matches)) {
                result.status = FE2_RENDER_PROGRAM_BAD_VALUE;
                return result;
            }
            if ((command == 19 && !matches) || (command == 20 && matches)) {
                skip = (token >> 5); /* even byte count / 2 */
                if (!skip) {
                    result.cursor += 2;
                    ++result.commands;
                    result.status = FE2_RENDER_PROGRAM_COMPLETE;
                    return result;
                }
            }
            if (skip > program.count - start - 2) return result;
            result.cursor += 2 + skip;
        }
        ++result.commands;
    }
}

fe2_render_program_result fe2_render_program_execute(fe2_render_words program,
                                                      fe2_render_program_state *state, size_t budget)
{
    return execute(program, NULL, state, NULL, budget);
}

fe2_render_program_result fe2_render_program_execute_encoded(const uint8_t *data, size_t bytes,
                                                              fe2_render_program_state *state, size_t budget)
{
    fe2_render_words words = {NULL, bytes / 2};
    if (!data && bytes) {
        fe2_render_program_result bad = {FE2_RENDER_PROGRAM_BAD_VALUE, 0, 0, 0};
        return bad;
    }
    return execute(words, data, state, NULL, budget);
}

fe2_render_program_result fe2_render_program_execute_drawing(const uint8_t *data, size_t bytes,
                                                              fe2_render_program_state *state,
                                                              fe2_render_program_drawing *drawing, size_t budget)
{
    fe2_render_words words = {NULL, bytes / 2};
    if ((!data && bytes) || !drawing) {
        fe2_render_program_result bad = {FE2_RENDER_PROGRAM_BAD_VALUE, 0, 0, 0};
        return bad;
    }
    return execute(words, data, state, drawing, budget);
}
