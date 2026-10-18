/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "execution.h"

/* Opcode 21. A positive token places vertex
 * signed_byte(token >> 5) through the native cache, inserts a
 * batch node keyed like an ordinary vertex group into the active queue and
 * makes the batch's own zero-key-sentinel queue the active one. The original
 * keeps four queue roots (DS:0x1f8b active, 0x1f8d..0x1f91 saved): opening
 * shifts them down, discarding the oldest saved root; closing shifts them up
 * and refills the last slot with the frame root. */
static void execute_batch(void *target, const void *payload)
{
    fe2_render_queue_execute((fe2_render_queue *)payload, target);
}

fe2_render_program_status fe2_render_execute_batch_command(uint16_t token,
    fe2_render_program_drawing *d)
{
    fe2_render_execution *e;
    fe2_render_command_group *batch;
    fe2_vertex *vertex;
    unsigned index;
    if (!d || !(e = d->execution) || !e->depth || !d->queue || d->queue->executing)
        return FE2_RENDER_PROGRAM_BAD_VALUE;
    if (token & 0x8000u) {
        e->trees[0] = e->trees[1]; e->trees[1] = e->trees[2];
        e->trees[2] = e->trees[3]; e->trees[3] = NULL;
        return FE2_RENDER_PROGRAM_COMPLETE;
    }
    /* The original would splice the batch node into a linear group stream. */
    if (e->active_group) return FE2_RENDER_PROGRAM_BAD_VALUE;
    index = (token >> 5) & 255u;
    vertex = fe2_render_execution_place(d, index < 128u ? (int)index : (int)index - 256);
    if (!vertex) return FE2_RENDER_PROGRAM_BAD_VALUE;
    if (e->batch_count >= e->batch_capacity) return FE2_RENDER_PROGRAM_OUTPUT_FULL;
    if (!e->batches) return FE2_RENDER_PROGRAM_BAD_VALUE;
    batch = e->batches + e->batch_count;
    fe2_render_queue_init(&batch->commands);
    if (fe2_render_queue_insert(fe2_render_execution_tree(d), &batch->node,
            fe2_render_depth_key(vertex->view, d->scale), execute_batch, &batch->commands))
        return FE2_RENDER_PROGRAM_BAD_VALUE;
    ++e->batch_count;
    e->trees[3] = e->trees[2]; e->trees[2] = e->trees[1];
    e->trees[1] = e->trees[0]; e->trees[0] = &batch->commands;
    return FE2_RENDER_PROGRAM_COMPLETE;
}
