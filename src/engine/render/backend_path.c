/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "backend.h"

/*, using native path elements. */
int fe2_render_draw_path(const fe2_render_path_command *command, const fe2_render_backend *backend)
{
    fe2_render_scanlines packet;
    size_t i;
    if (!command || !backend || !backend->color || !backend->span ||
        (command->count && !command->elements)) return -1;
    fe2_render_scanlines_init(&packet);
    for (i = 0; i < command->count; ++i) {
        const fe2_render_path_element *element = command->elements + i;
        switch (element->kind) {
        case FE2_RENDER_PATH_SEGMENT:
            if (fe2_render_scanlines_edge(&packet, element->points[0], element->points[1])) return -1;
            break;
        case FE2_RENDER_PATH_CURVE:
            if (fe2_render_cubic_edges(&packet, element->points[0], element->points + 1)) return -1;
            break;
        case FE2_RENDER_PATH_FILL:
            if (fe2_render_scanlines_draw(&packet, backend->color(backend->context, element->material),
                backend->span, backend->context)) return -1;
            fe2_render_scanlines_init(&packet);
            break;
        default:
            return -1;
        }
    }
    return fe2_render_scanlines_draw(&packet, backend->color(backend->context, command->material),
        backend->span, backend->context);
}

void fe2_render_backend_path(void *backend, const fe2_render_path_command *command)
{
    (void)fe2_render_draw_path(command, backend);
}
