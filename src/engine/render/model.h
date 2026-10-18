/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_MODEL_H
#define FE2_RENDER_MODEL_H

#include <stddef.h>
#include "view.h"
#include "math/math3.h"

typedef struct fe2_render_bytes {
    const uint8_t *data;
    size_t count;
} fe2_render_bytes;

/* Bounded views into a compiled model resource. Relative offsets are resolved
 * once; no segment or virtual address appears in the native context. */
typedef struct fe2_render_model {
    fe2_render_bytes program, context, geometry;
    size_t vertex_count;
    uint16_t parameter_bytes;
} fe2_render_model;

/* Read the five-word render header at header_offset. Every relative target
 * must stay in the supplied resource; program must contain a word. Vertex
 * allocation must contain at least one complete 32-byte record. Incomplete
 * trailing vertex bytes are ignored by the recovered marker-clear count.
 * Output unchanged on error. Resource storage must outlive all its views. */
int fe2_render_model_open(fe2_render_bytes resource, size_t header_offset, fe2_render_model *out);

enum { FE2_RENDER_PREFIX_VERTICES = 5 };
typedef struct fe2_render_model_workspace {
    /* Five prefix records followed by vertex_count ordinary records. */
    fe2_vertex *vertices;
    size_t vertex_capacity; /* includes the prefix */
    uint16_t *parameters;
    size_t parameter_capacity;
} fe2_render_model_workspace;

typedef struct fe2_render_model_context {
    fe2_render_model model;
    fe2_render_view view;
    fe2_vertex *vertices; /* points after the five prefix records */
    uint16_t *parameters;
    size_t parameter_count;
    uint8_t stamp;
} fe2_render_model_context;

/* Initialize a fresh invocation. Validates capacities before changing anything.
 * Clear only vertex state words (five prefix and declared records), retaining
 * cached payloads. Copy the face header and fill the declared parameter words
 * with 0x8080 using the native math service. At least two parameter words are
 * required even if the original declared fewer than four bytes. Extra caller
 * storage is untouched. View direction is retained; position/shift receive
 * the original program-entry exponent adjustment. Stamp starts at two.
 * Context and inputs must not overlap the writable workspace arrays. */
int fe2_render_model_begin(const fe2_render_model *model, const fe2_render_view *view,
                             uint16_t exponent, uint32_t face_header,
                             const fe2_render_model_workspace *workspace, fe2_render_model_context *out);

#endif
