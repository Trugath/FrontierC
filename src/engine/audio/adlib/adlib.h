/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_ADLIB_H
#define FE2_ADLIB_H

/*
 * The AdLib music and effects driver (adlib).
 *
 * The game calls the service vectors. Each timer tick decrements effect
 * holds and, on the 327/910 accumulator, drains the event queue and steps
 * the ten channels. A channel reads its stream from the music bank: a
 * nonnegative word is a note, and 0x80..0xc9 are commands. Pitch-bend curves
 * and the bank are image data; the driver logic is this translation of the
 * recovered driver.
 *
 * Preferences, the game's random draw and the OPL chip are reached through
 * fe2_adlib_game. Register writes are the hardware side. There is no guest
 * memory and the assembled overlay is not executed.
 */
#include <stdint.h>

enum { FE2_ADLIB_IMAGE_BYTES = 0x67dd, FE2_ADLIB_CHANNELS = 10, FE2_ADLIB_CHANNEL_BYTES = 67 };

typedef struct fe2_adlib_game {
    void *opaque;
    int (*effects_enabled)(void *opaque);
    int (*ui_effect_enabled)(void *opaque);               /*, game effect 20 */
    int (*music_enabled)(void *opaque);
    int (*auto_music)(void *opaque);
    int (*music_mask)(void *opaque, uint16_t track);      /* + track */
    int (*time_running)(void *opaque);                    /* nonzero */
    /* Slot 0x124: AX is the bound on entry and the draw on return. */
    uint16_t (*random_bound)(void *opaque, uint16_t bound);
    void (*write_opl)(void *opaque, uint8_t reg, uint8_t value);
    void (*set_current_music)(void *opaque, int16_t index);
    void (*set_active)(void *opaque, int active);           /* bit 7 */
} fe2_adlib_game;

typedef struct fe2_adlib {
    fe2_adlib_game game;
    uint8_t image[FE2_ADLIB_IMAGE_BYTES];
    uint8_t channels[FE2_ADLIB_CHANNELS * FE2_ADLIB_CHANNEL_BYTES];
    uint16_t bank_pointer;
    uint16_t queue_read, queue_write;
    uint8_t queue[16];
    uint16_t hold_18, hold_0d, hold_0b, hold_ambient, hold_1d;
    uint16_t accumulator;
    uint16_t random_state;
    uint16_t stream;
    int16_t current_music;
    uint8_t guard;
    uint8_t voices;
    uint8_t rhythm_mode;
    uint8_t opl_bd;
    uint8_t operator_offset;
    uint8_t channel_index;
    unsigned di;
    uint8_t ah, al;
    uint8_t last_duration;
    uint8_t tempo;
    uint8_t level_reload, level_remaining, level_phase, level_value, gate;
    uint8_t base_bass, base_snare, base_hihat, base_tom, base_cymbal;
    uint8_t level_bass, level_snare, level_hihat, level_tom, level_cymbal;
    uint8_t bias_bass, bias_snare, bias_hihat, bias_tom, bias_cymbal;
} fe2_adlib;

void fe2_adlib_init(fe2_adlib *driver, const fe2_adlib_game *game);
void fe2_adlib_shutdown(fe2_adlib *driver);
void fe2_adlib_select_music(fe2_adlib *driver, int16_t index);
void fe2_adlib_check_music(fe2_adlib *driver);
void fe2_adlib_reset_music(fe2_adlib *driver);
void fe2_adlib_play_effect(fe2_adlib *driver, uint16_t effect);
void fe2_adlib_play_level(fe2_adlib *driver, uint16_t effect, uint8_t level);
void fe2_adlib_play_duration(fe2_adlib *driver, uint16_t effect, uint16_t duration);
void fe2_adlib_update(fe2_adlib *driver);
void fe2_adlib_stop_effects(fe2_adlib *driver);
void fe2_adlib_ambient(fe2_adlib *driver, uint16_t level, uint16_t pitch);
int16_t fe2_adlib_current_music(const fe2_adlib *driver);

#endif
