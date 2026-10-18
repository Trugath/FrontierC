/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* commander profile - legal status, ratings, ranks, offences, permits. */
#include "status_internal.h"

enum {
    TITLE = 0x9c05, STANDING_TEXT = 0x9c39, RANK_TEXT = 0x9c3a,
    FIRST_CRIMINAL_GRADE = 0x9c45,  /* Clean; then Offender..Fugitive */
    FIRST_COMBAT_RATING = 0x9c3b,   /* Harmless; then Mostly Harmless..Elite */
    FIRST_AUTHORITY_NAME = 0x9c67,
    FIRST_OFFENCE = 0x9e02, FIRST_PERMIT = 0x9c6b,
    STANDING_X = 2, STANDING_Y = 0x14, RANK_X = 2, RANK_Y = 0x1e,
    OFFENCE_X = 4, OFFENCE_TOP = 0x4b, OFFENCE_COLUMN = 5, OFFENCE_SECOND_X = 0x6c,
    PERMIT_X = 4, PERMIT_TOP = 0x84, PERMIT_BOTTOM = 0xa1, PERMIT_COLUMN_WIDTH = 0x4c,
    ROW_STEP = 9
};

/* Number of descending thresholds that `value` reaches. */
static unsigned grade(uint32_t value, const uint32_t *thresholds, unsigned count)
{
    unsigned skipped = 0;
    while (skipped < count && value < thresholds[skipped]) ++skipped;
    return count - skipped;
}

static void draw_standing(fe2_commander_status *status, const fe2_game_services *game,
                          fe2_call_args *args)
{
    const uint32_t *record = status->criminal_record;
    fe2_args_word(args, PARAM_2, (uint16_t)(FIRST_CRIMINAL_GRADE + grade(record[2],
                  fe2_status_criminal_thresholds, 4)));
    fe2_args_word(args, PARAM_3, (uint16_t)(FIRST_CRIMINAL_GRADE + grade(record[1],
                  fe2_status_criminal_thresholds, 4)));
    fe2_args_word(args, PARAM_10, (uint16_t)(FIRST_CRIMINAL_GRADE + grade(record[0],
                  fe2_status_criminal_thresholds, 4)));
    fe2_args_word(args, PARAM_11, (uint16_t)(FIRST_AUTHORITY_NAME + status->legal_authority));
    fe2_args_word(args, PARAM_1, (uint16_t)(FIRST_COMBAT_RATING + grade(status->elite_points,
                  fe2_status_combat_thresholds, 9)));
    fe2_status_draw_line(game, args, STANDING_TEXT, STANDING_X, STANDING_Y);
}

static void draw_ranks(fe2_commander_status *status, const fe2_game_services *game,
                       fe2_call_args *args)
{
    /* The original runs a zero award for each navy and reads the rank text
     * it leaves behind; a zero award changes nothing. */
    fe2_args_word(args, PARAM_0, 0);
    fe2_args_word(args, PARAM_4, 0xffff);
    fe2_args_word(args, PARAM_2, fe2_status_rank_text(status, FE2_NAVY_IMPERIAL));
    fe2_args_word(args, PARAM_1, fe2_status_rank_text(status, FE2_NAVY_FEDERAL));
    fe2_status_draw_line(game, args, RANK_TEXT, RANK_X, RANK_Y);
}

/* Offences of the selected authority, five to a column. */
static void draw_offences(const fe2_commander_status *status, const fe2_game_services *game,
                        fe2_call_args *args)
{
    uint16_t offences = status->offences[status->legal_authority];
    uint16_t x = OFFENCE_X, y = OFFENCE_TOP, drawn = 0;
    unsigned offence;

    fe2_args_word(args, PARAM_4, 0);
    for (offence = 0; offences != 0; ++offence, offences >>= 1) {
        if ((offences & 1u) == 0) continue;
        fe2_status_draw_line(game, args, (uint16_t)(FIRST_OFFENCE + offence), x, y);
        y = (uint16_t)(y + ROW_STEP);
        fe2_args_word(args, PARAM_4, ++drawn);
        if (drawn == OFFENCE_COLUMN) {
            x = OFFENCE_SECOND_X;
            y = OFFENCE_TOP;
        }
    }
}

static void draw_permits(const fe2_commander_status *status, const fe2_game_services *game,
                         fe2_call_args *args)
{
    uint32_t permits = status->permits[0];
    uint16_t x = PERMIT_X, y = PERMIT_TOP;
    unsigned permit;

    for (permit = 0; permits != 0; ++permit, permits >>= 1) {
        if ((permits & 1u) == 0) continue;
        fe2_status_draw_line(game, args, (uint16_t)(FIRST_PERMIT + permit), x, y);
        y = (uint16_t)(y + ROW_STEP);
        if (y >= PERMIT_BOTTOM) {
            x = (uint16_t)(x + PERMIT_COLUMN_WIDTH);
            y = PERMIT_TOP;
        }
    }
}

void fe2_status_draw_profile_page(fe2_commander_status *status, const fe2_game_services *game)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_string(&args, PARAM_10, status->commander_name);
    fe2_status_draw_heading(status, game, &args, TITLE);
    draw_standing(status, game, &args);
    draw_ranks(status, game, &args);
    draw_offences(status, game, &args);
    draw_permits(status, game, &args);
    fe2_status_draw_preview_frame(game);
}
