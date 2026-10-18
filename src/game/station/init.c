/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Initialize (0x6e / 0x78e). */
#include "station_internal.h"

void fe2_station_initialize(fe2_station_state *state, fe2_call_args *args)
{
    (void)args;
    state->rows[0].text = 0;
    state->rows[0].text_high = 0;
    state->screen_active = 0;
    state->screen_title = 0;
    state->ui_flags = 0;
    state->queue_cursor = 0;
    state->event_word = 0;
    state->deferred = 0;
}
