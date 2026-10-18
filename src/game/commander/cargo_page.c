/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* cargo inventory in two columns. */
#include "status_internal.h"

enum {
    TITLE = 0x9c03, FIRST_COLUMN_TEXT = 0x9c10, SECOND_COLUMN_TEXT = 0x9c11,
    FIRST_CARGO_NAME = 0x8e00,
    FIRST_X = 0x12, SECOND_X = 0x6a, LIST_TOP = 0x23, LIST_BOTTOM = 0xa5, LIST_STEP = 10,
    ICON_DX = 0x0c, REFUEL_ICON_X = 0x78, REFUEL_ICON_Y = 0x0f
};

void fe2_status_draw_cargo_page(fe2_commander_status *status, const fe2_game_services *game)
{
    fe2_call_args args;
    uint16_t text = FIRST_COLUMN_TEXT, x = FIRST_X, y = LIST_TOP;
    unsigned type;

    fe2_args_clear(&args);
    game->clear_panel(game->opaque);
    fe2_status_draw_heading(status, game, &args, TITLE);
    for (type = 0; type < FE2_CARGO_TYPES; ++type) {
        uint16_t tonnes = status->cargo[type];
        if (tonnes == 0) continue;
        fe2_args_dword(&args, PARAM_2, tonnes);
        if (y >= LIST_BOTTOM) {
            if (text == SECOND_COLUMN_TEXT) break;
            text = SECOND_COLUMN_TEXT;
            x = SECOND_X;
            y = LIST_TOP;
        }
        game->draw_icon(game->opaque, LIST_ICON, (uint16_t)(x - ICON_DX), (uint16_t)(y - 1));
        fe2_args_word(&args, PARAM_1, (uint16_t)(FIRST_CARGO_NAME + type));
        fe2_status_draw_line(game, &args, text, x, y);
        y = (uint16_t)(y + LIST_STEP);
    }
    fe2_status_draw_balance(status, game, &args);
    game->draw_icon(game->opaque, LIST_ICON, REFUEL_ICON_X, REFUEL_ICON_Y);
    fe2_status_draw_preview_frame(game);
}
