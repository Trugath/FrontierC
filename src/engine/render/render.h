/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_H
#define FE2_RENDER_H

/*
 * The renderer's projection and viewport arithmetic: camera points to the
 * 320 x 168 viewport (centre 160, 84; screen y grows downward), the
 * near-plane crossing of a segment, the viewport outcodes and endpoint
 * clipping (24.8 fixed point with a half-pixel bias), and the arccosine
 * lookup of the vertex-pair geometry.
 *
 * Screen coordinates are words, as the original keeps them; out-of-range
 * results wrap.
 */
#include <stdint.h>

enum { FE2_VIEWPORT_WIDTH = 320, FE2_VIEWPORT_HEIGHT = 168, FE2_NEAR_DEPTH = 0x40 };

typedef struct fe2_screen_point {
    int16_t x, y;
} fe2_screen_point;

/* ---- projection.c -------------------------------------------------------- */
/* (x, y) at `depth` on the viewport. Depths below 64 (or negative)
 * are rejected (returns 0; the original returns past its caller). A
 * quotient overflow halves the dividend and retries: the horizontal one
 * also halves y, the vertical one the projected x. */
int fe2_render_project(int32_t x, int32_t y, int32_t depth, fe2_screen_point *point);
/* the segment from `near` (at or behind depth 64) to `far`
 * (visible) refined to depth 64 by halving steps, then x and y scaled by
 * up to four and halved into (-0x1f00, 0x1f00), on the viewport. */
fe2_screen_point fe2_render_project_near(const int32_t near[3], const int32_t far[3]);

/* ---- clip.c -------------------------------------------------------------- */
/* `codes` with one x bit (negative 1, inside 2, right 4) and one y
 * bit (negative 8, inside 0x10, below 0x20). */
uint16_t fe2_render_outcode(int16_t x, int16_t y, uint16_t codes);
/* `first` moved toward `second` onto the viewport: x when it is
 * outside (as an unsigned word), then y. No rejection: the caller tests the
 * result, which can be outside for a segment that misses the viewport. */
fe2_screen_point fe2_render_clip_line(fe2_screen_point first, fe2_screen_point second);
/* y only, for a polygon edge, accepting row 168. */
fe2_screen_point fe2_render_clip_polygon(fe2_screen_point first, fe2_screen_point second);

/* ---- angle.c ------------------------------------------------------------- */
/* the doubled arccosine of a quotient's top seven bits (a signed
 * Q6 bin). */
uint16_t fe2_render_acos(uint16_t quotient);

/* Arccosine samples: floor(acos(q/64) * 32767 / pi), then the negative bins. */
extern const uint16_t fe2_render_acos_table[128];

#endif
