/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_INTRO_AUDIO_H
#define FE2_INTRO_AUDIO_H
#include "audio/adlib/adlib.h"
#include "audio/adlib/opl.h"
#include "audio/roland/roland.h"
#include "audio/speaker/speaker.h"
#include "asset_archive.h"
#include "render/runtime.h"
#include "session/session.h"
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <pthread.h>
#endif
#include <stdint.h>

/* Same values as the setup menu: 1 Roland, 2 AdLib, 4 PC speaker. 0 is silent. */
enum {
    FE2_HOST_SOUND_NONE = 0,
    FE2_HOST_SOUND_ROLAND = 1,
    FE2_HOST_SOUND_ADLIB = 2,
    FE2_HOST_SOUND_SPEAKER = 4
};

/* One sound device. AdLib and the PC speaker use the wave device. Roland
 * plays the packed scores on the system MIDI device. */
typedef struct fe2_intro_audio {
    fe2_adlib adlib;
    fe2_opl opl;
    fe2_roland roland;
    fe2_speaker speaker;
    fe2_speaker_game speaker_game;
    fe2_session *session;
    void *pcm;
    uint64_t phase;
#ifdef _WIN32
    HWAVEOUT wave;
    HMIDIOUT midi;
    MIDIHDR sysex_header;
    uint8_t sysex[512], sysex_sent[512];
    unsigned sysex_n, sysex_on;
    uint8_t midi_status, midi_need, midi_got, midi_data[2];
    WAVEHDR headers[4];
    int16_t buffers[4][512];
    unsigned prepared;
#else
    pthread_t thread;
    pthread_mutex_t lock;
    volatile int run;
#endif
    uint64_t speaker_clock;
    uint16_t speaker_divisor;
    int playback, thread_started, lock_ready, midi_scores, speaker_gate, speaker_high, device;
} fe2_intro_audio;

/* Loads music/<score> from the dat when the device is Roland. */
void fe2_intro_audio_bind_scores(fe2_intro_audio *audio, const fe2_asset_archive *archive);
void fe2_intro_audio_select_music(fe2_intro_audio *audio, int16_t index);
void fe2_intro_audio_check_music(fe2_intro_audio *audio);
void fe2_intro_audio_reset_music(fe2_intro_audio *audio);
void fe2_intro_audio_stop_effects(fe2_intro_audio *audio);
void fe2_intro_audio_play_effect(fe2_intro_audio *audio, uint16_t effect);
void fe2_intro_audio_play_duration(fe2_intro_audio *audio, uint16_t effect, uint16_t ticks);
void fe2_intro_audio_play_level(fe2_intro_audio *audio, uint16_t effect, uint8_t level);
void fe2_intro_audio_ambient(fe2_intro_audio *audio, uint16_t level, uint16_t pitch);

void fe2_intro_audio_init(fe2_intro_audio *audio, fe2_session *session, int playback, int device);
void fe2_intro_audio_close(fe2_intro_audio *audio);
void fe2_intro_audio_advance(fe2_intro_audio *audio, uint32_t elapsed_us);
void fe2_intro_audio_bind_sound(fe2_intro_audio *audio, fe2_render_runtime_resources *resources);
#endif
