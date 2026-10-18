/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* mining machines, their systems and prospects. */
#include "status_internal.h"

enum {
    TITLE = 0x9c08, DEPLOYED_TEXT = 0xa200, STOWED_TEXT = 0xa201, FIRST_PROSPECT = 0xa202,
    LIST_X = 0x18, LIST_TOP = 0x23, LIST_STEP = 10, ICON_DX = 0x12
};

void fe2_status_draw_mining_page(fe2_commander_status *status, const fe2_game_services *game)
{
    fe2_call_args args;
    uint16_t y = LIST_TOP;
    int remaining = (int16_t)status->mining_count;
    const fe2_mining_unit *unit = status->mining;

    fe2_args_clear(&args);
    fe2_status_draw_heading(status, game, &args, TITLE);
    for (; remaining > 0 && unit < status->mining + FE2_MAX_MINING_UNITS; --remaining, ++unit) {
        fe2_args_string(&args, PARAM_10, unit->name);
        fe2_args_dword(&args, PARAM_1, unit->system);
        fe2_args_word(&args, PARAM_2, (uint16_t)(FIRST_PROSPECT + unit->prospect));
        game->draw_icon(game->opaque, LIST_ICON, LIST_X - ICON_DX, (uint16_t)(y - 1));
        fe2_status_draw_line(game, &args, unit->system ? DEPLOYED_TEXT : STOWED_TEXT, LIST_X, y);
        y = (uint16_t)(y + LIST_STEP);
    }
    fe2_status_draw_balance(status, game, &args);
    fe2_status_draw_preview_frame(game);
}
