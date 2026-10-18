/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "pipeline.h"
#include <string.h>

int fe2_render_scanlines_draw(const fe2_render_scanlines *p, uint8_t color,
                              fe2_render_span_fn draw, void *context)
{
    int y;
    unsigned i, j;
    if (!p || !draw || p->min_y < 0 || p->max_y > FE2_VIEWPORT_HEIGHT ||
        p->max_y < 0 || p->min_y > FE2_VIEWPORT_HEIGHT) return -1;
    for (y = 0; y < FE2_VIEWPORT_HEIGHT; ++y) {
        unsigned n = p->count[y];
        if (n > FE2_RENDER_INTERSECTIONS || (n & 1u)) return -1;
        if (n && (y < p->min_y || y >= p->max_y)) return -1;
        if (p->direct)
            for (i = 0; i < n; ++i)
                if ((uint16_t)p->x[y][i] >= FE2_VIEWPORT_WIDTH) return -1;
    }
    for (y = p->max_y - 1; y >= p->min_y; --y) {
        int16_t sorted[FE2_RENDER_INTERSECTIONS];
        unsigned n = p->count[y];
        for (i = 0; i < n; ++i) {
            int16_t x = p->x[y][i];
            j = i;
            while (j && sorted[j - 1] > x) { sorted[j] = sorted[j - 1]; --j; }
            sorted[j] = x;
        }
        for (i = 0; i < n; i += 2) {
            int left = sorted[i], right = sorted[i + 1];
            if (!p->direct) {
                if (left >= FE2_VIEWPORT_WIDTH || right < 0) continue;
                if (left < 0) left = 0;
                if (right > FE2_VIEWPORT_WIDTH) right = FE2_VIEWPORT_WIDTH;
            }
            draw(context, (unsigned)y, (unsigned)left, (unsigned)(right - left), color);
        }
    }
    return 0;
}

void fe2_render_surface_span(void *context, unsigned y, unsigned x, unsigned count, uint8_t color)
{
    fe2_render_surface *s = context;
    size_t length = count ? count : 1;
    if (!s || !s->pixels || s->stride < s->width || y >= s->height || x >= s->width) return;
    if (length > s->width - x) length = s->width - x;
    memset(s->pixels + (size_t)y * s->stride + x, color, length);
}
