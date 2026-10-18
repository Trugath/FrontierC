/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "backend.h"

/* Main frame callbacks for round payloads: 0x2ca5, 0x2cc8, 0x2d13 and the
 * shaded-curve chain 0x2aac/0x2aed/0x2a62/0x2b25, plus the disc list 0x2f74. */

static int contour(const fe2_render_backend *b, const fe2_screen_point curves[2][4], uint16_t material)
{
    fe2_render_scanlines packet;
    unsigned i;
    fe2_render_scanlines_init(&packet);      /* math small context */
    for (i = 0; i < 2; ++i)
        if (fe2_render_cubic_edges(&packet, curves[i][0], &curves[i][1])) return -1;
    return fe2_render_scanlines_draw(&packet, b->color(b->context, material), b->span, b->context);
}

int fe2_render_draw_round(const fe2_render_round_command *c, const fe2_render_backend *b)
{
    unsigned selector;
    if (!c || !b || !b->color || !b->span) return -1;
    switch (c->kind) {
    case FE2_RENDER_DISC:
        return fe2_render_draw_disc_checked(c->center.x, c->center.y, (uint16_t)c->size,
            b->color(b->context, c->material[0]), b->span, b->context) < 0 ? -1 : 0;
    case FE2_RENDER_SMALL_DISC:
        selector = (uint16_t)c->size;
        if (selector < 1 || selector > 9) return -1;
        if (fe2_render_draw_disc_checked(c->center.x, c->center.y, fe2_render_paired_disc_sizes[0][selector - 1],
                b->color(b->context, c->material[1]), b->span, b->context) < 0) return -1;
        return fe2_render_draw_disc_checked(c->center.x, c->center.y, fe2_render_paired_disc_sizes[1][selector - 1],
            b->color(b->context, c->material[0]), b->span, b->context) < 0 ? -1 : 0;
    case FE2_RENDER_MARKER: {
        int16_t selector16 = (int16_t)(uint16_t)((unsigned)(uint16_t)c->size << 5);
        if (fe2_render_draw_shape_rows(c->center.x, c->center.y, selector16,
                b->color(b->context, c->material[1]), b->span, b->context)) return -1;
        return fe2_render_draw_shape_rows(c->center.x, c->center.y, (int16_t)(uint16_t)(selector16 + 16),
            b->color(b->context, c->material[0]), b->span, b->context);
    }
    case FE2_RENDER_SHADED_CURVE:
        if (contour(b, (const fe2_screen_point (*)[4])c->curves, c->material[0])) return -1;
        return contour(b, (const fe2_screen_point (*)[4])(c->curves + 2), c->material[1]);
    }
    return -1;
}

int fe2_render_draw_disc_list(uint16_t material, uint16_t size, const fe2_screen_point *points,
                              size_t count, const fe2_render_backend *b)
{
    uint8_t color;
    size_t i;
    if (!b || !b->color || !b->span || (count && !points)) return -1;
    color = b->color(b->context, material);
    for (i = 0; i < count; ++i)
        if (fe2_render_draw_disc(points[i].x, points[i].y, size, color, b->span, b->context) < 0) return -1;
    return 0;
}

void fe2_render_backend_round(void *backend, const fe2_render_round_command *command)
{
    (void)fe2_render_draw_round(command, backend);
}
