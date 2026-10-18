/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_COMMANDER_H
#define FE2_COMMANDER_H

/*
 * Preferences, time controls and commander files (commander).
 *
 * - Preferences: the shape detail, view, control and sound/music toggles,
 *   drawn as a panel of widgets.
 * - Time controls: the five time-acceleration keys, pausing and resuming.
 * - Commander files: the load, save and delete dialogs over a directory
 *   listing (drives, parent, subdirectories, files). A commander file is an
 *   encoded image of the resident game state (fe2_save_image).
 *
 * Drawing, keys, sound, the flight/panels/station overlays and the game's restart and
 * quit come through `fe2_commander_services`; files and directories through the
 * DOS-like `fe2_dos_platform`.
 */
#include <stdint.h>

#include "game/call_args.h"
#include "game/text_table.h"

enum {
    FE2_COMMANDER_PREFERENCES = 33,
    FE2_COMMANDER_PREFERENCE_WIDGETS = 31,
    FE2_COMMANDER_FILENAME_BYTES = 20,
    FE2_COMMANDER_EXTENSION_BYTES = 5,
    FE2_COMMANDER_DIRECTORY_BYTES = 68,
    FE2_COMMANDER_ENTRIES = 64,
    FE2_COMMANDER_ENTRY_NAME_BYTES = 14,
    FE2_COMMANDER_DRIVES = 26,
    FE2_DOS_NAME_BYTES = 13,
    /* The commander file: a 0x11 marker, the encoded image and a checksum
     * (see commander_codec.c). */
    FE2_COMMANDER_BUFFER_BYTES = 0xb84e,
    FE2_COMMANDER_MINIMUM_BYTES = 0x258,
    FE2_SAVE_OBJECT_BYTES = 0x80ee,
    FE2_SAVE_OVERLAY_RECORDS = 64,
    FE2_SAVE_TABLE_DWORDS = 64,
    FE2_SAVE_RESIDENT_START = 0x1fc1, FE2_SAVE_RESIDENT_END = 0x55cb,
    FE2_SAVE_STACK_START = 0xfd08, FE2_SAVE_STACK_END = 0xfd59
};

/* Preference bytes (state->preferences, from 0x4629). */
enum fe2_commander_preference {
    FE2_PREF_DETAIL_LOW = 0, FE2_PREF_DETAIL_MEDIUM = 1, FE2_PREF_DETAIL_HIGH = 2,
    FE2_PREF_SPACE_DUST = 4, FE2_PREF_BACKGROUND_STARS = 5, FE2_PREF_EXTERNAL_VIEW_LOCK = 6,
    FE2_PREF_REVERSE_LEFT_RIGHT = 7, FE2_PREF_REVERSE_UP_DOWN = 8, FE2_PREF_ELITE_CONTROLS = 9,
    FE2_PREF_TARGETING_TUNNELS = 10, FE2_PREF_SHIP_TARGETING_TUNNELS = 11,
    FE2_PREF_FILENAME_EXTENSIONS = 12, FE2_PREF_SOUND_EFFECTS = 13, FE2_PREF_ENGINE_SOUND = 14,
    FE2_PREF_STATION_SOUND = 15, FE2_PREF_WIND_SOUND = 16, FE2_PREF_UI_EFFECT = 17,
    FE2_PREF_MUSIC = 18, FE2_PREF_HYPERSPACE_MUSIC = 19, FE2_PREF_DOCKING_MUSIC = 20,
    FE2_PREF_BATTLE_MUSIC = 21, FE2_PREF_AUTO_MUSIC = 22, FE2_PREF_MUSIC_TRACKS = 23 /* 8 */
};

/* Menu modes. */
enum { FE2_MENU_CLOSED = 0, FE2_MENU_PREFERENCES = 1, FE2_MENU_LOAD = 2, FE2_MENU_SAVE = 3,
       FE2_MENU_DELETE = 4 };

/* Directory entry kinds. */
enum { FE2_ENTRY_FILE = 0, FE2_ENTRY_DIRECTORY = 2, FE2_ENTRY_PARENT = 4, FE2_ENTRY_DRIVE = 6 };

typedef struct fe2_directory_entry {
    uint16_t kind;
    uint32_t size;
    char name[FE2_COMMANDER_ENTRY_NAME_BYTES];
} fe2_directory_entry;

/* A directory search result (the DOS find record). */
typedef struct fe2_dos_found {
    uint8_t attributes;
    uint32_t size;
    char name[FE2_DOS_NAME_BYTES];
} fe2_dos_found;

typedef struct fe2_commander_state {
    /* Time. */
    uint32_t time_step;               /* the selected step */
    uint16_t time_exponent;
    uint32_t game_time_step;
    uint16_t game_time_exponent;
    uint32_t frame_step;
    uint32_t frame_step_high;
    uint32_t time_accumulator;
    /* Preferences. */
    uint8_t preferences[FE2_COMMANDER_PREFERENCES];
    uint8_t menu_mode;
    uint16_t detail[3];               /* renderer detail parameters */
    uint32_t event_base, event_time;  /* (set at start) */
    uint8_t expected_letter;          /* manual check (cleared at start) */
    uint8_t input_mask;
    uint8_t game_flags;               /* bit 0: no time keys; bit 6: a sound playing */
    uint8_t flight_state;             /* 8: hyperspace */
    uint8_t panel_flags;              /* bit 1: time keys only warn */
    uint16_t current_message;
    int16_t current_music;
    /* Commander. */
    char commander_name[FE2_COMMANDER_FILENAME_BYTES];
    char extension[FE2_COMMANDER_EXTENSION_BYTES];       /* ".xx" or NUL-first when hidden */
    char upper_name[FE2_COMMANDER_FILENAME_BYTES];
    uint16_t buffer_limit;
    /* The last file loaded or saved (the original also keeps an attribute
     * word, 0x5bef, which nothing uses). */
    char last_name[FE2_COMMANDER_FILENAME_BYTES];
    uint32_t last_size;
    /* Directories. */
    char directory[FE2_COMMANDER_DIRECTORY_BYTES]; /* "D:\path\" */
    uint8_t drive;                    /* current drive letter */
    char drives[FE2_COMMANDER_DRIVES + 1];  /* drive letters */
    fe2_directory_entry entries[FE2_COMMANDER_ENTRIES]; /* segment [0x7fe] */
    uint16_t entry_count;
    uint32_t selected_size;
    uint16_t dos_error;               /* the last DOS error */
    /* Screen. */
    uint8_t colours[11];              /* palette indices */
    uint8_t format_colours[0x0f];     /* formatter colour slots */
    uint16_t tick;                    /* a timer tick when it reaches 0 */
    uint16_t edit_right, edit_bottom; /* the name box */
    uint8_t line_colour;
} fe2_commander_state;

/*
 * The resident image a commander file holds (the native layout):
 * - the object block (FE2_SAVE_OBJECT_BYTES);
 * - the second dword of each 8-byte overlay record (from 0x396);
 * - the 64 dwords at 0x5d6;
 * - DS 0x1fc1..0x55ca;
 * - SS 0xfd08..0xfd58.
 */
typedef struct fe2_save_image {
    uint8_t *objects;
    uint8_t *overlay_records;         /* 64 records of 8 bytes */
    uint8_t *table;                   /* 64 dwords */
    uint8_t *resident;                /* from FE2_SAVE_RESIDENT_START */
    uint8_t *stack;                   /* from FE2_SAVE_STACK_START */
} fe2_save_image;

/* DOS-like files and directories; each returns 0 or a DOS error code. */
typedef struct fe2_dos_platform {
    void *opaque;
    int (*current_directory)(void *opaque, uint8_t drive, char *path);     /* 0x47, 64 bytes */
    /* A search session (the find record). */
    void (*search_begin)(void *opaque);
    int (*search_first)(void *opaque, const char *pattern, uint16_t attributes,
                        fe2_dos_found *found);                              /* 0x4e */
    int (*search_next)(void *opaque, fe2_dos_found *found);                 /* 0x4f */
    void (*search_end)(void *opaque);
    int (*open_file)(void *opaque, const char *path, uint8_t mode, uint16_t *handle); /* 0x3d */
    int (*create_file)(void *opaque, const char *path, uint16_t attributes,
                       uint16_t *handle);                                   /* 0x3c */
    int (*read_file)(void *opaque, uint16_t handle, uint8_t *buffer, uint16_t count,
                     uint16_t *done);                                       /* 0x3f */
    int (*write_file)(void *opaque, uint16_t handle, const uint8_t *buffer, uint16_t count,
                      uint16_t *done);                                      /* 0x40 */
    void (*close_file)(void *opaque, uint16_t handle);                      /* 0x3e */
    int (*delete_file)(void *opaque, const char *path);                     /* 0x41 */
} fe2_dos_platform;

typedef struct fe2_commander_services {
    void *opaque;
    const fe2_dos_platform *dos;
    /* Panels and drawing. */
    void (*set_panel_mode)(void *opaque, uint16_t mode);
    int (*poll_key)(void *opaque, uint8_t *key);
    void (*forward_key)(void *opaque, uint8_t key);
    void (*show_panel)(void *opaque);
    void (*restore_view)(void *opaque);
    void (*clear_panel)(void *opaque);
    void (*present)(void *opaque);
    void (*set_controls)(void *opaque, uint16_t layout_ax, uint16_t layout_bx, uint8_t buttons);
    /* Widgets: left, top, right, bottom, event; 0xffff ends. */
    void (*set_widgets)(void *opaque, const uint16_t *widgets);
    void (*add_hotspot)(void *opaque, uint16_t right, uint16_t bottom, uint16_t width,
                        uint16_t height, uint16_t event);
    void (*draw_sprite)(void *opaque, uint16_t sprite, uint16_t x, uint16_t y);
    void (*draw_icon)(void *opaque, uint16_t icon, uint16_t x, uint16_t y);
    void (*draw_text)(void *opaque, uint16_t text, uint8_t colour, uint16_t x, uint16_t y,
                      const fe2_call_args *args);
    void (*draw_string)(void *opaque, const char *text, uint8_t colour, uint16_t x, uint16_t y);
    void (*draw_edit_box)(void *opaque, uint16_t left, uint16_t top, uint16_t right);
    /* Edit `text` (up to `length`) at x, y; parameter 8 is the text. */
    void (*edit_text)(void *opaque, char *text, uint16_t length, uint8_t colour, uint16_t x,
                      uint16_t y, const fe2_call_args *args);
    void (*format_table_text)(void *opaque, uint16_t selector, const fe2_text_table *table,
                              fe2_text_out *out);
    void (*clear_space_dust)(void *opaque);
    void (*wait_tick)(void *opaque);  /* the timer clears state->tick */
    /* Game. */
    void (*post_event)(void *opaque, uint16_t event);
    /* The raw object record of `handle` (0x11e bytes). */
    uint8_t *(*object_record)(void *opaque, uint8_t handle);
    fe2_save_image (*save_image)(void *opaque);
    void (*restart_game)(void *opaque);                                     /* JMP FAR [0x44] */
    void (*quit_game)(void *opaque);                                        /* JMP FAR [0x40] */
    /* Sound. */
    void (*check_music)(void *opaque);
    void (*select_music)(void *opaque, uint16_t track);
    void (*play_effect)(void *opaque, uint16_t effect);
    void (*stop_effects)(void *opaque);
    void (*reset_music)(void *opaque);
    /* Overlays. */
    void (*select_panel)(void *opaque, uint16_t request, const fe2_call_args *args); /* panels 0x8a */
    void (*refresh_cockpit)(void *opaque);                                  /* flight +0x8 */
    void (*show_message)(void *opaque, uint16_t text, const fe2_call_args *args); /* station */
} fe2_commander_services;

/* Results of the entries that can leave the game loop. */
enum { FE2_COMMANDER_CONTINUE = 0, FE2_COMMANDER_RESTARTED = 1, FE2_COMMANDER_QUIT = 2 };

/* 0x6e: AL=0 starts the game (defaults, directories, commander name);
 * AL=4 restores the commander name, and AX=0x404 opens the load dialog,
 * 0x504 reloads the last commander. */
int fe2_commander_initialize(fe2_commander_state *state, const fe2_commander_services *game, uint16_t request);
/* 0x72: pause, or open the preferences when paused. */
int fe2_commander_draw_if_paused(fe2_commander_state *state, const fe2_commander_services *game);
/* 0x7a: the time-acceleration keys, then the menu's own input. */
int fe2_commander_time_input(fe2_commander_state *state, const fe2_commander_services *game);
/* 0x7e: preferences and file dialog input. */
int fe2_commander_menu_input(fe2_commander_state *state, const fe2_commander_services *game);
/* 0x8a: restore the normal time step; nonzero (AL=0xff) when it changed. */
int fe2_commander_restore_time_step(fe2_commander_state *state, const fe2_commander_services *game);
/* 0x8e: leave the menu at normal time. */
void fe2_commander_resume(fe2_commander_state *state, const fe2_commander_services *game);
/* 0x92: commander text `selector` (AX & 0x3fe). */
void fe2_commander_text(const fe2_commander_services *game, uint16_t selector, fe2_text_out *out);

/* The commander file codec (commander_codec.c). Encode `image` into
 * `file` (returns its length); decode `length` bytes of `file` into
 * `image` (returns nonzero when the marker and checksum match). */
uint16_t fe2_commander_encode(const fe2_save_image *image, uint16_t buffer_limit, uint8_t *file);
int fe2_commander_decode(fe2_save_image *image, uint16_t buffer_limit, uint8_t *file, uint16_t length);

/* Native tables (tools/commander_tables_c.py). */
extern const uint8_t *fe2_commander_default_preferences;
extern const uint16_t *fe2_commander_preference_widgets;
extern const uint8_t (*fe2_commander_detail_presets)[4];
extern const uint16_t *fe2_commander_filename_widgets;
extern const uint8_t *fe2_commander_text_directory;
extern const char *fe2_commander_commander_directory;   /* "COMMANDR" */
extern const char *fe2_commander_default_name;          /* "Jameson" */
extern const char *fe2_commander_default_extension;     /* "._01" */
extern const char *fe2_commander_parent_label;          /* "(Parent Dir)" */

#endif
