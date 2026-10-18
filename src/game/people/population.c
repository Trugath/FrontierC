/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* System population. */
#include "people_internal.h"

enum { RELATED_CAP = 0x19, GROUP_NEAR = 100000, GROUP_FAR = 200000 };

/* a random market; its station is looked up into parameter 8
 * (parameter 10 is the record's address in the original). */
int people_select_market(people_context *c, const fe2_people_market **market)
{
    const fe2_people_market *chosen;

    if (c->s->market_count == 0)
        return 0;
    chosen = &c->s->markets[people_random_below(c, c->s->market_count)];
    people_forget(c, P_CURSOR);
    fe2_args_objects(c->args, P_LIST, c->s->objects);
    people_set_object(c, P_CURRENT, people_lookup(c, chosen->station));
    *market = chosen;
    return 1;
}

/* the n-th flagged market (n random below the flagged count); the
 * scan has no end in the original. */
static const fe2_people_market *people_select_flagged(people_context *c)
{
    const fe2_people_market *market = c->s->markets;
    int16_t pick;

    if (c->s->flagged_count == 0)
        return 0;
    pick = (int16_t)people_random_below(c, c->s->flagged_count);
    for (;; ++market)
        if ((market->flags & 0x10) && --pick < 0)
            break;
    people_forget(c, P_CURSOR);
    return market;
}

/* The 64-bit game time plus a random dword shifted left `shift`. */
static void people_time_ahead(people_context *c, fe2_object *object, unsigned shift)
{
    uint64_t ahead = (uint64_t)people_random(c, 0) << shift;
    uint64_t time = ((uint64_t)c->s->day << 32 | c->s->time_of_day) + ahead;

    object->orbit[0] = (uint32_t)time;
    object->orbit[1] = (uint32_t)(time >> 32);
}

/* a trader bound for a market's station (behaviour 0x3c, then 0x20);
 * the relationship services must accept it. */
static void people_spawn_trader(people_context *c)
{
    const fe2_people_market *market;
    fe2_object *object;

    if (!people_allocate(c, 1))
        return;
    object = people_object_arg(c, P_CREATED);
    fe2_args_word(c->args, P_SETUP, 0x5000);
    fe2_args_word(c->args, P_MODE, 0x33);
    people_setup_fields(c, object);
    object->behaviour = BEHAVIOUR_ARRIVAL_PENDING;
    object->resume_behaviour = BEHAVIOUR_ADOPT_TARGET_PARENT;
    if (!people_select_market(c, &market)) {
        people_release(c, object);
        return;
    }
    people_set_object(c, P_CREATED, people_object_arg(c, P_CURRENT));
    people_set_object(c, P_CURRENT, object);
    object->behaviour_target = market->station;
    people_time_ahead(c, object, 2);
    if (!people_prepare_relationships(c, object)) {
        people_release(c, object);
        return;
    }
    people_slot_flags(c, object, 2);
}

/* a craft docked at (or beside) a market's station, leaving it
 * (behaviour 0x84). */
static void people_spawn_docked(people_context *c)
{
    const fe2_people_market *market;
    fe2_object *object;
    fe2_object *station;
    unsigned n;

    if (!people_allocate(c, 1))
        return;
    object = people_object_arg(c, P_CREATED);
    if (!people_select_market(c, &market)) {
        people_release(c, object);
        return;
    }
    station = people_object_arg(c, P_CURRENT);
    people_set_object(c, P_CREATED, station);
    people_set_object(c, P_CURRENT, object);
    if (station->flags & FLAG_COPY_BASIS) {
        for (n = 0; n < 3; ++n) {
            object->position_low[n] = station->position_low[n];
            object->position_high[n] = station->position_high[n];
        }
        for (n = 0; n < 9; ++n)
            object->orientation[n] = station->orientation[n];
        object->parent_handle = station->parent_handle;
    } else {
        object->parent_handle = station->handle;
    }
    object->station_slot = 0xff;
    if (people_market(c, 1, object->handle).result != 0) {
        people_release(c, object);
        return;
    }
    for (n = 0; n < 3; ++n) {
        uint32_t low = object->position_low[n] + people_arg(c, P0 + n);

        object->position_high[n] += people_arg(c, P3 + n) + (low < object->position_low[n] ? 1u : 0u);
        object->position_low[n] = low;
    }
    object->behaviour = BEHAVIOUR_DOCKED;
    object->behaviour_target = 0;
    object->behaviour_parent = station->handle;
    people_time_ahead(c, object, 4);
    c->g->attach(c->g->opaque, object);
    people_slot_flags(c, object, station->flags & FLAG_LOUD ? 0x4e : 0x02);
}

/* a craft (marker 0xb, behaviour 0x2c) for a flagged market. */
static void people_spawn_flagged(people_context *c)
{
    const fe2_people_market *market = people_select_flagged(c);
    fe2_object *object;

    if (market == 0 || !people_allocate(c, 0x0b))
        return;
    object = people_object_arg(c, P_CREATED);
    people_set_object(c, P_CURRENT, object);
    object->control_class = 0xff;
    fe2_args_word(c->args, P_SETUP, 0x5000);
    fe2_args_word(c->args, P_MODE, 0x18);
    people_setup_fields(c, object);
    object->behaviour = BEHAVIOUR_INACTIVE_RELATIONSHIP;
    object->parent_handle = market->station;
}

/* a value from the variant table (`bias` 0 or 8 bytes in). */
static void people_assign_value(people_context *c, fe2_object *object, unsigned bias)
{
    unsigned at = bias + ((uint16_t)people_random(c, 0) & 0x1e);

    object->reward = people_table_word(T_VARIANTS + at);
    object->map_marker = MARKER_VALUED;
}

/* Allocation then the shared setup of a valued craft (behaviour 0x14). */
static fe2_object *people_spawn_valued(people_context *c, uint8_t marker, unsigned bias)
{
    fe2_object *object;

    if (!people_allocate(c, marker))
        return 0;
    object = people_object_arg(c, P_CREATED);
    object->behaviour_parent = (uint8_t)people_arg(c, P_MODE);
    fe2_args_word(c->args, P_SETUP, 0x5000);
    fe2_args_word(c->args, P_MODE, 0x33);
    people_setup_fields(c, object);
    object->behaviour = BEHAVIOUR_CHOOSE_PATROL_OFFSET;
    people_assign_value(c, object, bias);
    return object;
}

/* a valued craft (marker 6), or a group of three (marker 7: the
 * others copy the first, offset along x without a carry). Parameter 8 is
 * kept. */
static void people_spawn_valued_group(people_context *c)
{
    fe2_call_arg current;
    fe2_object *leader;
    uint16_t pick;

    fe2_args_byte(c->args, P_MODE, c->s->player_handle);
    pick = people_random_below(c, 4);
    if (pick > 1)
        return;
    current = c->args->arg[P_CURRENT];
    if (pick == 0) {
        people_spawn_valued(c, 6, 8);
    } else if ((leader = people_spawn_valued(c, 7, 0)) != 0) {
        static const uint32_t offsets[2] = {GROUP_NEAR, GROUP_FAR};
        unsigned n;

        people_set_object(c, P_CURRENT, leader);
        for (n = 0; n < 2; ++n) {
            fe2_object *member;

            if (!people_allocate_model(c, 7, leader->model))
                break;
            member = people_object_arg(c, P_CREATED);
            member->position_low[0] += offsets[n];
            people_assign_value(c, member, 0);
        }
    }
    c->args->arg[P_CURRENT] = current;
}

/* catalogue gives the population (parameters 0..5); the low words of
 * parameters 0..7 are restored after (not their high words). */
void people_populate(people_context *c)
{
    fe2_people_reply reply;
    uint16_t saved[8];
    uint16_t count;
    unsigned n;

    fe2_args_dword(c->args, P1, c->s->current_system);
    reply = c->g->catalogue_query(c->g->opaque, 0, c->args);
    for (n = 0; n < 6; ++n)
        fe2_args_dword(c->args, n, reply.value[n]);
    c->s->lookup_objects = c->s->objects;
    fe2_args_objects(c->args, P_LIST, c->s->objects);
    for (n = 0; n < 8; ++n)
        saved[n] = (uint16_t)people_arg(c, n);

    for (count = people_random_below(c, saved[P5]); count != 0; --count)
        people_spawn_trader(c);
    if (!(c->s->event_flags & 1)) {
        /* Docked craft for the markets (up to 0x19); then, or without
         * markets, flagged craft. No docked craft: no flagged ones. */
        int flagged = 1;

        if (c->s->market_count != 0) {
            count = people_random_below(c, (uint16_t)(c->s->market_count * 4u));
            if ((int16_t)count > RELATED_CAP)
                count = RELATED_CAP;
            flagged = count != 0;
            for (; count != 0; --count)
                people_spawn_docked(c);
        }
        if (flagged)
            for (count = people_random_below(c, saved[P5]); count != 0; --count)
                people_spawn_flagged(c);
    }
    count = saved[P3];
    if (!(c->s->event_flags & 1))
        count = (uint16_t)(count << 2);
    for (count = people_random_below(c, count); count != 0; --count)
        people_spawn_valued_group(c);
    for (count = people_random_below(c, saved[P3]); count != 0; --count)
        ;                              /* an empty spawn */
    for (n = 0; n < 8; ++n)
        fe2_args_word(c->args, n, saved[n]);
}
