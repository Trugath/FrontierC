/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "intro_audio.h"
#ifndef _WIN32
#include <alsa/asoundlib.h>
#endif
#include <string.h>
#include <stdio.h>

enum { PERIOD = 256 };

static void write_opl(void *opaque, uint8_t reg, uint8_t value)
{
    fe2_intro_audio *audio = opaque;
#ifndef _WIN32
    if (audio->lock_ready) pthread_mutex_lock(&audio->lock);
#endif
    fe2_opl_write(&audio->opl, reg, value);
#ifndef _WIN32
    if (audio->lock_ready) pthread_mutex_unlock(&audio->lock);
#endif
}
static int effects_on(void *opaque)
{
    (void)opaque;
    return 1;
}
static int music_on(void *opaque)
{
    (void)opaque;
    return 1;
}
static int auto_music(void *opaque)
{
    fe2_intro_audio *audio = opaque;
    return audio->session && audio->session->music_off;
}
static int mask_on(void *opaque, uint16_t track)
{
    (void)opaque;
    (void)track;
    return 1;
}
static int time_on(void *opaque)
{
    fe2_intro_audio *audio = opaque;
    return audio->session && audio->session->time_step != 0;
}
static void set_current(void *opaque, int16_t index)
{
    fe2_intro_audio *audio = opaque;
    if (audio->session) audio->session->current_music = index;
}
static void set_pitch(void *opaque, uint16_t divisor)
{
    fe2_intro_audio *audio = opaque;
#ifndef _WIN32
    if (audio->lock_ready) pthread_mutex_lock(&audio->lock);
#endif
    audio->speaker_divisor = divisor;
#ifndef _WIN32
    if (audio->lock_ready) pthread_mutex_unlock(&audio->lock);
#endif
}
static void connect_speaker(void *opaque)
{
    fe2_intro_audio *audio = opaque;
#ifndef _WIN32
    if (audio->lock_ready) pthread_mutex_lock(&audio->lock);
#endif
    audio->speaker_gate = 1;
#ifndef _WIN32
    if (audio->lock_ready) pthread_mutex_unlock(&audio->lock);
#endif
}
static void disconnect_speaker(void *opaque)
{
    fe2_intro_audio *audio = opaque;
#ifndef _WIN32
    if (audio->lock_ready) pthread_mutex_lock(&audio->lock);
#endif
    audio->speaker_gate = 0;
#ifndef _WIN32
    if (audio->lock_ready) pthread_mutex_unlock(&audio->lock);
#endif
}
static void set_driver_active(void *opaque, int active)
{
    (void)opaque;
    (void)active;
}
static void render_speaker(fe2_intro_audio *audio, int16_t *out, unsigned frames)
{
    unsigned i;
    for (i = 0; i < frames; ++i) {
        uint32_t divisor = audio->speaker_divisor;
        uint64_t half;
        if (!audio->speaker_gate || divisor < 2) { out[i] = 0; continue; }
        half = (uint64_t)(divisor / 2u) * FE2_OPL_RATE;
        audio->speaker_clock += 1193182u;
        while (audio->speaker_clock >= half) {
            audio->speaker_clock -= half;
            audio->speaker_high ^= 1;
        }
        out[i] = (int16_t)(audio->speaker_high ? 9000 : -9000);
    }
}
static void render(fe2_intro_audio *audio, int16_t *out, unsigned frames)
{
    if (audio->device == FE2_HOST_SOUND_SPEAKER) render_speaker(audio, out, frames);
    else fe2_opl_render(&audio->opl, out, frames);
}

#ifdef _WIN32
static void send_sysex(fe2_intro_audio *audio)
{
    if (!audio->midi || audio->sysex_n < 2) return;
    if (audio->sysex_header.dwFlags & WHDR_PREPARED) {
        if ((audio->sysex_header.dwFlags & WHDR_DONE) == 0) return;
        midiOutUnprepareHeader(audio->midi, &audio->sysex_header, sizeof audio->sysex_header);
    }
    memcpy(audio->sysex_sent, audio->sysex, audio->sysex_n);
    memset(&audio->sysex_header, 0, sizeof audio->sysex_header);
    audio->sysex_header.lpData = (LPSTR)audio->sysex_sent;
    audio->sysex_header.dwBufferLength = audio->sysex_n;
    if (midiOutPrepareHeader(audio->midi, &audio->sysex_header, sizeof audio->sysex_header) != MMSYSERR_NOERROR) return;
    midiOutLongMsg(audio->midi, &audio->sysex_header, sizeof audio->sysex_header);
}
static void midi_byte(void *opaque, uint8_t value)
{
    fe2_intro_audio *audio = opaque;
    unsigned kind;
    DWORD message;
    if (!audio->midi) return;
    if (value == 0xf0 || audio->sysex_on) {
        if (value == 0xf0) { audio->sysex_on = 1; audio->sysex_n = 0; }
        if (audio->sysex_n < sizeof audio->sysex) audio->sysex[audio->sysex_n++] = value;
        if (value == 0xf7) { audio->sysex_on = 0; send_sysex(audio); }
        return;
    }
    if (value >= 0xf0) return;
    if (value & 0x80u) {
        kind = value >> 4;
        audio->midi_status = value;
        audio->midi_need = (uint8_t)((kind == 0xcu || kind == 0xdu) ? 1 : 2);
        audio->midi_got = 0;
        return;
    }
    if (!audio->midi_status || audio->midi_got >= sizeof audio->midi_data) return;
    audio->midi_data[audio->midi_got++] = value;
    if (audio->midi_got < audio->midi_need) return;
    message = audio->midi_status | ((DWORD)audio->midi_data[0] << 8);
    if (audio->midi_need == 2) message |= (DWORD)audio->midi_data[1] << 16;
    midiOutShortMsg(audio->midi, message);
    audio->midi_got = 0;
}
#endif

#ifdef _WIN32
/* The window thread owns both the driver and chip. Completed waveOut buffers
 * are refilled there, so register writes and synthesis require no worker lock. */
static void pump_audio(fe2_intro_audio *audio)
{
    unsigned i;
    if (!audio->wave) return;
    for (i = 0; i < audio->prepared; ++i) {
        WAVEHDR *header = &audio->headers[i];
        if ((header->dwFlags & WHDR_INQUEUE) != 0) continue;
        render(audio, audio->buffers[i], 512);
        if (waveOutWrite(audio->wave, header, sizeof(*header)) != MMSYSERR_NOERROR)
            break;
    }
}

static void open_device(fe2_intro_audio *audio)
{
    WAVEFORMATEX format;
    unsigned i;
    if (!audio->playback) return;
    if (audio->device != FE2_HOST_SOUND_ADLIB && audio->device != FE2_HOST_SOUND_SPEAKER) return;
    memset(&format, 0, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = FE2_OPL_RATE;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = FE2_OPL_RATE * 2;
    if (waveOutOpen(&audio->wave, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        fprintf(stderr, "Intro audio: no waveOut device; continuing silently\n");
        audio->wave = NULL;
        return;
    }
    for (i = 0; i < 4; ++i) {
        WAVEHDR *header = &audio->headers[i];
        header->lpData = (LPSTR)audio->buffers[i];
        header->dwBufferLength = sizeof(audio->buffers[i]);
        if (waveOutPrepareHeader(audio->wave, header, sizeof(*header)) != MMSYSERR_NOERROR) break;
        ++audio->prepared;
    }
    fprintf(stderr, "Intro audio: waveOut %u Hz, %u buffers\n", FE2_OPL_RATE, audio->prepared);
}
#else
static void *playback(void *opaque)
{
    fe2_intro_audio *audio = opaque;
    int16_t buffer[PERIOD];
    while (audio->run) {
        snd_pcm_sframes_t wrote = 0;
        pthread_mutex_lock(&audio->lock);
        render(audio, buffer, PERIOD);
        pthread_mutex_unlock(&audio->lock);
        while (audio->run && wrote < PERIOD) {
            snd_pcm_sframes_t result = snd_pcm_writei(audio->pcm, buffer + wrote, (snd_pcm_uframes_t)(PERIOD - wrote));
            if (result < 0) {
                if (snd_pcm_recover(audio->pcm, (int)result, 1) < 0) audio->run = 0;
                break;
            }
            wrote += result;
        }
    }
    return NULL;
}

static void open_device(fe2_intro_audio *audio)
{
    snd_pcm_t *pcm = NULL;
    if (!audio->playback) return;
    if (audio->device != FE2_HOST_SOUND_ADLIB && audio->device != FE2_HOST_SOUND_SPEAKER) return;
    if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) return;
    if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, FE2_OPL_RATE, 1, 200000) < 0) {
        snd_pcm_close(pcm);
        return;
    }
    audio->pcm = pcm;
    audio->run = 1;
    if (pthread_create(&audio->thread, NULL, playback, audio) == 0) audio->thread_started = 1;
    else audio->run = 0;
}
#endif

void fe2_intro_audio_init(fe2_intro_audio *audio, fe2_session *session, int playback, int device)
{
    static const char *names[] = {[FE2_HOST_SOUND_NONE] = "none", [FE2_HOST_SOUND_ROLAND] = "roland",
        [FE2_HOST_SOUND_ADLIB] = "adlib", [FE2_HOST_SOUND_SPEAKER] = "speaker"};
    fe2_adlib_game game;
    if (!audio) return;
    memset(audio, 0, sizeof *audio);
    audio->session = session;
    audio->playback = playback;
    audio->device = device;
    if ((unsigned)device < sizeof names / sizeof names[0] && names[device])
        fprintf(stderr, "Sound: %s\n", names[device]);
#ifndef _WIN32
    if (pthread_mutex_init(&audio->lock, NULL) == 0) audio->lock_ready = 1;
#endif
    if (device == FE2_HOST_SOUND_ADLIB) {
        fe2_opl_init(&audio->opl);
        memset(&game, 0, sizeof game);
        game.opaque = audio;
        game.effects_enabled = effects_on;
        game.ui_effect_enabled = effects_on;
        game.music_enabled = music_on;
        game.auto_music = auto_music;
        game.music_mask = mask_on;
        game.time_running = time_on;
        game.write_opl = write_opl;
        game.set_current_music = set_current;
        fe2_adlib_init(&audio->adlib, &game);
    } else if (device == FE2_HOST_SOUND_SPEAKER) {
        audio->speaker_game.opaque = audio;
        audio->speaker_game.sound_effects_on = effects_on;
        audio->speaker_game.ui_effect_on = effects_on;
        audio->speaker_game.time_running = time_on;
        audio->speaker_game.set_driver_active = set_driver_active;
        audio->speaker_game.set_pitch = set_pitch;
        audio->speaker_game.connect_speaker = connect_speaker;
        audio->speaker_game.disconnect_speaker = disconnect_speaker;
        fe2_speaker_init(&audio->speaker, &fe2_speaker_default_sounds, &audio->speaker_game);
        fe2_speaker_initialize(&audio->speaker);
    }
    open_device(audio);
}

void fe2_intro_audio_close(fe2_intro_audio *audio)
{
    if (!audio) return;
#ifdef _WIN32
    if (audio->wave) {
        unsigned i;
        waveOutReset(audio->wave);
        for (i = 0; i < audio->prepared; ++i)
            waveOutUnprepareHeader(audio->wave, &audio->headers[i], sizeof(WAVEHDR));
        waveOutClose(audio->wave);
        audio->wave = NULL;
        audio->prepared = 0;
    }
#else
    audio->run = 0;
    if (audio->pcm) snd_pcm_drop(audio->pcm);
    if (audio->thread_started) pthread_join(audio->thread, NULL);
    audio->thread_started = 0;
    if (audio->pcm) snd_pcm_close(audio->pcm);
    audio->pcm = NULL;
#endif
#ifdef _WIN32
    if (audio->midi_scores) fe2_roland_shutdown(&audio->roland);
    if (audio->midi) {
        midiOutReset(audio->midi);
        if (audio->sysex_header.dwFlags & WHDR_PREPARED)
            midiOutUnprepareHeader(audio->midi, &audio->sysex_header, sizeof audio->sysex_header);
        midiOutClose(audio->midi);
        audio->midi = NULL;
    }
    audio->midi_scores = 0;
#endif
    if (audio->device == FE2_HOST_SOUND_ADLIB) fe2_adlib_shutdown(&audio->adlib);
    if (audio->device == FE2_HOST_SOUND_SPEAKER) fe2_speaker_shutdown(&audio->speaker);
#ifndef _WIN32
    if (audio->lock_ready) pthread_mutex_destroy(&audio->lock);
#endif
    audio->lock_ready = 0;
}

void fe2_intro_audio_advance(fe2_intro_audio *audio, uint32_t elapsed_us)
{
    uint64_t phase;
    uint32_t ticks;
    if (!audio) return;
    phase = audio->phase + (uint64_t)elapsed_us * UINT32_C(1193182);
    audio->phase = phase % UINT64_C(5899000000);
    ticks = (uint32_t)(phase / UINT64_C(5899000000));
    while (ticks--) {
        if (audio->device == FE2_HOST_SOUND_ADLIB) fe2_adlib_update(&audio->adlib);
        else if (audio->device == FE2_HOST_SOUND_ROLAND && audio->midi_scores) fe2_roland_update(&audio->roland);
        else if (audio->device == FE2_HOST_SOUND_SPEAKER) fe2_speaker_update(&audio->speaker);
    }
#ifdef _WIN32
    pump_audio(audio);
#endif
}

void fe2_intro_audio_bind_scores(fe2_intro_audio *audio, const fe2_asset_archive *archive)
{
    static const char *names[FE2_ROLAND_MUSIC_TRACKS] = {
        "music/hallking", "music/elitet", "music/babayaga", "music/barem", "music/jupalt",
        "music/valkries", "music/bluedan", "music/ggofkiev", "music/intro"
    };
    const fe2_asset_entry *scores[FE2_ROLAND_MUSIC_TRACKS];
    unsigned i;
    if (!audio || !archive || !audio->playback || audio->device != FE2_HOST_SOUND_ROLAND) return;
    for (i = 0; i < FE2_ROLAND_MUSIC_TRACKS; ++i) {
        scores[i] = fe2_asset_archive_find(archive, names[i]);
        if (!scores[i] || scores[i]->count < 4 || memcmp(scores[i]->data, "MThd", 4) != 0) {
            fprintf(stderr, "MIDI: %s is not in frontier.dat\n", names[i]);
            return;
        }
    }
#ifdef _WIN32
    {
        fe2_roland_services services;
        if (midiOutOpen(&audio->midi, MIDI_MAPPER, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            audio->midi = NULL;
            fprintf(stderr, "MIDI: no system MIDI device\n");
            return;
        }
        memset(&services, 0, sizeof services);
        services.context = audio;
        services.send_byte = midi_byte;
        services.current_music = set_current;
        fe2_roland_init(&audio->roland, &services);
        for (i = 0; i < FE2_ROLAND_MUSIC_TRACKS; ++i)
            fe2_roland_bind_music(&audio->roland, i, scores[i]->data, scores[i]->count);
        audio->midi_scores = 1;
        fprintf(stderr, "MIDI: playing the packed scores\n");
    }
#else
    (void)scores;
    fprintf(stderr, "MIDI: this build has no MIDI output\n");
#endif
}
void fe2_intro_audio_select_music(fe2_intro_audio *audio, int16_t index)
{
    if (!audio) return;
    if (audio->device == FE2_HOST_SOUND_ROLAND && audio->midi_scores) fe2_roland_select_music(&audio->roland, index);
    else if (audio->device == FE2_HOST_SOUND_ADLIB) fe2_adlib_select_music(&audio->adlib, index);
}
void fe2_intro_audio_check_music(fe2_intro_audio *audio)
{
    if (!audio) return;
    if (audio->device == FE2_HOST_SOUND_ROLAND && audio->midi_scores) {
        audio->roland.auto_music = (uint8_t)auto_music(audio);
        fe2_roland_check_music(&audio->roland);
    } else if (audio->device == FE2_HOST_SOUND_ADLIB) fe2_adlib_check_music(&audio->adlib);
}
void fe2_intro_audio_reset_music(fe2_intro_audio *audio)
{
    if (!audio) return;
    if (audio->device == FE2_HOST_SOUND_ROLAND && audio->midi_scores) fe2_roland_reset_music(&audio->roland);
    else if (audio->device == FE2_HOST_SOUND_ADLIB) fe2_adlib_reset_music(&audio->adlib);
}
void fe2_intro_audio_stop_effects(fe2_intro_audio *audio)
{
    if (!audio) return;
    if (audio->device == FE2_HOST_SOUND_ADLIB) fe2_adlib_stop_effects(&audio->adlib);
    else if (audio->device == FE2_HOST_SOUND_ROLAND && audio->midi_scores) fe2_roland_stop_effects(&audio->roland);
    else if (audio->device == FE2_HOST_SOUND_SPEAKER) fe2_speaker_stop(&audio->speaker);
}
void fe2_intro_audio_play_effect(fe2_intro_audio *audio, uint16_t effect)
{
    if (!audio) return;
    if (audio->device == FE2_HOST_SOUND_ADLIB) fe2_adlib_play_effect(&audio->adlib, effect);
    else if (audio->device == FE2_HOST_SOUND_ROLAND && audio->midi_scores) fe2_roland_play_effect(&audio->roland, effect);
    else if (audio->device == FE2_HOST_SOUND_SPEAKER) fe2_speaker_play_effect(&audio->speaker, effect);
}
void fe2_intro_audio_play_duration(fe2_intro_audio *audio, uint16_t effect, uint16_t ticks)
{
    if (!audio) return;
    if (audio->device == FE2_HOST_SOUND_ADLIB) fe2_adlib_play_duration(&audio->adlib, effect, ticks);
    else if (audio->device == FE2_HOST_SOUND_ROLAND && audio->midi_scores) fe2_roland_play_duration(&audio->roland, effect, ticks);
    else if (audio->device == FE2_HOST_SOUND_SPEAKER) fe2_speaker_play_with_duration(&audio->speaker, effect, ticks);
}
void fe2_intro_audio_play_level(fe2_intro_audio *audio, uint16_t effect, uint8_t level)
{
    if (!audio) return;
    if (audio->device == FE2_HOST_SOUND_ADLIB) fe2_adlib_play_level(&audio->adlib, effect, level);
    else if (audio->device == FE2_HOST_SOUND_ROLAND && audio->midi_scores) fe2_roland_play_level(&audio->roland, effect, level);
    else if (audio->device == FE2_HOST_SOUND_SPEAKER) fe2_speaker_play_level(&audio->speaker, effect, level);
}
void fe2_intro_audio_ambient(fe2_intro_audio *audio, uint16_t level, uint16_t pitch)
{
    if (!audio) return;
    if (audio->device == FE2_HOST_SOUND_ADLIB) fe2_adlib_ambient(&audio->adlib, level, pitch);
    else if (audio->device == FE2_HOST_SOUND_ROLAND && audio->midi_scores) fe2_roland_ambient(&audio->roland, level, pitch);
}

static void model_sound(void *context, uint16_t event, uint16_t duration)
{
    fe2_intro_audio_play_duration(context, event, duration);
}

void fe2_intro_audio_bind_sound(fe2_intro_audio *audio, fe2_render_runtime_resources *resources)
{
    if (!resources) return;
    resources->sound = model_sound;
    resources->sound_context = audio;
}
