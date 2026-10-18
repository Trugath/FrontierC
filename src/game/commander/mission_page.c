/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* outstanding military missions. */
#include "status_internal.h"

enum {
    TITLE = 0x9c07,
    FIRST_KILL_TITLE = 0x9c1b,     /* missions 0..2: assassinations */
    KILL_DETAIL = 0x9c1f, KILL_LOCATION = 0x9c1e,
    FIRST_RECON_TITLE = 0x9c1e,    /* missions 3..6: film and bomb */
    RECON_DETAIL = 0x9c25,
    DONE_TEXT = 0x9c20,
    CIVILIAN_TARGET_CLASS = 0x999d, MILITARY_TARGET_CLASS = 0x99a5,
    FIRST_RECON_TARGET = 0x994f,
    TARGET_NAME_MODE = 0x0f,
    TITLE_STYLE = 0x78,
    LIST_X = 2, LIST_TOP = 0x1b, LIST_STEP = 0x18
};

static void draw_title(const fe2_game_services *game, fe2_call_args *args, uint16_t text,
                       uint16_t y)
{
    fe2_args_word(args, PARAM_STYLE, TITLE_STYLE);
    game->draw_text(game->opaque, text, TEXT_COLOUR_WHITE, LIST_X, y, args);
}

static void draw_kill_mission(const fe2_military_contract *mission, unsigned type,
                              const fe2_game_services *game, fe2_call_args *args, uint16_t y,
                              char name[FE2_TEXT_BYTES])
{
    uint16_t target_class = type == 0 ? CIVILIAN_TARGET_CLASS : MILITARY_TARGET_CLASS;
    uint16_t model = (uint16_t)(mission->target_model | mission->quality << 8);

    fe2_args_word(args, PARAM_3, target_class);
    fe2_args_word(args, PARAM_2, (uint16_t)(target_class + (mission->type >> 4)));
    fe2_args_dword(args, PARAM_10, mission->target_seed);
    fe2_args_dword(args, PARAM_1, mission->home_system);
    draw_title(game, args, (uint16_t)(FIRST_KILL_TITLE + type), y);

    fe2_args_string(args, PARAM_11, mission->target_name);
    fe2_args_dword(args, PARAM_1, mission->target_system);
    fe2_status_draw_line(game, args, KILL_LOCATION, LIST_X, y);

    game->generate_name(game->opaque, TARGET_NAME_MODE, mission->target_seed, name);
    fe2_args_string(args, PARAM_10, name);
    /* The model word includes the following quality byte. */
    fe2_args_word(args, PARAM_11, game->ship_type(game->opaque, model)->name_text);
    fe2_args_dword(args, PARAM_1, mission->target_seed);
    fe2_args_dword(args, PARAM_2, mission->deadline);
    fe2_args_dword(args, PARAM_3, mission->reward);
    fe2_status_draw_line(game, args, KILL_DETAIL, LIST_X, y);
}

static void draw_recon_mission(const fe2_military_contract *mission, unsigned type,
                               const fe2_game_services *game, fe2_call_args *args, uint16_t y)
{
    fe2_args_dword(args, PARAM_1, mission->home_system);
    fe2_args_word(args, PARAM_2, (uint16_t)(FIRST_RECON_TARGET + (mission->type >> 4)));
    draw_title(game, args, (uint16_t)(FIRST_RECON_TITLE + type), y);

    fe2_args_string(args, PARAM_11, mission->target_name);
    fe2_args_dword(args, PARAM_1, mission->target_system);
    fe2_args_dword(args, PARAM_2, mission->deadline);
    fe2_args_dword(args, PARAM_3, mission->reward);
    fe2_status_draw_line(game, args, RECON_DETAIL, LIST_X, y);
}

void fe2_status_draw_mission_page(fe2_commander_status *status, const fe2_game_services *game)
{
    fe2_call_args args;
    char name[FE2_TEXT_BYTES];
    uint16_t y = LIST_TOP;
    int remaining = (int16_t)status->military_count;
    const fe2_military_contract *mission = status->military;

    fe2_args_clear(&args);
    fe2_status_draw_heading(status, game, &args, TITLE);
    for (; remaining > 0 && mission < status->military + FE2_MAX_MILITARY_CONTRACTS;
         --remaining, ++mission) {
        unsigned type = mission->type & 0x0f;
        if (type <= 2) draw_kill_mission(mission, type, game, &args, y, name);
        else if (type <= 6) draw_recon_mission(mission, type, game, &args, y);
        if (mission->outcome != 0) fe2_status_draw_line(game, &args, DONE_TEXT, LIST_X, y);
        y = (uint16_t)(y + LIST_STEP);
    }
}
