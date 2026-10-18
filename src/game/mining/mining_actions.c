/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* deploying, loading and recovering machines, and record slots. */
#include "mining_internal.h"

enum {
    DEPLOY_TEXT = 0xa20c, ASSESSMENT_TEXT = 0xa211, EMPTY_LOADING_TEXT = 0xa20d,
    LOADING_TEXT = 0xa20f, CARGO_FULL_TEXT = 0xa210, NO_ROOM_TEXT = 0x98cc,
    TOO_HOT_TEXT = 0xa212, TOO_COLD_TEXT = 0xa213, UNSTABLE_TEXT = 0xa214,
    WATER_ICE_TEXT = 0xa215, LIQUID_WATER_TEXT = 0xa216, COMMODITY_TEXT = 0x8e00,
    HOTTEST = 473, COLDEST = 50, WATER_TEMPERATURE = 0x107, TEMPERATURE_BASE = 0x111,
    UNSTABLE_MODEL = 0x82, ICE_MODEL = 0x8c, WATER_CATEGORY = 2, UNLIMITED = 0xff,
    LARGE_BODY = 29, MINING_ACTIVE = 0x16, ILLEGAL_MINING = 4,
    DELTA_PARAMETER = 0, TEXT_PARAMETER = 2, TEMPERATURE_PARAMETER = 1, SYSTEM_PARAMETER = 1,
    COMMODITY_PARAMETER = 3
};

static const uint16_t water_models[] = {0x8e, 0x8a, 0x88, 0x90};

typedef struct assessment {
    uint8_t category;
    uint16_t text;
} assessment;

static int is_water(uint16_t model)
{
    unsigned i;
    for (i = 0; i < sizeof water_models / sizeof water_models[0]; ++i)
        if (model == water_models[i]) return 1;
    return 0;
}

/* A sampled yield: the catalogue profile's field 0x5 sets a richness factor
 * (10, 9, 8, 6, 4 for 0, <3, <14, <27, more), a size factor of 2 or 3
 * (size exponent from 29), and the yield is row (n * n) / 32 of a random n
 * below their product. */
static void sample_yield(fe2_mining_state *s, const fe2_mining_services *game, const fe2_object *body,
                         unsigned record, assessment *a)
{
    fe2_call_args args;
    uint16_t richness, factor, n;
    const fe2_mining_yield *row;
    fe2_args_clear(&args);
    fe2_args_dword(&args, SYSTEM_PARAMETER, s->current_system);
    richness = game->system_profile(game->opaque, &args);
    factor = richness == 0 ? 10 : richness < 3 ? 9 : richness < 14 ? 8 : richness < 27 ? 6 : 4;
    factor = (uint16_t)(factor * ((int16_t)body->size_exponent < LARGE_BODY ? 2 : 3));
    n = game->random_below(game->opaque, factor);
    row = &fe2_mining_yields[(uint16_t)(n * n) >> 5];
    a->text = row->assessment_text;
    fe2_mining_record(s, record)[FE2_MINING_RESERVE] = (uint8_t)row->reserve;
    a->category = (uint8_t)row->category;
}

/* The body: too hot, too cold or unstable bodies yield nothing; water
 * bodies (and cool ones half the time) give unlimited water; others give
 * a sampled yield. The record's reserve is set as the original sets it. */
static assessment assess(fe2_mining_state *s, const fe2_mining_services *game, const fe2_object *body,
                         unsigned record)
{
    assessment a;
    uint16_t temperature = body->as.body.temperature;
    a.category = 0;
    fe2_mining_record(s, record)[FE2_MINING_RESERVE] = 0;
    if (temperature > HOTTEST) { a.text = TOO_HOT_TEXT; return a; }
    if (temperature < COLDEST) { a.text = TOO_COLD_TEXT; return a; }
    if (body->model == UNSTABLE_MODEL) { a.text = UNSTABLE_TEXT; return a; }
    fe2_mining_record(s, record)[FE2_MINING_RESERVE] = UNLIMITED;
    a.category = WATER_CATEGORY;
    a.text = WATER_ICE_TEXT;
    if (body->model == ICE_MODEL) return a;
    a.text = LIQUID_WATER_TEXT;
    if (is_water(body->model)) return a;
    a.text = WATER_ICE_TEXT;
    if (temperature <= WATER_TEMPERATURE && (game->random(game->opaque) & 0x8000u)) return a;
    sample_yield(s, game, body, record, &a);
    return a;
}

/* The machine is placed from the ship's basis: per axis 4 * the third
 * column less the second, as signed dwords. */
static void place(const fe2_mining_services *game, const fe2_object *ship, fe2_object *machine)
{
    fe2_call_args args;
    unsigned axis;
    fe2_args_clear(&args);
    for (axis = 0; axis < 3; ++axis)
        fe2_args_dword(&args, DELTA_PARAMETER + axis,
                       (uint32_t)((int32_t)ship->orientation[3 * axis + 2] * 4 -
                                  (int32_t)ship->orientation[3 * axis + 1]));
    game->add_position(game->opaque, machine, &args);
}

static void copy_name(fe2_mining_state *s, uint8_t *record, const fe2_object *body)
{
    uint8_t *to = record + FE2_MINING_NAME, *end = s->records.bytes + sizeof s->records.bytes;
    unsigned i;
    for (i = 0; i < FE2_OBJECT_NAME_BYTES && to < end; ++i, ++to) {
        *to = (uint8_t)body->name[i];
        if (*to == 0) break;
    }
}

/* Deploy a machine into a free record, assess the body below and report;
 * deploying is illegal mining. */
static void deploy(fe2_mining_state *s, const fe2_mining_services *game)
{
    int free_record = fe2_mining_find_free(s);
    fe2_object *machine, *body;
    fe2_call_args args;
    assessment a;
    uint8_t *r;
    unsigned axis;
    if (free_record == NO_RECORD) return;
    s->creation_objects = s->objects;
    fe2_args_clear(&args);
    fe2_args_word(&args, KIND_PARAMETER, MACHINE_KIND);
    fe2_args_word(&args, MODEL_PARAMETER, MACHINE_MODEL);
    fe2_args_object(&args, OBJECT_PARAMETER, s->ship);
    machine = game->create_object(game->opaque, &args);
    if (!machine) return;
    machine->role = MACHINE_ROLE;
    machine->map_marker = MACHINE_MARKER;
    machine->cargo_type = 0;
    machine->behaviour = 0;
    place(game, s->ship, machine);
    fe2_args_clear(&args);
    fe2_args_object(&args, OBJECT_PARAMETER, machine);
    fe2_args_objects(&args, OBJECTS_PARAMETER, s->objects);
    game->attach_object(game->opaque, machine, &args);
    s->cargo_capacity = (uint16_t)(s->cargo_capacity + MACHINE_CARGO);
    r = fe2_mining_record(s, (unsigned)free_record);
    fe2_mining_put32(r + FE2_MINING_SYSTEM, s->current_system);
    for (axis = 0; axis < 3; ++axis) {
        fe2_mining_put32(r + FE2_MINING_POSITION + 4 * axis, machine->position_low[axis]);
        fe2_mining_put32(r + FE2_MINING_POSITION + 12 + 4 * axis, machine->position_high[axis]);
    }
    r[FE2_MINING_OBJECT] = machine->handle;
    r[FE2_MINING_HOPPER] = 0;
    --s->free_count;
    fe2_args_clear(&args);
    fe2_args_objects(&args, OBJECTS_PARAMETER, s->objects);
    body = game->lookup_object(game->opaque, machine->parent_handle, &args);
    a = assess(s, game, body, (unsigned)free_record);
    fe2_mining_record(s, (unsigned)free_record)[FE2_MINING_CATEGORY] = a.category;
    fe2_args_clear(&args);
    fe2_mining_message(game, DEPLOY_TEXT, 0, &args);
    fe2_args_clear(&args);
    fe2_args_word(&args, TEXT_PARAMETER, a.text);
    fe2_args_dword(&args, TEMPERATURE_PARAMETER,
                   (uint32_t)(int32_t)(int16_t)(uint16_t)(body->as.body.temperature - TEMPERATURE_BASE));
    fe2_mining_message(game, ASSESSMENT_TEXT, 0, &args);
    copy_name(s, fe2_mining_record(s, (unsigned)free_record), body);
    s->activity = MINING_ACTIVE;
    game->commit_offence(game->opaque, ILLEGAL_MINING);
}

/* The selected machine's hopper goes into cargo (what fits, compared
 * signed); an empty machine is recovered if 30 tonnes of cargo space are
 * free. */
static void load(fe2_mining_state *s, const fe2_mining_services *game)
{
    fe2_call_args args;
    fe2_object *machine;
    uint8_t *r;
    int record;
    uint16_t amount, room, text = LOADING_TEXT, commodity;
    fe2_args_clear(&args);
    fe2_args_objects(&args, OBJECTS_PARAMETER, s->objects);
    machine = game->lookup_object(game->opaque, s->selected_machine, &args);
    if (machine->model != MACHINE_MODEL) return;
    record = fe2_mining_find_machine(s, machine->handle);
    if (record == NO_RECORD) return;
    r = fe2_mining_record(s, (unsigned)record);
    amount = r[FE2_MINING_HOPPER];
    room = (uint16_t)(s->cargo_capacity - s->cargo_used);
    fe2_args_clear(&args);
    if (amount == 0) {
        if (room < MACHINE_CARGO) {
            fe2_mining_message(game, NO_ROOM_TEXT, 0, &args);
            return;
        }
        machine->behaviour = RECOVERING;
        s->ship_flags &= (uint8_t)~MACHINE_SELECTED;
        fe2_mining_message(game, EMPTY_LOADING_TEXT, 0, &args);
        return;
    }
    if ((int16_t)amount > (int16_t)room) {
        amount = room;
        text = CARGO_FULL_TEXT;
    }
    s->cargo_used = (uint16_t)(s->cargo_used + amount);
    r[FE2_MINING_HOPPER] = (uint8_t)(r[FE2_MINING_HOPPER] - (uint8_t)amount);
    commodity = fe2_mining_category_cargo[r[FE2_MINING_CATEGORY]];
    s->cargo[commodity] = (uint16_t)(s->cargo[commodity] + amount);
    fe2_args_word(&args, COMMODITY_PARAMETER, (uint16_t)(commodity + COMMODITY_TEXT));
    fe2_mining_message(game, text, amount, &args);
    game->post_event(game->opaque, MINING_EVENT);
}

static int remove_free_slot(fe2_mining_state *s)
{
    int record = fe2_mining_find_free(s);
    if (record == NO_RECORD) return 0;
    fe2_mining_close_gap(s, (unsigned)record);
    --s->count;
    --s->free_count;
    return 1;
}

int fe2_mining_action(fe2_mining_state *s, const fe2_mining_services *game, uint16_t action)
{
    switch (action) {
    case FE2_MINING_DEPLOY:
        deploy(s, game);
        return 0;
    case FE2_MINING_LOAD:
        load(s, game);
        return 0;
    case FE2_MINING_ADD_SLOT:
        if (s->count >= FE2_MINING_RECORDS) return 0;
        fe2_mining_put32(fe2_mining_record(s, s->count) + FE2_MINING_SYSTEM, 0);
        ++s->count;
        ++s->free_count;
        return 0;
    case FE2_MINING_REMOVE_SLOT:
        return remove_free_slot(s);
    default:
        /* The free count is kept consistent; the original loops forever
         * otherwise. */
        while (s->free_count != 0 && remove_free_slot(s)) {}
        return 0;
    }
}
