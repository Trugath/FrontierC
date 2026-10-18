/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "backend.h"

static int word(uint32_t bits)
{
    unsigned value = bits & 65535u;
    return value < 32768u ? (int)value : (int)value - 65536;
}
static int32_t signed32(uint32_t bits)
{
    return bits < UINT32_C(0x80000000) ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
}
static uint32_t half(uint32_t bits)
{
    return bits >> 1 | (bits & UINT32_C(0x80000000));
}
static void line_span(fe2_render_span_fn span, void *context, int y, int x, int count, uint8_t color)
{
    if (count < 0) return;
    if (x < 0) { count = word((uint32_t)(count + x)); if (count <= 0) return; x = 0; }
    if (x >= 320) return;
    if (count > 320 - x) count = 320 - x;
    span(context, (uint16_t)y, (unsigned)x, (unsigned)count, color);
}
static void clipped_span(fe2_render_span_fn span, void *context, int y, int x, int count, uint8_t color)
{
    if ((unsigned)y < 168u) line_span(span, context, y, x, count, color);
}

int fe2_render_solid_line_unchecked(fe2_screen_point a, fe2_screen_point b, uint8_t color,
    fe2_render_span_fn span, void *context)
{
    int dx = (int)b.x - a.x, dy = (int)b.y - a.y, sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int x = a.x, y = a.y, error, run = 0, i;
    if (!span || dx < -32767 || dx > 32767) return -1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dy <= dx) {
        error = -(dx / 2);
        for (i = 0; i < dx; ++i) {
            ++run;
            if (sx < 0) --x;
            error = word((uint32_t)(error + dy));
            if (error > 0) {
                error = word((uint32_t)(error - dx));
                line_span(span, context, y, x, run, color);
                if (sx > 0) x += run;
                y += sy; run = 0;
            } else if (i + 1 == dx) line_span(span, context, y, x, run, color);
        }
    } else {
        error = -(dy / 2);
        for (i = 0; i < dy; ++i) {
            run = 0; error += dx;
            if (error > 0) { error -= dy; run = 1; if (sx < 0) --x; }
            line_span(span, context, y, x, run, color);
            if (sx > 0) x += run;
            y += sy;
        }
    }
    return 0;
}

int fe2_render_solid_line(fe2_screen_point a, fe2_screen_point b, uint8_t color,
    fe2_render_span_fn span, void *context)
{
    if ((uint16_t)a.y >= 168u || (uint16_t)b.y >= 168u) return -1;
    return fe2_render_solid_line_unchecked(a, b, color, span, context);
}

typedef struct solid_edge { uint32_t x, step; } solid_edge;
static solid_edge edge(fe2_screen_point from, fe2_screen_point to)
{
    solid_edge result;
    int dx = word((uint32_t)(to.x - from.x)), dy = word((uint32_t)(to.y - from.y));
    int64_t quotient;
    if (!dy) {
        result.step = (uint32_t)(uint16_t)dx << 16 | (dx < 0 ? 65535u : 0u);
        result.x = 0;
        return result;
    }
    quotient = (int64_t)dx * 256 / dy;
    if (quotient < -32768 || quotient > 32767) {
        int32_t dividend = dx * 64;
        quotient = dividend / dy;
        if (quotient < -32768 || quotient > 32767) quotient = word((uint32_t)dividend);
        /* The byte shuffle after recovery discards bits 16..23 of the
         * expanded quotient, retaining its sign in the upper byte. */
        quotient *= 4;
        result.step = ((uint32_t)quotient & 65535u) << 8;
        if (quotient < 0) result.step |= UINT32_C(0xff000000);
    } else result.step = (uint32_t)(int32_t)quotient << 8;
    result.x = ((uint32_t)(uint16_t)from.x << 16) + 0x8000u - half(result.step);
    return result;
}

/* Keep each geometric edge's biased accumulator when the other is replaced.
 * Original phases sort initial slopes, then compare current positions. */
static void rows(solid_edge *a, solid_edge *b, int *y, int length, int initial, int changed,
    uint8_t color, int clipped, fe2_render_span_fn span, void *context)
{
    int a_left = initial ? signed32(b->step) > signed32(a->step) : signed32(b->x) > signed32(a->x);
    int i;
    if (initial || changed == 0) a->x += a_left ? 0u - 0x8000u : 0x8000u;
    if (initial || changed == 1) b->x += a_left ? 0x8000u : 0u - 0x8000u;
    for (i = 0; i < length; ++i, ++*y) {
        int left, right;
        a->x += a->step; b->x += b->step;
        left = word((a_left ? a->x : b->x) >> 16);
        right = word((a_left ? b->x : a->x) >> 16);
        if (clipped) clipped_span(span, context, *y, left, word((uint32_t)(right - left)), color);
        else {
            int count = word((uint32_t)(right - left));
            if (count >= 0) span(context, (uint16_t)*y, (uint16_t)left, (unsigned)count, color);
        }
    }
}

int fe2_render_solid_polygon(const fe2_screen_point *points, unsigned count, uint8_t color,
    int clipped, fe2_render_span_fn span, void *context)
{
    fe2_screen_point p[4], swap;
    solid_edge a, b;
    unsigned i, top = 0, inside = 0;
    int y;
    if (!points || !span || (count != 3 && count != 4)) return -1;
    for (i = 0; i < count; ++i) {
        p[i] = points[i];
        if (!clipped && (uint16_t)p[i].y >= 168u) return -1;
        if ((uint16_t)p[i].x < 320u && (uint16_t)p[i].y < 168u) ++inside;
        if (p[i].y < p[top].y) top = i;
    }
    if (clipped && !inside) return 0;
    if (inside == count) clipped = 0;
    if (count == 3) {
        if (p[0].y > p[1].y) { swap = p[0]; p[0] = p[1]; p[1] = swap; }
        if (p[0].y > p[2].y) { swap = p[0]; p[0] = p[2]; p[2] = swap; }
        if (p[1].y > p[2].y) { swap = p[1]; p[1] = p[2]; p[2] = swap; }
        y = p[0].y; a = edge(p[0], p[1]); b = edge(p[0], p[2]);
        rows(&a, &b, &y, p[1].y - p[0].y, 1, 0, color, clipped, span, context);
        a = edge(p[1], p[2]);
        rows(&a, &b, &y, p[2].y - p[1].y, 0, 0, color, clipped, span, context);
    } else {
        for (i = 0; i < 4; ++i) p[i] = points[(top + i) % 4];
        y = p[0].y; a = edge(p[0], p[1]); b = edge(p[0], p[3]);
        if (p[3].y <= p[1].y) {
            rows(&a, &b, &y, p[3].y - p[0].y, 1, 0, color, clipped, span, context);
            b = edge(p[3], p[2]);
            if (p[2].y <= p[1].y) {
                rows(&a, &b, &y, p[2].y - p[3].y, 0, 1, color, clipped, span, context);
                b = edge(p[2], p[1]);
                rows(&a, &b, &y, p[1].y - p[2].y, 0, 1, color, clipped, span, context);
            } else {
                rows(&a, &b, &y, p[1].y - p[3].y, 0, 1, color, clipped, span, context);
                a = edge(p[1], p[2]);
                rows(&a, &b, &y, p[2].y - p[1].y, 0, 0, color, clipped, span, context);
            }
        } else {
            rows(&a, &b, &y, p[1].y - p[0].y, 1, 0, color, clipped, span, context);
            a = edge(p[1], p[2]);
            if (p[2].y <= p[3].y) {
                rows(&a, &b, &y, p[2].y - p[1].y, 0, 0, color, clipped, span, context);
                a = edge(p[2], p[3]);
                rows(&a, &b, &y, p[3].y - p[2].y, 0, 0, color, clipped, span, context);
            } else {
                rows(&a, &b, &y, p[3].y - p[1].y, 0, 0, color, clipped, span, context);
                b = edge(p[3], p[2]);
                rows(&a, &b, &y, p[2].y - p[3].y, 0, 1, color, clipped, span, context);
            }
        }
    }
    return 0;
}

void fe2_render_backend_line(void *context, fe2_screen_point first, fe2_screen_point second, uint16_t material)
{
    const fe2_render_backend *b = context;
    if (b && b->color) (void)fe2_render_solid_line(first, second, b->color(b->context, material), b->span, b->context);
}
void fe2_render_backend_surface(void *context, const fe2_render_surface_command *command)
{
    const fe2_render_backend *b = context;
    if (!b || !b->color || !command) return;
    if (command->kind == FE2_RENDER_CLIPPED_POLYGON) {
        fe2_render_polygon polygon = command->polygon;
        polygon.color = b->color(b->context, command->material);
        (void)fe2_render_polygon_draw(&polygon, b->span, b->context);
    } else {
        fe2_screen_point p[4];
        unsigned i;
        if (command->polygon.count != 3 && command->polygon.count != 4) return;
        for (i = 0; i < command->polygon.count; ++i) p[i] = command->polygon.points[i];
        if (command->polygon.count == 4) { p[2] = command->polygon.points[3]; p[3] = command->polygon.points[2]; }
        (void)fe2_render_solid_polygon(p, command->polygon.count, b->color(b->context, command->material), 0, b->span, b->context);
    }
}
