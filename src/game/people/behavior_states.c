/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Following, patrol and approach behaviours. */
#include "people_internal.h"

static void people_clear_controls(fe2_object *object)
{
    object->controls[0] = 0;
    object->controls[1] = 0;
    object->controls[2] = 0;
}

/* The probe or relative motion service with parameter 7 as `mode`; the
 * mode it leaves is in parameter 7. */
static int16_t people_probe(people_context *c, fe2_object *object, uint16_t mode)
{
    fe2_args_word(c->args, P_MODE, mode);
    fe2_args_word(c->args, P_MODE, c->g->relative_probe(c->g->opaque, object, c->args));
    return (int16_t)people_arg(c, P_MODE);
}

static int16_t people_relative(people_context *c, fe2_object *object, uint16_t mode)
{
    fe2_args_word(c->args, P_MODE, mode);
    fe2_args_word(c->args, P_MODE, c->g->relative_motion(c->g->opaque, object, c->args));
    return (int16_t)people_arg(c, P_MODE);
}

static void people_steer(people_context *c, fe2_object *object)
{
    c->g->clamp_controls(c->g->opaque, object, c->args);
    c->g->apply_controls(c->g->opaque, object, c->args);
}

/* escorting the parent: steer while the probe gives more than 5. */
void people_update_escort_parent(people_context *c, fe2_object *object)
{
    people_use_capacity(c, object);
    people_lookup_parent(c, object);
    if (people_probe(c, object, (uint16_t)(int16_t)(int8_t)object->control_class) > 5) {
        people_steer(c, object);
        return;
    }
    people_follow(c, object);
}

/* no target: wait for an event; another target: take it as the
 * parent (0x20); the parent itself: engage it. */
void people_follow(people_context *c, fe2_object *object)
{
    uint8_t target = object->behaviour_target;

    if (target == 0) {
        people_wait_for_event(c, object);
        return;
    }
    if (target != object->behaviour_parent) {
        people_update_adopt_target_as_parent(c, object);
        return;
    }
    people_engage_parent(c, object, target);
}

/* an armed parent (parameter 9, flag 0x10) lets market service 6 (with
 * `handle`) place the craft: its source offset is market's offsets / 1024,
 * and it attacks (0x80). */
void people_engage_parent(people_context *c, fe2_object *object, uint8_t handle)
{
    unsigned axis;

    if (!(people_object_arg(c, P_CREATED)->flags & FLAG_ARMED))
        return;
    people_set_object(c, P_CURRENT, object);
    if (people_market(c, 6, handle).result != 0)
        return;
    for (axis = 0; axis < 3; ++axis)
        object->source_offset[axis] = (int16_t)(uint16_t)people_sar32(people_arg(c, P0 + axis), 10);
    people_play_sound(c, 0x0c, object->level);
    object->behaviour = BEHAVIOUR_RETURN_TO_PARENT;
}

/* returning to the parent: turn toward it while the relative
 * motion (mode 0xfffe) stays positive (or at time step 0x3e2); then the
 * contact transition, the parent's basis (or the identity) and attach. */
void people_update_return_to_parent(people_context *c, fe2_object *object)
{
    fe2_object *parent;
    unsigned n;

    people_use_capacity(c, object);
    people_lookup_parent(c, object);
    if (people_relative(c, object, 0xfffe) > 0 || c->s->time_step == 0x3e2) {
        fe2_args_word(c->args, P_MODE, 0xffff);
        c->g->rates_toward(c->g->opaque, object, people_object_arg(c, P_CREATED), c->args);
        c->g->clamp_controls(c->g->opaque, object, c->args);
        return;
    }
    people_contact_transition(c, object);
    parent = people_object_arg(c, P_CREATED);
    if (parent->flags & FLAG_COPY_BASIS) {
        for (n = 0; n < 9; ++n)
            object->orientation[n] = parent->orientation[n];
    } else {
        for (n = 0; n < 9; ++n)
            object->orientation[n] = n % 4 == 0 ? 0x7f00 : 0;
    }
    c->g->attach(c->g->opaque, object);
}

/* docking: unless the parent's docking state is 3, market service 4
 * docks it (slot flags 0x4e or 0x2), 0x84 with a deadline two days and a
 * random time ahead. */
void people_update_dock_at_parent(people_context *c, fe2_object *object)
{
    fe2_object *parent = people_lookup_parent(c, object);
    uint64_t deadline;

    if (parent->as.body.docking_state == 3)
        return;
    people_set_object(c, P_CURRENT, object);
    people_market(c, 4, object->handle);
    people_slot_flags(c, object, people_object_arg(c, P_CREATED)->flags & FLAG_LOUD ? 0x4e : 0x02);
    object->behaviour = BEHAVIOUR_DOCKED;
    object->behaviour_target = 0;
    deadline = ((uint64_t)c->s->day << 32 | c->s->time_of_day) + people_random(c, 0) + ((uint64_t)2 << 32);
    object->orbit[0] = (uint32_t)deadline;
    object->orbit[1] = (uint32_t)(deadline >> 32);
}

/* when the station's docking state is 0xfe or 0, patrol (0x4). */
void people_update_wait_for_station_clearance(people_context *c, fe2_object *object)
{
    uint8_t state;

    people_use_capacity(c, object);
    state = c->s->station->as.body.docking_state;
    if (state == 0xfe || state == 0)
        object->behaviour = BEHAVIOUR_PATROL_CRUISE;
}

/* a random source offset / 8, then 0x10. */
void people_update_choose_patrol_offset(people_context *c, fe2_object *object)
{
    uint32_t second;
    uint32_t first = people_random(c, &second);

    object->source_offset[0] = people_sar16((uint16_t)first, 3);
    object->source_offset[1] = people_sar16((uint16_t)second, 3);
    object->source_offset[2] = people_sar16((uint16_t)first, 3);
    object->behaviour = BEHAVIOUR_APPROACH_PARENT;
    people_update_approach_parent(c, object);
}

/* closing on the parent (mode 1) from 0xa on; nearer, patrol (0x4)
 * with, for the player's parent outside mode 8, commander's alarm (message
 * 0x98ac, sound 0x1e and the music). A docking parent (0x84) stops it. */
void people_update_approach_parent(people_context *c, fe2_object *object)
{
    people_use_capacity(c, object);
    if (people_lookup_parent(c, object)->behaviour == BEHAVIOUR_DOCKED) {
        people_clear_controls(object);
        return;
    }
    if (people_relative(c, object, 1) >= 0x0a) {
        people_steer(c, object);
        return;
    }
    object->behaviour = BEHAVIOUR_PATROL_CRUISE;
    if (object->behaviour_parent == c->s->player_handle && c->s->mode != 8 && c->g->commander_check(c->g->opaque) != 0) {
        fe2_args_word(c->args, P_MODE, 0xffff);
        c->g->station_message(c->g->opaque, 0x98ac, c->args);
        c->g->play_effect(c->g->opaque, 0x1e);
        if (c->s->music_enabled & c->s->music_preference)
            c->g->select_music(c->g->opaque, 0xfffe);
        else
            c->g->check_music(c->g->opaque);
    }
    object->source_offset[0] = 0;
    object->source_offset[1] = 0;
    object->source_offset[2] = 0;
    people_patrol(c, object);
}


void people_update_patrol_parent(people_context *c, fe2_object *object)
{
    people_use_capacity(c, object);
    people_patrol(c, object);
}

/* patrolling near the parent (probe mode 1): beyond 0xb pick a new
 * offset (0x14); 7..8 slow (0x8), 9..0xb cruise (0x4); nearer, approach. */
void people_patrol(people_context *c, fe2_object *object)
{
    int16_t range;

    c->s->event_flags |= 2;
    people_lookup_parent(c, object);
    range = people_probe(c, object, 1);
    if (range > 0x0b) {
        people_update_choose_patrol_offset(c, object);
        return;
    }
    if (range > 6) {
        object->behaviour = range > 8 ? BEHAVIOUR_PATROL_CRUISE : BEHAVIOUR_PATROL_HOLD;
        people_steer(c, object);
        return;
    }
    people_approach(c, object);
}

/* by the range in parameter 7: track (0xc) at 4 or less; else
 * hold (0x8) and face the steering direction (0x4 unless the basis's
 * third column is within it), firing at random. */
void people_approach(people_context *c, fe2_object *object)
{
    int32_t facing;
    uint16_t roll;

    if ((int16_t)people_arg(c, P_MODE) <= 4) {
        object->behaviour = BEHAVIOUR_TRACK_PARENT;
        people_track(c, object);
        return;
    }
    people_clear_controls(object);
    object->behaviour = BEHAVIOUR_PATROL_HOLD;
    c->g->apply_controls(c->g->opaque, object, c->args);
    facing = (int32_t)((uint32_t)((int32_t)object->steering[0] * object->orientation[2]) +
                       (uint32_t)((int32_t)object->steering[1] * object->orientation[5]) +
                       (uint32_t)((int32_t)object->steering[2] * object->orientation[8]));
    if (facing <= 0x60000000)
        object->behaviour = BEHAVIOUR_PATROL_CRUISE;
    roll = (uint16_t)people_random(c, 0);
    if (roll >= 0x2000)
        return;
    fe2_args_byte(c->args, P_MODE, object->behaviour_parent);
    if (!c->g->deploy_slot(c->g->opaque, object, (uint16_t)((roll & 7) + 1), c->args))
        c->g->consume_effect(c->g->opaque, object);
}

/* the scaled difference to the parent steers; at 4 or less hold
 * at full rate, at 5 hold with the controls cleared, beyond approach. */
void people_track(people_context *c, fe2_object *object)
{
    fe2_people_metric metric;

    people_lookup_parent(c, object);
    metric = c->g->scaled_difference(c->g->opaque, object, c->args);
    object->steering[0] = metric.x;
    object->steering[1] = metric.y;
    object->steering[2] = metric.z;
    object->steering_shift = (uint16_t)metric.distance;
    if (metric.distance <= 4) {
        fe2_args_dword(c->args, P0, 0);
        fe2_args_dword(c->args, P1, 0);
        fe2_args_dword(c->args, P2, 0x7fff);
        c->g->clamp_controls(c->g->opaque, object, c->args);
        c->g->hold_controls(c->g->opaque, object, c->args);
        return;
    }
    if (metric.distance <= 5) {
        people_clear_controls(object);
        c->g->hold_controls(c->g->opaque, object, c->args);
        return;
    }
    object->behaviour = BEHAVIOUR_PATROL_CRUISE;
    fe2_args_word(c->args, P_MODE, (uint16_t)metric.distance);
    people_approach(c, object);
}


void people_update_track_parent(people_context *c, fe2_object *object)
{
    people_use_capacity(c, object);
    people_track(c, object);
}

/* hold at full rate, an effect at random, and random spin. */
void people_update_spin_and_emit_effects(people_context *c, fe2_object *object)
{
    uint32_t value;

    people_use_capacity(c, object);
    fe2_args_dword(c->args, P0, 0);
    fe2_args_dword(c->args, P1, 0);
    fe2_args_dword(c->args, P2, 0x7fff);
    c->g->clamp_controls(c->g->opaque, object, c->args);
    c->g->hold_controls(c->g->opaque, object, c->args);
    value = people_random(c, 0);
    if ((int32_t)value < 0)
        c->g->activate_effect(c->g->opaque, object);
    object->scalar = (object->scalar & 0xffffu) | (uint32_t)(uint16_t)((object->scalar >> 16) + (value & 0xff)) << 16;
    value = people_random(c, 0);
    object->scalar = (object->scalar & 0xffff0000u) | (uint16_t)(object->scalar + (value & 0xff));
}
