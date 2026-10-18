/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "system_record.h"

static unsigned in_record(unsigned offset) { return offset % FE2_RECORD_BYTES; }

uint8_t fe2_record_byte(const fe2_system_record *record, unsigned offset)
{
    return record->bytes[in_record(offset)];
}

uint16_t fe2_record_word(const fe2_system_record *record, unsigned offset)
{
    return (uint16_t)(fe2_record_byte(record, offset) | fe2_record_byte(record, offset + 1) << 8);
}

uint32_t fe2_record_dword(const fe2_system_record *record, unsigned offset)
{
    return fe2_record_word(record, offset) | (uint32_t)fe2_record_word(record, offset + 2) << 16;
}

void fe2_record_set_byte(fe2_system_record *record, unsigned offset, uint8_t value)
{
    record->bytes[in_record(offset)] = value;
}

void fe2_record_set_word(fe2_system_record *record, unsigned offset, uint16_t value)
{
    fe2_record_set_byte(record, offset, (uint8_t)value);
    fe2_record_set_byte(record, offset + 1, (uint8_t)(value >> 8));
}

void fe2_record_set_dword(fe2_system_record *record, unsigned offset, uint32_t value)
{
    fe2_record_set_word(record, offset, (uint16_t)value);
    fe2_record_set_word(record, offset + 2, (uint16_t)(value >> 16));
}

char *fe2_record_name(fe2_system_record *record)
{
    return (char *)&record->bytes[FE2_RECORD_NAME];
}

const char *fe2_record_name_const(const fe2_system_record *record)
{
    return (const char *)&record->bytes[FE2_RECORD_NAME];
}

/* Positions are 16-bit offsets in the record segment and wrap like them. */
static uint8_t *space_at(const fe2_record_space *space, uint32_t position)
{
    uint32_t wrapped = position & 0xffffu;
    return &space->records[(wrapped / FE2_RECORD_BYTES) % space->count]
                .bytes[wrapped % FE2_RECORD_BYTES];
}

uint8_t fe2_space_byte(const fe2_record_space *space, uint32_t position)
{
    return *space_at(space, position);
}

uint16_t fe2_space_word(const fe2_record_space *space, uint32_t position)
{
    return (uint16_t)(fe2_space_byte(space, position) | fe2_space_byte(space, position + 1) << 8);
}

void fe2_space_set_byte(fe2_record_space *space, uint32_t position, uint8_t value)
{
    *space_at(space, position) = value;
}

uint32_t fe2_space_copy_name(fe2_record_space *space, uint32_t position, const char *name)
{
    do fe2_space_set_byte(space, position++, (uint8_t)*name);
    while (*name++ != 0);
    return position;
}

uint32_t fe2_space_copy_within(fe2_record_space *space, uint32_t position, uint32_t source)
{
    uint8_t byte;
    do {
        byte = fe2_space_byte(space, source++);
        fe2_space_set_byte(space, position++, byte);
    } while (byte != 0);
    return position;
}
