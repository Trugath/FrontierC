/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* profile selection, naming and finalization of a record. */
#include "systems_internal.h"

enum {
    SIZE_BIAS = 0x3f, SCALAR_BIAS = 0x19, ANCESTOR_BIAS = 0x32,
    ORBIT_SCALE = 0x5edb,
    NEAR_PARENT_BYTES = 0x81,
    ANCESTOR_TEMPERATURE_LIMIT = 1000
};

static fe2_scalar scalar(uint16_t mantissa, uint16_t exponent)
{
    fe2_scalar s;
    s.mantissa = mantissa;
    s.exponent = exponent;
    return s;
}

static fe2_scalar record_scalar(const fe2_system_record *r, unsigned offset)
{
    return scalar(fe2_record_word(r, offset), fe2_record_word(r, offset + 2));
}

fe2_scalar fe2_systems_orbit_root(fe2_system_generation *g, fe2_scalar body, fe2_scalar parent)
{
    const fe2_system_services *game = g->game;
    fe2_scalar square = game->multiply(game->opaque, body, body);
    fe2_scalar cube = game->multiply(game->opaque, square, body);
    return game->square_root(game->opaque, game->divide(game->opaque, cube, parent));
}

fe2_scalar fe2_systems_orbit_scale(fe2_system_generation *g, fe2_scalar root)
{
    return g->game->multiply(g->game->opaque, root, scalar(ORBIT_SCALE, 0));
}

/* floor(U * W / 65536) of the curve position U and limit W. */
static uint32_t scaled_size(uint16_t u, uint32_t w)
{
    return (uint32_t)u * (w >> 16) + (((uint32_t)u * (w & 0xffffu)) >> 16);
}

/* Ancestor correction of the temperature: the square of each ancestor
 * profile value relative to the body, summed, then a fourth root. */
static uint16_t ancestor_heat(fe2_system_generation *g, unsigned record)
{
    const fe2_system_services *game = g->game;
    fe2_scalar sum = scalar(0, 0);
    unsigned node = record;

    for (;;) {
        uint16_t parent = fe2_record_word(fe2_systems_record(g, node), FE2_RECORD_PARENT);
        unsigned ancestor;
        uint32_t value;
        if (parent == 0) break;
        ancestor = (g->base + parent - 1u) % g->space.count;
        value = fe2_record_dword(fe2_systems_record(g, ancestor), FE2_RECORD_PROFILE_OR_OBJECT_REFERENCE);
        if (value != 0) {
            fe2_scalar term = game->normalize_dword(game->opaque, value);
            term.exponent = (uint16_t)(term.exponent + ANCESTOR_BIAS);
            term = game->divide(game->opaque, term,
                                record_scalar(fe2_systems_record(g, node), FE2_RECORD_SCALAR_MANTISSA));
            term = game->multiply(game->opaque, term, term);
            sum = game->add(game->opaque, term, sum);
        }
        node = ancestor;
    }
    sum = game->square_root(game->opaque, game->square_root(game->opaque, sum));
    return (uint16_t)game->to_signed_dword(game->opaque, sum);
}

void fe2_systems_finalize_profile(fe2_system_generation *g, unsigned *record)
{
    const fe2_system_services *game = g->game;
    fe2_system_record *r = fe2_systems_record(g, *record);
    uint32_t size = scaled_size((uint16_t)g->position_u, g->limit);
    uint32_t inherited = g->name_cursor;
    uint16_t high = (uint16_t)(g->random_a >> 16), turned, seed;
    fe2_scalar s;

    fe2_record_set_dword(r, FE2_RECORD_SCALAR_MANTISSA, size);
    if (size != 0) {
        fe2_scalar derived = scalar(0, 0);
        s = game->normalize_dword(game->opaque, size);
        s.exponent = (uint16_t)(s.exponent + SCALAR_BIAS);
        fe2_record_set_word(r, FE2_RECORD_SCALAR_MANTISSA, s.mantissa);
        fe2_record_set_word(r, FE2_RECORD_SCALAR_EXPONENT, s.exponent);
        if (g->parent_index != 0) {
            const fe2_system_record *parent = fe2_systems_record(g, g->parent);
            int16_t distance = (int16_t)(uint16_t)((*record - g->parent) * FE2_RECORD_BYTES);
            if (fe2_record_word(parent, FE2_RECORD_KIND) == 0 && distance <= NEAR_PARENT_BYTES)
                ++s.exponent;
            derived = fe2_systems_orbit_scale(g, fe2_systems_orbit_root(
                g, s, record_scalar(parent, FE2_RECORD_SIZE_MANTISSA)));
            /* Only this path points the seed read below at the parent. */
            inherited = fe2_systems_position(g->parent, 0);
        }
        fe2_record_set_word(r, FE2_RECORD_DERIVED_MANTISSA, derived.mantissa);
        fe2_record_set_word(r, FE2_RECORD_DERIVED_EXPONENT, derived.exponent);
        fe2_record_set_word(r, FE2_RECORD_ORBIT_LONGITUDE, (uint16_t)g->random_a);
    }

    fe2_record_set_dword(r, FE2_RECORD_SEED, g->random_b);
    ++g->record_count;
    fe2_record_set_word(r, FE2_RECORD_PARENT, g->parent_index);
    s = game->normalize_dword(game->opaque, g->value);
    fe2_record_set_word(r, FE2_RECORD_SIZE_MANTISSA, s.mantissa);
    fe2_record_set_word(r, FE2_RECORD_SIZE_EXPONENT, (uint16_t)(s.exponent + SIZE_BIAS));
    fe2_record_set_word(r, FE2_RECORD_ORBIT_ECCENTRICITY, (uint16_t)(((uint32_t)high * high) >> 24));
    turned = (uint16_t)((high >> 3) | (high << 13));
    /* Without a parent the word comes from beyond the last name written. */
    fe2_record_set_word(r, FE2_RECORD_ORBIT_INCLINATION, (uint16_t)((((uint32_t)turned * turned) >> 20) +
                        fe2_space_word(&g->space, inherited + FE2_RECORD_BASIS_SEED)));
    seed = fe2_record_word(r, FE2_RECORD_SEED_HIGH);
    seed = (uint16_t)((((uint32_t)seed * seed) >> 16) >> 1);
    fe2_record_set_word(r, FE2_RECORD_BASIS_SEED, seed);
    fe2_record_set_byte(r, FE2_RECORD_ORBIT_SHIFT, (uint8_t)(seed & 3u));
    fe2_record_set_byte(r, FE2_RECORD_GENERATION, g->depth);
    if ((int16_t)fe2_record_word(r, FE2_RECORD_TEMPERATURE) <= ANCESTOR_TEMPERATURE_LIMIT)
        fe2_record_set_word(r, FE2_RECORD_TEMPERATURE,
                            (uint16_t)(fe2_record_word(r, FE2_RECORD_TEMPERATURE) +
                                       ancestor_heat(g, *record)));
    fe2_record_set_word(r, FE2_RECORD_GENERATION_RESERVED, 0);
    g->published_count = g->record_count;
    if (g->rule != 0) g->rule(g, record);
}

int fe2_systems_create_profile(fe2_system_generation *g, unsigned *record)
{
    fe2_system_record *r = fe2_systems_record(g, *record);
    uint32_t value = g->value, limit = g->limit;
    uint16_t u = (uint16_t)g->position_u;
    unsigned parent = g->parent, profile = 0;

    /* The original has no bound: a size of FFFFFFFFh never stops. */
    while (profile < FE2_PROFILES - 1 && value >= fe2_systems_profile_thresholds[profile]) ++profile;
    fe2_record_set_dword(r, FE2_RECORD_PROFILE_OR_OBJECT_REFERENCE, fe2_systems_profile_or_object_references[profile]);
    fe2_record_set_word(r, FE2_RECORD_KIND, fe2_systems_profile_kind[profile]);
    fe2_record_set_word(r, FE2_RECORD_TEMPERATURE, fe2_systems_profile_temperature[profile]);
    g->subtree_position = fe2_systems_profile_subtree_position[profile];
    g->rule = fe2_systems_profile_rule(profile);
    fe2_record_set_word(r, FE2_RECORD_DESCRIPTOR, fe2_systems_profile_descriptor[profile]);
    if (fe2_systems_profile_descriptor[profile] == 0) return 0;

    ++g->named_bodies;
    fe2_systems_name_profile(g, *record);
    fe2_systems_finalize_profile(g, record);
    g->value = value;
    g->position_u = (g->position_u & 0xffff0000u) | u;
    g->limit = limit;
    g->parent = parent;
    return 1;
}
