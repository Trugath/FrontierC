/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "pipeline.h"

static int inside(fe2_screen_point p)
{
    return (uint16_t)p.x < FE2_VIEWPORT_WIDTH && (uint16_t)p.y < FE2_VIEWPORT_HEIGHT;
}

int fe2_render_draw_line(fe2_screen_point a, fe2_screen_point b, uint16_t color,
                          fe2_render_line_fn draw, void *context)
{
    fe2_screen_point t;
    if (!draw) return 0;
    if (!inside(a)) {
        a = fe2_render_clip_line(a, b);
        if (!inside(a)) return 0;
    }
    if (!inside(b)) {
        t = a; a = b; b = t;
        a = fe2_render_clip_line(a, b);
        if (!inside(a)) return 0;
    }
    draw(context, a, b, color);
    return 1;
}
