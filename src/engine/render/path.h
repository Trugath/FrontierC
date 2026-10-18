/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_PATH_H
#define FE2_RENDER_PATH_H

/*
 * Model geometry producers that build scanline-filled paths and cubic
 * strokes: value-token paths (opcode 5, 0x6773..0x6d9f with the transformed-
 * vertex ellipse 0x6da0..0x6f90/0xc092..0xc331), vertex-pair lenses (opcode 9,
 * 0x7205..0x757f) and four-vertex cubic strokes (opcode 22, 0x6f92..0x7204).
 *
 * The original appended 0x2aac/0x2ab7/0x2ad2/0x2aed/0x2b25 records to one queued
 * node. Natively a path is an ordered list of typed edges; a 0x2ad2 "line to"
 * record is resolved to an explicit segment from the previous record's final
 * point. Materials are the resolved RGB12/material words before palette
 * mapping; the backend owns scanline collection, cubic flattening and fill.
 */
#include "execution.h"

typedef enum fe2_render_path_kind {
    FE2_RENDER_PATH_SEGMENT, /* points[0] -> points[1] */
    FE2_RENDER_PATH_CURVE,   /* cubic points[0..3] */
    FE2_RENDER_PATH_FILL     /* fill the edges since the previous fill with material, then restart */
} fe2_render_path_kind;

typedef struct fe2_render_path_element {
    fe2_render_path_kind kind;
    fe2_screen_point points[4];
    uint16_t material;
} fe2_render_path_element;

/* A filled path. The final fill (material) closes the elements; FILL
 * elements inside the list fill earlier parts with their own material. */
typedef struct fe2_render_path_command {
    uint32_t key;
    uint16_t material;
    const fe2_render_path_element *elements;
    size_t count;
} fe2_render_path_command;

/* Opcode 22: a cubic stroke through four projected control points. */
typedef struct fe2_render_curve_command {
    uint32_t key;
    fe2_screen_point points[4];
    uint16_t material;
} fe2_render_curve_command;

/* Caller-owned output storage, bound through fe2_render_execution.paths.
 * Counts are allocation cursors; reset them with the queue for a new frame.
 * Elements of committed paths must stay alive until the queue is drawn. */
typedef struct fe2_render_path_output {
    fe2_render_command *nodes;
    fe2_render_path_command *paths;
    size_t capacity, count;
    fe2_render_path_element *elements;
    size_t element_capacity, element_count;
    fe2_render_command *curve_nodes;
    fe2_render_curve_command *curves;
    size_t curve_capacity, curve_count;
} fe2_render_path_output;

/* Geometry opcodes 5, 9, 16, 17 and 22. operands are the resource bytes after
 * the command word; cursor reports the operand words consumed. Requires a
 * bound execution (frame transform, normals and face cache) with path output.
 * Payloads execute through fe2_render_execution_target's path/curve/round
 * callbacks. Vertex refreshes happen in the original order and survive
 * errors; a failing command commits no partial path. */
fe2_render_program_result fe2_render_execute_geometry_command(uint16_t token,
    fe2_render_bytes operands, fe2_render_program_state *state,
    fe2_render_program_drawing *drawing);

#endif
