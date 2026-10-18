/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* dialogs, 0x159c the directory list, 0x16b8 directory navigation,
 * 0x1751 enumeration, 0x13da file errors and 0x1055/0x11f8 menu input. */
#include <string.h>

#include "commander_internal.h"

enum {
    LOAD_PROMPT = 0x9403, DELETE_PROMPT = 0x9404, SAVE_PROMPT = 0x9405, HEADING = 0x9406,
    READING_TEXT = 0x9408, DISC_ERROR_CODE = 0x940c, FILE_EXISTS = 0x940e,
    CORRUPT = 0x9410, WRITE_PROTECTED = 0x9411, DISC_FULL = 0x9412, PROTECTED = 0x9413,
    NO_DISC = 0x9414,
    KEY_LOAD = 0xf7, KEY_SAVE = 0xf8, KEY_DELETE = 0xf9, KEY_QUIT = 0xfa,
    KEY_FIRST_ENTRY = 0x80, KEY_LAST_ENTRY = 0xbf, KEY_SAVE_FILE = 0x0d, KEY_EDIT_NAME = 0xe0,
    ICON_OFF = 0x5c,
    LIST_LEFT = 0x0a, LIST_TOP = 0x1e, LIST_BOTTOM = 0x9e, LIST_ROW = 0x0a, LIST_COLUMN = 0x3e,
    HOTSPOT_WIDTH = 0x1f, HOTSPOT_HEIGHT = 5, ENTRY_EVENT = 0x80,
    ATTRIBUTE_DIRECTORY = 0x10, SEARCH_DIRECTORIES = 0x31, SEARCH_FILES = 0x21,
    WAIT_ERROR = 0xfa
};

/* Which palette entry draws each entry kind (kind / 2). */
static const uint8_t kind_colours[4] = {COLOUR_10, COLOUR_2, COLOUR_3, COLOUR_4};

/* DOS error codes and the texts that report them; others show the code. */
static const struct { uint16_t code, text; } dos_errors[] = {
    {0x50, FILE_EXISTS}, {0x17, CORRUPT}, {0x19, CORRUPT}, {0x1a, CORRUPT}, {0x1b, CORRUPT},
    {0x1f, CORRUPT}, {0x13, WRITE_PROTECTED}, {0x02, WRITE_PROTECTED}, {0x08, DISC_FULL},
    {0x05, PROTECTED}, {0x03, NO_DISC}, {0x0f, NO_DISC}, {0x15, NO_DISC}
};

void fe2_commander_text_args(fe2_call_args *args)
{
    fe2_args_clear(args);
    fe2_args_word(args, STYLE_PARAMETER, 0);
}

/* An entry: the name is cut at 14 bytes (and then terminated). */
static void append_entry(fe2_commander_state *s, uint16_t kind, uint32_t size, const char *name)
{
    fe2_directory_entry *e;
    unsigned i;
    if (s->entry_count >= FE2_COMMANDER_ENTRIES) return;
    e = &s->entries[s->entry_count++];
    e->kind = kind;
    e->size = size;
    for (i = 0; i < FE2_COMMANDER_ENTRY_NAME_BYTES; ++i)
        if ((e->name[i] = name[i]) == 0) return;
    e->name[FE2_COMMANDER_ENTRY_NAME_BYTES - 1] = 0;
}

static void draw_status(fe2_commander_state *s, const fe2_commander_services *game, uint16_t text)
{
    fe2_call_args args;
    s->format_colours[SLOT_9E9] = s->colours[COLOUR_1];
    fe2_commander_text_args(&args);
    fe2_args_string(&args, SUBJECT_PARAMETER, s->directory);
    game->draw_text(game->opaque, text, s->colours[COLOUR_10], 0x64, 0x46, &args);
    game->present(game->opaque);
}

/*
 * The listing: the drives, the parent, the subdirectories (not "." or
 * ".."), then the files, up to 64 entries. Only files carry their size;
 * the other entries carry whatever the original's registers held (see
 * the README).
 */
void fe2_commander_enumerate(fe2_commander_state *s, const fe2_commander_services *game)
{
    const fe2_dos_platform *dos = game->dos;
    fe2_dos_found found;
    char pattern[FE2_COMMANDER_DIRECTORY_BYTES + 4], name[FE2_DOS_NAME_BYTES + 1];
    const char *drive;
    game->clear_panel(game->opaque);
    draw_status(s, game, READING_TEXT);
    s->entry_count = 0;
    for (drive = s->drives; *drive; ++drive) {
        name[0] = *drive;
        name[1] = ':';
        name[2] = 0;
        append_entry(s, FE2_ENTRY_DRIVE, 0, name);
    }
    append_entry(s, FE2_ENTRY_PARENT, 0, fe2_commander_parent_label);
    dos->search_begin(dos->opaque);
    strcpy(pattern, s->directory);
    strcat(pattern, "*.*");
    if (!dos->search_first(dos->opaque, pattern, SEARCH_DIRECTORIES, &found)) {
        do {
            if (!(found.attributes & ATTRIBUTE_DIRECTORY) || (found.name[0] == '.' && found.name[1] == '.') ||
                strcmp(found.name, ".") == 0)
                continue;
            name[0] = '\\';
            memcpy(name + 1, found.name, FE2_DOS_NAME_BYTES);
            append_entry(s, FE2_ENTRY_DIRECTORY, 0, name);
        } while (!dos->search_next(dos->opaque, &found));
    }
    if (!dos->search_first(dos->opaque, pattern, SEARCH_FILES, &found)) {
        do append_entry(s, FE2_ENTRY_FILE, found.size, found.name);
        while (!dos->search_next(dos->opaque, &found));
    }
    dos->search_end(dos->opaque);
}

/* The current directory, then the entries in columns of hotspots (events
 * 0x80 + index). */
static void draw_entries(fe2_commander_state *s, const fe2_commander_services *game)
{
    fe2_call_args args;
    uint16_t x = LIST_LEFT, y = LIST_TOP, i;
    game->show_panel(game->opaque);
    s->format_colours[SLOT_9E9] = s->colours[COLOUR_1];
    fe2_commander_text_args(&args);
    fe2_args_string(&args, SUBJECT_PARAMETER, s->directory);
    game->draw_text(game->opaque, HEADING, s->colours[COLOUR_1], 0x0a, 0x0f, &args);
    for (i = 0; i < s->entry_count; ++i) {
        const fe2_directory_entry *e = &s->entries[i];
        game->draw_string(game->opaque, e->name, s->colours[kind_colours[e->kind >> 1]], x, y);
        game->add_hotspot(game->opaque, (uint16_t)(x + HOTSPOT_WIDTH), (uint16_t)(y + HOTSPOT_HEIGHT),
                          HOTSPOT_WIDTH, HOTSPOT_HEIGHT, (uint16_t)(i + ENTRY_EVENT));
        y = (uint16_t)(y + LIST_ROW);
        if ((int16_t)y >= LIST_BOTTOM) {
            x = (uint16_t)(x + LIST_COLUMN);
            y = LIST_TOP;
        }
    }
}

static void draw_listing_dialog(fe2_commander_state *s, const fe2_commander_services *game, uint16_t prompt)
{
    fe2_call_args args;
    game->clear_panel(game->opaque);
    fe2_commander_text_args(&args);
    game->draw_text(game->opaque, prompt, s->colours[COLOUR_2], 0x32, 5, &args);
    draw_entries(s, game);
    game->present(game->opaque);
}

/* The save dialog adds the name and extension, the name widgets and an
 * off icon at the name box. */
void fe2_commander_draw_save_dialog(fe2_commander_state *s, const fe2_commander_services *game)
{
    fe2_call_args args;
    game->clear_panel(game->opaque);
    s->format_colours[SLOT_9E9] = s->colours[COLOUR_1];
    s->format_colours[SLOT_9EA] = s->colours[COLOUR_2];
    fe2_commander_text_args(&args);
    fe2_args_string(&args, SUBJECT_PARAMETER, s->commander_name);
    fe2_args_string(&args, SECOND_SUBJECT_PARAMETER, s->extension);
    game->draw_text(game->opaque, SAVE_PROMPT, s->colours[COLOUR_2], 0x2d, 5, &args);
    draw_entries(s, game);
    game->set_widgets(game->opaque, fe2_commander_filename_widgets);
    game->draw_icon(game->opaque, ICON_OFF, fe2_commander_filename_widgets[0], fe2_commander_filename_widgets[1]);
    game->present(game->opaque);
}

void fe2_commander_draw_dialog(fe2_commander_state *s, const fe2_commander_services *game)
{
    if (s->menu_mode == FE2_MENU_SAVE) fe2_commander_draw_save_dialog(s, game);
    else draw_listing_dialog(s, game, s->menu_mode == FE2_MENU_DELETE ? DELETE_PROMPT : LOAD_PROMPT);
}

void fe2_commander_open_dialog(fe2_commander_state *s, const fe2_commander_services *game, uint8_t mode)
{
    s->menu_mode = mode;
    fe2_commander_enumerate(s, game);
    fe2_commander_draw_dialog(s, game);
}

/* A file error waits, a tick at a time, for a key; the listing is then
 * read again. */
void fe2_commander_show_error(fe2_commander_state *s, const fe2_commander_services *game, uint16_t text)
{
    fe2_call_args args;
    uint8_t key;
    fe2_commander_text_args(&args);
    if (text == DISC_ERROR_CODE) fe2_args_dword(&args, CODE_PARAMETER, s->dos_error);
    game->draw_text(game->opaque, text, s->colours[COLOUR_2], 0x64, 0x5a, &args);
    game->present(game->opaque);
    do {
        s->tick = 1;
        game->wait_tick(game->opaque);
    } while (!game->poll_key(game->opaque, &key));
    fe2_commander_enumerate(s, game);
    fe2_commander_draw_dialog(s, game);
}

void fe2_commander_show_dos_error(fe2_commander_state *s, const fe2_commander_services *game)
{
    unsigned i;
    game->clear_panel(game->opaque);
    for (i = 0; i < sizeof dos_errors / sizeof dos_errors[0]; ++i)
        if (dos_errors[i].code == s->dos_error) break;
    fe2_commander_show_error(s, game, i < sizeof dos_errors / sizeof dos_errors[0] ? dos_errors[i].text : DISC_ERROR_CODE);
}

/* The directory chosen from a drive (or other) entry: its name and a
 * backslash. */
static void enter_directory(fe2_commander_state *s, const fe2_commander_services *game, const char *name)
{
    size_t length = strlen(name);
    memcpy(s->directory, name, length);
    s->directory[length] = '\\';
    s->directory[length + 1] = 0;
    fe2_commander_enumerate(s, game);
}

/* Up one level: cut after the previous backslash (not at the root). */
static void leave_directory(fe2_commander_state *s, const fe2_commander_services *game)
{
    char *at = fe2_commander_end(s->directory) - 1;
    while (--at > s->directory)
        if (*at == '\\') {
            at[1] = 0;
            fe2_commander_enumerate(s, game);
            return;
        }
}

/* Into a subdirectory ("\NAME"): appended while it fits (the directory
 * ends before 0x5bda), with a backslash after. */
static void open_subdirectory(fe2_commander_state *s, const fe2_commander_services *game, const char *name)
{
    char *to = fe2_commander_end(s->directory + 1), *limit = s->directory + (FE2_COMMANDER_DIRECTORY_BYTES - 1);
    const char *from = name + 1;
    for (;;) {
        char c;
        if (to >= limit) goto done;
        c = *from++;
        *to++ = c;
        if (c == 0) break;
    }
    if (to < limit) { to[-1] = '\\'; to[0] = 0; }
done:
    fe2_commander_enumerate(s, game);
}

/* A file chosen: the save dialog takes its name (up to the dot), delete
 * deletes it, and load (or the preferences) loads it. */
static int choose_entry(fe2_commander_state *s, const fe2_commander_services *game, const fe2_directory_entry *e)
{
    char name[FE2_COMMANDER_ENTRY_NAME_BYTES];
    memcpy(name, e->name, sizeof name);
    switch (e->kind) {
    case FE2_ENTRY_FILE:
        break;
    case FE2_ENTRY_DIRECTORY:
        open_subdirectory(s, game, name);
        fe2_commander_draw_dialog(s, game);
        return FE2_COMMANDER_CONTINUE;
    case FE2_ENTRY_PARENT:
        leave_directory(s, game);
        fe2_commander_draw_dialog(s, game);
        return FE2_COMMANDER_CONTINUE;
    default:
        enter_directory(s, game, name);
        fe2_commander_draw_dialog(s, game);
        return FE2_COMMANDER_CONTINUE;
    }
    if (s->menu_mode == FE2_MENU_SAVE) {
        unsigned i;
        for (i = 0; i < FE2_COMMANDER_ENTRY_NAME_BYTES && name[i] != '.'; ++i)
            if ((s->commander_name[i] = name[i]) == 0) break;
        if (i < FE2_COMMANDER_ENTRY_NAME_BYTES && name[i] == '.') s->commander_name[i] = 0;
        fe2_commander_draw_save_dialog(s, game);
        return FE2_COMMANDER_CONTINUE;
    }
    if (s->menu_mode == FE2_MENU_DELETE) {
        fe2_commander_delete(s, game, name);
        return FE2_COMMANDER_CONTINUE;
    }
    return fe2_commander_load(s, game, name, e->size);
}

int fe2_commander_file_key(fe2_commander_state *s, const fe2_commander_services *game, uint8_t key)
{
    if (key >= KEY_FIRST_ENTRY && key <= KEY_LAST_ENTRY)
        return choose_entry(s, game, &s->entries[key - KEY_FIRST_ENTRY]);
    if (s->menu_mode == FE2_MENU_SAVE && key == KEY_SAVE_FILE) {
        fe2_commander_save(s, game);
        return FE2_COMMANDER_CONTINUE;
    }
    if (s->menu_mode == FE2_MENU_SAVE && key == KEY_EDIT_NAME) {
        fe2_commander_edit_name(s, game);
        return FE2_COMMANDER_CONTINUE;
    }
    game->forward_key(game->opaque, key);
    return FE2_COMMANDER_CONTINUE;
}

int fe2_commander_menu_input(fe2_commander_state *s, const fe2_commander_services *game)
{
    uint8_t key;
    if (!game->poll_key(game->opaque, &key)) return FE2_COMMANDER_CONTINUE;
    switch (key) {
    case KEY_LOAD: fe2_commander_open_dialog(s, game, FE2_MENU_LOAD); return FE2_COMMANDER_CONTINUE;
    case KEY_SAVE: fe2_commander_open_dialog(s, game, FE2_MENU_SAVE); return FE2_COMMANDER_CONTINUE;
    case KEY_DELETE: fe2_commander_open_dialog(s, game, FE2_MENU_DELETE); return FE2_COMMANDER_CONTINUE;
    case KEY_QUIT: game->quit_game(game->opaque); return FE2_COMMANDER_QUIT;
    default: break;
    }
    if (s->menu_mode == FE2_MENU_PREFERENCES) {
        if (!fe2_commander_preference_key(s, game, key)) game->forward_key(game->opaque, key);
        return FE2_COMMANDER_CONTINUE;
    }
    return fe2_commander_file_key(s, game, key);
}
