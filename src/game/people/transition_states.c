/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Missiles, waiting, departures and effects. */
#include "people_internal.h"

static int64_t people_time_until(const people_context *c, const fe2_object *object)
{
    uint64_t now = (uint64_t)c->s->day << 32 | c->s->time_of_day;

    return (int64_t)(now - ((uint64_t)object->orbit[1] << 32 | object->orbit[0]));
}


void people_update_stationary_projectile(people_context *c, fe2_object *object)
{
    (void)c;
    (void)object;
}

/* a homing missile: wrecked after 0x3c updates; it homes on its
 * parent (mode 3) and hits it at 4 or less. */
void people_update_home_missile(people_context *c, fe2_object *object)
{
    if (++object->cargo_type > 0x3c) {
        people_spawn_wreck(c, object);
        return;
    }
    people_use_capacity(c, object);
    people_lookup_parent(c, object);
    fe2_args_word(c->args, P_MODE, 3);
    fe2_args_word(c->args, P_MODE, c->g->relative_motion(c->g->opaque, object, c->args));
    if ((int16_t)people_arg(c, P_MODE) <= 4) {
        people_hit(c, object);
        return;
    }
    fe2_args_dword(c->args, P2, people_arg(c, P2) + 0x1108);
    c->g->clamp_controls(c->g->opaque, object, c->args);
    c->g->apply_controls(c->g->opaque, object, c->args);
}


void people_update_consume_frame_capacity(people_context *c, fe2_object *object)
{
    people_use_capacity(c, object);
}

/* moved 20000000 along x (no carry), then waits. */
void people_update_offset_and_wait_for_event(people_context *c, fe2_object *object)
{
    object->position_low[0] += 0x1312d00;
    people_wait_for_event(c, object);
}

/* two tries at relationships: on success it hides as an event
 * craft (0x68, +0x74 and +0x7a 0xffff); else it seeks a market. */
void people_wait_for_event(people_context *c, fe2_object *object)
{
    if (people_prepare_relationships(c, object) || people_prepare_relationships(c, object)) {
        object->behaviour = BEHAVIOUR_HIDDEN_CRAFT;
        object->cargo_type = 0xffff;
        object->size_mantissa = 0xffff;
        people_arrive(c, object);
        return;
    }
    people_seek_market(c, object);
}

/* a market's station becomes the target (0x20); without markets
 * the craft is removed. */
void people_seek_market(people_context *c, fe2_object *object)
{
    const fe2_people_market *market;

    if (!people_select_market(c, &market)) {
        people_remove(c, object);
        return;
    }
    object->behaviour_target = people_object_arg(c, P_CURRENT)->handle;
    object->behaviour = BEHAVIOUR_ADOPT_TARGET_PARENT;
}

/* a launch flash: removed at +0x7e; until then the countdown shows
 * the complement of the time left << 8. */
void people_update_expire_launch_flash(people_context *c, fe2_object *object)
{
    int64_t since = people_time_until(c, object);
    uint32_t shown;

    if (since >= 0) {
        people_remove(c, object);
        return;
    }
    shown = ~((uint32_t)since << 8);
    object->size_mantissa = (uint16_t)(shown >> 16);
    object->size_exponent = (uint16_t)shown;
}

/* hidden until +0x7e (then removed); the countdown shows the
 * complement of an eighth of the time left. */
void people_update_expire_hidden_craft(people_context *c, fe2_object *object)
{
    int64_t since = people_time_until(c, object);
    uint32_t shown;

    if (since >= 0) {
        people_remove(c, object);
        return;
    }
    shown = ~(uint32_t)(uint64_t)(since >> 3);
    object->size_mantissa = (uint16_t)(shown >> 16);
    object->size_exponent = (uint16_t)shown;
}

/* an emitted object, removed from level 0x10. */
void people_update_remove_distant_emission(people_context *c, fe2_object *object)
{
    if ((int8_t)object->level < 0x10)
        return;
    --c->s->emitted_count;
    people_remove(c, object);
}

/* released from level 0xf. */
void people_update_remove_distant_effect(people_context *c, fe2_object *object)
{
    if ((int8_t)object->level >= 0x0f)
        people_release(c, object);
}

/* removed from level 0xd. */
void people_update_remove_distant_cargo(people_context *c, fe2_object *object)
{
    people_use_capacity(c, object);
    if ((int8_t)object->level >= 0x0d)
        people_remove(c, object);
}

/* the word at +0x11a counts down; below 0 it drifts (0x50). */
void people_update_countdown_to_drift(people_context *c, fe2_object *object)
{
    uint16_t count = (uint16_t)((object->trail | object->effect << 8) - 1);

    (void)c;
    object->trail = (uint8_t)count;
    object->effect = (uint8_t)(count >> 8);
    if ((int16_t)count < 0)
        object->behaviour = BEHAVIOUR_DRIFTING_CARGO;
}

/* a timed effect: released once updated, above level 0x14, or when
 * its time (+0x86, advancing by the update step << 8) reaches the time
 * step. */
void people_update_expire_timed_effect(people_context *c, fe2_object *object)
{
    if (object->update_countdown == 0 && (int8_t)object->level <= 0x14) {
        object->scalar += c->s->update_step << 8;
        if (object->scalar < c->s->time_step)
            return;
    }
    people_release(c, object);
}

/* escorting (mode from +0xd5's sign) beyond 0xa; nearer, patrol
 * (0x4), the player's escort reporting (message 0x98b9 with its seed and
 * reward). */
void people_update_report_escort_arrival(people_context *c, fe2_object *object)
{
    people_use_capacity(c, object);
    people_set_object(c, P_CURRENT, object);
    people_lookup_parent(c, object);
    fe2_args_word(c->args, P_MODE, (uint16_t)(int16_t)(int8_t)object->control_class);
    fe2_args_word(c->args, P_MODE, c->g->relative_motion(c->g->opaque, object, c->args));
    if ((int16_t)people_arg(c, P_MODE) > 0x0a) {
        c->g->clamp_controls(c->g->opaque, object, c->args);
        c->g->apply_controls(c->g->opaque, object, c->args);
        return;
    }
    if (object->behaviour_parent == c->s->player_handle) {
        fe2_args_dword(c->args, P3, object->target_seed >> 16 | object->target_seed << 16);
        fe2_args_word(c->args, P_MODE, 0);
        fe2_args_dword(c->args, P_CURSOR, object->reward);
        c->g->station_message(c->g->opaque, 0x98b9, c->args);
        c->g->check_music(c->g->opaque);
    }
    object->behaviour = BEHAVIOUR_PATROL_CRUISE;
}
