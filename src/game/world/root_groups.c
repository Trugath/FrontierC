/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* root record groups. A star (or a pair of bodies) at the top
 * of a system or of a multi-star subsystem. */
#include "systems_internal.h"

/* Sizes that make a direct pair of profiles instead of a separate root. */
#define DIRECT_PAIR_FROM UINT32_C(0xffffffef)
#define DIRECT_PAIR_LOW UINT32_C(0x0a21fe80)
#define DIRECT_PAIR_HIGH UINT32_C(0x0a6e49c0)

enum {
    ROOT_DESCRIPTOR = 0x20, ROOT_NAME_PREFIX = 12, SIZE_BIAS = 0x3f,
    COMPANION_SCALAR_MARK = 0x7fff
};

/* A profile record at the destination; returns the last record written. */
static unsigned create_at_destination(fe2_system_generation *g)
{
    unsigned record = g->destination;
    fe2_systems_create_profile(g, &record);
    g->destination = (record + 1u) % g->space.count;
    return record;
}

/* "Name.A,B": up to twelve bytes of the system name, a space or dot, then
 * the root letter, a comma and the following byte value. */
static void name_root(fe2_system_generation *g, unsigned record)
{
    uint32_t position = fe2_systems_position(record, FE2_RECORD_NAME);
    unsigned copied = 0;
    char byte;
    uint8_t letter = (uint8_t)(g->root_letter + 'A');

    do {
        byte = g->root_name[copied++];
        fe2_space_set_byte(&g->space, position++, (uint8_t)byte);
    } while (copied < ROOT_NAME_PREFIX && byte != 0);
    if (byte == 0) fe2_space_set_byte(&g->space, position - 1, ' ');
    else fe2_space_set_byte(&g->space, position++, '.');
    fe2_space_set_byte(&g->space, position, letter);
    fe2_space_set_byte(&g->space, position + 1, ',');
    fe2_space_set_byte(&g->space, position + 2, (uint8_t)(letter + 1));
    fe2_space_set_byte(&g->space, position + 3, 0);
}

/* Companion size: a random factor in 0xe000..0xffff times the size's high
 * word, at least 0x1036640. */
static uint32_t companion_size(fe2_system_generation *g, uint32_t size)
{
    uint16_t random = (uint16_t)fe2_systems_random(g);
    uint16_t factor = (uint16_t)(0xe000u + ((random & 0x7fffu) >> 2));
    uint32_t companion = (uint32_t)factor * (size >> 16);
    return companion < FE2_NAMED_BODY_SIZE ? FE2_NAMED_BODY_SIZE : companion;
}

/* A separate root record (no profile) above the original body and a
 * companion; the root's size is their saturated sum. */
static void separate_root(fe2_system_generation *g, uint32_t size, uint16_t companion_u)
{
    unsigned root = g->destination, record;
    fe2_system_record *r = fe2_systems_record(g, root);
    uint32_t companion, total;
    fe2_scalar s;

    fe2_record_set_word(r, FE2_RECORD_KIND, 0);
    fe2_record_set_word(r, FE2_RECORD_DESCRIPTOR, ROOT_DESCRIPTOR);
    g->rule = 0;
    g->value = size;
    record = root;
    fe2_systems_finalize_profile(g, &record);
    name_root(g, root);
    g->destination = (root + 1u) % g->space.count;
    g->parent_index = g->record_count;

    companion = companion_size(g, size);
    total = size + companion < size ? 0xffffffffu : size + companion;
    s = g->game->normalize_dword(g->game->opaque, total);
    fe2_record_set_word(r, FE2_RECORD_SIZE_MANTISSA, s.mantissa);
    fe2_record_set_word(r, FE2_RECORD_SIZE_EXPONENT, (uint16_t)(s.exponent + SIZE_BIAS));

    g->value = size;
    g->position_u = (g->position_u & 0xffff0000u) | companion_u;
    record = create_at_destination(g);
    fe2_record_set_word(fe2_systems_record(g, record), FE2_RECORD_ORBIT_LONGITUDE, 0);
    /* The record before the last one written (the root, unless the profile
     * added stations) takes the profile value. */
    fe2_record_set_dword(fe2_systems_record(g, (record + g->space.count - 1u) % g->space.count),
                         FE2_RECORD_PROFILE_OR_OBJECT_REFERENCE,
                         fe2_record_dword(fe2_systems_record(g, record), FE2_RECORD_PROFILE_OR_OBJECT_REFERENCE));

    g->position_u = (g->position_u & 0xffff0000u) | companion_u;
    g->value = companion;
    record = create_at_destination(g);
    fe2_record_set_word(fe2_systems_record(g, record), FE2_RECORD_ORBIT_LONGITUDE, COMPANION_SCALAR_MARK);
    g->value = size;
}

/* A profile for the size and another for a quarter of it. */
static void direct_pair(fe2_system_generation *g, uint32_t size, uint16_t second_u)
{
    g->value = size;
    create_at_destination(g);
    g->parent_index = g->record_count;
    g->value = size >> 2;
    g->position_u = (g->position_u & 0xffff0000u) | second_u;
    create_at_destination(g);
}

void fe2_systems_generate_root_group(fe2_system_generation *g, uint32_t size)
{
    uint16_t parent_index = g->parent_index;
    unsigned parent = g->parent;
    uint32_t delta = g->delta, position_u = g->position_u;

    if (size >= DIRECT_PAIR_FROM || (size >= DIRECT_PAIR_LOW && size < DIRECT_PAIR_HIGH))
        direct_pair(g, size, (uint16_t)delta);
    else
        separate_root(g, size, (uint16_t)delta);
    g->position_u = position_u;
    g->delta = delta;
    g->parent = parent;
    g->parent_index = parent_index;
}
