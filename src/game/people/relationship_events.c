/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Relationship events (0x9a, 0x2404..0x25ad). */
#include "people_internal.h"

/* The handle the event is about (parameter 2's byte). */
static uint8_t people_event_handle(const people_context *c)
{
    return (uint8_t)people_arg(c, P2);
}

/* the event's craft seeks a market (a police craft, marker 0xa,
 * clearing the launch latch first); parameters 2 and 9 are kept. */
static void people_reroute(people_context *c, fe2_object *object)
{
    fe2_call_arg handle = c->args->arg[P2];
    fe2_call_arg created = c->args->arg[P_CREATED];

    if (object->map_marker == MARKER_STATION_SEED) {
        c->s->launch_phase = 0;
        c->s->police_flags &= 0xfe;
    }
    people_seek_market(c, object);
    c->args->arg[P2] = handle;
    c->args->arg[P_CREATED] = created;
}

/* a craft whose parent is the event's takes that object's target
 * (parameter 9's +0xd4) as its parent: 0x14, or rerouted without one. */
void people_reroute_to_event_target(people_context *c, fe2_object *object)
{
    if (people_event_handle(c) != object->behaviour_parent)
        return;
    object->behaviour_parent = people_object_arg(c, P_CREATED)->behaviour_target;
    if (object->behaviour_parent == 0)
        people_reroute(c, object);
    else
        object->behaviour = BEHAVIOUR_CHOOSE_PATROL_OFFSET;
}


void people_reroute_on_parent_event(people_context *c, fe2_object *object)
{
    if (people_event_handle(c) == object->behaviour_parent)
        people_reroute(c, object);
}

/* a missile whose parent is the event's runs out; one targeting
 * it loses its target. */
void people_expire_or_untarget_missile(people_context *c, fe2_object *object)
{
    uint8_t handle = people_event_handle(c);

    if (handle == object->behaviour_parent)
        object->cargo_type = 0x3c;
    if (handle == object->behaviour_target)
        object->behaviour_target = 0;
}

/* for the player's object (parameter 7's byte), offences event 0xa
 * once (motion flag 3). */
void people_report_player_offence_once(people_context *c, fe2_object *object)
{
    if ((uint8_t)people_arg(c, P_MODE) != c->s->player_handle)
        return;
    if (c->s->motion_flags & 8)
        return;
    c->s->motion_flags |= 8;
    people_set_object(c, P_CURRENT, object);
    c->g->offences_event(c->g->opaque, 0x0a, c->args);
}

/* the new parent (parameter 7's byte), patrol (0x4). */
void people_assign_patrol_parent(people_context *c, fe2_object *object)
{
    object->behaviour_parent = (uint8_t)people_arg(c, P_MODE);
    object->behaviour = BEHAVIOUR_PATROL_CRUISE;
    people_report_player_offence_once(c, object);
}

/* market service 2 first (parameters 7 and 9 kept). */
void people_release_market_and_assign_parent(people_context *c, fe2_object *object)
{
    fe2_call_arg mode = c->args->arg[P_MODE];
    fe2_call_arg created = c->args->arg[P_CREATED];

    people_set_object(c, P_CURRENT, object);
    people_set_object(c, P_CREATED, people_lookup(c, object->behaviour_parent));
    people_market(c, 2, object->handle);
    c->args->arg[P_CREATED] = created;
    fe2_args_word(c->args, P_MODE, (uint16_t)mode.value);
    people_assign_patrol_parent(c, object);
}

/* a random turn, then tracking (0xc). */
void people_turn_and_track(people_context *c, fe2_object *object)
{
    uint16_t turn = (uint16_t)people_sar16((uint16_t)people_random(c, 0), 6);

    object->scalar = (object->scalar & 0xffffu) | (uint32_t)(uint16_t)((object->scalar >> 16) + turn) << 16;
    object->behaviour = BEHAVIOUR_TRACK_PARENT;
}

/* depart now (0x70; a contract craft's +0xee byte 0x40). */
void people_depart_immediately(people_context *c, fe2_object *object)
{
    if (object->map_marker == MARKER_CONTRACT)
        object->reward = (object->reward & ~0xffu) | 0x40;
    object->behaviour = BEHAVIOUR_DEPARTURE_REQUEST;
    object->orbit[0] = c->s->time_of_day;
    object->orbit[1] = c->s->day;
    people_report_player_offence_once(c, object);
}


void people_grant_departure_clearance(people_context *c, fe2_object *object)
{
    people_slot_flags(c, object, 0x4f);
    object->behaviour = BEHAVIOUR_LAUNCHING;
}


void fe2_people_relationship_object(void *context, fe2_object *object)
{
    fe2_people_pass *pass = context;
    people_context c;

    if (object->role != FE2_PEOPLE_CRAFT)
        return;
    people_init(&c, pass->state, pass->game, pass->args);
    people_behaviour_of(object)->walk(&c, object);
}

void fe2_people_relationship_event(fe2_people_state *state, const fe2_people_services *game,
                                    fe2_call_args *args, fe2_object *object, uint8_t handle,
                                    uint16_t form)
{
    fe2_people_pass pass;
    people_context c;

    people_init(&c, state, game, args);
    if (form != 0) {
        if (object->role == FE2_PEOPLE_CRAFT)
            people_behaviour_of(object)->callback(&c, object);
        return;
    }
    handle = people_resolve_related(&c, object, handle);
    people_set_object(&c, P_CREATED, object);
    fe2_args_word(args, P1, form);
    fe2_args_byte(args, P2, handle);
    people_forget(&c, P_CALLBACK);
    pass.state = state;
    pass.game = game;
    pass.args = args;
    pass.cursor = 0;
    game->visit(game->opaque, state->objects, fe2_people_relationship_object, &pass);
}
