/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Contract targets, departure and launch. */
#include "people_internal.h"

/* a contract craft (marker 0xc) of the player's (parameter 7's
 * byte) matching a contract by its seed marks the contract with +0xee's
 * byte. Returns the original's AL: `handle` for other craft, parameter
 * 7's byte, +0xee's byte on a match, else the seed's third byte. */
uint8_t people_resolve_related(people_context *c, fe2_object *object, uint8_t handle)
{
    fe2_people_contract *contract = c->s->contracts;
    uint32_t seed;
    int16_t left;

    if (object->map_marker != MARKER_CONTRACT)
        return handle;
    handle = (uint8_t)people_arg(c, P_MODE);
    if (handle != c->s->player_handle)
        return handle;
    left = (int16_t)(c->s->contract_count - 1);
    if (left < 0)
        return handle;
    seed = object->target_seed >> 16 | object->target_seed << 16;
    for (; left >= 0; --left, ++contract) {
        if (contract->target_seed == seed) {
            contract->outcome = (uint8_t)object->reward;
            return (uint8_t)object->reward;
        }
    }
    return (uint8_t)seed;
}

/* when the deadline (+0x7e) has passed, depart (0x70) from now. */
void people_update_wait_for_departure(people_context *c, fe2_object *object)
{
    uint64_t deadline = (uint64_t)object->orbit[1] << 32 | object->orbit[0];
    uint64_t now = (uint64_t)c->s->day << 32 | c->s->time_of_day;

    if ((int64_t)(deadline - now) >= 0)
        return;
    object->orbit[0] = c->s->time_of_day;
    object->orbit[1] = c->s->day;
    object->behaviour = BEHAVIOUR_DEPARTURE_REQUEST;
    people_update_request_departure(c, object);
}

/* market service 5 clears the departure: slot flags 0x4f, 0x78. */
void people_update_request_departure(people_context *c, fe2_object *object)
{
    people_lookup_parent(c, object);
    people_set_object(c, P_CURRENT, object);
    if (people_market(c, 5, object->handle).result == 0)
        return;
    people_slot_flags(c, object, 0x4f);
    object->behaviour = BEHAVIOUR_LAUNCHING;
}

/* once the parent's docking state is 0xfe or 0, the craft leaves
 * (0x1c) along the parent's basis (a random spread) or straight out. */
void people_update_launch_from_parent(people_context *c, fe2_object *object)
{
    fe2_object *parent = people_lookup_parent(c, object);
    uint8_t flags = (uint8_t)parent->flags;

    if (parent->as.body.docking_state != 0xfe && parent->as.body.docking_state != 0)
        return;
    object->behaviour = BEHAVIOUR_LEAVE_PARENT;
    if (flags & FLAG_COPY_BASIS) {
        int16_t spread = (int16_t)((uint16_t)people_random(c, 0) & 0xfff);
        unsigned row;

        fe2_args_word(c->args, P0, 0x1388);
        for (row = 0; row < 3; ++row) {
            const int16_t *axis = &parent->orientation[3 * row];
            uint32_t sum = (uint32_t)((int32_t)axis[0] * 0x1388) + (uint32_t)((int32_t)axis[1] * 0x3a98) +
                           (uint32_t)((int32_t)axis[2] * spread);

            object->source_offset[row] = (int16_t)(uint16_t)(sum << 1 >> 16);
        }
    } else {
        object->source_offset[2] = 0x1388;
        object->source_offset[2] = (int16_t)(0x1388 + ((uint16_t)people_random(c, 0) & 0x1ff));
        object->source_offset[0] = 0;
        object->source_offset[1] = 0;
    }
    object->velocity[0] = 0;
    object->velocity[1] = 0;
    object->velocity[2] = 0;
    people_update_leave_parent(c, object);
}
