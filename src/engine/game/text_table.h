/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_GAME_TEXT_TABLE_H
#define FE2_GAME_TEXT_TABLE_H

#include <stdint.h>

/* Opaque view of a serialized text directory. Shared declaration lets
 * independent native modules expose it together under strict C99. */
typedef struct fe2_text_table fe2_text_table;

/* Text composed into the caller's buffer. */
typedef struct fe2_text_out {
    uint8_t *buffer;          /* 64 KB segment */
    uint16_t at;
} fe2_text_out;

#endif
