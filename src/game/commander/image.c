/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* commander's own fields inside the resident image a commander file holds
 * (little-endian). A loaded file replaces them; a save writes them first. */
#include <stddef.h>

#include "commander_internal.h"

typedef struct image_field {
    uint16_t offset;                  /* in the resident image (DS) */
    size_t at;                        /* in fe2_commander_state */
    uint8_t element;                  /* bytes per element */
    uint8_t count;
} image_field;

#define FIELD(offset, member, element, count) {offset, offsetof(fe2_commander_state, member), element, count}

static const image_field fields[] = {
    FIELD(0x1fc9, time_accumulator, 4, 1), FIELD(0x1fd1, detail, 2, 3), FIELD(0x200b, game_flags, 1, 1),
    FIELD(0x2016, expected_letter, 1, 1), FIELD(0x201b, event_base, 4, 1), FIELD(0x204f, event_time, 4, 1),
    FIELD(0x2075, flight_state, 1, 1), FIELD(0x2109, upper_name, 1, FE2_COMMANDER_FILENAME_BYTES),
    FIELD(0x219e, input_mask, 1, 1), FIELD(0x4623, time_step, 4, 1), FIELD(0x4627, time_exponent, 2, 1),
    FIELD(0x4629, preferences, 1, FE2_COMMANDER_PREFERENCES), FIELD(0x464a, menu_mode, 1, 1),
    FIELD(0x4651, panel_flags, 1, 1), FIELD(0x4b0f, current_message, 2, 1),
    FIELD(0x53a3, current_music, 2, 1), FIELD(0x55a5, commander_name, 1, FE2_COMMANDER_FILENAME_BYTES),
    FIELD(0x55b9, extension, 1, FE2_COMMANDER_EXTENSION_BYTES)
};

static uint32_t get_value(const uint8_t *member, unsigned element)
{
    if (element == 4) return *(const uint32_t *)(const void *)member;
    if (element == 2) return *(const uint16_t *)(const void *)member;
    return *member;
}

static void put_value(uint8_t *member, unsigned element, uint32_t value)
{
    if (element == 4) *(uint32_t *)(void *)member = value;
    else if (element == 2) *(uint16_t *)(void *)member = (uint16_t)value;
    else *member = (uint8_t)value;
}

void fe2_commander_read_image(fe2_commander_state *s, const fe2_save_image *image)
{
    unsigned i, n, b;
    for (i = 0; i < sizeof fields / sizeof fields[0]; ++i) {
        const image_field *f = &fields[i];
        const uint8_t *from = image->resident + (f->offset - FE2_SAVE_RESIDENT_START);
        for (n = 0; n < f->count; ++n, from += f->element) {
            uint32_t value = 0;
            for (b = 0; b < f->element; ++b) value |= (uint32_t)from[b] << (8 * b);
            put_value((uint8_t *)s + f->at + n * f->element, f->element, value);
        }
    }
}

void fe2_commander_write_image(const fe2_commander_state *s, fe2_save_image *image)
{
    unsigned i, n, b;
    for (i = 0; i < sizeof fields / sizeof fields[0]; ++i) {
        const image_field *f = &fields[i];
        uint8_t *to = image->resident + (f->offset - FE2_SAVE_RESIDENT_START);
        for (n = 0; n < f->count; ++n, to += f->element) {
            uint32_t value = get_value((const uint8_t *)s + f->at + n * f->element, f->element);
            for (b = 0; b < f->element; ++b) to[b] = (uint8_t)(value >> (8 * b));
        }
    }
}
