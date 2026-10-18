/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* record lookup; the record compaction shared by removals. */
#include <string.h>

#include "mining_internal.h"

uint8_t *fe2_mining_record(fe2_mining_state *s, unsigned index)
{
    return s->records.bytes + index * FE2_MINING_RECORD_BYTES;
}

uint32_t fe2_mining_get32(const uint8_t *at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

void fe2_mining_put32(uint8_t *at, uint32_t value)
{
    unsigned i;
    for (i = 0; i < 4; ++i) at[i] = (uint8_t)(value >> (8 * i));
}

int fe2_mining_find_free(fe2_mining_state *s)
{
    unsigned i;
    for (i = 0; i < s->count; ++i)
        if (fe2_mining_get32(fe2_mining_record(s, i) + FE2_MINING_SYSTEM) == 0) return (int)i;
    return NO_RECORD;
}

/* Free records are not skipped. */
int fe2_mining_find_machine(fe2_mining_state *s, uint8_t handle)
{
    unsigned i;
    for (i = 0; i < s->count; ++i)
        if (fe2_mining_record(s, i)[FE2_MINING_OBJECT] == handle) return (int)i;
    return NO_RECORD;
}

/* The original copies words at descending odd offsets (0x33..1) from the
 * next record: the following records move down one slot, except that the
 * removed record keeps its first byte, and each moved record's last byte is
 * the first byte of the record after it (for the last, the slot past the
 * count). */
void fe2_mining_close_gap(fe2_mining_state *s, unsigned index)
{
    unsigned following = s->count - 1u - index;
    memmove(fe2_mining_record(s, index) + 1, fe2_mining_record(s, index + 1) + 1,
            following * FE2_MINING_RECORD_BYTES);
}

void fe2_mining_message(const fe2_mining_services *game, uint16_t text, uint16_t amount,
                     fe2_call_args *args)
{
    fe2_args_word(args, STYLE_PARAMETER, 0xffff);
    game->show_message(game->opaque, text, amount, args);
}
