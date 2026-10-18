/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "backend.h"

/* MCGA solid-disc and shape-profile primitives. */

static int16_t w(int value)
{
    unsigned u = (unsigned)value & 65535u;
    return (int16_t)(u < 32768u ? (int)u : (int)u - 65536);
}

/* fill_solid_span_count  after its callers' checks: positive counts
 * draw as given, zero draws one pixel when X < 320, negative counts nothing. */
static void span(fe2_render_span_fn draw, void *context, int y, int x, int count, uint8_t color)
{
    if (count < 0 || (!count && (uint16_t)x >= FE2_VIEWPORT_WIDTH)) return;
    draw(context, (unsigned)(uint16_t)y, (unsigned)(uint16_t)x, (unsigned)count, color);
}

/* rows of the six fixed patterns, as (dy, dx, count). */
static const int8_t small_rows[6][5][3] = {
    {{0, 0, 1}},
    {{-1, -1, 2}, {0, -1, 2}},
    {{-1, 0, 1}, {0, -1, 3}, {1, 0, 1}},
    {{-1, -1, 3}, {0, -1, 3}, {1, -1, 3}},
    {{-2, -1, 2}, {-1, -2, 4}, {0, -2, 4}, {1, -1, 2}},
    {{-2, 0, 1}, {-1, -1, 3}, {0, -2, 5}, {1, -1, 3}, {2, 0, 1}}
};
static const uint8_t small_counts[6] = {1, 2, 3, 3, 4, 5};

typedef struct disc_record {
    int16_t left, count;
} disc_record;

int fe2_render_draw_disc(int16_t x, int16_t y, uint16_t size, uint8_t color,
                         fe2_render_span_fn draw, void *context)
{
    disc_record records[1008];
    int half, tf, tb, bf, bb, di, bx, dx, si, cx, row, rows, top, i;
    if (!draw || size >= 0x3f0u) return 0;
    if (size < 6) {
        if (x < 2 || x >= 0x13e || y < 2 || y >= 0xa6) return 0;
        for (i = 0; i < small_counts[size]; ++i)
            span(draw, context, y + small_rows[size][i][0], x + small_rows[size][i][1],
                 small_rows[size][i][2], color);
        return 1;
    }
    half = size >> 1;
    si = half;              /* (size * 2) & ~3, in records */
    tf = 0; tb = bf = si; bb = 2 * si;
    di = 0; bx = 0; cx = half;
    dx = size - 2;
    if (dx == 5) dx += 3;
    dx = -dx;
    dx += di * 4 + 6;
    for (;;) {
        di += 1; ++bx;
        if (si > di) {
            /* wide symmetric pair; then narrow when the error is >= 0 */
            records[--tb].left = w(x - cx); records[tb].count = w(cx * 2);
            records[bf].left = records[tb].left; records[bf++].count = records[tb].count;
            if (w(dx) >= 0) {
                records[tf].left = w(x - bx); records[tf++].count = w(bx * 2);
                records[--bb].left = records[tf - 1].left; records[bb].count = records[tf - 1].count;
                dx = w(dx + di * 4 - si * 4 + 10);
                --si; --cx;
            } else {
                dx = w(dx + di * 4 + 6);
            }
            continue;
        }
        if (si == di) {
            records[--tb].left = w(x - cx); records[tb].count = w(cx * 2);
            records[bf].left = records[tb].left; records[bf++].count = records[tb].count;
        }
        break;
    }
    /* Span output: skip rows above the viewport, choose direct or clipped. */
    top = w(y - half); row = 0;
    if (top < 0) { row = -top; top = 0; }
    if (x >= half && w(x + half) <= FE2_VIEWPORT_WIDTH) {
        int bottom = w(half + y);
        if (bottom >= FE2_VIEWPORT_HEIGHT) bottom = FE2_VIEWPORT_HEIGHT;
        rows = w(bottom - top);
        if (rows <= 0 || row + rows > 2 * half) return -1;
        for (i = 0; i < rows; ++i, ++row)
            span(draw, context, top + i, records[row].left, records[row].count, color);
        return 1;
    } else {
        int bottom = w(half + y);
        if (half + y <= 0) return 1; /* JLE after ADD: the unwrapped sum */
        if (bottom >= FE2_VIEWPORT_HEIGHT) bottom = FE2_VIEWPORT_HEIGHT;
        rows = w(bottom - top);
        if (rows <= 0 || row + rows > 2 * half) return -1;
        for (i = 0; i < rows; ++i, ++row) {
            int left = records[row].left, count = records[row].count, over;
            if (left < 0) {
                count = w(count + left);
                if (count <= 0) continue;
                left = 0;
            }
            over = w(left + count);
            if (over >= FE2_VIEWPORT_WIDTH) {
                over = w(over - FE2_VIEWPORT_WIDTH);
                if (count - over <= 0) continue;
                count = w(count - over);
            }
            span(draw, context, top + i, left, count, color);
        }
        return 1;
    }
}

int fe2_render_draw_disc_checked(int16_t x, int16_t y, uint16_t size, uint8_t color,
                                 fe2_render_span_fn draw, void *context)
{
    int half = w(size) >> 1;
    if ((uint16_t)x >= FE2_VIEWPORT_WIDTH) {
        if (x >= 0 ? w(x - half) >= FE2_VIEWPORT_WIDTH : x + half <= 0) return 0;
    }
    if ((uint16_t)y >= FE2_VIEWPORT_HEIGHT) {
        if (y >= 0 ? w(y - half) >= FE2_VIEWPORT_HEIGHT : y + half <= 0) return 0;
    }
    return fe2_render_draw_disc(x, y, size, color, draw, context);
}

int fe2_render_draw_shape_rows(int16_t anchor, int16_t y, int16_t selector, uint8_t color,
                               fe2_render_span_fn draw, void *context)
{
    int left0 = w(anchor + 2), row = w(y - 7);
    long index = 0xe0L + selector;
    if (!draw) return -1;
    for (;;) {
        int8_t half;
        int left, count;
        if (index < 0 || index >= FE2_RENDER_SHAPE_PROFILES) return -1;
        half = (int8_t)fe2_render_shape_profiles[index++];
        row = w(row + 1);
        if (!half) continue;
        if (half < 0) return 0;
        if ((uint16_t)row >= FE2_VIEWPORT_HEIGHT) continue;
        left = w(left0 - half);
        if (left < 0) left = 0;
        count = half * 2 - 1;
        if (w(left + count) >= FE2_VIEWPORT_WIDTH) count = w(FE2_VIEWPORT_WIDTH - 1 - left);
        span(draw, context, row, left, count, color);
    }
}
