/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_INTERACTION_TEXT_INTERACTION_TEXT_H
#define FE2_INTERACTION_TEXT_INTERACTION_TEXT_H

/*
 * Interaction texts (interaction): 462 native text entries (messages such as
 * misjumps and drive failures, selectors 98xxh) and the provider that
 * formats one through the resident text formatter.
 */
#include <stdint.h>

#include "game/text_table.h"

enum { FE2_INTERACTION_ENTRIES = 462, FE2_INTERACTION_SELECTOR_MASK = 0x3fe };

typedef struct fe2_interaction_services {
    void *opaque;
    /* Format entry `selector` / 2 of `table` into `out`. */
    void (*format_table_text)(void *opaque, uint16_t selector, const fe2_text_table *table,
                              fe2_text_out *out);
} fe2_interaction_services;

/* The generated text directory: 462 word offsets relative to its start,
 * then the NUL-terminated entries with their raw formatter controls. */
extern const uint8_t *fe2_interaction_text_text_directory;

/* 0x92: format interaction text `selector` (AX & 0x3fe, unchecked). */
void fe2_interaction_display(const fe2_interaction_services *game, uint16_t selector,
                             fe2_text_out *out);
/* Entry `index` (raw bytes up to and including the NUL); null past the
 * last entry. */
const uint8_t *fe2_interaction_entry(uint16_t index, uint16_t *length);

#endif
