/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_POINTS_H
#define FE2_RENDER_POINTS_H

/*
 * Opcode 24: variable-length solid-disc point lists (0x8f60..0x91cc, the
 * original's 0x2f74 command). The token selects an RGB12/material word, one
 * operand word a disc size, and packed signed-byte vertex pairs follow until
 * a 0x7f selector. Points with depth >= 0x40 whose decremented screen
 * coordinates lie in 0..318 x 0..166 are kept (the original stores X-1, Y-1).
 * Materials are passed unmapped; the backend owns palette lookup and the
 * solid-disc primitive  for each point.
 */
#include "execution.h"

typedef struct fe2_render_point_command {
    uint32_t key;
    uint16_t size, material;
    const fe2_screen_point *points; /* already decremented, as stored */
    size_t count;
} fe2_render_point_command;

/* Caller-owned storage bound through fe2_render_execution.points. Counts are
 * allocation cursors; reset them with the queue for a new frame. */
typedef struct fe2_render_point_output {
    fe2_render_command *nodes;
    fe2_render_point_command *commands;
    size_t capacity, count;
    fe2_screen_point *points;
    size_t point_capacity, point_count;
} fe2_render_point_output;

/* Token material selector (no base word, bits 0 and 8 ignored, bit 4 adds the
 * inherited dword, E-class selectors address the material table). Returns -1
 * for table reads outside the native material resource. */
int fe2_render_point_material(uint16_t token, uint32_t inherited, uint16_t *out);

/* Disc size from an already loaded size word: bit 15 set gives a fixed
 * magnitude; otherwise the word is shifted by exponent-7 and divided by the
 * frame origin depth after an 8-bit numerator shift. Returns 1 with *size,
 * 0 when depth <= 0 discards the list, 2 when the quotient overflowed and the
 * original divide-fault resume produced (numerator low byte) << 8. */
int fe2_render_point_size(uint16_t value, uint16_t exponent, int32_t depth, uint16_t *size);

/* Execute opcode 24. operands are the resource bytes after the command word;
 * cursor reports the operand words consumed (size word through terminator).
 * Requires a bound execution with point output. Vertex refreshes happen in
 * the original order (high selector, then low) and survive errors. */
fe2_render_program_result fe2_render_submit_points(uint16_t token, fe2_render_bytes operands,
    fe2_render_program_state *state, fe2_render_program_drawing *drawing);

#endif
