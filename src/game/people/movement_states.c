/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Leaving, arrival and waiting. */
#include "people_internal.h"

/* leaving the parent: steer away while the probe (mode from
 * +0xd5's sign) gives more than 6, else market service 2 and follow. */
void people_update_leave_parent(people_context *c, fe2_object *object)
{
    people_use_capacity(c, object);
    people_lookup_parent(c, object);
    fe2_args_word(c->args, P_MODE, (uint16_t)(int16_t)(int8_t)object->control_class);
    fe2_args_word(c->args, P_MODE, c->g->relative_probe(c->g->opaque, object, c->args));
    if ((int16_t)people_arg(c, P_MODE) > 6) {
        c->g->clamp_controls(c->g->opaque, object, c->args);
        fe2_args_dword(c->args, P_MODE, c->s->update_step);
        c->g->steer_controls(c->g->opaque, object, c->args);
        return;
    }
    people_set_object(c, P_CURRENT, object);
    people_market(c, 2, object->handle);
    people_follow(c, object);
}

/* the craft arrives: its model waits in +0xba (0xba shows), action
 * 0xa, +0x58 = 8, and the motion sample advances by the delay (+0xce) in
 * the original's word order. */
void people_arrive(people_context *c, fe2_object *object)
{
    uint16_t low = (uint16_t)object->event_delay;
    uint16_t high = (uint16_t)(object->event_delay >> 16);
    uint64_t sample = (uint64_t)object->orbit[1] << 32 | object->orbit[0];

    object->controls[0] = 0;
    object->controls[1] = 0;
    object->controls[2] = 0;
    object->stored_model = object->model;
    object->model = MODEL_EVENT;
    people_slot_flags(c, object, 0x0a);
    object->model_parameter = 8;
    sample += (uint64_t)high | (uint64_t)low << 16 | (uint64_t)high << 32 |
              (uint64_t)(high & 0x8000 ? 0xffff : 0) << 48;
    object->orbit[0] = (uint32_t)sample;
    object->orbit[1] = (uint32_t)(sample >> 32);
}

static int64_t people_time_since(const people_context *c, const fe2_object *object)
{
    uint64_t now = (uint64_t)c->s->day << 32 | c->s->time_of_day;

    return (int64_t)(now - ((uint64_t)object->orbit[1] << 32 | object->orbit[0]));
}

/* arrival at +0x7e: 0x40, +0x74 cleared. */
void people_update_start_arrival_countdown(people_context *c, fe2_object *object)
{
    if (people_time_since(c, object) < 0)
        return;
    object->behaviour = BEHAVIOUR_ARRIVAL_COUNTDOWN;
    object->cargo_type = 0;
    people_arrive(c, object);
}

/* until +0x7e the countdown (+0x7a, +0x7c) shows an eighth of the
 * time left; then a launch effect object (kind 0xa: behaviour 0x6c, marker
 * 0xfe) and the craft becomes visible, resuming its behaviour (+0xcc). */
void people_update_complete_arrival(people_context *c, fe2_object *object)
{
    int64_t since = people_time_since(c, object);
    fe2_object *flash;

    if (since < 0) {
        uint64_t eighth = (uint64_t)(since >> 3);

        object->size_mantissa = (uint16_t)(eighth >> 16);
        object->size_exponent = (uint16_t)eighth;
        return;
    }
    people_set_object(c, P_CURRENT, object);
    fe2_args_word(c->args, P0, 0x0a);
    fe2_args_word(c->args, P1, 0);
    flash = people_object_arg(c, P_CREATED);
    if (c->g->allocate(c->g->opaque, c->args, &flash)) {
        uint64_t sample;

        people_set_object(c, P_CREATED, flash);
        flash->behaviour = BEHAVIOUR_LAUNCH_FLASH;
        flash->map_marker = MARKER_LAUNCHED;
        sample = ((uint64_t)flash->orbit[1] << 32 | flash->orbit[0]) + 0xff9e8e;
        flash->orbit[0] = (uint32_t)sample;
        flash->orbit[1] = (uint32_t)(sample >> 32);
        flash->cargo_type = 1;
    } else {
        people_set_object(c, P_CREATED, flash);
    }
    people_slot_flags(c, object, 0x4f);
    object->hyperspace[0] = 0;
    object->model_parameter = 0;
    object->model = object->stored_model;
    object->behaviour = object->resume_behaviour;
    object->velocity[0] = 0;
    object->velocity[1] = 0;
    object->velocity[2] = 0x71b7;
    object->event_param = 0;
    object->resume_behaviour = 0;
    object->event_pad = 0;
    object->event_delay = 0;
}

/* the target (+0xd4) becomes the parent; behaviour 0x0. A parent
 * with flag 0x10 gives a source offset: along its basis (flag 0x20) or
 * 0xfa0 ahead. */
void people_update_adopt_target_as_parent(people_context *c, fe2_object *object)
{
    fe2_object *parent;
    uint8_t flags;

    object->behaviour_parent = object->behaviour_target;
    parent = people_lookup(c, object->behaviour_parent);
    people_set_object(c, P_CREATED, parent);
    flags = (uint8_t)parent->flags;
    object->source_offset[0] = 0;
    object->source_offset[1] = 0;
    object->source_offset[2] = 0;
    object->behaviour = BEHAVIOUR_ESCORT_PARENT;
    if (flags & FLAG_ARMED) {
        if (flags & FLAG_COPY_BASIS) {
            unsigned row;

            fe2_args_word(c->args, P0, 0);
            for (row = 0; row < 3; ++row) {
                uint32_t product = (uint32_t)((int32_t)parent->orientation[3 * row + 1] * 0x1770);

                object->source_offset[row] = (int16_t)(uint16_t)(product << 1 >> 16);
            }
        } else {
            object->source_offset[2] = 0x0fa0;
        }
    }
    people_update_escort_parent(c, object);
}
