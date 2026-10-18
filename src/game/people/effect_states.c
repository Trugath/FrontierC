/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Effect, missile and hit handlers. */
#include "people_internal.h"
#include "game/model.h"

enum {
    MODEL_CARGO = 0xb8,                /* a cargo canister */
    MODEL_MISSILE = 0x10,
    FLIGHT_MOUNTS = 0x12,
    INSTALLED_AT = 4                   /* : installed[4] onwards */
};

/* a trail effect (kind 4) at the object, then as 0x50. */
void people_motion_emit_trail_and_drift(people_context *c, fe2_object *object, uint64_t change)
{
    people_spawn_effect_at(c, object, 4);
    people_motion_drift_and_collect_cargo(c, object, change);
}

/* the player's ship fits the canister in its last empty mount
 * (0xc0), using a unit of capacity. */
static int people_fit_canister(people_context *c)
{
    fe2_object *ship = c->s->ship;
    const uint8_t *model;
    int16_t mount;

    people_set_object(c, P_CREATED, ship);
    model = c->g->model(c->g->opaque, ship->model);
    mount = (int16_t)(fe2_model_word(model + fe2_model_word(model, FE2_MODEL_FLIGHT), FLIGHT_MOUNTS) - 1);
    for (; mount >= 0; --mount) {
        if (ship->as.flight.installed[INSTALLED_AT + mount] == 0) {
            ship->as.flight.installed[INSTALLED_AT + mount] = 0xc0;
            --c->s->capacity;
            return 1;
        }
    }
    return 0;
}

/* a canister drifts; scooped by the player (contact flag 0x10) it
 * becomes cargo (model 0xb8: commodity +0x74) or a mounted item, if there
 * is room. */
void people_motion_drift_and_collect_cargo(people_context *c, fe2_object *object, uint64_t change)
{
    fe2_people_contacts contacts;

    people_transform(c, object, change);
    c->g->linked_force(c->g->opaque, object, c->s->frame_step);
    contacts = c->g->scan_contacts(c->g->opaque, object);
    people_set_object(c, P_CREATED, contacts.contact);
    if (!(contacts.flags & 0x10))
        return;
    if (contacts.handle == c->s->player_handle) {
        if (c->s->cargo_used >= c->s->capacity)
            return;
        if (object->model == MODEL_CARGO) {
            uint16_t commodity = (uint16_t)(object->cargo_type << 1) >> 1;

            fe2_args_word(c->args, P1, (uint16_t)(object->cargo_type + 0x8e00));
            people_set_object(c, P_CURRENT, object);
            fe2_args_word(c->args, P_MODE, 0xffff);
            c->g->station_message(c->g->opaque, 0x98ee, c->args);
            c->g->play_effect(c->g->opaque, 0x11);
            ++c->s->cargo_used;
            ++c->s->cargo[commodity];
            c->g->post_event(c->g->opaque, 0x16, commodity);
        } else if (!people_fit_canister(c)) {
            return;
        }
    }
    people_remove(c, object);
}

/* a wreck (kind 9, model 0xbc, from the object as template:
 * behaviour 0x64, countdown and +0x74 0xffff, a random basis), then the
 * object's explosion sound and removal. */
void people_spawn_wreck(people_context *c, fe2_object *object)
{
    fe2_object *wreck = people_object_arg(c, P_CREATED);

    people_set_object(c, P_CURRENT, object);
    fe2_args_word(c->args, P0, 9);
    fe2_args_word(c->args, P1, 0xbc);
    if (c->g->allocate(c->g->opaque, c->args, &wreck)) {
        uint32_t second;
        uint32_t first;

        people_set_object(c, P_CREATED, wreck);
        wreck->role = FE2_PEOPLE_CRAFT;
        wreck->behaviour = BEHAVIOUR_BURNING_WRECK;
        wreck->size_mantissa = 0xffff;
        wreck->size_exponent = 0xffff;
        wreck->cargo_type = 0xffff;
        first = people_random(c, &second);
        c->s->basis_angle[0] = (uint16_t)first;
        c->s->basis_angle[1] = (uint16_t)second;
        c->g->basis(c->g->opaque, wreck);
    } else {
        people_set_object(c, P_CREATED, wreck);
    }
    people_play_sound(c, 6, object->level);
    people_remove(c, object);
}

/* the hit target (parameter 9) takes the object's damage (the
 * table word at byte model - 6); the object is wrecked if that destroys
 * it, else only removed. */
static void people_damage_target(people_context *c, fe2_object *object)
{
    uint16_t amount = people_table_word(T_DAMAGE + (uint16_t)(object->model - 6));

    fe2_args_byte(c->args, P_MODE, object->behaviour_target);
    if (c->g->damage(c->g->opaque, people_object_arg(c, P_CREATED), amount, c->args)) {
        people_spawn_wreck(c, object);
        return;
    }
    people_play_sound(c, 6, object->level);
    people_remove(c, object);
}

/* a missile reaching a contract's target (mission 5 or 6 in this
 * system whose target is the missile's parent, hit directly or through its
 * parent) completes the contract: the target turns to model 0xca (slot
 * flags 8) and a low-level hit damages the player. */
static void people_missile_hit(people_context *c, fe2_object *object)
{
    fe2_object *target = people_object_arg(c, P_CREATED);
    fe2_people_contract *contract = c->s->contracts;
    int16_t left = (int16_t)(c->s->contract_count - 1);
    fe2_object *body;

    for (; left >= 0; --left, ++contract) {
        uint8_t kind = contract->type & 0x0f;

        if ((kind == 5 || kind == 6) && contract->target_system == c->s->current_system &&
            contract->target != 0 && contract->target == object->behaviour_parent)
            break;
    }
    if (left < 0 || (contract->target != target->handle &&
                     people_lookup(c, contract->target)->parent_handle != target->handle)) {
        people_damage_target(c, object);
        return;
    }
    people_spawn_wreck(c, object);
    people_object_arg(c, P_CREATED)->model_parameter = 0x0a;
    contract->outcome = 0xff;
    body = people_lookup(c, contract->target);
    body->model = 0xca;
    body->model_parameter = 6;
    people_slot_flags(c, body, 8);
    if (people_object_arg(c, P_CREATED)->level <= 0x10) {
        fe2_args_byte(c->args, P_MODE, 0);
        c->g->damage(c->g->opaque, c->s->ship, 0xc8, c->args);
    }
}

/* the object hit its contact (parameter 9), unless that is its
 * target with +0x74 at most 2. */
void people_hit(people_context *c, fe2_object *object)
{
    fe2_object *target = people_object_arg(c, P_CREATED);

    people_set_object(c, P_CURRENT, object);
    if (target->handle == object->behaviour_target && (int16_t)object->cargo_type <= 2)
        return;
    if (object->model == MODEL_MISSILE)
        people_missile_hit(c, object);
    else
        people_damage_target(c, object);
}

/* the transform, a trail effect behind a moving missile (kind 0 at
 * the position less the velocity), then the contacts: a hit, or (with
 * game flag 4) a wreck for models other than 6, 0xe, 0x10 and 0xc without
 * flag 8. */
static void people_missile_motion(people_context *c, fe2_object *object, uint64_t change)
{
    fe2_people_contacts contacts;

    people_transform(c, object, change);
    if (object->cargo_type != 0 && object->model > 8) {
        unsigned axis;

        for (axis = 0; axis < 3; ++axis) {
            fe2_args_dword(c->args, P0 + axis, object->position_low[axis] - (uint32_t)object->velocity[axis]);
            fe2_args_dword(c->args, P3 + axis, object->position_high[axis]);
        }
        fe2_args_word(c->args, P_MODE, 0);
        people_set_object(c, P_CURRENT, object);
        c->g->spawn_effect(c->g->opaque, c->args);
    }
    contacts = c->g->scan_contacts(c->g->opaque, object);
    people_set_object(c, P_CREATED, contacts.contact);
    if (contacts.flags & 0x80) {
        people_hit(c, object);
        return;
    }
    if (!(c->s->game_flags & 4))
        return;
    switch (object->model) {
    case 0x10: case 0x0e: case 0x06:
        return;
    case 0x0c:
        if (!(c->s->game_flags & 8))
            return;
        break;
    default:
        break;
    }
    people_spawn_wreck(c, object);
}


void people_motion_apply_projectile_force(people_context *c, fe2_object *object, uint64_t change)
{
    c->g->linked_force(c->g->opaque, object, (uint32_t)change);
    people_missile_motion(c, object, change);
}

/* as the common motion's engine steps. */
void people_motion_integrate_homing_missile(people_context *c, fe2_object *object, uint64_t change)
{
    uint32_t step = (uint32_t)change;

    object->update_countdown = 0;
    c->g->linked_force(c->g->opaque, object, step);
    c->g->integrate_limited(c->g->opaque, object, step);
    fe2_args_dword(c->args, P_SETUP, step);
    fe2_args_dword(c->args, P_MODE, step);
    fe2_args_word(c->args, P5, 1);
    c->g->turn(c->g->opaque, object, c->args);
    people_missile_motion(c, object, change);
}
