/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Relationships of a new craft. */
#include "people_internal.h"

/* chart relates the craft to the system (its result in +0xc6), catalogue
 * must rank it above 3, then chart and drives give its arrival (+0xca, +0xce;
 * a negative parameter 0 refuses). */
static int people_relate(people_context *c, fe2_object *object)
{
    fe2_people_reply reply;
    fe2_call_arg saved;
    unsigned n;

    fe2_args_dword(c->args, P1, c->s->current_system);
    people_set_object(c, P_CURRENT, object);
    reply = c->g->galactic_map_relationship(c->g->opaque, 1, c->args);
    fe2_args_dword(c->args, P0, reply.value[0]);
    if (reply.result == 0)
        return 0;
    object->hyperspace[0] = people_arg(c, P0);
    fe2_args_dword(c->args, P1, people_arg(c, P0));
    saved = c->args->arg[P1];
    reply = c->g->catalogue_query(c->g->opaque, 1, c->args);
    for (n = 0; n < 6; ++n)
        fe2_args_dword(c->args, n, reply.value[n]);
    c->args->arg[P1] = saved;
    if ((int8_t)reply.result <= 3)
        return 0;
    reply = c->g->galactic_map_relationship(c->g->opaque, 2, c->args);
    fe2_args_dword(c->args, P0, reply.value[0]);
    fe2_args_dword(c->args, P1, people_arg(c, P0));
    reply = c->g->hyperspace_arrival(c->g->opaque, c->args);
    for (n = 0; n < 3; ++n)
        fe2_args_dword(c->args, n, reply.value[n]);
    if ((int16_t)people_arg(c, P0) < 0)
        return 0;
    object->event_param = (uint16_t)people_arg(c, P2);
    object->event_delay = people_arg(c, P1);
    return 1;
}

/* four tries, parameter 2 being 1, 1, 0, 0 (the original returns
 * from the successful try straight to its caller). */
int people_prepare_relationships(people_context *c, fe2_object *object)
{
    static const uint16_t tries[4] = {1, 1, 0, 0};
    unsigned n;

    for (n = 0; n < 4; ++n) {
        fe2_args_word(c->args, P2, tries[n]);
        if (people_relate(c, object))
            return 1;
    }
    return 0;
}
