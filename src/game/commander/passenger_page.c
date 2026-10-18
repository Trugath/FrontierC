/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* passengers and small items (first thirteen contracts). */
#include "status_internal.h"

enum {
    TITLE = 0x9c04,
    PASSENGER_TEXT = 0x9c12, PARCEL_TEXT = 0x9c13,
    FEDERAL_TEXT = 0x9c14, IMPERIAL_TEXT = 0x9c15,
    LIST_X = 2, LIST_TOP = 0x23, LIST_STEP = 10, VISIBLE_ROWS = 13
};

void fe2_status_draw_passenger_page(fe2_commander_status *status, const fe2_game_services *game)
{
    fe2_call_args args;
    uint16_t y = LIST_TOP;
    int rows = (int16_t)status->contract_count;
    const fe2_contract *contract = status->contracts;

    fe2_args_clear(&args);
    game->clear_panel(game->opaque);
    fe2_status_draw_heading(status, game, &args, TITLE);
    if (rows > VISIBLE_ROWS) rows = VISIBLE_ROWS;
    for (; rows > 0; --rows, ++contract) {
        uint16_t text = (contract->kind & FE2_CONTRACT_PREPAID) ? PARCEL_TEXT : PASSENGER_TEXT;
        fe2_args_dword(&args, PARAM_3, contract->fee);
        fe2_args_dword(&args, PARAM_4, (uint16_t)(contract->group | contract->kind << 8));
        fe2_args_dword(&args, PARAM_1, contract->destination);
        fe2_args_dword(&args, PARAM_2, contract->deadline);
        fe2_args_dword(&args, PARAM_10, contract->client);
        if (contract->client == 0)
            text = (contract->kind & FE2_CONTRACT_IMPERIAL) ? IMPERIAL_TEXT : FEDERAL_TEXT;
        else
            fe2_args_dword(&args, PARAM_2, (uint16_t)((contract->group >> 2) + 1));
        fe2_status_draw_line(game, &args, text, LIST_X, y);
        y = (uint16_t)(y + LIST_STEP);
    }
    fe2_status_draw_balance(status, game, &args);
    fe2_status_draw_preview_frame(game);
}
