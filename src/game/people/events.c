/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Events (0x76, 0x69b..0x916) and initialization (0x6e). */
#include "people_internal.h"

enum { EQUIPMENT_EVENT_BEACON = 24, METRIC_NEAR = 0x10, SPECIAL_NEAR = 0xf };

static fe2_people_pass people_pass(people_context *c)
{
    fe2_people_pass pass;

    pass.state = c->s;
    pass.game = c->g;
    pass.args = c->args;
    pass.cursor = 0;
    return pass;
}

/* The handle goes in as a word: the next one overwrites its NUL. */
static void people_append(fe2_people_pass *pass, const fe2_object *object)
{
    pass->state->event_ids[pass->cursor] = object->handle;
    pass->state->event_ids[pass->cursor + 1] = 0;
    ++pass->cursor;
}

/* the scaled distance from the player (parameter 9). */
static int16_t people_distance_from_ship(people_context *c, fe2_object *object)
{
    people_set_object(c, P_CREATED, c->s->ship);
    return c->g->scaled_difference(c->g->opaque, object, c->args).distance;
}

/* event beacons (marker 0x8), waiting event craft (model 0xba) of
 * this jump, and valued craft (marker 0xfb) fitted with equipment bit 24
 * near the player. */
void fe2_people_collect_object(void *context, fe2_object *object)
{
    fe2_people_pass *pass = context;
    people_context c;

    people_init(&c, pass->state, pass->game, pass->args);
    if (object->role != FE2_PEOPLE_CRAFT)
        return;
    if (object->map_marker == MARKER_EVENT) {
        people_append(pass, object);
        return;
    }
    if (object->model == MODEL_EVENT) {
        if ((int16_t)object->cargo_type >= 0 || object->hyperspace[0] != c.s->jump_value)
            return;
        object->cargo_type = (uint16_t)people_distance_from_ship(&c, object);
        people_append(pass, object);
        return;
    }
    if (object->map_marker != MARKER_VALUED)
        return;
    if (!(object->as.flight.equipment >> EQUIPMENT_EVENT_BEACON & 1))
        return;
    if (people_distance_from_ship(&c, object) >= METRIC_NEAR)
        return;
    people_append(pass, object);
}


static void people_collect(people_context *c)
{
    fe2_people_pass pass = people_pass(c);

    people_forget(c, P_CALLBACK);
    people_forget(c, P_CURSOR);
    c->s->event_ids[0] = 0;
    c->g->visit(c->g->opaque, c->s->objects, fe2_people_collect_object, &pass);
}

/* action 0xa on each collected craft. */
static void people_apply(people_context *c)
{
    unsigned i;

    fe2_args_objects(c->args, P_LIST, c->s->objects);
    for (i = 0; c->s->event_ids[i] != 0; ++i)
        people_slot_flags(c, people_lookup(c, c->s->event_ids[i]), 0x0a);
}

/* drives places the craft for the jump (a negative parameter 0
 * refuses: the craft is released); it waits in behaviour 0x3c near the
 * player, resuming its behaviour after. */
static void people_configure(people_context *c, fe2_object *object)
{
    fe2_people_reply reply;
    unsigned n;

    people_set_object(c, P_CURRENT, object);
    fe2_args_dword(c->args, P1, c->s->jump_distance);
    reply = c->g->hyperspace_arrival(c->g->opaque, c->args);
    for (n = 0; n < 3; ++n)
        fe2_args_dword(c->args, n, reply.value[n]);
    if ((int16_t)people_arg(c, P0) < 0) {
        people_release(c, object);
        return;
    }
    object->event_param = (uint16_t)people_arg(c, P2);
    object->event_delay = people_arg(c, P1);
    people_setup_near_ship(c, object);
    object->resume_behaviour = object->behaviour;
    object->behaviour = BEHAVIOUR_ARRIVAL_PENDING;
}

/* a waiting event craft: placed near the player (or set up afar
 * from a distance of 0xf), it goes to a market's station (behaviour 0x40,
 * resuming 0x20), or resumes 0x38 without one. */
static void people_configure_waiting(people_context *c, fe2_object *object)
{
    const fe2_people_market *market;

    if ((int16_t)object->cargo_type < SPECIAL_NEAR)
        people_setup_near_ship(c, object);
    else
        people_setup_distant(c, object);
    object->behaviour = BEHAVIOUR_ARRIVAL_COUNTDOWN;
    object->resume_behaviour = BEHAVIOUR_ADOPT_TARGET_PARENT;
    object->cargo_type = 0;
    if (people_select_market(c, &market))
        object->behaviour_target = market->station;
    else
        object->resume_behaviour = BEHAVIOUR_WAIT_AFTER_OFFSET;
}

/* reconfigure each collected craft for the jump, then populate. */
static void people_reconfigure(people_context *c)
{
    unsigned i;

    fe2_args_objects(c->args, P_LIST, c->s->objects);
    for (i = 0; c->s->event_ids[i] != 0; ++i) {
        fe2_object *object = people_lookup(c, c->s->event_ids[i]);

        object->hyperspace[0] = c->s->previous_system;
        object->parent_handle = 0;
        object->station_slot = 0;
        object->render_state = 0;
        if (object->map_marker == MARKER_EVENT) {
            if (object->model == MODEL_EVENT) {
                object->behaviour = object->resume_behaviour;
                object->model = object->stored_model;
            }
            people_configure(c, object);
        } else if (object->model == MODEL_EVENT) {
            people_configure_waiting(c, object);
        } else {
            people_configure(c, object);
        }
    }
    c->s->event_flags &= (uint8_t)~1u;
    people_populate(c);
}


void people_populate_after_event(people_context *c)
{
    if (c->s->mode == 8)
        return;
    c->s->event_flags |= 1;
    people_populate(c);
}

void fe2_people_event(fe2_people_state *state, const fe2_people_services *game,
                       fe2_call_args *args, uint16_t selector)
{
    people_context c;

    people_init(&c, state, game, args);
    switch (selector) {
    case FE2_PEOPLE_EVENT_COLLECT: people_collect(&c); break;
    case FE2_PEOPLE_EVENT_APPLY: people_apply(&c); break;
    case FE2_PEOPLE_EVENT_RECONFIGURE: people_reconfigure(&c); break;
    case FE2_PEOPLE_EVENT_POPULATE: people_populate_after_event(&c); break;
    default: break;
    }
}

void fe2_people_initialize(fe2_people_state *state, const fe2_people_services *game,
                            fe2_call_args *args, uint8_t request)
{
    people_context c;

    if (request != 3)
        return;
    people_init(&c, state, game, args);
    state->event_flags &= (uint8_t)~1u;
    people_populate(&c);
}
