/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "roland.h"
#include <string.h>
#ifdef FE2_ROLAND_EMBEDDED
#include "roland_tables.h"
#else
static const uint8_t *const effect_data[FE2_ROLAND_EFFECT_STREAMS] = {0};
static const size_t effect_size[FE2_ROLAND_EFFECT_STREAMS] = {0};
static const uint8_t *const music_data[FE2_ROLAND_MUSIC_TRACKS] = {0};
static const size_t music_size[FE2_ROLAND_MUSIC_TRACKS] = {0};
#endif

static const uint8_t effect_map[FE2_ROLAND_GAME_EFFECTS] = {
    255,255,255,255,255,13,255,255,255,255,24,28,43,39,47,36,17,19,33,4,
    1,38,22,3,255,7,8,6,35,5,23,255,32,21,48,24,28,255,22,2
};
static const uint8_t duration_map[15] = {50,50,50,49,47,45,40,30,20,10,5,3,2,1,1};
static const uint8_t routes[FE2_ROLAND_EFFECT_STREAMS] = {
    8,8,8,10,10,8,8,8,10,10,10,10,10,10,10,10,9,9,9,9,8,8,8,10,
    10,10,10,10,10,10,10,10,10,8,10,9,9,8,10,10,10,10,10,10,10,10,9,10
};
static void byte(fe2_roland *d, uint8_t value)
{ if (d->services.send_byte) d->services.send_byte(d->services.context, value); }
static void message(fe2_roland *d, uint8_t status, uint8_t a, uint8_t b)
{ byte(d, status); byte(d, a); byte(d, b); }
static void current(fe2_roland *d, int16_t value)
{ d->current = value; if (d->services.current_music) d->services.current_music(d->services.context, value); }
static void reset_channels(fe2_roland *d, unsigned first, unsigned count)
{
    unsigned i;
    for (i = first; i < first + count; ++i) {
        message(d, (uint8_t)(0xb0 + i), 0x79, 0);
        message(d, (uint8_t)(0xb0 + i), 0x7b, 0);
        message(d, (uint8_t)(0xb0 + i), 7, 0x70);
    }
}
void fe2_roland_reset_music(fe2_roland *d)
{ ++d->lock; d->streams[0].active = 0; current(d, -1); reset_channels(d, 0, 7); --d->lock; }
void fe2_roland_stop_effects(fe2_roland *d)
{
    unsigned i;
    ++d->lock;
    for (i = 1; i < 4; ++i) d->streams[i].active = 0;
    d->hold24 = d->hold13 = d->hold11 = d->effect_delay = 0;
    d->ambient_hold = 1;
    reset_channels(d, 7, 3); --d->lock;
}
void fe2_roland_check_music(fe2_roland *d) { if (!d->auto_music) fe2_roland_reset_music(d); }
void fe2_roland_shutdown(fe2_roland *d) { fe2_roland_reset_music(d); fe2_roland_stop_effects(d); }
static void sysex(fe2_roland *d, const uint8_t *data, unsigned count)
{
    unsigned i, sum = 0;
    if (d->services.command) d->services.command(d->services.context, 0xdf);
    byte(d, 0xf0); byte(d, 0x41); byte(d, 0x10); byte(d, 0x16); byte(d, 0x12);
    for (i = 0; i < count; ++i) { byte(d, data[i]); sum += data[i]; }
    byte(d, (uint8_t)(0x80u - (sum & 0x7fu))); byte(d, 0xf7);
}
void fe2_roland_init(fe2_roland *d, const fe2_roland_services *services)
{
    static const uint8_t a[] = {0x7f,0,0}, b[] = {0x10,0,0x16,0x64};
    memset(d, 0, sizeof *d);
    if (services) d->services = *services;
    d->random[0] = 0x12345678; d->random[1] = 0x89abcdef; d->controller = 0x68;
    d->effects_enabled = d->ui_enabled = d->music_enabled = d->time_running = 1;
    memset(d->masks, 1, sizeof d->masks);
    if (d->services.command) { d->services.command(d->services.context, 0xff); d->services.command(d->services.context, 0x3f); }
    sysex(d, a, sizeof a); sysex(d, b, sizeof b);
    memcpy(d->scores, music_data, sizeof d->scores);
    memcpy(d->score_size, music_size, sizeof d->score_size);
    fe2_roland_reset_music(d); fe2_roland_stop_effects(d);
}
void fe2_roland_bind_music(fe2_roland *d, unsigned index, const uint8_t *data, size_t size)
{
    if (!d || index >= FE2_ROLAND_MUSIC_TRACKS) return;
    d->scores[index] = data;
    d->score_size[index] = size;
}
static uint32_t ror(uint32_t value, unsigned bits) { return value >> bits | value << (32 - bits); }
static uint16_t random_word(fe2_roland *d)
{
    uint32_t value = d->random[0];
    d->random[0] += ror(d->random[1] ^ 0x1234567f, 7);
    d->random[1] = ror(value, 19);
    return (uint16_t)d->random[1];
}
static unsigned bounded(fe2_roland *d, unsigned bound)
{ return d->services.random_bound ? d->services.random_bound(d->services.context, (uint16_t)bound) : (unsigned)((uint32_t)random_word(d) * bound >> 16); }
void fe2_roland_select_music(fe2_roland *d, int16_t selection)
{
    unsigned candidate, tries;
    fe2_midi_stream *s = &d->streams[0];
    if (selection >= 0 && !d->music_enabled) return;
    if (selection < 0 && d->current != -1) return;
    candidate = selection < 0 ? bounded(d, (selection & 1) ? 7 : 6) : (unsigned)selection;
    for (tries = 1; tries < 40 && (candidate >= 9 || !d->masks[candidate]); ++tries) candidate = bounded(d, 7);
    if (candidate >= 9 || !d->masks[candidate]) {
        for (candidate = 0; candidate < 8 && !d->masks[candidate]; ++candidate) {}
        if (candidate == 8) return;
    }
    ++d->lock; fe2_roland_reset_music(d); current(d, (int16_t)candidate);
    s->selection = (uint16_t)candidate; s->data = d->scores[candidate]; s->size = d->score_size[candidate];
    s->cursor = s->size > 26 && s->data[26] == 0x55 ? 59 : 47;
    s->active = 1; s->delay = 1; message(d, 0xb9, 0x79, 0); --d->lock;
}
static void effect(fe2_roland *d, unsigned id)
{
    fe2_midi_stream *s;
    unsigned route;
    if (!id || id > 48) { d->error = 1; return; }
    route = routes[id - 1]; s = &d->streams[route - 7];
    if (route == 8 && ((id == 3 && s->active == 1 && s->selection == 3) || (id == 1 && s->active))) return;
    if (route == 9) {
        if (id == 0x24) d->hold24 = 400;
        if (id == 0x13) d->hold13 = 600;
        if (id == 0x11) d->hold11 = 600;
        if ((id == 0x24 || id == 0x13 || id == 0x11) && s->selection == id) return;
    }
    if (route == 10 && (id < 9 || id > 16) && s->active && s->selection >= 9 && s->selection <= 16) return;
    s->selection = (uint16_t)id; s->data = effect_data[id - 1]; s->size = effect_size[id - 1];
    s->cursor = 0; s->active = 1; s->delay = 1;
    if (route == 8) { ++d->lock; byte(d, 0xb7); byte(d, 0x7b); --d->lock; }
    if (route == 10) message(d, 0xb9, 0x79, 0);
}
void fe2_roland_play_effect(fe2_roland *d, uint16_t id)
{
    if (!d->effects_enabled) return;
    if (id == 14) { if (!d->effect_delay) d->effect_delay = (uint16_t)((random_word(d) & 2047u) + 1000u); return; }
    if (id == 20 && !d->ui_enabled) return;
    if (id >= sizeof effect_map) { d->error = 1; return; }
    if (effect_map[id] < 128) effect(d, effect_map[id]);
}
void fe2_roland_play_duration(fe2_roland *d, uint16_t id, uint16_t duration)
{
    unsigned selected;
    if (!d->effects_enabled || !duration) return;
    if ((uint8_t)id == 5 || (uint8_t)id == 6 || (uint8_t)id == 31) {
        d->controller ^= 11; message(d, 0x99, d->controller, (uint8_t)(duration + 0x40u)); return;
    }
    if (id >= sizeof effect_map) { d->error = 1; return; }
    selected = effect_map[id]; if (selected >= 128) return;
    if (selected == 24 || selected == 28 || selected == 39 || selected == 43) selected = (uint8_t)(selected + ((uint8_t)(duration >> 4) ^ 3u));
    effect(d, selected);
}
void fe2_roland_play_level(fe2_roland *d, uint16_t id, uint8_t level)
{ if (d->effects_enabled && d->time_running && level < 15) fe2_roland_play_duration(d, id, duration_map[level]); }
void fe2_roland_ambient(fe2_roland *d, uint16_t level, uint16_t pitch)
{
    uint8_t bend;
    if (!d->effects_enabled || d->current >= 0) return;
    if (level > 63) level = 63;
    bend = (uint8_t)(63u - (uint8_t)((uint16_t)(pitch - 174u) >> 2)); if (bend > 63) bend = 63;
    ++d->lock; message(d, 0xe1, 0, bend); message(d, 0xb1, 7, (uint8_t)(level * 2u));
    if (!d->ambient_hold) { message(d, 0xb1, 10, 64); byte(d, 0xc1); byte(d, 0x6b); message(d, 0x91, 48, 127); message(d, 0x91, 49, 127); }
    d->ambient_hold = 200; --d->lock;
}
static int read_byte(fe2_roland *d, fe2_midi_stream *s, size_t *cursor)
{
    if (!s->data || *cursor >= s->size) { d->error = 1; s->active = 0; return -1; }
    return s->data[(*cursor)++];
}
static void advance(fe2_roland *d, unsigned index)
{
    static const uint8_t sizes[8] = {3,3,3,3,2,2,3,1};
    fe2_midi_stream *s = &d->streams[index];
    size_t cursor = s->cursor;
    if (!s->active || --s->delay) return;
    for (;;) {
        int value = read_byte(d, s, &cursor);
        unsigned count, delay = 0;
        if (value < 0) return;
        if (value == 255) {
            s->active = 0;
            if (!index) { current(d, -1); if (d->auto_music) fe2_roland_select_music(d, -1); }
            return;
        }
        if (value & 128) { s->status = (uint8_t)value; s->message_bytes = sizes[(value >> 4) - 8]; count = s->message_bytes; }
        else { --cursor; value = s->status; count = s->message_bytes; }
        if (!count || count > 3) { d->error = 1; s->active = 0; return; }
        byte(d, (uint8_t)value);
        while (--count) { value = read_byte(d, s, &cursor); if (value < 0) return; byte(d, (uint8_t)value); }
        do { value = read_byte(d, s, &cursor); if (value < 0) return; delay = (uint16_t)((delay << 7) + (value & 127)); } while (value & 128);
        if (delay) { s->cursor = cursor; s->delay = (uint16_t)delay; return; }
    }
}
void fe2_roland_update(fe2_roland *d)
{
    unsigned i;
    if (d->lock) return;
    ++d->lock;
    if (d->ambient_hold && !--d->ambient_hold) { message(d, 0x81, 48, 0); message(d, 0x81, 49, 0); }
    if (d->effect_delay && !--d->effect_delay && d->effects_enabled) effect(d, 47);
    if (d->hold24 && !--d->hold24 && d->streams[2].selection == 36) effect(d, 37);
    if (d->hold13 && !--d->hold13 && d->streams[2].selection == 19) effect(d, 20);
    if (d->hold11 && !--d->hold11 && d->streams[2].selection == 17) effect(d, 18);
    for (i = d->music_blocked ? 1 : 0; i < 4; ++i) advance(d, i);
    --d->lock;
}
