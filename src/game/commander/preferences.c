/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* start, 0xd37 entering the preferences, 0xdab shape detail, 0xe17
 * text, 0xe44 widgets, 0x1055..0x11f7 preference keys. */
#include <string.h>

#include "commander_internal.h"

enum {
    PREFERENCES_PANEL = 0x0a, PANEL_REQUEST = 4, PREFERENCES_HEADING = 0x9416,
    MENU_SPRITE = 0x64, CONTROLS_BX = 0x0f0f, CONTROLS_CL = 0x0f,
    PREFERENCE_TEXT = 0x9400, SOUND_TEXT = 0x9401, ON_ICON = 0x5b, OFF_ICON = 0x5c,
    KEY_FIRST_DETAIL = 0x81, KEY_LAST_DETAIL = 0x84, KEY_DUST = 0x85, KEY_EFFECTS = 0x8a,
    KEY_MUSIC = 0x8b, KEY_AUTO_MUSIC = 0x8c, KEY_UI_EFFECT = 0x8e, KEY_FIRST_TRACK = 0x91,
    KEY_LAST_TRACK = 0x98, KEY_STATION = 0xa8, KEY_WIND = 0xa4,
    SOUND_PLAYING = 0x40, AUTOMATIC_MUSIC = 0xffff, EXTENSION_DOT = 0x2e,
    START_201B = 0x5555370, SEARCH_DIRECTORIES = 0x30
};

/* Keys that toggle one preference (NOT). */
static const struct { uint8_t key, preference; } toggles[] = {
    {0x86, FE2_PREF_BACKGROUND_STARS}, {0x87, FE2_PREF_EXTERNAL_VIEW_LOCK},
    {0x88, FE2_PREF_REVERSE_LEFT_RIGHT}, {0x89, FE2_PREF_REVERSE_UP_DOWN},
    {0xa9, FE2_PREF_ELITE_CONTROLS}, {0x9a, FE2_PREF_TARGETING_TUNNELS},
    {0x9b, FE2_PREF_SHIP_TARGETING_TUNNELS}, {0x8d, FE2_PREF_ENGINE_SOUND},
    {0xa5, FE2_PREF_HYPERSPACE_MUSIC}, {0xa6, FE2_PREF_DOCKING_MUSIC},
    {0xa7, FE2_PREF_BATTLE_MUSIC}, {KEY_UI_EFFECT, FE2_PREF_UI_EFFECT}
};

/* The first set detail flag (low, medium, high) picks the renderer's three
 * detail parameters; none set is the highest detail. */
void fe2_commander_apply_detail(fe2_commander_state *s)
{
    static const uint16_t detail[4][3] = {
        {0xfffe, 0xfffc, 0x4200}, {0xffff, 0xfffe, 0x4100}, {0, 0, 0x4030}, {1, 2, 0}
    };
    unsigned level = s->preferences[FE2_PREF_DETAIL_LOW] ? 0 : s->preferences[FE2_PREF_DETAIL_MEDIUM] ? 1
                     : s->preferences[FE2_PREF_DETAIL_HIGH] ? 2 : 3;
    memcpy(s->detail, detail[level], sizeof s->detail);
}

void fe2_commander_copy_upper_name(fe2_commander_state *s)
{
    unsigned i;
    for (i = 0; i < FE2_COMMANDER_FILENAME_BYTES; ++i) {
        char c = s->commander_name[i];
        s->upper_name[i] = (char)(c >= 'a' && c <= 'z' ? c - 0x20 : c);
        if (c == 0) break;
    }
}

void fe2_commander_text(const fe2_commander_services *game, uint16_t selector, fe2_text_out *out)
{
    game->format_table_text(game->opaque, selector & 0x3fe, (const fe2_text_table *)fe2_commander_text_directory,
                            out);
}

void fe2_commander_enter_preferences(fe2_commander_state *s, const fe2_commander_services *game)
{
    fe2_call_args args;
    game->set_panel_mode(game->opaque, PREFERENCES_PANEL);
    s->menu_mode = FE2_MENU_PREFERENCES;
    fe2_args_clear(&args);
    fe2_args_word(&args, PANEL_TEXT_PARAMETER, PREFERENCES_HEADING);
    game->select_panel(game->opaque, PANEL_REQUEST, &args);
    game->draw_sprite(game->opaque, MENU_SPRITE, 0x100, 0xbc);
    s->time_step = 0;
    s->game_time_step = 0;
    game->set_controls(game->opaque, 0, CONTROLS_BX, CONTROLS_CL);
}

static void draw_preference_text(fe2_commander_state *s, const fe2_commander_services *game, uint16_t text)
{
    fe2_call_args args;
    s->format_colours[SLOT_9F7] = s->colours[COLOUR_1];
    s->format_colours[SLOT_9EB] = s->colours[COLOUR_3];
    s->format_colours[SLOT_9E9] = s->colours[COLOUR_4];
    fe2_args_clear(&args);
    fe2_args_word(&args, STYLE_PARAMETER, 0);
    game->draw_text(game->opaque, text, s->colours[COLOUR_4], 6, 9, &args);
}

/* One on/off icon per widget, then the two text columns. */
void fe2_commander_draw_preferences(fe2_commander_state *s, const fe2_commander_services *game)
{
    unsigned i;
    game->clear_panel(game->opaque);
    for (i = 0; !(fe2_commander_preference_widgets[5 * i] & 0x8000u); ++i)
        game->draw_icon(game->opaque, s->preferences[i] ? ON_ICON : OFF_ICON, fe2_commander_preference_widgets[5 * i],
                        fe2_commander_preference_widgets[5 * i + 1]);
    draw_preference_text(s, game, PREFERENCE_TEXT);
    draw_preference_text(s, game, SOUND_TEXT);
    game->present(game->opaque);
}

static void refresh(fe2_commander_state *s, const fe2_commander_services *game)
{
    game->play_effect(game->opaque, EFFECT_MENU);
    fe2_commander_apply_detail(s);
    fe2_commander_draw_preferences(s, game);
}

/* Music changes reselect the automatic music when automatic music is on. */
static void reselect_music(fe2_commander_state *s, const fe2_commander_services *game)
{
    if (s->preferences[FE2_PREF_AUTO_MUSIC] != 0) game->select_music(game->opaque, AUTOMATIC_MUSIC);
}

/* A music track enabled or disabled: disabling the playing track stops it;
 * either may reselect automatic music. */
static void toggle_track(fe2_commander_state *s, const fe2_commander_services *game, unsigned track)
{
    uint8_t *enabled = &s->preferences[FE2_PREF_MUSIC_TRACKS + track];
    *enabled ^= 0xff;
    if (*enabled != 0) {
        if (s->current_music < 0) reselect_music(s, game);
        return;
    }
    if ((uint16_t)s->current_music != track) return;
    game->reset_music(game->opaque);
    reselect_music(s, game);
}

/* Returns 0 when the key is not a preference key (it is forwarded). */
int fe2_commander_preference_key(fe2_commander_state *s, const fe2_commander_services *game, uint8_t key)
{
    uint8_t *p = s->preferences;
    unsigned i;
    if (key >= KEY_FIRST_DETAIL && key <= KEY_LAST_DETAIL) {
        memcpy(p + FE2_PREF_DETAIL_LOW, fe2_commander_detail_presets[key - KEY_FIRST_DETAIL], 4);
    } else if (key == KEY_DUST) {
        if ((p[FE2_PREF_SPACE_DUST] ^= 0xff) == 0) game->clear_space_dust(game->opaque);
    } else if (key == 0x9c) {
        p[FE2_PREF_FILENAME_EXTENSIONS] = (uint8_t)~p[FE2_PREF_FILENAME_EXTENSIONS];
        s->extension[0] ^= EXTENSION_DOT;
    } else if (key == KEY_EFFECTS) {
        if ((p[FE2_PREF_SOUND_EFFECTS] ^= 0xff) == 0) {
            game->stop_effects(game->opaque);
            s->game_flags &= (uint8_t)~SOUND_PLAYING;
        }
    } else if (key == KEY_MUSIC) {
        if ((p[FE2_PREF_MUSIC] ^= 0xff) == 0 && p[FE2_PREF_AUTO_MUSIC] == 0) game->check_music(game->opaque);
    } else if (key == KEY_AUTO_MUSIC) {
        if ((p[FE2_PREF_AUTO_MUSIC] ^= 0xff) == 0) game->check_music(game->opaque);
        else game->select_music(game->opaque, AUTOMATIC_MUSIC);
    } else if (key == KEY_STATION) {
        p[FE2_PREF_STATION_SOUND] ^= 0xff;
    } else if (key == KEY_WIND) {
        if ((p[FE2_PREF_WIND_SOUND] ^= 0xff) == 0) s->game_flags &= (uint8_t)~SOUND_PLAYING;
    } else if (key >= KEY_FIRST_TRACK && key <= KEY_LAST_TRACK) {
        toggle_track(s, game, (unsigned)(key - KEY_FIRST_TRACK));
    } else {
        for (i = 0; i < sizeof toggles / sizeof toggles[0]; ++i)
            if (toggles[i].key == key) break;
        if (i == sizeof toggles / sizeof toggles[0]) return 0;
        p[toggles[i].preference] = (uint8_t)~p[toggles[i].preference];
    }
    refresh(s, game);
    return 1;
}

/* The game's start: default preferences and time, the current directory
 * (with its COMMANDR subdirectory when there is one) and the default
 * commander name. */
static void start_game(fe2_commander_state *s, const fe2_commander_services *game)
{
    const fe2_dos_platform *dos = game->dos;
    fe2_dos_found found;
    char *end;
    int missing;
    s->last_size = 0;
    memcpy(s->preferences, fe2_commander_default_preferences, sizeof s->preferences);
    fe2_commander_apply_detail(s);
    s->time_step = NORMAL_STEP;
    s->event_base = START_201B;
    s->event_time = 0;
    s->expected_letter = 0;
    s->input_mask = 0xff;
    s->time_exponent = 0;
    s->game_time_step = s->time_step;
    s->game_time_exponent = s->time_exponent;
    s->directory[0] = (char)s->drive;
    s->directory[1] = ':';
    s->directory[2] = '\\';
    dos->current_directory(dos->opaque, (uint8_t)(s->drive - 0x40), s->directory + 3);
    end = fe2_commander_end(s->directory + 1);
    if (end[-1] != '\\') { end[0] = '\\'; end[1] = 0; }
    dos->search_begin(dos->opaque);
    missing = dos->search_first(dos->opaque, fe2_commander_commander_directory, SEARCH_DIRECTORIES, &found);
    dos->search_end(dos->opaque);
    if (!missing) {
        end = fe2_commander_end(s->directory + 1);
        strcpy(end, fe2_commander_commander_directory);
        end += strlen(fe2_commander_commander_directory);
        end[0] = '\\';
        end[1] = 0;
    }
    strcpy(s->commander_name, fe2_commander_default_name);
    strcpy(s->extension, fe2_commander_default_extension);
    s->extension[0] = EXTENSION_DOT;
    fe2_commander_copy_upper_name(s);
    s->buffer_limit = FE2_COMMANDER_BUFFER_BYTES;
}

static int open_load_dialog(fe2_commander_state *s, const fe2_commander_services *game)
{
    s->menu_mode = FE2_MENU_CLOSED;
    fe2_commander_pause(s, game);
    fe2_commander_enter_preferences(s, game);
    fe2_commander_open_dialog(s, game, FE2_MENU_LOAD);
    return FE2_COMMANDER_CONTINUE;
}

int fe2_commander_initialize(fe2_commander_state *s, const fe2_commander_services *game, uint16_t request)
{
    s->menu_mode = FE2_MENU_CLOSED;
    if ((uint8_t)request == 0) {
        start_game(s, game);
        return FE2_COMMANDER_CONTINUE;
    }
    if ((uint8_t)request != 4) return FE2_COMMANDER_CONTINUE;
    strcpy(s->commander_name, fe2_commander_default_name);
    fe2_commander_copy_upper_name(s);
    if (request == 0x404) return open_load_dialog(s, game);
    if (request != 0x504) return FE2_COMMANDER_CONTINUE;
    if (s->last_size == 0) return open_load_dialog(s, game);
    s->menu_mode = FE2_MENU_CLOSED;
    fe2_commander_pause(s, game);
    fe2_commander_enter_preferences(s, game);
    s->menu_mode = FE2_MENU_LOAD;
    {
        char name[FE2_COMMANDER_FILENAME_BYTES];
        memcpy(name, s->last_name, sizeof name);
        return fe2_commander_load(s, game, name, s->last_size);
    }
}
