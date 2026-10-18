/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "backend.h"
#include <string.h>

static int16_t word(uint32_t value)
{
    uint16_t bits = (uint16_t)value;
    return (int16_t)(bits < 32768u ? (int)bits : (int)bits - 65536);
}
static int32_t signed32(uint32_t value)
{
    return value < UINT32_C(0x80000000) ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}
static int32_t sar(uint32_t value, unsigned shift)
{
    return signed32(value >> shift | (value & UINT32_C(0x80000000) ? UINT32_MAX << (32 - shift) : 0));
}

uint32_t fe2_render_texture_divide(int16_t numerator, int16_t denominator)
{
    int64_t dividend, quotient;
    if (denominator) {
        dividend = (int32_t)numerator * 256 + (numerator < 0 ? 1 : numerator ? -1 : 255);
        quotient = dividend / denominator;
        if (quotient >= -32768 && quotient <= 32767) return (uint32_t)(int32_t)quotient << 8;
        quotient = ((int32_t)numerator * 64) / denominator;
        if (quotient >= -32768 && quotient <= 32767) return (uint32_t)(int32_t)quotient << 10;
    }
    return numerator < 0 ? UINT32_C(0x80000001) : UINT32_C(0x7fffffff);
}

typedef struct texture_edge {
    uint32_t x, u, v, dx, du, dv;
    unsigned end;
} texture_edge;
typedef struct texture_raster {
    fe2_screen_point points[4];
    uint8_t samples[65][256];
    unsigned width, height, count, selector;
    int y;
    texture_edge left, right;
    fe2_render_span_fn span;
    void *context;
    uint8_t background;
} texture_raster;

static int32_t cross(fe2_screen_point a, fe2_screen_point b, fe2_screen_point c)
{
    int16_t x = word((uint16_t)b.x - (uint16_t)a.x), y = word((uint16_t)b.y - (uint16_t)a.y);
    int16_t u = word((uint16_t)c.x - (uint16_t)b.x), v = word((uint16_t)c.y - (uint16_t)b.y);
    return signed32((uint32_t)((int32_t)x * v) - (uint32_t)((int32_t)y * u));
}

static int sample_image(texture_raster *r, const fe2_render_texture_image *image,
    const uint8_t *palette, unsigned orientation, unsigned shift)
{
    int start = 0, column = 1, row = (int)image->width;
    unsigned width = image->width, height = image->height, x, y;
    int bottom = (int)((image->width & 255u) * (image->height - 1));
    switch (orientation) {
    case 0: break;
    case 1: start = (int)width - 1; column = row; row = -1; break;
    case 2: start = (int)width - 1 + bottom; column = -1; row = -row; break;
    case 3: start = bottom; column = -row; row = 1; break;
    case 4: column = row; row = 1; break;
    case 5: start = bottom; row = -row; break;
    case 6: start = (int)width - 1 + bottom; column = -row; row = -1; break;
    case 7: start = (int)width - 1; column = -1; break;
    default: return -1;
    }
    if (orientation == 1 || orientation == 3 || orientation == 5 || orientation == 7) {
        unsigned swap = width; width = height; height = swap;
    }
    r->width = width >> shift; if (!r->width) r->width = 1;
    r->height = height >> shift; if (!r->height) r->height = 1;
    /* The two diagonal-mirror traversals keep the original dimensions. */
    width = (orientation == 4 || orientation == 6 ? image->width : width) >> shift;
    height = (orientation == 4 || orientation == 6 ? image->height : height) >> shift;
    if (!width) width = 1;
    if (!height) height = 1;
    if (width > 256 || height > 65) return -1;
    column *= (int)(1u << shift); row *= (int)(1u << shift);
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x) {
            int offset = start + (int)y * row + (int)x * column;
            if (offset < 0 || (size_t)offset >= image->bytes) return -1;
            r->samples[y][x] = palette[image->pixels[offset]];
        }
    return 0;
}

static void uv(const texture_raster *r, unsigned vertex, unsigned *u, unsigned *v)
{
    *u = vertex == 1 || (r->count == 4 && vertex == 2) ? r->width : 0;
    *v = vertex == r->count - 1 || (r->count == 4 && vertex == 2) ? r->height : 0;
}
static void set_edge(texture_raster *r, texture_edge *edge, unsigned from, unsigned to)
{
    unsigned u0, v0, u1, v1;
    int16_t dy = word((uint16_t)r->points[to].y - (uint16_t)r->points[from].y);
    uv(r, from, &u0, &v0); uv(r, to, &u1, &v1);
    edge->x = (uint32_t)(uint16_t)r->points[from].x << 16;
    edge->u = u0 ? ((uint32_t)u0 << 16) - 1 : 0;
    edge->v = v0 ? ((uint32_t)v0 << 16) - 1 : 0;
    edge->dx = fe2_render_texture_divide(word((uint16_t)r->points[to].x - (uint16_t)r->points[from].x), dy);
    /* ABDC computes BC in reverse. A zero X delta has a deliberate 255
     * numerator, so reversing this divide changes vertical-edge rounding. */
    if (r->count == 4 && from == 1 && to == 2 && r->points[3].y <= r->points[2].y &&
        r->points[1].y <= r->points[3].y)
        edge->dx = fe2_render_texture_divide(word((uint16_t)r->points[from].x - (uint16_t)r->points[to].x), (int16_t)-dy);
    edge->du = u0 == u1 ? 0 : fe2_render_texture_divide(word(u1 - u0), dy);
    edge->dv = v0 == v1 ? 0 : fe2_render_texture_divide(word(v1 - v0), dy);
    edge->end = to;
}

static int16_t span_step(uint32_t delta, int16_t width)
{
    int32_t value = sar(delta, 8);
    int64_t quotient;
    if (value) {
        value = signed32((uint32_t)value + 1);
        if (value >= 0) value = signed32((uint32_t)value - 2);
    }
    quotient = (int64_t)value / width;
    return quotient < -32768 || quotient > 32767 ? word((uint32_t)value) : (int16_t)quotient;
}

static int phase(texture_raster *r, int end)
{
    unsigned left_fill = 0, right_fill = 0;
    texture_edge *a = &r->left, *b = &r->right;
    if (r->selector != 0xd0 && (r->selector < 0xd3 || r->selector > 0xd8)) {
        if (signed32(a->dx) < 0) left_fill = ((0u - a->dx) + 0x17fffu) >> 17;
        if (signed32(b->dx) >= 0) right_fill = (b->dx + 0x17fffu) >> 17;
    }
    a->x -= 0x8000u; b->x += 0x8000u;
    for (; r->y < end; ++r->y) {
        int left = word(a->x >> 16), right = word(b->x >> 16), x;
        if ((unsigned)r->y < 168u) {
            if (left >= 16 && left <= 320 && left_fill) {
                unsigned length = left_fill >= 16 && left_fill > (unsigned)left ? (unsigned)left : left_fill;
                r->span(r->context, (unsigned)r->y, (unsigned)left - length, length, r->background);
            }
            if ((unsigned)right <= 303u && right_fill) {
                unsigned length = right_fill >= 16 && right_fill > 319u - (unsigned)right ? 319u - (unsigned)right : right_fill;
                if (length) r->span(r->context, (unsigned)r->y, (unsigned)right + 1, length, r->background);
            }
            if (right >= 0 && left <= 319 && right > left) {
                int16_t du = span_step(b->u - a->u, (int16_t)(right - left));
                int16_t dv = span_step(b->v - a->v, (int16_t)(right - left));
                uint16_t u = (uint16_t)(a->u >> 8), v = (uint16_t)(a->v >> 8);
                int first = left < 0 ? 0 : left, last = right > 319 ? 319 : right;
                u = (uint16_t)(u + (first - left) * du); v = (uint16_t)(v + (first - left) * dv);
                for (x = first; x <= last; ++x) {
                    unsigned sx = u >> 8, sy = v >> 8;
                    if (sx >= r->width || sy >= r->height || sy >= 65) return -1;
                    r->span(r->context, (unsigned)r->y, (unsigned)x, 1, r->samples[sy][sx]);
                    u = (uint16_t)(u + du); v = (uint16_t)(v + dv);
                }
            }
        }
        a->x += a->dx; a->u += a->du; a->v += a->dv;
        b->x += b->dx; b->u += b->du; b->v += b->dv;
    }
    a->x += 0x8000u; b->x -= 0x8000u;
    return 0;
}

static int fallback(texture_raster *r)
{
    return fe2_render_solid_polygon(r->points, r->count, r->background, 1, r->span, r->context) ? -1 : 2;
}

int fe2_render_draw_texture_image(const fe2_screen_point *points, unsigned count, uint16_t selector,
    const fe2_render_texture_image *image, const uint8_t palette[256], fe2_render_span_fn span, void *context)
{
    texture_raster r;
    unsigned i, top = 0, bottom = 0, orientation = selector >> 12, mirror = orientation & 4u;
    unsigned metric_x = 0, metric_y = 0, estimate, shift = 0, left, right;
    if (!points || !image || !image->pixels || !palette || !span || (count != 3 && count != 4) ||
        !image->width || image->width > 255 || !image->height || image->height > 255) return -1;
    memset(&r, 0, sizeof(r));
    r.count = count; r.selector = selector & 0xfffu; r.span = span; r.context = context; r.background = palette[0];
    for (i = 0; i < count; ++i) {
        if (points[i].x < -2000 || points[i].x > 2320 || points[i].y < -2000 || points[i].y > 2168) return -1;
        r.points[i] = points[i];
    }
    if (cross(r.points[0], r.points[1], r.points[2]) < 0) {
        fe2_screen_point swap = r.points[1];
        r.points[1] = r.points[count - 1]; r.points[count - 1] = swap;
        if (count == 4) { mirror ^= 4u; orientation = (0u - orientation) & 255u; }
    }
    if (count == 4 && cross(r.points[3], r.points[0], r.points[1]) <= 0) return fallback(&r);
    for (i = 1; i < count; ++i) if (r.points[i].y < r.points[top].y) top = i;
    if (count == 4) {
        fe2_screen_point rotated[4];
        for (i = 0; i < 4; ++i) rotated[i] = r.points[(top + i) % 4];
        memcpy(r.points, rotated, sizeof(rotated));
        orientation = ((orientation + top) & 3u) | mirror; top = 0;
        if ((r.points[3].y > r.points[2].y && r.points[1].y > r.points[2].y) || r.points[0].y == r.points[2].y)
            return fallback(&r);
    } else orientation = 0;
    for (i = 0; i < (count == 3 ? 2u : 4u); ++i) {
        int dx = r.points[i].x - r.points[(i + 1) % count].x;
        int dy = r.points[i].y - r.points[(i + 1) % count].y;
        metric_x += (unsigned)(dx < 0 ? -dx : dx); metric_y += (unsigned)(dy < 0 ? -dy : dy);
    }
    metric_x >>= count == 3 ? 1 : 2; metric_y >>= count == 3 ? 1 : 2;
    estimate = image->width + image->height; estimate -= estimate >> 2;
    if (count == 4 && r.selector == 0xdf) estimate >>= 1;
    while ((metric_x + metric_y) < (estimate >>= 1)) ++shift;
    if (sample_image(&r, image, palette, orientation, shift)) return -1;
    for (i = 1; i < count; ++i) if (r.points[i].y > r.points[bottom].y) bottom = i;
    left = (top + count - 1) % count; right = (top + 1) % count;
    r.y = r.points[top].y;
    set_edge(&r, &r.left, top, left); set_edge(&r, &r.right, top, right);
    while (r.y < r.points[bottom].y) {
        int end = r.points[left].y < r.points[right].y ? r.points[left].y : r.points[right].y;
        if (end < r.y) return fallback(&r);
        if (phase(&r, end)) return -1;
        if (end == r.points[bottom].y) break;
        if (r.points[left].y == end) {
            unsigned next = (left + count - 1) % count;
            set_edge(&r, &r.left, left, next); left = next;
        }
        if (r.points[right].y == end) {
            unsigned next = (right + 1) % count;
            set_edge(&r, &r.right, right, next); right = next;
        }
    }
    return 0;
}

int fe2_render_draw_texture(const fe2_render_texture_command *command, const fe2_render_backend *backend)
{
    fe2_render_texture_image image;
    fe2_screen_point points[4];
    uint8_t palette[256] = {0};
    unsigned i;
    if (!command || !backend || !backend->texture || !backend->color || command->color_count > 7 ||
        (command->count != 3 && command->count != 4)) return -1;
    if (backend->texture_map) memcpy(palette, backend->texture_map, sizeof(palette));
    for (i = 0; i < command->color_count; ++i) palette[i] = backend->color(backend->context, command->colors[i]);
    if (backend->texture(backend->context, command->texture & 0xfffu, &image)) return -1;
    for (i = 0; i < command->count; ++i) points[i] = command->points[i];
    if (command->count == 4) { points[2] = command->points[3]; points[3] = command->points[2]; }
    return fe2_render_draw_texture_image(points, command->count, command->texture, &image, palette, backend->span, backend->context);
}

void fe2_render_backend_texture(void *backend, const fe2_render_texture_command *command)
{
    (void)fe2_render_draw_texture(command, backend);
}
