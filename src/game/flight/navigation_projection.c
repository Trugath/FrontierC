/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* projections drawn after the map render - orbit rings of bodies
 * with a parent, and size markers of the other objects. */
#include <string.h>

#include "navigation_internal.h"

enum {
    RING_MODEL = 0xb2, SIZED_MARKER_MODEL = 0xb4, PLAIN_MARKER_MODEL = 0xb6,
    RING_SCALE_BIAS = 0x19,
    NO_SIZE = 0xff, LARGEST_SIZE = 300, DEFAULT_MARKER_SIZE = 7
};

/*
 * A body's orbit ring is a temporary object at its parent's position.
 * - Basis: from the body's orbit phase (+0x9c) and the high word of its orbit
 *   product (+0x96).
 * - Scalar: the body's scalar, high word less 0x19; that word is also the
 *   ring's model parameter.
 * - Parent handle: the parent's own parent.
 * The original builds it in uninitialized stack and sets only these fields.
 */
static void project_ring(fe2_navigation_state *s, const fe2_navigation_services *game,
                         const fe2_object *body)
{
    fe2_object ring;
    const fe2_object *parent;
    fe2_call_args args;
    uint16_t radius = (uint16_t)((body->scalar >> 16) - RING_SCALE_BIAS);
    if (body->parent_handle == 0 || (body->flags & OBJECT_HIDDEN)) return;
    s->basis_angle[0] = body->as.body.orbit_inclination;
    s->basis_angle[1] = body->as.body.orbit_product[3];
    parent = game->lookup_object(game->opaque, body->parent_handle, 0);
    memset(&ring, 0, sizeof ring);
    ring.scalar = (body->scalar & 0xffffu) | (uint32_t)radius << 16;
    ring.model_parameter = radius;
    ring.parent_handle = parent->parent_handle;
    ring.model = RING_MODEL;
    memcpy(ring.position_low, parent->position_low, sizeof ring.position_low);
    memcpy(ring.position_high, parent->position_high, sizeof ring.position_high);
    game->basis_b(game->opaque, ring.orientation);
    fe2_navigation_camera_args(s, &args);
    game->draw_object(game->opaque, &ring, &args);
}

/* Markers are drawn with a temporary model and size (+0x74), restored after.
 * The special model is a plain marker of size 1; objects with map marker
 * 0xff are plain markers of size 0; others are sized from their model. */
static void project_marker(fe2_navigation_state *s, const fe2_navigation_services *game,
                           fe2_object *o)
{
    uint16_t model = o->model, kept_size = o->cargo_type, size;
    fe2_call_args args;
    if (model == SPECIAL_MODEL) {
        size = 1;
        o->model = PLAIN_MARKER_MODEL;
    } else if (o->map_marker == NO_SIZE) {
        size = 0;
        o->model = PLAIN_MARKER_MODEL;
    } else {
        fe2_navigation_model info = game->model(game->opaque, model);
        uint16_t value = info.has_size ? info.size : 0xffff;
        size = value <= LARGEST_SIZE ? fe2_navigation_marker_sizes[value >> 4] : DEFAULT_MARKER_SIZE;
        o->model = SIZED_MARKER_MODEL;
    }
    o->cargo_type = size;
    fe2_navigation_camera_args(s, &args);
    game->draw_object(game->opaque, o, &args);
    o->cargo_type = kept_size;
    o->model = model;
}

void fe2_navigation_project_object(void *pass, fe2_object *o)
{
    fe2_navigation_state *s = ((fe2_navigation_pass *)pass)->state;
    const fe2_navigation_services *game = ((fe2_navigation_pass *)pass)->game;
    if (o->role == ROLE_BODY) project_ring(s, game, o);
    else project_marker(s, game, o);
}
