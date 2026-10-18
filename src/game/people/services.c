/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The services as the module calls them (the argument block they read and
 * the parameters they leave). */
#include "people_internal.h"

fe2_object *people_lookup(people_context *c, uint8_t handle)
{
    return c->g->lookup(c->g->opaque, handle, c->args);
}

fe2_object *people_lookup_parent(people_context *c, fe2_object *object)
{
    fe2_object *parent = people_lookup(c, object->behaviour_parent);

    people_set_object(c, P_CREATED, parent);
    return parent;
}

uint32_t people_random(people_context *c, uint32_t *second)
{
    uint32_t high = 0;
    uint32_t value = c->g->random(c->g->opaque, &high);

    if (second != 0)
        *second = high;
    return value;
}

uint16_t people_random_below(people_context *c, uint16_t bound)
{
    return c->g->random_below(c->g->opaque, bound);
}

/* market leaves parameters 0..2 (service 6's offsets). */
fe2_people_reply people_market(people_context *c, uint16_t service, uint8_t handle)
{
    fe2_people_reply reply = c->g->market_overlay_service(c->g->opaque, service, handle, c->args);
    unsigned n;

    for (n = 0; n < 3; ++n)
        fe2_args_dword(c->args, n, reply.value[n]);
    return reply;
}

void people_release(people_context *c, fe2_object *object)
{
    c->g->release(c->g->opaque, object);
}

void people_remove(people_context *c, fe2_object *object)
{
    c->g->remove(c->g->opaque, object);
}

void people_slot_flags(people_context *c, fe2_object *object, uint8_t flags)
{
    c->g->set_slot_flags(c->g->opaque, object, flags);
}

void people_position_args(people_context *c, const fe2_object *object)
{
    unsigned axis;

    for (axis = 0; axis < 3; ++axis) {
        fe2_args_dword(c->args, P0 + axis, object->position_low[axis]);
        fe2_args_dword(c->args, P3 + axis, object->position_high[axis]);
    }
}

void people_spawn_effect_at(people_context *c, const fe2_object *object, uint16_t kind)
{
    people_position_args(c, object);
    fe2_args_word(c->args, P_MODE, kind);
    people_set_object(c, P_CURRENT, (fe2_object *)object);
    c->g->spawn_effect(c->g->opaque, c->args);
}

/* The change's low dword in parameter 2, its high dword in 3. */
void people_transform(people_context *c, fe2_object *object, uint64_t change)
{
    fe2_args_dword(c->args, P2, (uint32_t)change);
    fe2_args_dword(c->args, P3, (uint32_t)(change >> 32));
    c->g->transform_vector(c->g->opaque, object, c->args);
}

void people_play_sound(people_context *c, uint16_t sound, uint8_t level)
{
    c->g->play_sound(c->g->opaque, sound, level);
}
