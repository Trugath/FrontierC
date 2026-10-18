/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio/speaker/speaker.h"
#include "audio/roland/roland.h"
#include "setup/setup.h"

static unsigned pitches, connections, disconnections, midi_bytes, active;
static int yes(void *p) { (void)p; return 1; }
static void set_active(void *p, int value) { (void)p; active = (unsigned)value; }
static void pitch(void *p, uint16_t value) { (void)p; (void)value; ++pitches; }
static void connect(void *p) { (void)p; ++connections; }
static void disconnect(void *p) { (void)p; ++disconnections; }
static void send_byte(void *p, uint8_t value) { (void)p; (void)value; ++midi_bytes; }
static void audio(void)
{
    fe2_speaker speaker;
    fe2_speaker_game game;
    fe2_roland roland;
    fe2_roland_services services;
    unsigned effect, tick;
    memset(&game, 0, sizeof game);
    game.sound_effects_on = yes; game.ui_effect_on = yes; game.time_running = yes;
    game.set_driver_active = set_active; game.set_pitch = pitch;
    game.connect_speaker = connect; game.disconnect_speaker = disconnect;
    fe2_speaker_init(&speaker, &fe2_speaker_default_sounds, &game);
    fe2_speaker_initialize(&speaker); assert(active);
    for (effect = 0; effect < FE2_SPEAKER_GAME_EFFECTS; ++effect) {
        fe2_speaker_play_effect(&speaker, (uint16_t)effect);
        for (tick = 0; tick < 200; ++tick) fe2_speaker_update(&speaker);
        assert(speaker.depth <= FE2_SPEAKER_LOOPS && !speaker.updating);
    }
    fe2_speaker_play_effect(&speaker, 65535);
    fe2_speaker_play_level(&speaker, 0, 255);
    fe2_speaker_stop(&speaker); fe2_speaker_update(&speaker);
    fe2_speaker_shutdown(&speaker); assert(!active);
    assert(pitches && connections && disconnections);
    memset(&services, 0, sizeof services); services.send_byte = send_byte;
#ifdef FE2_ROLAND_EMBEDDED
    fe2_roland_init(&roland, &services);
    for (effect = 0; effect < FE2_ROLAND_GAME_EFFECTS; ++effect) {
        fe2_roland_init(&roland, &services);
        fe2_roland_play_effect(&roland, (uint16_t)effect);
        for (tick = 0; tick < 200; ++tick) fe2_roland_update(&roland);
        if (roland.error) fprintf(stderr, "MIDI effect %u failed\n", effect);
        assert(!roland.error);
    }
    for (effect = 0; effect < FE2_ROLAND_MUSIC_TRACKS; ++effect) {
        fe2_roland_init(&roland, &services);
        fe2_roland_select_music(&roland, (int16_t)effect);
        for (tick = 0; tick < 400; ++tick) fe2_roland_update(&roland);
        assert(!roland.error);
    }
    fe2_roland_shutdown(&roland); assert(midi_bytes > 100);
    fe2_roland_play_effect(&roland, 65535); assert(roland.error);
#else
    fe2_roland_init(&roland, &services);
    fe2_roland_shutdown(&roland);
#endif
}

static int failure_stage = -1;
static unsigned printed;
static uint8_t choice = '1';
static uint8_t version(void *p) { (void)p; return failure_stage == 0 ? 0 : 5; }
static int cpu_stuck(void *p) { (void)p; return failure_stage == 1; }
static uint16_t zero_word(void *p) { (void)p; return 0; }
static uint16_t control(void *p) { (void)p; return 0x3f; }
static int open_device(void *p, const char *name, uint16_t *handle)
{ (void)p; assert(!strcmp(name, "EMMXXXX0")); *handle = 42; return failure_stage == 3; }
static int info(void *p, uint16_t handle, uint16_t *value)
{ (void)p; assert(handle == 42); *value = 0x80; return failure_stage == 4; }
static int ready(void *p, uint16_t handle, uint8_t *value)
{ (void)p; assert(handle == 42); *value = 255; return failure_stage == 5; }
static int close_device(void *p, uint16_t handle)
{ (void)p; assert(handle == 42); return failure_stage == 6; }
static uint8_t status(void *p) { (void)p; return (uint8_t)(failure_stage == 7); }
static uint16_t pages(void *p) { (void)p; return failure_stage == 8 ? 47 : 48; }
static uint8_t frame(void *p) { (void)p; return (uint8_t)(failure_stage == 9); }
static void print(void *p, const char *text) { (void)p; assert(text && *text); ++printed; }
static uint8_t key(void *p) { (void)p; return choice; }
static void setup(void)
{
    fe2_setup_state state;
    fe2_setup_platform platform;
    int stage;
    memset(&platform, 0, sizeof platform);
    platform.dos_major_version = version; platform.open_device = open_device;
    platform.device_information = info; platform.input_status = ready; platform.close_file = close_device;
    platform.print = print; platform.read_key = key; platform.ems_status = status;
    platform.ems_free_pages = pages; platform.ems_page_frame = frame;
    platform.flags_high_bits_stuck = cpu_stuck; platform.flags_high_bits_settable = yes;
    platform.alignment_check_toggles = yes; platform.fpu_status = zero_word;
    platform.fpu_control = control; platform.fpu_infinity_status = zero_word;
    for (stage = 0; stage <= 9; ++stage) {
        memset(&state, 0, sizeof state); printed = 0; failure_stage = stage;
        state.free_base_paragraphs = stage == 2 ? FE2_SETUP_BASE_PARAGRAPHS - 1 : FE2_SETUP_BASE_PARAGRAPHS;
        assert(fe2_setup_check_system(&state, &platform) == FE2_SETUP_FAILED);
        assert(printed == 1);
    }
    failure_stage = -1; state.free_base_paragraphs = FE2_SETUP_BASE_PARAGRAPHS;
    for (choice = '1'; choice <= '4'; ++choice) {
        static const uint8_t selections[] = {1, 2, 4, 4};
        assert(fe2_setup_check_system(&state, &platform) == FE2_SETUP_PASSED);
        assert(state.cpu_class == FE2_CPU_486 && state.fpu_class == FE2_CPU_486);
        assert(state.sound_selection == selections[choice - '1']);
    }
}
int main(void) { audio(); setup(); puts("Optional audio and setup tests passed"); return 0; }
