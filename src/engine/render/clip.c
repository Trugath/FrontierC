/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* viewport outcodes and endpoint clipping. */
#include "render.h"

enum {
    LEFT = 1, INSIDE_X = 2, RIGHT = 4, ABOVE = 8, INSIDE_Y = 0x10, BELOW = 0x20,
    POLYGON_ROWS = FE2_VIEWPORT_HEIGHT + 1,
    FIXED = 8,                         /* 24.8 */
    HALF_PIXEL = 0x80,
    QUARTER_PIXEL = 0x40,              /* refinement stops below a quarter pixel step */
    /* The far limits in 24.8: a quarter pixel inside the right and bottom
     * edges (reached or passed), a polygon's three quarters past row 168. */
    RIGHT_LIMIT = (FE2_VIEWPORT_WIDTH << FIXED) - QUARTER_PIXEL,
    BOTTOM_LIMIT = (FE2_VIEWPORT_HEIGHT << FIXED) - QUARTER_PIXEL,
    POLYGON_LIMIT = (FE2_VIEWPORT_HEIGHT << FIXED) + 0xc0
};

uint16_t fe2_render_outcode(int16_t x, int16_t y, uint16_t codes)
{
    codes |= x < 0 ? LEFT : x >= FE2_VIEWPORT_WIDTH ? RIGHT : INSIDE_X;
    codes |= y < 0 ? ABOVE : y >= FE2_VIEWPORT_HEIGHT ? BELOW : INSIDE_Y;
    return codes;
}

static int32_t half(int32_t v) { return (int32_t)(((uint32_t)v >> 1) | ((uint32_t)v & 0x80000000u)); }

typedef enum { REACH_RIGHT, REACH_BOTTOM, PASS_POLYGON_BOTTOM } far_limit;

/* Is the active coordinate still short of the boundary? Toward the far
 * edge (a negative difference) by the limit; toward 0 while below a
 * quarter pixel. */
static int short_of(int32_t active, int descending, far_limit limit)
{
    if (!descending) return active < QUARTER_PIXEL;
    switch (limit) {
    case REACH_RIGHT: return active >= RIGHT_LIMIT;
    case REACH_BOTTOM: return active >= BOTTOM_LIMIT;
    default: return active > POLYGON_LIMIT;
    }
}

/*
 * Binary refinement of the first endpoint along the active axis: the
 * active step (a magnitude, halved logically) moves toward the boundary,
 * the other axis's signed step (halved arithmetically) follows; once past
 * it, steps go back, until the active step is below a quarter pixel. The
 * direction is the difference's sign, whichever side the points are on.
 */
static void refine(int32_t *active, int32_t *other, int32_t difference, int32_t other_difference, far_limit limit)
{
    int descending = difference < 0;
    uint32_t step = descending ? 0u - (uint32_t)difference : (uint32_t)difference;
    int32_t follow = other_difference;
    int32_t sign = descending ? -1 : 1;
    for (;;) {
        follow = half(follow);
        step >>= 1;
        *other += follow;
        *active = (int32_t)((uint32_t)*active + (uint32_t)sign * step);
        if (step < QUARTER_PIXEL) return;
        while (!short_of(*active, descending, limit)) {
            follow = half(follow);
            step >>= 1;
            *other -= follow;
            *active = (int32_t)((uint32_t)*active - (uint32_t)sign * step);
            if (step < QUARTER_PIXEL) return;
        }
    }
}

static int32_t fixed(int16_t v) { return (int32_t)((uint32_t)(int32_t)v << FIXED) | HALF_PIXEL; }
static int32_t fixed_difference(int16_t to, int16_t from) { return (int32_t)((uint32_t)((int32_t)to - from) << FIXED); }
static int16_t whole(int32_t v) { return (int16_t)(uint16_t)((uint32_t)v >> FIXED); }

static fe2_screen_point clip_x(fe2_screen_point a, fe2_screen_point b)
{
    int32_t x = fixed(a.x), y = fixed(a.y);
    refine(&x, &y, fixed_difference(b.x, a.x), fixed_difference(b.y, a.y), REACH_RIGHT);
    a.x = whole(x);
    a.y = whole(y);
    return a;
}

static fe2_screen_point clip_y(fe2_screen_point a, fe2_screen_point b, far_limit limit)
{
    int32_t x = fixed(a.x), y = fixed(a.y);
    refine(&y, &x, fixed_difference(b.y, a.y), fixed_difference(b.x, a.x), limit);
    a.x = whole(x);
    a.y = whole(y);
    return a;
}

fe2_screen_point fe2_render_clip_line(fe2_screen_point a, fe2_screen_point b)
{
    if ((uint16_t)a.x >= FE2_VIEWPORT_WIDTH) a = clip_x(a, b);
    if ((uint16_t)a.y >= FE2_VIEWPORT_HEIGHT) a = clip_y(a, b, REACH_BOTTOM);
    return a;
}

fe2_screen_point fe2_render_clip_polygon(fe2_screen_point a, fe2_screen_point b)
{
    if ((uint16_t)a.y >= POLYGON_ROWS) a = clip_y(a, b, PASS_POLYGON_BOTTOM);
    return a;
}
