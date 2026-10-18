/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Creation (0x8a, 0x3a1..0x69a). */
#include "people_internal.h"

static void people_add_offset(fe2_object *object, unsigned axis, uint32_t low, uint32_t high)
{
    uint32_t sum = object->position_low[axis] + low;

    object->position_high[axis] += high + (sum < low ? 1u : 0u);
    object->position_low[axis] = sum;
}

/* a craft whose parent is `parent` (+0xd2) and whose reward is the
 * caller's parameter 10, with a random offset. */
uint16_t people_create_linked(people_context *c, uint16_t subtype, uint16_t parent)
{
    uint32_t reward = people_arg(c, P_CURSOR);
    fe2_object *object;
    uint32_t second;
    uint32_t first;

    if (!people_allocate(c, (uint8_t)subtype))
        return 0;
    object = people_object_arg(c, P_CREATED);
    fe2_args_word(c->args, P_SETUP, 0x5000);
    fe2_args_word(c->args, P_MODE, 0x31);
    people_setup_fields(c, object);
    object->behaviour = BEHAVIOUR_REPORT_ESCORT_ARRIVAL;
    object->behaviour_parent = (uint8_t)parent;
    object->reward = reward;
    first = people_random(c, &second);
    object->source_offset[0] = people_sar16((uint16_t)first, 3);
    object->source_offset[2] = object->source_offset[0];
    object->source_offset[1] = people_sar16((uint16_t)second, 3);
    return 1;
}

/* a craft at parameters 0..2 (low dwords), 3..5 (high), as given
 * before the allocation reuses them. */
uint16_t people_create_positioned(people_context *c)
{
    uint32_t at[6];
    fe2_object *object;
    unsigned n;

    for (n = 0; n < 6; ++n)
        at[n] = people_arg(c, n);
    if (!people_allocate(c, (uint8_t)people_arg(c, P_SETUP)))
        return 0;
    object = people_object_arg(c, P_CREATED);
    for (n = 0; n < 3; ++n) {
        object->position_low[n] = at[n];
        object->position_high[n] = at[3 + n];
    }
    object->behaviour_parent = c->s->player_handle;
    object->render_state = 0;
    object->basis_generation = 0;
    object->source_offset[2] = -10;
    return 1;
}

/* a craft of `model` near the object whose handle is parameter 2's
 * low byte. Parameter 5 seeds its name (+0x76, swapped halves), 3 and 4
 * are its motion sample; the offset added is parameters 0..5 as the
 * allocation and the services leave them. */
uint16_t people_create_related(people_context *c, uint16_t model)
{
    uint8_t handle = (uint8_t)people_arg(c, P2);
    uint32_t sample_low = people_arg(c, P3);
    uint32_t sample_high = people_arg(c, P4);
    uint32_t seed = people_arg(c, P5);
    char name[FE2_PEOPLE_NAME_BYTES];
    fe2_object *object;
    fe2_object *related;
    unsigned n;

    fe2_args_template(c->args, P_CURRENT, fe2_people_template);
    if (!people_allocate_model(c, MARKER_CONTRACT, model))
        return 0;
    object = people_object_arg(c, P_CREATED);
    object->target_seed = seed >> 16 | seed << 16;
    name[0] = 0;
    c->g->names_name(c->g->opaque, 0xf, seed, name);
    for (n = 0; n < FE2_OBJECT_NAME_BYTES; ++n) {
        object->name[n] = name[n];
        if (name[n] == 0)
            break;
    }
    fe2_args_objects(c->args, P_LIST, c->s->objects);
    people_set_object(c, P_CURRENT, object);
    related = people_lookup(c, handle);
    people_set_object(c, P_CREATED, related);
    if (related->flags & FLAG_COPY_BASIS) {
        for (n = 0; n < 3; ++n) {
            object->position_low[n] = related->position_low[n];
            object->position_high[n] = related->position_high[n];
        }
        for (n = 0; n < 9; ++n)
            object->orientation[n] = related->orientation[n];
        object->parent_handle = related->parent_handle;
    } else {
        object->parent_handle = related->handle;
    }
    object->station_slot = 0xff;
    if (people_market(c, 1, object->handle).result != 0) {
        people_release(c, object);
        return 0;
    }
    for (n = 0; n < 3; ++n)
        people_add_offset(object, n, people_arg(c, P0 + n), people_arg(c, P3 + n));
    object->behaviour = BEHAVIOUR_DOCKED;
    object->reward = (object->reward & ~0xffu) | 0x80;
    object->behaviour_target = 0;
    object->behaviour_parent = related->handle;
    object->orbit[0] = sample_low;
    object->orbit[1] = sample_high;
    c->g->attach(c->g->opaque, object);
    people_slot_flags(c, object, related->flags & FLAG_LOUD ? 0x4e : 0x02);
    return 1;
}

uint16_t fe2_people_create(fe2_people_state *state, const fe2_people_services *game,
                            fe2_call_args *args, uint16_t parent)
{
    static const uint8_t linked[4] = {8, 4, 2, 0xa};
    uint8_t mode = (uint8_t)args->arg[P_MODE].value;
    people_context c;

    people_init(&c, state, game, args);
    fe2_args_word(args, P_SETUP, 0xa);
    if (mode == 5)
        return people_create_positioned(&c);
    fe2_args_word(args, P_SETUP, 4);
    if (mode == 7)
        return people_create_positioned(&c);
    fe2_args_word(args, P_SETUP, 2);
    if (mode == 8)
        return people_create_positioned(&c);
    if (mode == 6)
        return people_create_related(&c, parent);
    if (mode >= 1 && mode <= 4)
        return people_create_linked(&c, linked[mode - 1], parent);
    return 0;
}
