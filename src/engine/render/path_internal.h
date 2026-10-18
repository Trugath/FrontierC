/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_PATH_INTERNAL_H
#define FE2_RENDER_PATH_INTERNAL_H

/* Shared helpers of the path producers (path.c, value.c, tube.c). */
#include "path.h"

static inline int fe2_path_sb(unsigned value)
{
    value &= 255u;
    return value < 128u ? (int)value : (int)value - 256;
}
static inline int16_t fe2_path_sw(uint32_t value)
{
    uint16_t bits = (uint16_t)value;
    return (int16_t)(bits < 32768u ? (int)bits : (int)bits - 65536);
}
static inline uint16_t fe2_path_word(fe2_render_bytes b, size_t index)
{
    return (uint16_t)((unsigned)b.data[index * 2] | (unsigned)b.data[index * 2 + 1] << 8);
}
/* High word of a 32-bit product after SHL AX,1 / RCL DX,1 (bits 15..30). */
static inline int16_t fe2_path_high1(int32_t product) { return fe2_path_sw((uint32_t)product >> 15); }
/* High word of a product (DX after IMUL). */
static inline int16_t fe2_path_high(int32_t product) { return fe2_path_sw((uint32_t)product >> 16); }
/* Arithmetic halving of a word (SAR r,1). */
static inline int16_t fe2_path_half(int16_t value)
{
    return fe2_path_sw((uint32_t)(uint16_t)value >> 1 | ((uint16_t)value & 0x8000u));
}

/* Vertex references: availability check and stale-stamp refresh. */
int fe2_path_resolve(fe2_render_program_drawing *d, int index);
void fe2_path_current(const fe2_render_program_drawing *d, int index, fe2_render_vertex *out);
int32_t fe2_path_depth(const fe2_render_program_drawing *d, int index);
fe2_screen_point fe2_path_screen(const fe2_render_program_drawing *d, int index);
/* with the behind record first. */
fe2_screen_point fe2_path_near(const fe2_render_program_drawing *d, int behind, int visible);

int fe2_path_bank(uint32_t value, uint16_t *out);
/* Face cache word, classifying an 0x8080 entry once. */
int fe2_path_classify(fe2_render_program_drawing *d, uint16_t index, uint16_t *face);
int fe2_path_quotient(int32_t dividend, int16_t divisor, int16_t *out);

/* An open path: elements appended to the caller-owned pool from start. */
typedef struct fe2_path_builder {
    fe2_render_path_output *out;
    size_t start;
    int have_point;
    fe2_screen_point point;
} fe2_path_builder;

void fe2_path_begin(fe2_path_builder *b, fe2_render_path_output *out);
void fe2_path_discard(fe2_path_builder *b);
fe2_render_program_status fe2_path_edge(fe2_path_builder *b, fe2_screen_point first, fe2_screen_point second);
/* a segment from the preceding record's final point. */
fe2_render_program_status fe2_path_line_to(fe2_path_builder *b, fe2_screen_point point);
fe2_render_program_status fe2_path_curve(fe2_path_builder *b, fe2_screen_point p0, fe2_screen_point p1,
    fe2_screen_point p2, fe2_screen_point p3);
/* current point only. */
void fe2_path_move(fe2_path_builder *b, fe2_screen_point point);
/* fill and restart. */
fe2_render_program_status fe2_path_fill(fe2_path_builder *b, uint16_t material);
/* Queue the elements since begin with the final fill material. */
fe2_render_program_status fe2_path_commit(fe2_render_program_drawing *d, fe2_path_builder *b,
    uint32_t key, uint16_t material);
void fe2_path_execute_round(void *target, const void *payload);

/* six projections around centre: A = c+u, B = c+u+w, C = c+u-w,
 * D = c-u, E = c-u+w, F = c-u-w (offsets negated as words). Returns 0 when
 * any projection is rejected (depth below 64). */
int fe2_path_ellipse_points(const int32_t centre[3], const int16_t u[3], const int16_t w[3],
    fe2_screen_point points[6]);
/* the degenerate (face-on) radius vectors. */
void fe2_path_round_vectors(uint16_t radius, int16_t u[3], int16_t w[3]);

fe2_render_program_result fe2_path_value(uint16_t token, fe2_render_bytes ops,
    fe2_render_program_state *state, fe2_render_program_drawing *d);
fe2_render_program_result fe2_path_tube(uint16_t token, fe2_render_bytes ops,
    fe2_render_program_state *state, fe2_render_program_drawing *d);

#endif
