/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Names of generated bodies: suns and giants take the system name with a
 * letter, moons their parent's name with a digit or letter, and some bodies
 * a generated name. */
#include "systems_internal.h"

enum { ROOT_LETTER_BASE = 0x40, MOON_LETTER_BASE = 0x60, TENS_PREFIX = '1',
       GENERATED_PARENT_NAME = 0x0a };

void fe2_systems_generate_name(fe2_system_generation *g, unsigned record, uint16_t type)
{
    fe2_system_record *r = fe2_systems_record(g, record);
    char name[FE2_NAME_TEXT_BYTES];

    fe2_record_set_byte(r, FE2_RECORD_GENERATION,
                        fe2_record_byte(r, FE2_RECORD_GENERATION) | FE2_RECORD_GENERATED_NAME);
    g->game->generate_name(g->game->opaque, type, fe2_record_dword(r, FE2_RECORD_SEED), name);
    g->name_cursor = fe2_space_copy_name(&g->space, fe2_systems_position(record, FE2_RECORD_NAME), name);
}

/* Moons of a first-level body count 1..9, 10..; deeper levels use letters
 * and then digits. */
static uint32_t append_moon_suffix(fe2_system_generation *g, uint32_t end)
{
    fe2_record_space *space = &g->space;
    uint8_t digit;

    if (g->depth == 0) {
        digit = ++g->moon_digit;
        if ((int8_t)digit >= 10) {
            fe2_space_set_byte(space, end - 1, TENS_PREFIX);
            ++end;
            digit = (uint8_t)(digit - 10);
        }
        fe2_space_set_byte(space, end - 1, (uint8_t)(digit + '0'));
    } else if (g->depth == 1) {
        fe2_space_set_byte(space, end - 1, (uint8_t)(++g->moon_letter + MOON_LETTER_BASE));
    } else {
        fe2_space_set_byte(space, end - 1, (uint8_t)(++g->sub_digit + '0'));
    }
    return end;
}

void fe2_systems_name_profile(fe2_system_generation *g, unsigned record)
{
    fe2_record_space *space = &g->space;
    uint32_t position = fe2_systems_position(record, FE2_RECORD_NAME);
    const fe2_system_record *parent = fe2_systems_record(g, g->parent);

    if (g->value >= FE2_NAMED_BODY_SIZE) {
        position = fe2_space_copy_name(space, position, g->root_name);
        fe2_space_set_byte(space, position - 1, ' ');
        ++g->root_letter;
        if (g->parent_index != 0)
            fe2_space_set_byte(space, position++, (uint8_t)(g->root_letter + ROOT_LETTER_BASE));
    } else if (fe2_record_byte(parent, FE2_RECORD_GENERATION) & FE2_RECORD_GENERATED_NAME) {
        fe2_record_set_dword(fe2_systems_record(g, record), FE2_RECORD_SEED, g->random_b);
        fe2_systems_generate_name(g, record, GENERATED_PARENT_NAME);
        return;
    } else {
        position = fe2_space_copy_within(space, position,
                                         fe2_systems_position(g->parent, FE2_RECORD_NAME));
        position = append_moon_suffix(g, position);
    }
    fe2_space_set_byte(space, position, 0);
    g->name_cursor = position;
}
