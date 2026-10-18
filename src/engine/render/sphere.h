/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_SPHERE_H
#define FE2_RENDER_SPHERE_H

/*
 * Opcode 31: shaded spheres (planets and stars) with surface features,
 * terminators and ring layers (0x93d5..0xa030, 0xa031..0xb113 and the palette
 * helpers 0xb114..0xb206).
 *
 * The original appended one 0x2b52 record stream to a queued node: 0x2b80
 * control-point records (the outline's two cubic halves), 0x2b5d edges,
 * 0x2bb8 row toggles, then 0x2c96 (or 0x2bc9) and the palette slot words.
 * Natively the stream is an ordered list of typed elements and a colour
 * block. Ring layers in front of the sphere are ordinary filled paths
 * (path.h) queued with key FFFFFFFFh before the sphere. Colours are RGB12
 * words before palette mapping; the backend owns the scanline fill.
 */
#include "path.h"

typedef enum fe2_render_sphere_kind {
    FE2_RENDER_SPHERE_CONTROL, /* points[0..3] */
    FE2_RENDER_SPHERE_EDGE,    /* points[0] -> points[1], value = colour index */
    FE2_RENDER_SPHERE_ROW      /* row points[0].y toggles value's colour mask */
} fe2_render_sphere_kind;

typedef struct fe2_render_sphere_element {
    fe2_render_sphere_kind kind;
    fe2_screen_point points[4];
    uint16_t value;
} fe2_render_sphere_element;

/* color_count is 8  or 16 (0x2bc9, the second eight shaded). */
typedef struct fe2_render_sphere_command {
    uint32_t key;
    const fe2_render_sphere_element *elements;
    size_t count;
    uint16_t colors[16];
    unsigned color_count;
} fe2_render_sphere_command;

/* Caller-owned storage bound through fe2_render_execution.spheres. Counts
 * are allocation cursors (reset with the queue). Rings at or below layer_threshold are not drawn. A shaded atmosphere sets rgb (the
 * current drawing colour channels) and rgb_set. */
typedef struct fe2_render_sphere_output {
    fe2_render_command *nodes;
    fe2_render_sphere_command *commands;
    size_t capacity, count;
    fe2_render_sphere_element *elements;
    size_t element_capacity, element_count;
    int16_t layer_threshold;
    uint8_t rgb[3];
    int rgb_set;
} fe2_render_sphere_output;

/* Opcode 31. operands are the resource bytes after the command word. The
 * sphere's detail is execution->detail_shift[1]. Above 4 skips
 * the sphere; the low-detail presets are negative and still draw. */
fe2_render_program_result fe2_render_execute_sphere_command(uint16_t token,
    fe2_render_bytes operands, fe2_render_program_state *state,
    fe2_render_program_drawing *drawing);

#endif
