/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "pipeline.h"
#include <string.h>

void fe2_render_queue_init(fe2_render_queue *q)
{
    memset(q, 0, sizeof(*q));
}

int fe2_render_queue_insert(fe2_render_queue *q, fe2_render_command *node,
                            uint32_t key, fe2_render_command_fn execute, const void *payload)
{
    fe2_render_command *parent, **link;
    if (!q || !node || !execute || q->executing || node == &q->sentinel) return -1;
    parent = &q->sentinel;
    for (;;) {
        link = key > parent->key ? &parent->greater : &parent->lower;
        if (!*link) break;
        parent = *link;
    }
    node->key = key;
    node->lower = node->greater = NULL;
    node->parent = parent;
    node->execute = execute;
    node->payload = payload;
    *link = node;
    return 0;
}

int fe2_render_queue_execute(fe2_render_queue *q, void *context)
{
    fe2_render_command *node;
    if (!q || q->executing) return -1;
    q->executing = 1;
    node = q->sentinel.greater;
    while (node) {
        while (node->greater) node = node->greater;
        for (;;) {
            node->execute(context, node->payload);
            if (node->lower) { node = node->lower; break; }
            while (node->parent != &q->sentinel && node == node->parent->lower)
                node = node->parent;
            node = node->parent;
            if (node == &q->sentinel) { node = NULL; break; }
        }
    }
    q->executing = 0;
    return 0;
}
