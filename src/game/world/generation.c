/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the object action entry - generate a system (handcrafted,
 * or procedural from one profile, a single star or a multi-star root) or
 * instantiate its records - and the 0x4e19 text entry. */
#include "systems_internal.h"

enum {
    ROOT_KIND_PROFILE = 0, ROOT_KIND_MULTIPLE = 2,
    MULTI_ROOT_SCALE_STEP = 0x081b3200u,
    PROFILE_SCALE_SHIFT = 8, SINGLE_SCALE_SHIFT = 6,
    TEXT_SELECTOR_MASK = 0x03fe
};

static void terminate(fe2_system_generation *g)
{
    fe2_record_set_dword(fe2_systems_record(g, g->destination), FE2_RECORD_SIZE_MANTISSA, 0);
}

/* A random word in 0xe000..0xffff (as unsigned): bit 15 set, then halved twice
 * keeping the sign. */
static uint16_t high_random_word(fe2_system_generation *g)
{
    uint16_t word = (uint16_t)(fe2_systems_random(g) | 0x8000u);
    return (uint16_t)(0xc000u | (word >> 2));
}

/* one body from the profile table, then its children. */
static void generate_from_profile(fe2_system_generation *g)
{
    unsigned record = g->destination;
    if (!fe2_systems_create_profile(g, &record)) {
        terminate(g);
        return;
    }
    g->position = g->subtree_position;
    ++g->parent_index;
    g->parent = record;
    g->destination = (record + 1u) % g->space.count;
    g->limit = g->scale;
    g->scale >>= PROFILE_SCALE_SHIFT;
    g->value = (g->value & 0xffff0000u) | (uint16_t)fe2_systems_random(g);
    fe2_systems_generate_children(g);
    terminate(g);
}

/* a single star system. */
static void generate_single(fe2_system_generation *g)
{
    uint16_t spacing;
    uint32_t limit;

    --g->root_budget;
    g->scale = g->value;
    fe2_systems_random(g);
    spacing = (uint16_t)((uint16_t)g->random_b >> 3);
    g->delta = (g->delta & 0xffff0000u) | spacing;
    limit = g->scale >= 0x20000000u ? 0xffffffffu : g->scale << 3;
    g->limit = limit;
    g->parent = g->destination;
    g->position_u = 0;
    fe2_systems_generate_root_group(g, g->value);
    ++g->parent_index;
    g->position = (uint16_t)(spacing << 3);
    g->scale >>= SINGLE_SCALE_SHIFT;
    g->value = fe2_systems_random(g);
    fe2_systems_generate_children(g);
    terminate(g);
}

/* a multiple star system. */
static void generate_multiple(fe2_system_generation *g)
{
    uint16_t spacing;

    g->scale = g->value;
    fe2_systems_random(g);
    spacing = (uint8_t)(g->random_b >> 8);
    g->delta = (g->delta & 0xffff0000u) | spacing;
    g->limit = 0xffffffffu;
    g->parent = g->destination;
    g->position_u = 0;
    fe2_systems_generate_root_group(g, g->scale);
    ++g->parent_index;
    g->position = (uint16_t)(spacing << 4);
    --g->root_budget;
    g->scale = (int8_t)g->root_budget > 2 ? 0xffffffffu : g->scale + MULTI_ROOT_SCALE_STEP;
    g->value = (g->value & 0xffff0000u) | high_random_word(g);
    fe2_systems_generate_children(g);
    terminate(g);
}

void fe2_systems_action(fe2_system_generation *g, const fe2_system_services *game,
                        fe2_record_space space, const fe2_system_request *request)
{
    g->game = game;
    g->space = space;
    g->value = request->seed_a;
    g->position_u = request->seed_b;
    g->limit = request->limit;
    g->destination = request->destination % space.count;
    g->parent = request->parent % space.count;
    g->name_cursor = request->name_cursor;
    g->root_name = request->root_name;
    g->base = g->destination;
    g->root_size = request->seed_b;
    g->key = request->key;
    g->danger = request->danger;
    g->mode = request->mode;
    if (g->mode != 0) {
        fe2_systems_instantiate(g, request->orbit);
        return;
    }
    g->station_count = g->sol_station_count = 0;
    g->scale = 0;
    g->record_count = 0;
    g->named_bodies = g->root_letter = 0;
    g->parent_index = 0;
    g->moon_digit = g->moon_letter = g->sub_digit = g->depth = 0;
    if ((request->seed_a >> 16) == 0) {
        fe2_systems_load_handcrafted(g);
        return;
    }
    g->random_a = request->seed_a;
    g->random_b = request->seed_b;
    g->scale = g->value = request->seed_b;
    g->root_kind = g->root_budget = (uint8_t)request->limit;
    if (g->root_kind == ROOT_KIND_PROFILE) generate_from_profile(g);
    else if ((int8_t)g->root_kind < ROOT_KIND_MULTIPLE) generate_single(g);
    else generate_multiple(g);
}

void fe2_systems_format_text(const fe2_system_services *game, uint16_t selector)
{
    game->format_table_text(game->opaque, selector & TEXT_SELECTOR_MASK);
}
