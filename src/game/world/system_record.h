/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SYSTEMS_SYSTEM_RECORD_H
#define FE2_SYSTEMS_SYSTEM_RECORD_H

/*
 * Compact 64-byte system records: one per star, planet, moon or station of a
 * generated or handcrafted system, later instantiated as runtime objects.
 *
 * Records keep their native byte layout. The original copies them whole,
 * copies dwords that straddle fields and reads words at computed byte
 * offsets, so fields are accessed by offset and explicit width rather than
 * through a compiler-laid-out structure.
 */
#include <stdint.h>

enum { FE2_RECORD_BYTES = 64, FE2_RECORD_NAME_BYTES = 22 };

typedef struct fe2_system_record {
    uint8_t bytes[FE2_RECORD_BYTES];
} fe2_system_record;

/* Field offsets, named from generation, chart and orbit consumers.
 * Reserved words are preserved for binary compatibility. */
enum fe2_record_field {
    FE2_RECORD_SIZE_MANTISSA = 0x00,   /* leading normalized size, bias +63 */
    FE2_RECORD_SIZE_EXPONENT = 0x02,
    FE2_RECORD_SEED = 0x04,            /* dword: name seed / identity */
    FE2_RECORD_SEED_HIGH = 0x06,
    FE2_RECORD_PARENT = 0x08,          /* one-based parent index, 0 = root */
    FE2_RECORD_KIND = 0x0a,
    FE2_RECORD_DESCRIPTOR = 0x0c,      /* model */
    FE2_RECORD_SCALAR_MANTISSA = 0x0e, /* normalized scalar, bias +25 */
    FE2_RECORD_SCALAR_EXPONENT = 0x10,
    FE2_RECORD_ORBIT_LONGITUDE = 0x12,
    FE2_RECORD_DERIVED_MANTISSA = 0x14,
    FE2_RECORD_DERIVED_EXPONENT = 0x16,
    FE2_RECORD_PROFILE_OR_OBJECT_REFERENCE = 0x18,        /* profile value; runtime object pointer */
    FE2_RECORD_ORBIT_ECCENTRICITY = 0x1c,
    FE2_RECORD_ORBIT_INCLINATION = 0x1e,
    FE2_RECORD_TEMPERATURE = 0x20,
    FE2_RECORD_NAME = 0x22,
    FE2_RECORD_TEMPLATE_RESERVED = 0x38,
    FE2_RECORD_ORBIT_SHIFT = 0x3a,     /* byte */
    FE2_RECORD_GENERATION = 0x3b,      /* byte: bit 7 generated name, depth */
    FE2_RECORD_GENERATION_RESERVED = 0x3c,
    FE2_RECORD_BASIS_SEED = 0x3e
};

enum {
    FE2_RECORD_KIND_VERBATIM = 0x46,   /* copied unconverted; special objects */
    FE2_RECORD_GENERATED_NAME = 0x80
};

uint8_t fe2_record_byte(const fe2_system_record *record, unsigned offset);
uint16_t fe2_record_word(const fe2_system_record *record, unsigned offset);
uint32_t fe2_record_dword(const fe2_system_record *record, unsigned offset);
void fe2_record_set_byte(fe2_system_record *record, unsigned offset, uint8_t value);
void fe2_record_set_word(fe2_system_record *record, unsigned offset, uint16_t value);
void fe2_record_set_dword(fe2_system_record *record, unsigned offset, uint32_t value);

/* The record's name field as a C string (not bounded by the original). */
char *fe2_record_name(fe2_system_record *record);
const char *fe2_record_name_const(const fe2_system_record *record);

/* A run of records in one segment. Byte positions may cross records: the
 * original reads words at computed offsets beyond the current record. */
typedef struct fe2_record_space {
    fe2_system_record *records;
    unsigned count;
} fe2_record_space;

uint8_t fe2_space_byte(const fe2_record_space *space, uint32_t position);
uint16_t fe2_space_word(const fe2_record_space *space, uint32_t position);
void fe2_space_set_byte(fe2_record_space *space, uint32_t position, uint8_t value);
/* Copy a NUL-terminated name to `position` without a length bound, as the
 * original does. Returns the position after the copied NUL. */
uint32_t fe2_space_copy_name(fe2_record_space *space, uint32_t position, const char *name);
/* The same from another position of the space. */
uint32_t fe2_space_copy_within(fe2_record_space *space, uint32_t position, uint32_t source);

#endif
