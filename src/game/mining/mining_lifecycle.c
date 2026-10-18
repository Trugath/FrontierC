/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* reset, the hopper tick and recreating machines; 0xa6b: progress;
 * 0xb17: removing a machine's record. */
#include <string.h>

#include "mining_internal.h"

enum { EVENT_ARRIVAL = 2, EVENT_DAY = 0x10, SELECT_LEVEL = 5, MINING_VIEW = 0x68,
       LOADED_TEXT = 0xa20e, SELECTOR_MASK = 0x3fe };

void fe2_mining_reset(fe2_mining_state *s)
{
    s->count = 0;
    s->free_count = 0;
}

/* Each day a machine moves a tonne from the body's reserve into a hopper
 * holding under 10. */
static void tick(fe2_mining_state *s)
{
    unsigned i;
    for (i = 0; i < s->count; ++i) {
        uint8_t *r = fe2_mining_record(s, i);
        if (r[FE2_MINING_RESERVE] == 0 || r[FE2_MINING_HOPPER] >= HOPPER_LIMIT) continue;
        --r[FE2_MINING_RESERVE];
        ++r[FE2_MINING_HOPPER];
    }
}

/* the body whose name matches the record's becomes the new
 * machine's parent. Names are NUL-terminated within the object's name. */
void fe2_mining_name_object(void *pass, fe2_object *o)
{
    fe2_mining_pass *p = (fe2_mining_pass *)pass;
    const uint8_t *name = fe2_mining_record(p->state, p->record) + FE2_MINING_NAME;
    const uint8_t *end = p->state->records.bytes + sizeof p->state->records.bytes;
    unsigned i;
    for (i = 0; i < FE2_OBJECT_NAME_BYTES && (uint8_t)o->name[i] != 0; ++i)
        if (name + i >= end || (uint8_t)o->name[i] != name[i]) return;
    if (name + i < end && name[i] == 0) p->machine->parent_handle = o->handle;
}

static fe2_object *create_machine(fe2_mining_state *s, const fe2_mining_services *game)
{
    fe2_call_args args;
    s->creation_objects = s->objects;
    fe2_args_clear(&args);
    fe2_args_word(&args, KIND_PARAMETER, MACHINE_KIND);
    fe2_args_word(&args, MODEL_PARAMETER, MACHINE_MODEL);
    fe2_args_object(&args, OBJECT_PARAMETER, s->ship);
    return game->create_object(game->opaque, &args);
}

/* On arrival every record forgets its object; the machines in this system
 * are recreated at their positions and linked to their bodies by name. */
static void recreate(fe2_mining_state *s, const fe2_mining_services *game)
{
    unsigned i, axis;
    for (i = 0; i < s->count; ++i) {
        uint8_t *r = fe2_mining_record(s, i);
        fe2_mining_pass pass;
        fe2_call_args args;
        fe2_object *machine;
        r[FE2_MINING_OBJECT] = 0;
        if (s->current_system != fe2_mining_get32(r + FE2_MINING_SYSTEM)) continue;
        machine = create_machine(s, game);
        if (!machine) continue;
        r = fe2_mining_record(s, i);
        machine->role = MACHINE_ROLE;
        machine->map_marker = MACHINE_MARKER;
        machine->behaviour = 0;
        machine->cargo_type = 0xffff;
        machine->station_slot = UNDOCKED;
        for (axis = 0; axis < 3; ++axis) {
            machine->position_low[axis] = fe2_mining_get32(r + FE2_MINING_POSITION + 4 * axis);
            machine->position_high[axis] = fe2_mining_get32(r + FE2_MINING_POSITION + 12 + 4 * axis);
        }
        r[FE2_MINING_OBJECT] = machine->handle;
        pass.state = s;
        pass.record = i;
        pass.machine = machine;
        game->visit_objects(game->opaque, s->objects, fe2_mining_name_object, &pass);
        fe2_args_clear(&args);
        game->attach_object(game->opaque, machine, &args);
    }
}

void fe2_mining_event(fe2_mining_state *s, const fe2_mining_services *game, uint8_t event)
{
    if (event == EVENT_DAY) tick(s);
    else if (event == EVENT_ARRIVAL) recreate(s, game);
}

static void check_idle(fe2_mining_state *s)
{
    if (s->free_count == s->count) s->activity = 0;
}

/*
 * A deploying machine's progress rises by a quarter of the frame step (low
 * word), clamped at 0xffff. In the mining view (0x68) a machine of level 5 or
 * less becomes the selection; the first selection posts the event. A
 * recovering machine's progress falls; on passing zero (not on reaching it)
 * the machine is loaded: its slot frees, cargo space returns and the object
 * becomes a loaded machine.
 */
void fe2_mining_progress(fe2_mining_state *s, const fe2_mining_services *game, fe2_object *machine)
{
    uint16_t step = (uint16_t)(s->frame_step >> 2);
    fe2_call_args args;
    int record;
    if (machine->behaviour == 0) {
        machine->cargo_type = (uint16_t)(machine->cargo_type + step) < machine->cargo_type
                                  ? 0xffff : (uint16_t)(machine->cargo_type + step);
        if (machine->level > SELECT_LEVEL || s->flight_state != MINING_VIEW) return;
        s->selected_machine = machine->handle;
        if (s->ship_flags & MACHINE_SELECTED) return;
        s->ship_flags |= MACHINE_SELECTED;
        game->post_event(game->opaque, MINING_EVENT);
        return;
    }
    if (step <= machine->cargo_type) {
        machine->cargo_type = (uint16_t)(machine->cargo_type - step);
        return;
    }
    machine->cargo_type = (uint16_t)(machine->cargo_type - step);
    record = fe2_mining_find_machine(s, machine->handle);
    if (record == NO_RECORD) return;
    fe2_mining_put32(fe2_mining_record(s, (unsigned)record) + FE2_MINING_SYSTEM, 0);
    ++s->free_count;
    s->cargo_capacity = (uint16_t)(s->cargo_capacity - MACHINE_CARGO);
    fe2_args_clear(&args);
    fe2_mining_message(game, LOADED_TEXT, 0, &args);
    machine->model = LOADED_MODEL;
    game->release_object(game->opaque, machine);
    game->post_event(game->opaque, MINING_EVENT);
    check_idle(s);
}

void fe2_mining_remove(fe2_mining_state *s, const fe2_mining_services *game, uint16_t request,
                       fe2_object *machine)
{
    int record;
    if (request != 0 || machine->model != MACHINE_MODEL) return;
    record = fe2_mining_find_machine(s, machine->handle);
    if (record == NO_RECORD) return;
    fe2_mining_close_gap(s, (unsigned)record);
    --s->count;
    game->post_event(game->opaque, MINING_EVENT);
    check_idle(s);
}

void fe2_mining_text(const fe2_mining_services *game, uint16_t selector, fe2_text_out *out)
{
    game->format_table_text(game->opaque, selector & SELECTOR_MASK,
                            (const fe2_text_table *)fe2_mining_text_directory, out);
}
