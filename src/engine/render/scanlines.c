/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "pipeline.h"
#include <string.h>

/* Explicit word wrap/signed decoding; no implementation-defined conversion. */
static int word(int value)
{
    unsigned u = (unsigned)value & 65535u;
    return u < 32768u ? (int)u : (int)u - 65536;
}

void fe2_render_scanlines_init(fe2_render_scanlines *p)
{
    memset(p, 0, sizeof(*p));
    p->min_y = FE2_VIEWPORT_HEIGHT;
    p->direct = 1;
}

int fe2_render_scanlines_edge(fe2_render_scanlines *p, fe2_screen_point a, fe2_screen_point b)
{
    fe2_screen_point t;
    int y, height, width, direction, x, error, step = 0, increment = 0;
    if (!p) return -1;
    /* Match the collector's endpoint swap and vertical rejection order. */
    if ((uint16_t)b.y >= 169u) {
        if ((uint16_t)a.y >= 169u && ((a.y < 0) == (b.y < 0))) return 0;
        t = a; a = b; b = t;
    }
    if ((uint16_t)a.y >= 169u) a = fe2_render_clip_polygon(a, b);
    if ((uint16_t)b.y >= 169u) {
        t = a; a = b; b = t;
        a = fe2_render_clip_polygon(a, b);
    }
    if (a.y > b.y) { t = a; a = b; b = t; }
    height = b.y - a.y;
    width = (int)b.x - a.x;
    if (width < -32767 || width > 32767) return -1;
    /* Endpoints may sit on row 168 (the exclusive bottom). Rows stored are
     * 0..167; a clipped end of 169 must not index the panel row. */
    for (y = a.y; y < b.y; ++y) {
        if ((unsigned)y >= FE2_VIEWPORT_HEIGHT) continue;
        if (p->count[y] >= FE2_RENDER_INTERSECTIONS) return -1;
    }
    if ((uint16_t)a.x >= 320u || (uint16_t)b.x >= 320u) p->direct = 0;
    if (b.y > p->max_y) p->max_y = b.y > FE2_VIEWPORT_HEIGHT ? FE2_VIEWPORT_HEIGHT : b.y;
    if (a.y < p->min_y) p->min_y = a.y < 0 ? 0 : a.y;
    if (!height) return 0;
    direction = width < 0 ? -1 : 1;
    if (width < 0) width = -width;
    x = a.x;
    if (height > width) {
        error = width - height / 2;
        if (error > 0) { error -= height; x = word(x + direction); }
        for (y = a.y; y < b.y; ++y) {
            if ((unsigned)y < FE2_VIEWPORT_HEIGHT)
                p->x[y][p->count[y]++] = (int16_t)x;
            error = word(error + width);
            if (error > 0) { error = word(error - height); x = word(x + direction); }
        }
    } else {
        error = -(width / 2);
        if (height < width / 2) {
            int quotient = width / height;
            x = word(x - direction * (quotient / 2));
            step = quotient - 1;
            increment = height * step;
            error = -(width / 2 + increment / 2);
        }
        for (y = a.y; y < b.y; ++y) {
            error = word(error + increment);
            x = word(x + direction * step);
            do { x = word(x + direction); error = word(error + height); } while (error <= 0);
            error = word(error - width);
            if ((unsigned)y < FE2_VIEWPORT_HEIGHT)
                p->x[y][p->count[y]++] = (int16_t)x;
        }
    }
    return 0;
}
