/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SPEAKER_H
#define FE2_SPEAKER_H

/*
 * The PC-speaker sound driver (speaker).
 *
 * A game effect maps to one of the driver's effects. An effect is a list
 * of records, each a pattern with a starting note and a duration in
 * updates; a pattern is a small script of note steps, waits, loops and
 * noise. The game calls `fe2_speaker_update` on its timer, and each update
 * steps the script, setting the PIT divisor for the note and connecting or
 * disconnecting the speaker.
 *
 * The game's preferences and timer state, and the hardware, are reached
 * through `fe2_speaker_game`.
 */
#include <stdint.h>

enum {
    FE2_SPEAKER_GAME_EFFECTS = 50,
    FE2_SPEAKER_LEVELS = 15,
    FE2_SPEAKER_NOTES = 256,
    FE2_SPEAKER_EFFECTS = 9,
    FE2_SPEAKER_PATTERNS = 10,
    FE2_SPEAKER_LOOPS = 4,
    FE2_SPEAKER_NO_EFFECT = 0x80       /* bit 7 of a game effect's mapping */
};

/* Effect records: a pattern, a note (FE2_SPEAKER_KEEP_NOTE keeps the
 * current one) and a duration (0 holds the note until the pattern
 * finishes it); or a pause and its length; or the end. */
enum { FE2_SPEAKER_END = 0xff, FE2_SPEAKER_PAUSE = 0xfe, FE2_SPEAKER_KEEP_NOTE = 0xff };

/* Pattern commands; any other byte is a note step (added to the note). */
enum {
    FE2_SPEAKER_WAIT = 0x80,           /* then the number of updates */
    FE2_SPEAKER_LOOP = 0x81,           /* then the count (0 is 256) */
    FE2_SPEAKER_LOOP_END = 0x82,
    FE2_SPEAKER_FINISH_NOTE = 0x83,
    FE2_SPEAKER_NOISE_ON = 0x7f,
    FE2_SPEAKER_NOISE_OFF = 0x7e
};

/* The driver's sounds. */
typedef struct fe2_speaker_sounds {
    const uint8_t *game_effects;       /* game effect -> effect, or FE2_SPEAKER_NO_EFFECT */
    const uint8_t *level_durations;    /* a level's duration */
    const uint16_t *divisors;          /* each note's PIT divisor */
    const uint8_t *const *effects;     /* each effect's records */
    const uint8_t *const *patterns;    /* each pattern's commands */
} fe2_speaker_sounds;

extern const fe2_speaker_sounds fe2_speaker_default_sounds;

typedef struct fe2_speaker_game {
    void *opaque;
    int (*sound_effects_on)(void *opaque);          /* the sound effects preference, 0x4636 */
    int (*ui_effect_on)(void *opaque);              /* the preference for game effect 20, 0x463a */
    int (*time_running)(void *opaque);              /* the game's time step  is nonzero */
    void (*set_driver_active)(void *opaque, int active);   /* bit 7 of the sound selection, 0x8920 */
    /* Hardware. */
    void (*set_pitch)(void *opaque, uint16_t divisor);     /* PIT channel 2, square wave */
    void (*connect_speaker)(void *opaque);          /* port 0x61: set the gate and data bits */
    void (*disconnect_speaker)(void *opaque);       /* port 0x61: clear the gate bit */
} fe2_speaker_game;

/* Driver flags. */
enum {
    FE2_SPEAKER_STOPPED = 0x01,        /* updates do nothing */
    FE2_SPEAKER_NEXT_RECORD = 0x02,    /* the next update reads the effect's next record */
    FE2_SPEAKER_DISCONNECTED = 0x04,   /* the next note connects the speaker */
    FE2_SPEAKER_NOISE = 0x08,          /* note steps are noise */
    FE2_SPEAKER_HOLD = 0x10            /* the note lasts until the pattern finishes it */
};

typedef struct fe2_speaker_loop {
    const uint8_t *start;              /* the first command of the loop's body */
    uint8_t count;
} fe2_speaker_loop;

typedef struct fe2_speaker {
    const fe2_speaker_sounds *sounds;
    const fe2_speaker_game *game;
    uint8_t flags;
    const uint8_t *record;             /* the effect's next record */
    const uint8_t *command;            /* the pattern's next command, or 0 before any */
    uint8_t note;
    uint8_t duration;                  /* updates left of the note */
    uint8_t pause;                     /* updates left of an effect pause */
    uint8_t wait;                      /* updates left of a pattern wait */
    fe2_speaker_loop loops[FE2_SPEAKER_LOOPS];
    unsigned depth;
    uint16_t noise;                    /* the noise generator's state */
    uint8_t updating;                  /* an update is running (reentry guard) */
} fe2_speaker;

/* Construction (loading the driver): stopped, with no pattern. */
void fe2_speaker_init(fe2_speaker *speaker, const fe2_speaker_sounds *sounds, const fe2_speaker_game *game);

/* The driver's vectors. */
void fe2_speaker_initialize(fe2_speaker *speaker);
void fe2_speaker_shutdown(fe2_speaker *speaker);
void fe2_speaker_play_effect(fe2_speaker *speaker, uint16_t effect);
/* A level (0..14) sets the duration, which only gates the effect. */
void fe2_speaker_play_level(fe2_speaker *speaker, uint16_t effect, uint8_t level);
void fe2_speaker_play_with_duration(fe2_speaker *speaker, uint16_t effect, uint16_t duration);
void fe2_speaker_update(fe2_speaker *speaker);
void fe2_speaker_stop(fe2_speaker *speaker);

/* The noise generator's next state. */
uint16_t fe2_speaker_next_noise(uint16_t noise);

#endif
