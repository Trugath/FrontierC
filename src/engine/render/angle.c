/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the arccosine lookup (the tables are generated). */
#include "render.h"

#include "render_tables.h"

enum { BIN_SHIFT = 9 };

uint16_t fe2_render_acos(uint16_t quotient)
{
    return (uint16_t)(fe2_render_acos_table[quotient >> BIN_SHIFT] << 1);
}
