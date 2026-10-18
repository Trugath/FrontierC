/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The docking approach's motion. */
#include "people_internal.h"

/* unless its parent (looked up into parameter 9) has flag 0x40,
 * the craft turns toward it and takes 1/128 of the difference as its
 * velocity. */
void people_motion_follow_docking_parent(people_context *c, fe2_object *object, uint64_t change)
{
    fe2_object *parent = people_lookup_parent(c, object);
    uint32_t difference[3];
    unsigned axis;

    if (parent->flags & FLAG_LOUD)
        return;
    fe2_args_word(c->args, P_MODE, 0xffff);
    c->g->rates_toward(c->g->opaque, object, parent, c->args);
    c->g->difference(c->g->opaque, object, c->args, difference);
    for (axis = 0; axis < 3; ++axis)
        fe2_args_dword(c->args, P0 + axis, difference[axis]);
    for (axis = 0; axis < 3; ++axis)
        object->velocity[axis] = people_sar32(people_arg(c, P0 + axis), 7);
    people_transform(c, object, change);
}
