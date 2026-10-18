/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* camera points onto the viewport. */
#include "render.h"

enum {
    CENTRE_X = FE2_VIEWPORT_WIDTH / 2, CENTRE_Y = FE2_VIEWPORT_HEIGHT / 2,
    DIVISOR_LIMIT = 0x8000,            /* the depth divides as a positive word */
    PACK_SHIFT = 8,                    /* coordinates divide as 24.8 */
    NEAR_BOUND = 0x1f00                /* projected near points stay inside +-0x1f00 */
};

/* Arithmetic halving of 32-bit values (as SAR does). */
static int32_t half(int32_t v) { return (int32_t)(((uint32_t)v >> 1) | ((uint32_t)v & 0x80000000u)); }
static int16_t half16(int16_t v) { return (int16_t)(uint16_t)(((uint16_t)v >> 1) | ((uint16_t)v & 0x8000u)); }

/* A signed 32 / 16 division; 0 when the quotient is not a word (the
 * divide fault). */
static int divide(uint32_t dividend, uint16_t divisor, int16_t *quotient)
{
    int64_t q = (int64_t)(int32_t)dividend / divisor;
    if (q < INT16_MIN || q > INT16_MAX) return 0;
    *quotient = (int16_t)q;
    return 1;
}

int fe2_render_project(int32_t x, int32_t y, int32_t depth, fe2_screen_point *point)
{
    uint32_t d = (uint32_t)depth, dividend;
    int16_t q, sx;
    if (depth < FE2_NEAR_DEPTH) return 0;
    while (d >= DIVISOR_LIMIT) {
        d >>= 1;
        x = half(x);
        y = half(y);
    }
    dividend = (uint32_t)x << PACK_SHIFT;
    while (!divide(dividend, (uint16_t)d, &q)) {
        dividend = (uint32_t)half((int32_t)dividend);
        y = half(y);
    }
    sx = (int16_t)(uint16_t)((uint16_t)q + CENTRE_X);
    dividend = (uint32_t)y << PACK_SHIFT;
    while (!divide(dividend, (uint16_t)d, &q)) {
        dividend = (uint32_t)half((int32_t)dividend);
        sx = half16(sx);
    }
    point->x = sx;
    point->y = (int16_t)(uint16_t)(CENTRE_Y - (uint16_t)q);
    return 1;
}

/* Doubling with the overflow flag of SHL. */
static int doubles(uint32_t v) { return ((v ^ (v << 1)) & 0x80000000u) == 0; }

static int inside(int32_t v) { return v > -NEAR_BOUND && v < NEAR_BOUND; }

fe2_screen_point fe2_render_project_near(const int32_t near[3], const int32_t far[3])
{
    uint32_t x = (uint32_t)near[0], y = (uint32_t)near[1], z = (uint32_t)near[2];
    uint32_t dx = (uint32_t)far[0] - x, dy = (uint32_t)far[1] - y, dz = (uint32_t)far[2] - z;
    int32_t px, py;
    int forward;
    fe2_screen_point p;
    /* Halving steps: the first toward the far point, then toward it while
     * at or behind the plane and back while in front, until the depth step
     * (halved logically) is zero. */
    for (forward = 1;; forward = (int32_t)z <= FE2_NEAR_DEPTH) {
        dx = (uint32_t)half((int32_t)dx);
        dy = (uint32_t)half((int32_t)dy);
        dz >>= 1;
        if (dz == 0) break;
        if (forward) {
            x += dx;
            y += dy;
            z += dz;
        } else {
            x -= dx;
            y -= dy;
            z -= dz;
        }
    }
    /* Times four when it fits: x alone, else both. */
    px = (int32_t)x;
    py = (int32_t)y;
    if (doubles(x) && doubles(x << 1)) {
        if (doubles(y) && doubles(y << 1)) {
            px = (int32_t)(x << 2);
            py = (int32_t)(y << 2);
        }
    }
    while (!inside(px)) {
        px = half(px);
        py = half(py);
    }
    while (!inside(py)) {
        px = half(px);
        py = half(py);
    }
    p.x = (int16_t)(uint16_t)((uint16_t)px + CENTRE_X);
    p.y = (int16_t)(uint16_t)(CENTRE_Y - (uint16_t)py);
    return p;
}
