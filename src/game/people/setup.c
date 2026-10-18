/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Field setup and identity, event placement. */
#include "people_internal.h"

/* two random dwords (parameters 0, 1) for the setup service, then
 * clear +0x8a..+0x8f. */
void people_setup_fields(people_context *c, fe2_object *object)
{
    uint32_t second;
    uint32_t first = people_random(c, &second);

    fe2_args_dword(c->args, P0, first);
    fe2_args_dword(c->args, P1, second);
    c->g->setup(c->g->opaque, object, c->args);
    object->vector_delta[0] = 0;
    object->vector_delta[1] = 0;
    object->vector_delta[2] = 0;
}

/* a random seed (+0x76 holds it with its halves swapped) and the
 * name names makes from it, copied up to its NUL. */
void people_generate_identity(people_context *c, fe2_object *object, uint16_t type)
{
    char name[FE2_PEOPLE_NAME_BYTES];
    uint32_t seed = people_random(c, 0);
    unsigned i;

    object->target_seed = seed >> 16 | seed << 16;
    name[0] = 0;
    c->g->names_name(c->g->opaque, type, seed, name);
    for (i = 0; i < FE2_OBJECT_NAME_BYTES; ++i) {
        object->name[i] = name[i];
        if (name[i] == 0)
            break;
    }
}

static void people_setup_for(people_context *c, fe2_object *object, uint16_t kind)
{
    fe2_args_word(c->args, P_SETUP, 0x5000);
    fe2_args_word(c->args, P_MODE, kind);
    people_setup_fields(c, object);
}

/* set up as kind 0x17, then offset by the player's position. */
void people_setup_near_ship(people_context *c, fe2_object *object)
{
    const fe2_object *ship;
    unsigned axis;

    people_setup_for(c, object, 0x17);
    ship = c->s->ship;
    for (axis = 0; axis < 3; ++axis) {
        uint32_t low = object->position_low[axis] + ship->position_low[axis];

        object->position_high[axis] += ship->position_high[axis] +
                                       (low < object->position_low[axis] ? 1u : 0u);
        object->position_low[axis] = low;
    }
}


void people_setup_distant(people_context *c, fe2_object *object)
{
    people_setup_for(c, object, 0x33);
}
