/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_PIPELINE_H
#define FE2_RENDER_PIPELINE_H

#include <stddef.h>
#include "render.h"

/* Caller-owned nodes and payloads remain alive until execution completes.
 * Equal keys execute in insertion order; key zero is below the frame sentinel
 * and is deliberately not executed. No allocation or recursive traversal. */
typedef void (*fe2_render_command_fn)(void *context, const void *payload);
typedef struct fe2_render_command {
    uint32_t key;
    struct fe2_render_command *lower, *greater, *parent;
    fe2_render_command_fn execute;
    const void *payload;
} fe2_render_command;

typedef struct fe2_render_queue {
    fe2_render_command sentinel;
    int executing;
} fe2_render_queue;

void fe2_render_queue_init(fe2_render_queue *queue);
/* Node must be distinct from every node already in this queue. */
int fe2_render_queue_insert(fe2_render_queue *queue, fe2_render_command *node,
                            uint32_t key, fe2_render_command_fn execute, const void *payload);
/* Callbacks may draw, but must not modify/reset the queue or its nodes.
 * Insert and nested execution are rejected during execution. */
int fe2_render_queue_execute(fe2_render_queue *queue, void *context);

enum { FE2_RENDER_INTERSECTIONS = 10 };
typedef struct fe2_render_scanlines {
    int16_t x[FE2_VIEWPORT_HEIGHT][FE2_RENDER_INTERSECTIONS];
    uint8_t count[FE2_VIEWPORT_HEIGHT];
    int16_t min_y, max_y;
    int direct; /* all accepted edge endpoints have X inside the viewport */
} fe2_render_scanlines;

void fe2_render_scanlines_init(fe2_render_scanlines *packet);
/* Collect rows [lowY, highY), clipping Y to 0..168. Horizontal and wholly
 * rejected edges succeed without intersections. Returns -1 for an X delta
 * outside signed-word magnitude or a full row; packet is unchanged on error.
 * Original packet overflows are intentionally outside the native contract. */
int fe2_render_scanlines_edge(fe2_render_scanlines *packet,
                              fe2_screen_point first, fe2_screen_point second);

typedef void (*fe2_render_span_fn)(void *context, unsigned y, unsigned x,
                                    unsigned count, uint8_t color);
/* Video driver's collected-polygon consumer: descending rows, signed X sort,
 * adjacent pairs, exclusive right endpoint (zero-width pairs draw one pixel).
 * Even intersection counts up to the collector's row capacity. Invalid
 * packets fail before drawing. Color is the low byte of the resident drawing color. */
int fe2_render_scanlines_draw(const fe2_render_scanlines *packet, uint8_t color,
                              fe2_render_span_fn draw, void *context);

typedef struct fe2_render_surface {
    uint8_t *pixels;
    size_t width, height, stride;
} fe2_render_surface;
/* A span backend for ordinary indexed-color memory. Count zero draws one
 * pixel, matching the shipped solid-span primitive. Clips to surface bounds;
 * caller provides height rows of at least stride bytes, with stride >= width. */
void fe2_render_surface_span(void *surface, unsigned y, unsigned x,
                              unsigned count, uint8_t color);

typedef void (*fe2_render_line_fn)(void *context, fe2_screen_point first,
                                    fe2_screen_point second, uint16_t color);
/* Clip and reject before submitting. Second-endpoint clipping swaps
 * the submitted endpoints, as in the original. Returns whether submitted. */
int fe2_render_draw_line(fe2_screen_point first, fe2_screen_point second,
                          uint16_t color, fe2_render_line_fn draw, void *context);

#endif
