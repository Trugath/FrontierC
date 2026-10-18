/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* federal and imperial military rank and permits. */
#include "status_internal.h"

enum {
    RANKS = 13,
    FEDERAL_FIRST_RANK = 0x9c4a, IMPERIAL_FIRST_RANK = 0x9c57,
    FEDERAL_HQ = 0x9c66, IMPERIAL_HQ = 0x9c65,
    FEDERAL_PORTRAIT = 0x36, IMPERIAL_PORTRAIT = 0x26,
    PROMOTION_TEXT = 0x9c36, DEMOTION_TEXT = 0x9c37,
    AWARD_TEXT = 0x9c6a, FIRST_AWARD = 0x9c6b
};

typedef struct navy_text {
    uint16_t first_rank, headquarters, portrait;
} navy_text;

static const navy_text navies[2] = {
    {FEDERAL_FIRST_RANK, FEDERAL_HQ, FEDERAL_PORTRAIT},
    {IMPERIAL_FIRST_RANK, IMPERIAL_HQ, IMPERIAL_PORTRAIT},
};

static uint16_t *points_of(fe2_commander_status *status, fe2_navy navy)
{
    return navy == FE2_NAVY_FEDERAL ? &status->federal_points : &status->imperial_points;
}

/* Rank 0..12; fe2_status_rank_bounds[12 - rank + 1] is its floor and
 * fe2_status_rank_bounds[12 - rank] the next rank's floor (0xffff at the top). */
static unsigned rank_of(uint16_t points)
{
    unsigned index = 1;
    while (index < RANKS && points < fe2_status_rank_bounds[index]) ++index;
    return (unsigned)(RANKS - index);
}

static uint16_t floor_of(unsigned rank) { return fe2_status_rank_bounds[RANKS - rank]; }
static uint16_t ceiling_of(unsigned rank) { return fe2_status_rank_bounds[RANKS - 1 - rank]; }

uint16_t fe2_status_rank_text(const fe2_commander_status *status, fe2_navy navy)
{
    uint16_t points = navy == FE2_NAVY_FEDERAL ? status->federal_points
                                               : status->imperial_points;
    return (uint16_t)(navies[navy].first_rank + rank_of(points));
}

static uint16_t add_saturating(uint16_t points, int16_t delta)
{
    int32_t sum = (int32_t)points + delta;
    return sum < 0 ? 0 : sum > 0xffff ? 0xffff : (uint16_t)sum;
}

/* a permit is announced (with fanfare) only when first granted. */
static void grant_permit(fe2_commander_status *status, const fe2_game_services *game,
                         fe2_call_args args, const navy_text *text, uint16_t permit)
{
    char name[FE2_TEXT_BYTES];
    uint32_t *word = &status->permits[(permit / 32u) % FE2_PERMIT_WORDS];
    uint32_t bit = UINT32_C(1) << (permit % 32u);

    if (*word & bit) return;
    *word |= bit;
    game->format_text(game->opaque, text->headquarters, &args, name);
    fe2_args_subject(&args, PARAM_SUBJECT, name);
    fe2_args_word(&args, PARAM_2, (uint16_t)(FIRST_AWARD + permit));
    game->show_message(game->opaque, AWARD_TEXT, &args);
    game->select_music(game->opaque, TRACK_FANFARE);
}

/* points move at most one rank per award. Saturated points
 * never promote. Returns the rank text after the award. */
static uint16_t update_rank(const fe2_game_services *game, fe2_call_args announcement,
                            const navy_text *text, uint16_t *points, int16_t delta)
{
    char name[FE2_TEXT_BYTES];
    unsigned rank = rank_of(*points);
    uint16_t rank_text = (uint16_t)(text->first_rank + rank);
    uint16_t floor = floor_of(rank), ceiling = ceiling_of(rank);
    uint16_t message;

    *points = add_saturating(*points, delta);
    if (*points >= ceiling && *points != 0xffff) {
        ++rank_text;
        message = PROMOTION_TEXT;
    } else if (*points < floor) {
        --rank_text;
        message = DEMOTION_TEXT;
    } else {
        return rank_text;
    }
    fe2_args_word(&announcement, PARAM_1, rank_text);
    fe2_args_word(&announcement, PARAM_2, *points);
    fe2_args_word(&announcement, PARAM_4, ceiling);
    fe2_args_word(&announcement, PARAM_5, floor);
    game->format_text(game->opaque, text->headquarters, &announcement, name);
    fe2_args_subject(&announcement, PARAM_SUBJECT, name);
    fe2_args_dword(&announcement, PARAM_2, rank_text);
    game->show_message(game->opaque, message, &announcement);
    if (message == PROMOTION_TEXT) game->select_music(game->opaque, TRACK_FANFARE);
    return rank_text;
}

void fe2_status_award(fe2_commander_status *status, const fe2_game_services *game,
                   fe2_call_args *args, fe2_navy navy, int16_t permit, int16_t delta)
{
    const navy_text *text = &navies[navy];
    fe2_call_args announcement = *args;

    fe2_args_word(&announcement, PARAM_3, text->first_rank);
    fe2_args_word(&announcement, PARAM_6, text->headquarters);
    fe2_args_word(&announcement, PARAM_STYLE, text->portrait);
    if (permit >= 0) grant_permit(status, game, announcement, text, (uint16_t)permit);
    fe2_args_word(args, PARAM_1, update_rank(game, announcement, text,
                                             points_of(status, navy), delta));
}
