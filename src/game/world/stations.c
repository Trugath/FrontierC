/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* stations added around inhabited bodies, as copies of Sol's
 * station records with fresh identities. */
#include "systems_internal.h"

#include <string.h>

enum {
    STATION_SIZE = 0x00390000, MAX_BATCH = 3,
    STATION_SCALAR_MANTISSA = 0x7fff, STATION_SCALAR_EXPONENT = 0x22,
    NAME_STATION = 6, NAME_SOL_STATION = 7
};

static unsigned next_station(fe2_system_generation *g, unsigned *record,
                             const fe2_system_record *model)
{
    fe2_system_record *r;
    *record = (*record + 1u) % g->space.count;
    r = fe2_systems_record(g, *record);
    memcpy(r, model, sizeof *r);
    fe2_record_set_dword(r, FE2_RECORD_SIZE_MANTISSA, STATION_SIZE);
    return *record;
}

static uint32_t rotate_left(uint32_t value, unsigned count)
{
    return (value << count) | (value >> (32 - count));
}

void fe2_systems_append_stations(fe2_system_generation *g, unsigned *record, uint16_t scale,
                             uint16_t base)
{
    uint32_t a = g->random_a, b = g->random_b;
    uint16_t remaining = (uint16_t)(((uint32_t)scale * (uint16_t)b) >> 16);

    if (g->station_count >= FE2_MAX_STATIONS) return;
    if ((int16_t)remaining >= MAX_BATCH + 1) remaining = MAX_BATCH;
    g->delta = a;
    g->limit = b;
    g->position_u = (g->position_u & 0xffff0000u) | remaining;
    for (;;) {
        fe2_system_record *r;
        unsigned pair, model;

        b += a;
        a = rotate_left(rotate_left(a, 5) + b, 16) + b;
        g->delta = a;
        g->limit = b;
        r = fe2_systems_record(g, next_station(g, record, &fe2_systems_sol_apollonius_city));
        fe2_record_set_word(r, FE2_RECORD_PARENT, g->published_count);
        fe2_record_set_dword(r, FE2_RECORD_SEED, a);
        pair = (++g->record_count & 0x0fu) * 2u;
        fe2_record_set_word(r, FE2_RECORD_ORBIT_INCLINATION, fe2_systems_station_orbit_parameters[pair]);
        fe2_record_set_word(r, FE2_RECORD_ORBIT_LONGITUDE, fe2_systems_station_orbit_parameters[pair + 1]);
        fe2_systems_generate_name(g, *record, NAME_STATION);
        model = (((g->danger >> 4) & 0x1eu) + base + (fe2_record_word(r, FE2_RECORD_SEED_HIGH) & 6u)) / 2u;
        fe2_record_set_word(r, FE2_RECORD_DESCRIPTOR, fe2_systems_generated_descriptors[model]);
        if (++g->station_count >= FE2_MAX_STATIONS) return;
        g->position_u = (g->position_u & 0xffff0000u) | (uint16_t)(g->position_u - 1u);
        if (g->position_u & 0x8000u) return;
    }
}

void fe2_systems_append_sol_station(fe2_system_generation *g, unsigned *record, uint16_t choice)
{
    const fe2_system_record *model = choice == 0 ? &fe2_systems_sol_daedalus
                                   : choice == 1 ? &fe2_systems_sol_galileo
                                                 : &fe2_systems_sol_abraham_lincoln;
    fe2_system_record *r;
    fe2_scalar body, root;
    unsigned parent;

    if (g->station_count >= FE2_MAX_STATIONS) return;
    ++g->station_count;
    ++g->sol_station_count;
    r = fe2_systems_record(g, next_station(g, record, model));
    fe2_record_set_dword(r, FE2_RECORD_SEED, g->random_a);
    fe2_record_set_word(r, FE2_RECORD_PARENT, g->published_count);
    ++g->record_count;
    body.mantissa = STATION_SCALAR_MANTISSA;
    body.exponent = STATION_SCALAR_EXPONENT;
    fe2_record_set_word(r, FE2_RECORD_SCALAR_MANTISSA, body.mantissa);
    fe2_record_set_word(r, FE2_RECORD_SCALAR_EXPONENT, body.exponent);
    /* The parent is the body published last, counted back from here. */
    parent = (*record + (uint16_t)(g->published_count - g->record_count)) % g->space.count;
    {
        const fe2_system_record *p = fe2_systems_record(g, parent);
        fe2_scalar size;
        size.mantissa = fe2_record_word(p, FE2_RECORD_SIZE_MANTISSA);
        size.exponent = fe2_record_word(p, FE2_RECORD_SIZE_EXPONENT);
        root = fe2_systems_orbit_scale(g, fe2_systems_orbit_root(g, body, size));
    }
    fe2_record_set_word(r, FE2_RECORD_DERIVED_MANTISSA, root.mantissa);
    fe2_record_set_word(r, FE2_RECORD_DERIVED_EXPONENT, root.exponent);
    fe2_systems_generate_name(g, *record, NAME_SOL_STATION);
}
