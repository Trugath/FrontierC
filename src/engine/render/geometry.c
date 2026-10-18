/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "geometry.h"

static uint32_t magnitude(int32_t value)
{
    uint32_t u = (uint32_t)value;
    return value < 0 ? 0u - u : u;
}

static uint32_t shift(uint32_t value, unsigned count)
{
    return count < 32 ? value >> count : 0;
}

uint32_t fe2_render_depth_key(const int32_t camera[3], int16_t scale)
{
    unsigned reduction = (3u - (uint16_t)scale) & 65535u;
    uint32_t key;
    if (!reduction || reduction >= 32768u) {
        reduction = (reduction + 24u) & 65535u;
        if (camera[2] >= 0)
            return shift((uint32_t)camera[2], reduction & 63u) | UINT32_C(0x80000000);
    }
    key = magnitude(camera[0]) + magnitude(camera[1]);
    key = (key >> 3) + (uint32_t)camera[2];
    if (key & UINT32_C(0x80000000)) return 1;
    key = shift(key, reduction);
    return key ? key : 1;
}

int fe2_render_clip_quad(const fe2_render_vertex v[4], uint8_t color, fe2_render_polygon *out)
{
    fe2_render_polygon result = {0};
    unsigned start = 0, attempts, i;
    if (!v || !out) return -1;
    /* The exchange chain rotates the next vertex to the front. */
    for (attempts = 0; attempts < 4; ++attempts) {
        if (v[start].camera[2] >= FE2_NEAR_DEPTH) break;
        start = (start + 1) % 4;
    }
    if (attempts == 4) return 0;
    result.color = color;
    result.points[result.count++] = v[start].screen;
    for (i = 0; i < 4; ++i) {
        unsigned a = (start + i) % 4, b = (a + 1) % 4;
        int visible_a = v[a].camera[2] >= FE2_NEAR_DEPTH;
        int visible_b = v[b].camera[2] >= FE2_NEAR_DEPTH;
        if (visible_a != visible_b) {
            unsigned near_index = visible_a ? b : a;
            unsigned far_index = visible_a ? a : b;
            result.points[result.count++] = fe2_render_project_near(v[near_index].camera, v[far_index].camera);
        }
        if (visible_b && b != start) result.points[result.count++] = v[b].screen;
    }
    *out = result;
    return 1;
}

int fe2_render_polygon_draw(const fe2_render_polygon *p, fe2_render_span_fn draw, void *context)
{
    fe2_render_scanlines packet;
    unsigned i;
    if (!p || p->count < 3 || p->count > FE2_RENDER_POLYGON_POINTS || !draw) return -1;
    fe2_render_scanlines_init(&packet);
    for (i = 0; i < p->count; ++i)
        if (fe2_render_scanlines_edge(&packet, p->points[i], p->points[(i + 1) % p->count])) return -1;
    return fe2_render_scanlines_draw(&packet, p->color, draw, context);
}

void fe2_render_polygon_to_surface(void *context, const void *polygon)
{
    (void)fe2_render_polygon_draw(polygon, fe2_render_surface_span, context);
}
