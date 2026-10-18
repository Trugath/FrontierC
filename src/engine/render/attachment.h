/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_ATTACHMENT_H
#define FE2_RENDER_ATTACHMENT_H
#include "model.h"
#include "game/object.h"

typedef struct fe2_render_attachment_state {
    uint16_t angle[2];
    uint8_t size_high; /* retained high byte of the attachment size word */
} fe2_render_attachment_state;

/* Model extension offsets refer into the original resource bank. descriptor
 * is its byte offset in bank. converted is the source in the render frame.
 * Source flag bits 2/3/4 are consumed in priority order. Invalid resources
 * leave both source flags and output unchanged. Output can alias converted. */
int fe2_render_place_attachment(fe2_object *source, const fe2_object *converted,
    fe2_render_bytes bank, size_t descriptor, const fe2_render_attachment_state *state,
    fe2_object *output);
/* Service 0xe4's zero-position model 0x180, after optional secondary-basis
 * refresh by the frame-tree owner. */
int fe2_render_basis_attachment(const fe2_object *source, fe2_object *output);

typedef struct fe2_render_attachment_services {
    void *context;
    int (*convert)(void *context, const fe2_object *source, fe2_object *converted);
    int (*model)(void *context, uint16_t selector, fe2_render_bytes *bank, size_t *descriptor);
    int (*render)(void *context, const fe2_object *placed);
} fe2_render_attachment_services;
/* Full flag-selected attachment entry, connecting object records to the
 * model renderer. Converted and placed records are local to this call. */
int fe2_render_object_attachment(fe2_object *source, const fe2_render_attachment_state *state,
    const fe2_render_attachment_services *services);
#endif
