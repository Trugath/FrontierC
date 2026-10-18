/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_ROLAND_H
#define FE2_ROLAND_H
#include <stddef.h>
#include <stdint.h>

enum { FE2_ROLAND_GAME_EFFECTS = 40, FE2_ROLAND_EFFECT_STREAMS = 48, FE2_ROLAND_MUSIC_TRACKS = 9 };

typedef struct fe2_midi_stream {
    const uint8_t *data;
    size_t size, cursor;
    uint16_t delay, selection, message_bytes;
    uint8_t active, status;
} fe2_midi_stream;

typedef struct fe2_roland_services {
    void *context;
    void (*send_byte)(void *context, uint8_t byte);
    void (*command)(void *context, uint8_t command);
    uint16_t (*random_bound)(void *context, uint16_t bound);
    void (*current_music)(void *context, int16_t selection);
} fe2_roland_services;

typedef struct fe2_roland {
    fe2_roland_services services;
    fe2_midi_stream streams[4];
    uint16_t hold24, hold13, hold11, effect_delay, ambient_hold;
    uint32_t random[2];
    int16_t current;
    uint8_t controller, lock, effects_enabled, ui_enabled, music_enabled;
    uint8_t auto_music, time_running, music_blocked, masks[9], error;
    const uint8_t *scores[FE2_ROLAND_MUSIC_TRACKS];
    size_t score_size[FE2_ROLAND_MUSIC_TRACKS];
} fe2_roland;

/* Native MIDI bytes, not an MPU-401 emulator. The platform owns MIDI output,
 * optional command handling and resource lifetime. Builtin streams are immutable. */
void fe2_roland_init(fe2_roland *driver, const fe2_roland_services *services);
void fe2_roland_shutdown(fe2_roland *driver);
/* Score `index` is the packed music/<name> order: hallking, elitet, babayaga,
 * barem, jupalt, valkries, bluedan, ggofkiev, intro. */
void fe2_roland_bind_music(fe2_roland *driver, unsigned index, const uint8_t *data, size_t size);
void fe2_roland_select_music(fe2_roland *driver, int16_t selection);
void fe2_roland_reset_music(fe2_roland *driver);
void fe2_roland_check_music(fe2_roland *driver);
void fe2_roland_stop_effects(fe2_roland *driver);
void fe2_roland_play_effect(fe2_roland *driver, uint16_t effect);
void fe2_roland_play_duration(fe2_roland *driver, uint16_t effect, uint16_t duration);
void fe2_roland_play_level(fe2_roland *driver, uint16_t effect, uint8_t level);
void fe2_roland_ambient(fe2_roland *driver, uint16_t level, uint16_t pitch);
void fe2_roland_update(fe2_roland *driver);
#endif
