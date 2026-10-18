/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Update dispatch  and the common motion. */
#include "people_internal.h"

void people_update_none(people_context *c, fe2_object *object)
{
    (void)c;
    (void)object;
}

void people_motion_none(people_context *c, fe2_object *object, uint64_t change)
{
    (void)c;
    (void)object;
    (void)change;
}

/* the capacity used this frame comes off +0x119; running out
 * selects a new frame and refills it to 0x13. */
void people_use_capacity(people_context *c, fe2_object *object)
{
    uint8_t used = c->g->capacity_used(c->g->opaque, object, c->s->update_step);
    uint8_t left = (uint8_t)(object->flags >> 8);

    object->flags = (uint16_t)((object->flags & 0xffu) | (uint16_t)(uint8_t)(left - used) << 8);
    if (used > left) {
        c->g->select_frame(c->g->opaque, object);
        object->flags = (uint16_t)((object->flags & 0xffu) | 0x13u << 8);
    }
}

/* the cooldown runs down by the frame step (cleared from 0x10000);
 * +0x74 falls by half the step (to 0) in behaviours to 0x28 and rises by it
 * (to 0xffff) above 0x6c, 0xffff staying. */
static void people_run_timers(people_context *c, fe2_object *object)
{
    uint32_t half = c->s->frame_step >> 1;

    if (object->cooldown != 0) {
        if (c->s->frame_step >> 16 != 0 || object->cooldown < (uint16_t)c->s->frame_step)
            object->cooldown = 0;
        else
            object->cooldown = (uint16_t)(object->cooldown - (uint16_t)c->s->frame_step);
    }
    if (object->behaviour > 0x6c) {
        uint32_t sum;

        if (object->cargo_type == 0xffff)
            return;
        sum = object->cargo_type + half;
        object->cargo_type = sum > 0xffff || sum < half ? 0xffff : (uint16_t)sum;
    } else if (object->behaviour <= 0x28 && object->cargo_type != 0) {
        int64_t left = (int64_t)object->cargo_type - half;

        object->cargo_type = left < 0 ? 0 : (uint16_t)left;
    }
}

/* market service 3 decides; on a refusal the object engages its
 * reference (parameter 9) with handle 0, otherwise it turns to 0x74. */
void people_contact_transition(people_context *c, fe2_object *object)
{
    people_set_object(c, P_CURRENT, object);
    if (people_market(c, 3, object->handle).result == 0) {
        people_engage_parent(c, object, 0);
        return;
    }
    object->behaviour = BEHAVIOUR_DOCKING;
}

/* the flight engine's steps over the change's low dword, the
 * transform of the change, the timers, the trail and the contacts (a
 * craft returning in 0x80 with contact flags 0x60 may dock). */
void people_common_motion(people_context *c, fe2_object *object, uint64_t change)
{
    uint32_t step = (uint32_t)change;
    fe2_people_contacts contacts;

    object->update_countdown = 0;
    c->g->linked_force(c->g->opaque, object, step);
    c->g->integrate_limited(c->g->opaque, object, step);
    fe2_args_dword(c->args, P_SETUP, step);
    fe2_args_word(c->args, P5, 1);
    fe2_args_dword(c->args, P_MODE, step);
    c->g->turn(c->g->opaque, object, c->args);
    people_transform(c, object, change);
    people_run_timers(c, object);
    if (object->trail != 0)
        people_spawn_effect_at(c, object, 0);
    contacts = c->g->scan_contacts(c->g->opaque, object);
    people_set_object(c, P_CREATED, contacts.contact);
    if (contacts.flags == 0 || object->behaviour != BEHAVIOUR_RETURN_TO_PARENT)
        return;
    if ((contacts.flags & 0x60) != 0x60)
        return;
    people_contact_transition(c, object);
}


void people_motion_patrol_and_activate_slot(people_context *c, fe2_object *object, uint64_t change)
{
    people_common_motion(c, object, change);
    c->g->activate_slot(c->g->opaque, object, 0);
}

void fe2_people_motion(fe2_people_state *state, const fe2_people_services *game,
                        fe2_call_args *args, fe2_object *object, uint64_t sample)
{
    uint64_t previous = (uint64_t)object->orbit[1] << 32 | object->orbit[0];
    people_context c;

    people_init(&c, state, game, args);
    object->orbit[0] = (uint32_t)sample;
    object->orbit[1] = (uint32_t)(sample >> 32);
    people_behaviour_of(object)->motion(&c, object, sample - previous);
}

void fe2_people_update(fe2_people_state *state, const fe2_people_services *game,
                        fe2_call_args *args, fe2_object *object, uint32_t step)
{
    people_context c;

    people_init(&c, state, game, args);
    state->update_step = step;
    people_behaviour_of(object)->update(&c, object);
}
