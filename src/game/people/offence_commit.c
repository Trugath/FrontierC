/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* commit an offence; 0x7d4: the police search. */
#include "offence_internal.h"

enum {
    INDEX_PARAMETER = 0, SCORE_PARAMETER = 1, TEXT_PARAMETER = 2, FINE_PARAMETER = 3,
    FLAGS_PARAMETER = 4, THRESHOLD_PARAMETER = 5, PRIOR_PARAMETER = 6, STYLE_PARAMETER = 7,
    SUBJECT_PARAMETER = 8, OBJECT_PARAMETER = 9,
    FIRST_FINE_TEXT = 0x9e00, ANOTHER_FINE_TEXT = 0x9e01, OFFENCE_TEXT = 0x9e02,
    MESSAGE_STYLE = 0x3a, FINE_EFFECT = 0x26, ALERT_MUSIC = 0xfffe,
    NOBODY = 0x7f, CERTAIN_THRESHOLD = 0x32
};

/* police bodies (flag 0x10) compete for the station by level; police
 * bodies and patrols (marker 0xa) set the lowest level seen. Equal levels
 * take the later object. */
void fe2_offence_search_object(void *pass, fe2_object *o)
{
    fe2_offence_state *s = ((fe2_offence_pass *)pass)->state;
    int8_t level = (int8_t)o->level;
    if (o->role == ROLE_SPECIAL) {
        if (o->map_marker != PATROL_MARKER) return;
    } else if (o->role == ROLE_BODY && (o->flags & POLICE_BODY)) {
        if (level <= s->station_level) {
            s->police_station = o;
            s->station_level = level;
        }
    } else {
        return;
    }
    if (level <= s->lowest_level) {
        s->lowest_handle = o->handle;
        s->lowest_level = level;
    }
}

/*
 * 0x7d4: the police station that responds, or null. The search fails
 * without a station. It succeeds when the lowest level is within the
 * threshold (as bytes), or the threshold is above 0x32; otherwise a random
 * 0..7 must exceed the (byte) difference.
 */
static fe2_object *find_police(fe2_offence_state *s, const fe2_offence_services *game,
                               uint16_t threshold)
{
    fe2_offence_pass pass;
    int8_t margin;
    s->lowest_level = NOBODY;
    s->station_level = NOBODY;
    s->lowest_handle = 0;
    s->police_station = 0;
    pass.state = s;
    pass.game = game;
    game->visit_objects(game->opaque, s->objects, fe2_offence_search_object, &pass);
    if (!s->police_station) return 0;
    margin = (int8_t)(uint8_t)((uint8_t)s->lowest_level - (uint8_t)threshold);
    if (s->lowest_level > (int8_t)(uint8_t)threshold && (int16_t)threshold <= CERTAIN_THRESHOLD &&
        (int8_t)(game->random(game->opaque) & 7u) <= margin)
        return 0;
    return s->police_station;
}

/* Ten times the fine's unsigned word, unless that overflows the signed
 * local fine. */
static void add_fine(fe2_offence_state *s, int16_t fine)
{
    uint32_t amount = (uint32_t)(uint16_t)fine * 10u, total = s->local_fine + amount;
    if (!(((s->local_fine ^ total) & (amount ^ total)) & 0x80000000u)) s->local_fine = total;
}

static void tell(fe2_offence_state *s, const fe2_offence_services *game, uint16_t offence,
                 const fe2_offence *row, uint16_t threshold, uint32_t prior, fe2_object *subject)
{
    fe2_call_args args;
    s->fined_handle = subject->handle;
    fe2_args_clear(&args);
    fe2_args_word(&args, INDEX_PARAMETER, offence);
    fe2_args_word(&args, SCORE_PARAMETER, row->score);
    fe2_args_word(&args, TEXT_PARAMETER, (uint16_t)(OFFENCE_TEXT + offence));
    fe2_args_dword(&args, FINE_PARAMETER, (uint32_t)(int32_t)row->fine);
    fe2_args_word(&args, FLAGS_PARAMETER, row->flags);
    fe2_args_word(&args, THRESHOLD_PARAMETER, threshold);
    fe2_args_dword(&args, PRIOR_PARAMETER, prior);
    fe2_args_word(&args, STYLE_PARAMETER, MESSAGE_STYLE);
    fe2_args_object(&args, SUBJECT_PARAMETER, subject);
    fe2_args_object(&args, OBJECT_PARAMETER, subject);
    game->show_message(game->opaque, prior == 0 ? FIRST_FINE_TEXT : ANOTHER_FINE_TEXT, &args);
    game->play_effect(game->opaque, FINE_EFFECT);
}

void fe2_offence_commit(fe2_offence_state *s, const fe2_offence_services *game, uint16_t offence,
                        fe2_object *subject)
{
    const fe2_offence *row = &fe2_offences_offences[offence];
    uint16_t threshold = row->threshold, *score;
    uint32_t prior;
    if (row->flags & FE2_OFFENCE_FIND_POLICE) {
        threshold = (uint16_t)(threshold + s->law_level);
        subject = find_police(s, game, threshold);
        if (!subject) return;
    }
    s->police_source = subject;
    prior = s->local_fine;
    add_fine(s, row->fine);
    score = &s->offence_score[s->legal_authority];
    *score = (uint16_t)(*score + row->score) < *score ? 0xffff : (uint16_t)(*score + row->score);
    s->offences[s->legal_authority] |= (uint16_t)(1u << offence);
    if (row->flags & FE2_OFFENCE_MESSAGE) tell(s, game, offence, row, threshold, prior, subject);
    if (row->flags & FE2_OFFENCE_ALERT) {
        s->police_flags |= ALERTED;
        if (s->alert_music == 0) game->check_music(game->opaque);
        else game->select_music(game->opaque, ALERT_MUSIC);
    }
}
