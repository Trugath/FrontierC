/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "backend.h"

/* Cubic forward differencing  and its two consumers: the
 * scanline-packet batch  and the clipped line batch. */

static int16_t w(int value)
{
    unsigned u = (unsigned)value & 65535u;
    return (int16_t)(u < 32768u ? (int)u : (int)u - 65536);
}

/* SAR/RCR chain: the word becomes the high half of a 32-bit value shifted
 * arithmetically right by n (n <= 16). */
static uint32_t widen(int16_t value, unsigned n)
{
    uint32_t v = (uint32_t)(uint16_t)value << 16;
    return n ? v >> n | ((v & UINT32_C(0x80000000)) ? UINT32_MAX << (32 - n) : 0) : v;
}

static int quarter(int16_t v)
{
    return v < 0 ? -((-(int)v + 3) / 4) : v / 4; /* arithmetic shift right by two */
}

static unsigned magnitude(int16_t v)
{
    return v < 0 ? (uint16_t)(0u - (unsigned)(uint16_t)v) : (uint16_t)v;
}

void fe2_render_cubic_setup(fe2_screen_point p0, const fe2_screen_point p[3], fe2_render_cubic_steps *s)
{
    static const unsigned limits[3] = {4, 0x1e, 0x61a8};
    int16_t mx, my, a[2], b[2], c[2], q0[2], q1[2], q2[2], q3[2];
    unsigned metric, level, i;
    q0[0] = p0.x; q0[1] = p0.y; q1[0] = p[0].x; q1[1] = p[0].y;
    q2[0] = p[1].x; q2[1] = p[1].y; q3[0] = p[2].x; q3[1] = p[2].y;
    /* sar of the wrapped coordinate sum, minus the anchor, magnitude */
    mx = w(quarter(w(q0[0] + q1[0] + q2[0] + q3[0])) - q0[0]);
    my = w(quarter(w(q0[1] + q1[1] + q2[1] + q3[1])) - q0[1]);
    metric = (magnitude(mx) + magnitude(my)) & 0xffffu;
    for (level = 0; level < 3 && metric >= limits[level]; ++level) {}
    for (i = 0; i < 2; ++i) {
        unsigned k = level + 2;
        uint32_t aa, bb, cc;
        a[i] = w(q3[i] - q0[i] + 3 * q1[i] - 3 * q2[i]);
        b[i] = w(3 * q0[i] - 6 * q1[i] + 3 * q2[i]);
        c[i] = w(3 * q1[i] - 3 * q0[i]);
        cc = widen(c[i], k); bb = widen(b[i], 2 * k); aa = widen(a[i], 3 * k);
        s->d1[i] = cc + bb + aa;
        s->d3[i] = aa * 6u;
        s->d2[i] = aa * 6u + (bb << 1);
    }
    s->x = (uint32_t)(uint16_t)p0.x << 16;
    s->y = (uint32_t)(uint16_t)p0.y << 16;
    s->segments = 4u << level;
}

unsigned fe2_render_cubic_points(fe2_screen_point p0, const fe2_screen_point p[3], fe2_screen_point *out)
{
    fe2_render_cubic_steps s;
    unsigned i;
    fe2_render_cubic_setup(p0, p, &s);
    out[0] = p0;
    for (i = 1; i <= s.segments; ++i) {
        s.x += s.d1[0]; s.d1[0] += s.d2[0]; s.d2[0] += s.d3[0];
        s.y += s.d1[1]; s.d1[1] += s.d2[1]; s.d2[1] += s.d3[1];
        out[i].x = w((int)(s.x >> 16));
        out[i].y = w((int)(s.y >> 16));
    }
    return s.segments;
}

int fe2_render_cubic_edges(fe2_render_scanlines *packet, fe2_screen_point p0, const fe2_screen_point p[3])
{
    fe2_screen_point points[33];
    unsigned i, n = fe2_render_cubic_points(p0, p, points);
    for (i = 0; i < n; ++i)
        if (fe2_render_scanlines_edge(packet, points[i], points[i + 1])) return -1;
    return 0;
}

static int inside(fe2_screen_point v)
{
    return (uint16_t)v.x < FE2_VIEWPORT_WIDTH && (uint16_t)v.y < FE2_VIEWPORT_HEIGHT;
}

void fe2_render_cubic_lines(fe2_screen_point p0, const fe2_screen_point p[3], uint16_t color,
                            fe2_render_line_fn draw, void *context)
{
    fe2_screen_point points[33];
    unsigned i, n = fe2_render_cubic_points(p0, p, points);
    for (i = 0; i < n; ++i) {
        fe2_screen_point a = points[i], b = points[i + 1];
        if (inside(a)) {
            if (!inside(b)) b = fe2_render_clip_line(b, a);
        } else {
            if (!inside(b)) continue;
            a = fe2_render_clip_line(a, b);
        }
        draw(context, a, b, color);
    }
}
