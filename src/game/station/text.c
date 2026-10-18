/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Text (0x92 / 0x4c2b): interaction texts come from interaction. */
#include "station_internal.h"

void fe2_station_format_text(const fe2_station_services *game, uint16_t selector, fe2_text_out *out)
{
    game->interaction_text_text(game->opaque, selector, out);
}
