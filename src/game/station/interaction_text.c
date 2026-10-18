/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the interaction text provider. */
#include "interaction_text.h"

#include "interaction_tables.h"

void fe2_interaction_display(const fe2_interaction_services *game, uint16_t selector,
                             fe2_text_out *out)
{
    game->format_table_text(game->opaque, selector & FE2_INTERACTION_SELECTOR_MASK,
                            (const fe2_text_table *)fe2_interaction_text_text_directory, out);
}

const uint8_t *fe2_interaction_entry(uint16_t index, uint16_t *length)
{
    const uint8_t *entry;
    uint16_t n = 0;
    if (index >= FE2_INTERACTION_ENTRIES) return 0;
    entry = fe2_interaction_text_text_directory + (fe2_interaction_text_text_directory[2 * index] |
                                      fe2_interaction_text_text_directory[2 * index + 1] << 8);
    while (entry[n++] != 0) {}
    if (length) *length = n;
    return entry;
}
