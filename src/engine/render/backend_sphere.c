/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "backend.h"
#include <string.h>

static int word(int value)
{
    unsigned bits = (unsigned)value & 65535u;
    return bits < 32768u ? (int)bits : (int)bits - 65536;
}
static int valid_mask(uint16_t mask) { return !(mask & ~60u); }

void fe2_render_color_scanlines_init(fe2_render_color_scanlines *packet)
{
    memset(packet, 0, sizeof(*packet));
    packet->min_y = FE2_VIEWPORT_HEIGHT;
    packet->max_y = -1;
}

static void insert(fe2_render_color_scanlines *packet, unsigned y, int x, uint16_t mask)
{
    unsigned n, i;
    if (y >= FE2_VIEWPORT_HEIGHT) return;
    n = packet->count[y]; i = n;
    if (n == FE2_RENDER_COLOR_EVENTS) return;
    while (i && x < packet->events[y][i - 1].x) {
        packet->events[y][i] = packet->events[y][i - 1];
        --i;
    }
    packet->events[y][i].x = (int16_t)x;
    packet->events[y][i].mask = mask;
    packet->count[y] = (uint8_t)(n + 1);
}

int fe2_render_color_row(fe2_render_color_scanlines *packet, int16_t row, uint16_t mask)
{
    if (!packet || row < 0 || row >= FE2_VIEWPORT_HEIGHT || !valid_mask(mask)) return -1;
    packet->toggle[row] ^= mask;
    return 0;
}

int fe2_render_color_edge(fe2_render_color_scanlines *packet,
    fe2_screen_point a, fe2_screen_point b, uint16_t mask)
{
    fe2_screen_point swap;
    int y, height, width, x, direction, error, step = 0, increment = 0;
    if (!packet || !valid_mask(mask)) return -1;
    if ((uint16_t)b.y >= 169u) {
        if ((uint16_t)a.y >= 169u && ((a.y < 0) == (b.y < 0))) return 0;
        swap = a; a = b; b = swap;
    }
    if ((uint16_t)a.y >= 169u) a = fe2_render_clip_polygon(a, b);
    if ((uint16_t)b.y >= 169u) {
        swap = a; a = b; b = swap;
        a = fe2_render_clip_polygon(a, b);
    }
    if (a.y > b.y) { swap = a; a = b; b = swap; }
    height = b.y - a.y;
    width = (int)b.x - a.x;
    if (width < -32767 || width > 32767) return -1;
    if (b.y > packet->max_y) packet->max_y = b.y > FE2_VIEWPORT_HEIGHT ? FE2_VIEWPORT_HEIGHT : b.y;
    if (a.y < packet->min_y) packet->min_y = a.y < 0 ? 0 : a.y;
    if (!height) return 0;
    direction = width < 0 ? -1 : 1;
    if (width < 0) width = -width;
    x = a.x;
    if (height > width) {
        error = width - height / 2;
        if (error > 0) { error -= height; x = word(x + direction); }
        for (y = a.y; y < b.y; ++y) {
            insert(packet, (unsigned)y, x, mask);
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
            insert(packet, (unsigned)y, x, mask);
        }
    }
    return 0;
}

int fe2_render_color_scanlines_draw(const fe2_render_color_scanlines *packet,
    const uint8_t *colors, unsigned color_count, fe2_render_span_fn span, void *context)
{
    uint16_t baseline;
    int y;
    if (!packet || !colors || !span || (color_count != 8 && color_count != 16) ||
        packet->max_y < -1 || packet->max_y > FE2_VIEWPORT_HEIGHT) return -1;
    y = packet->max_y - 1;
    baseline = y >= 167 ? 0 : packet->toggle[167];
    for (; y >= 0; --y) {
        unsigned i, n = packet->count[y];
        uint16_t selection;
        if (n > FE2_RENDER_COLOR_EVENTS || !valid_mask(packet->toggle[y])) return -1;
        baseline ^= packet->toggle[y];
        selection = baseline;
        for (i = 0; i < n; ++i) {
            uint16_t mask = packet->events[y][i].mask;
            if (!valid_mask(mask)) return -1;
            if (!mask) break;
            selection ^= mask;
        }
        if (i == n) continue;
        for (; i + 1 < n; ++i) {
            const fe2_render_color_event *a = packet->events[y] + i, *b = a + 1;
            int left = a->x < 0 ? 0 : a->x;
            int right = b->x >= FE2_VIEWPORT_WIDTH ? FE2_VIEWPORT_WIDTH : b->x;
            if (left >= FE2_VIEWPORT_WIDTH) break;
            if (b->x >= 0) {
                if ((selection & 3u) || selection / 4u >= color_count || right < left) return -1;
                span(context, (unsigned)y, (unsigned)left, (unsigned)(right - left), colors[selection / 4u]);
            }
            if (!b->mask) break;
            if (!valid_mask(b->mask)) return -1;
            selection ^= b->mask;
        }
    }
    return 0;
}

int fe2_render_draw_sphere(const fe2_render_sphere_command *command, const fe2_render_backend *backend)
{
    fe2_render_color_scanlines packet;
    uint8_t colors[16];
    size_t i;
    unsigned j;
    if (!command || !backend || !backend->color || !backend->span ||
        (command->count && !command->elements) || (command->color_count != 8 && command->color_count != 16)) return -1;
    fe2_render_color_scanlines_init(&packet);
    for (i = 0; i < command->count; ++i) {
        const fe2_render_sphere_element *element = command->elements + i;
        switch (element->kind) {
        case FE2_RENDER_SPHERE_CONTROL: {
            fe2_screen_point points[33];
            unsigned n = fe2_render_cubic_points(element->points[0], element->points + 1, points);
            for (j = 0; j < n; ++j)
                if (fe2_render_color_edge(&packet, points[j], points[j + 1], 0)) return -1;
            break;
        }
        case FE2_RENDER_SPHERE_EDGE:
            if (fe2_render_color_edge(&packet, element->points[0], element->points[1], element->value)) return -1;
            break;
        case FE2_RENDER_SPHERE_ROW:
            if (fe2_render_color_row(&packet, element->points[0].y, element->value)) return -1;
            break;
        default:
            return -1;
        }
    }
    for (j = 0; j < command->color_count; ++j) colors[j] = backend->color(backend->context, command->colors[j]);
    return fe2_render_color_scanlines_draw(&packet, colors, command->color_count, backend->span, backend->context);
}

void fe2_render_backend_sphere(void *backend, const fe2_render_sphere_command *command)
{
    (void)fe2_render_draw_sphere(command, backend);
}
