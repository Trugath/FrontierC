/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* crew roster, wages owed and the next pay day. */
#include "status_internal.h"

enum {
    TITLE = 0x9c06, MEMBER_TEXT = 0x9c16, OWED_TEXT = 0x9c17, PAYDAY_TEXT = 0x9c18,
    LIST_X = 0x18, LIST_TOP = 0x23, LIST_STEP = 10, ICON_DX = 0x12,
    OWED_X = 6, OWED_Y = 0x8c, PAYDAY_X = 6, PAYDAY_Y = 0x9b,
    DAYS_PER_WEEK = 7
};

/* Wages fall due at the start of each seventh day. Only the low word of the
 * day counter is rounded, without carry into the high word. */
static uint32_t next_payday(uint32_t day)
{
    uint16_t low = (uint16_t)day;
    uint16_t into_week = (uint16_t)(low % DAYS_PER_WEEK);
    if (into_week == 0) return day;
    return (day & UINT32_C(0xffff0000)) | (uint16_t)(low - into_week + DAYS_PER_WEEK);
}

void fe2_status_draw_crew_page(fe2_commander_status *status, const fe2_game_services *game)
{
    fe2_call_args args;
    uint16_t y = LIST_TOP;
    int remaining = (int16_t)status->crew_count;
    const fe2_crew_member *member = status->crew;
    int has_crew = remaining > 0;

    fe2_args_clear(&args);
    fe2_status_draw_heading(status, game, &args, TITLE);
    for (; remaining > 0 && member < status->crew + FE2_MAX_CREW; --remaining, ++member) {
        fe2_args_dword(&args, PARAM_10, member->name_seed);
        fe2_args_dword(&args, PARAM_3, (uint16_t)member->salary);
        game->draw_icon(game->opaque, LIST_ICON, LIST_X - ICON_DX, (uint16_t)(y - 1));
        fe2_status_draw_line(game, &args, MEMBER_TEXT, LIST_X, y);
        y = (uint16_t)(y + LIST_STEP);
    }
    /* Wages owed are only listed while there is crew. */
    if (has_crew && status->unpaid_wages != 0) {
        fe2_args_dword(&args, PARAM_1, status->unpaid_wages);
        fe2_status_draw_line(game, &args, OWED_TEXT, OWED_X, OWED_Y);
    }
    fe2_args_dword(&args, PARAM_1, next_payday(status->day));
    fe2_args_dword(&args, PARAM_2, (uint32_t)(int32_t)(int16_t)status->crew_needed);
    fe2_status_draw_line(game, &args, PAYDAY_TEXT, PAYDAY_X, PAYDAY_Y);
    fe2_status_draw_balance(status, game, &args);
    fe2_status_draw_preview_frame(game);
}
