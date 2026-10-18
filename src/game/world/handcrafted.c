/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* handcrafted systems (Sol, Lave,...) converted from their
 * native templates. */
#include "systems_internal.h"

#include <string.h>

enum { SIZE_BIAS = 0x3f, SCALAR_BIAS = 0x19, ORBIT_SCALE = 0x5edb, NEAR_PARENT_BYTES = -0x80 };

static const fe2_system_template *find_system(uint32_t key)
{
    unsigned i;
    for (i = 0; i < fe2_systems_handcrafted_count; ++i)
        if (fe2_systems_handcrafted_systems[i].key == key) return fe2_systems_handcrafted_systems[i].system;
    /* Key 0 matches the table terminator rather than selecting the fallback. */
    return key == 0 ? fe2_systems_unloaded_system : fe2_systems_fallback_system;
}

static void copy_dword(fe2_system_record *to, const fe2_system_record *from, unsigned offset)
{
    fe2_record_set_dword(to, offset, fe2_record_dword(from, offset));
}

static void copy_word(fe2_system_record *to, const fe2_system_record *from, unsigned offset)
{
    fe2_record_set_word(to, offset, fe2_record_word(from, offset));
}

/* Orbit word of a record with a parent, as in finalization but with the
 * final 0x5edb scaling done inline. */
static void derive_orbit(fe2_system_generation *g, unsigned record, uint16_t parent_index)
{
    fe2_system_record *r = fe2_systems_record(g, record);
    unsigned parent = (g->destination + parent_index - 1u) % g->space.count;
    const fe2_system_record *p = fe2_systems_record(g, parent);
    int16_t distance = (int16_t)(uint16_t)((parent - record) * FE2_RECORD_BYTES);
    fe2_scalar body, size, root;

    body.mantissa = fe2_record_word(r, FE2_RECORD_SCALAR_MANTISSA);
    body.exponent = fe2_record_word(r, FE2_RECORD_SCALAR_EXPONENT);
    if (fe2_record_word(p, FE2_RECORD_KIND) == 0 && distance >= NEAR_PARENT_BYTES) ++body.exponent;
    size.mantissa = fe2_record_word(p, FE2_RECORD_SIZE_MANTISSA);
    size.exponent = fe2_record_word(p, FE2_RECORD_SIZE_EXPONENT);
    root = fe2_systems_orbit_root(g, body, size);
    fe2_record_set_word(r, FE2_RECORD_DERIVED_EXPONENT, root.exponent);
    fe2_record_set_word(r, FE2_RECORD_DERIVED_MANTISSA,
                        (uint16_t)(((uint32_t)((int32_t)(int16_t)root.mantissa * ORBIT_SCALE) << 1) >> 16));
}

static void convert(fe2_system_generation *g, unsigned record, const fe2_system_record *t)
{
    const fe2_system_services *game = g->game;
    fe2_system_record *r = fe2_systems_record(g, record);
    uint16_t parent;
    fe2_scalar s;

    s = game->normalize_dword(game->opaque, fe2_record_dword(t, FE2_RECORD_SIZE_MANTISSA) - 1u);
    fe2_record_set_word(r, FE2_RECORD_SIZE_MANTISSA, s.mantissa);
    fe2_record_set_word(r, FE2_RECORD_SIZE_EXPONENT, (uint16_t)(s.exponent + SIZE_BIAS));
    copy_dword(r, t, FE2_RECORD_SEED);
    copy_word(r, t, FE2_RECORD_PARENT);
    copy_word(r, t, FE2_RECORD_KIND);
    copy_word(r, t, FE2_RECORD_DESCRIPTOR);
    s = game->normalize_dword(game->opaque, fe2_record_dword(t, FE2_RECORD_SCALAR_MANTISSA));
    fe2_record_set_word(r, FE2_RECORD_SCALAR_MANTISSA, s.mantissa);
    fe2_record_set_word(r, FE2_RECORD_SCALAR_EXPONENT, (uint16_t)(s.exponent + SCALAR_BIAS));
    copy_word(r, t, FE2_RECORD_ORBIT_LONGITUDE);
    copy_dword(r, t, FE2_RECORD_ORBIT_ECCENTRICITY);
    copy_dword(r, t, FE2_RECORD_TEMPERATURE);   /* with the first two name bytes */
    copy_dword(r, t, FE2_RECORD_TEMPLATE_RESERVED);
    copy_word(r, t, FE2_RECORD_BASIS_SEED);

    parent = fe2_record_word(r, FE2_RECORD_PARENT);
    if (parent != 0) {
        derive_orbit(g, record, parent);
    } else {
        fe2_record_set_word(r, FE2_RECORD_DERIVED_MANTISSA, 0);
        fe2_record_set_word(r, FE2_RECORD_DERIVED_EXPONENT, 0);
    }
    fe2_space_copy_name(&g->space, fe2_systems_position(record, FE2_RECORD_NAME),
                        fe2_record_name_const(t));
}

void fe2_systems_load_handcrafted(fe2_system_generation *g)
{
    const fe2_system_template *system = find_system(g->key);
    unsigned record = g->destination, i;

    for (i = 0; i < system->count; ++i, record = (record + 1u) % g->space.count) {
        const fe2_system_record *t = &system->records[i];
        if (fe2_record_word(t, FE2_RECORD_KIND) == FE2_RECORD_KIND_VERBATIM)
            memcpy(fe2_systems_record(g, record), t, sizeof *t);
        else
            convert(g, record, t);
    }
    fe2_record_set_dword(fe2_systems_record(g, record), FE2_RECORD_SIZE_MANTISSA, 0);
    g->named_bodies = (uint8_t)system->counters;
    g->root_letter = (uint8_t)(system->counters >> 8);
    g->moon_digit = (uint8_t)(system->counters >> 16);
    g->moon_letter = (uint8_t)(system->counters >> 24);
    g->station_count = (uint8_t)system->stations;
    g->sol_station_count = (uint8_t)(system->stations >> 8);
}
