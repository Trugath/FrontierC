/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the driver's vectors. */
#include "speaker_internal.h"

enum { UI_EFFECT = 20, SILENCE = 0, INITIAL_FLAGS = FE2_SPEAKER_STOPPED | FE2_SPEAKER_DISCONNECTED };

void fe2_speaker_init(fe2_speaker *s, const fe2_speaker_sounds *sounds, const fe2_speaker_game *game)
{
    unsigned i;
    s->sounds = sounds;
    s->game = game;
    s->flags = 0;
    s->record = sounds->effects[SILENCE];
    s->command = 0;
    s->note = s->duration = s->pause = s->wait = 0;
    for (i = 0; i < FE2_SPEAKER_LOOPS; ++i) s->loops[i].start = 0, s->loops[i].count = 0;
    s->depth = 0;
    s->noise = 0;
    s->updating = 0;
}

/* Starts silent: the first update reads the silence record. */
void fe2_speaker_initialize(fe2_speaker *s)
{
    s->flags = INITIAL_FLAGS;
    s->game->set_driver_active(s->game->opaque, 1);
    fe2_speaker_select(s, SILENCE);
}

void fe2_speaker_shutdown(fe2_speaker *s)
{
    fe2_speaker_select(s, SILENCE);
    fe2_speaker_read_record(s);
    s->game->set_driver_active(s->game->opaque, 0);
}

static void play(fe2_speaker *s, uint16_t effect)
{
    uint8_t sound;
    if (effect >= FE2_SPEAKER_GAME_EFFECTS) return;
    sound = s->sounds->game_effects[effect];
    if (!(sound & FE2_SPEAKER_NO_EFFECT)) fe2_speaker_select(s, sound);
}

/* Game effect 20 has its own preference. */
void fe2_speaker_play_effect(fe2_speaker *s, uint16_t effect)
{
    const fe2_speaker_game *game = s->game;
    if (!game->sound_effects_on(game->opaque)) return;
    if (effect == UI_EFFECT && !game->ui_effect_on(game->opaque)) return;
    play(s, effect);
}

void fe2_speaker_play_with_duration(fe2_speaker *s, uint16_t effect, uint16_t duration)
{
    if (!s->game->sound_effects_on(s->game->opaque) || duration == 0) return;
    play(s, effect);
}

/* Only while the game's time runs. */
void fe2_speaker_play_level(fe2_speaker *s, uint16_t effect, uint8_t level)
{
    const fe2_speaker_game *game = s->game;
    if (!game->sound_effects_on(game->opaque) || level >= FE2_SPEAKER_LEVELS) return;
    if (!game->time_running(game->opaque)) return;
    fe2_speaker_play_with_duration(s, effect, s->sounds->level_durations[level]);
}

void fe2_speaker_update(fe2_speaker *s)
{
    if (s->updating) return;
    s->updating = 1;
    if (!(s->flags & FE2_SPEAKER_STOPPED)) {
        if (s->flags & FE2_SPEAKER_NEXT_RECORD) fe2_speaker_read_record(s);
        fe2_speaker_advance(s);
    }
    s->updating = 0;
}

/* Silence: the speaker disconnects at the next update. */
void fe2_speaker_stop(fe2_speaker *s) { fe2_speaker_select(s, SILENCE); }
