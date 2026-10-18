/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* loading and saving commanders, 0x1349 the name editor, 0x1680 the
 * save preparation, 0x1859..0x1a55 DOS file operations. */
#include <string.h>

#include "commander_internal.h"

enum {
    LOADING_TEXT = 0x9409, SAVING_TEXT = 0x940a, DELETE_TEXT = 0x940b, INVALID_FILE = 0x9415,
    SAVE_EVENT = 4, LAST_HANDLE = 0x72, OBJECT_RECORD_BYTES = 0x11e,
    PATH_BYTES = 0x5c, FILE_NAME_BYTES = 0x40,
    ERROR_EXISTS = 0x50, ERROR_SHORT_WRITE = 0x08,
    KEY_YES = 'y', EDIT_LENGTH = 8, EDIT_FLAGS = 0x8000
};

static uint8_t file_buffer[FE2_COMMANDER_BUFFER_BYTES];

void fe2_commander_remember(fe2_commander_state *s, const char *name, uint32_t size)
{
    unsigned i;
    for (i = 0; i < FE2_COMMANDER_FILENAME_BYTES; ++i)
        if ((s->last_name[i] = name[i]) == 0) break;
    s->last_size = size;
}

/* The next save's extension: the two digits count up, wrapping at 99 (a
 * byte above '9', compared unsigned, also wraps). */
void fe2_commander_next_extension(fe2_commander_state *s)
{
    uint8_t *digits = (uint8_t *)s->extension + 2;
    if (++digits[1] <= '9') return;
    digits[1] = '0';
    if (++digits[0] > '9') digits[0] = '0';
}

static void full_path(const fe2_commander_state *s, const char *name, char path[PATH_BYTES])
{
    size_t length = strlen(s->directory);
    memcpy(path, s->directory, length);
    strncpy(path + length, name, PATH_BYTES - 1 - length);
    path[PATH_BYTES - 1] = 0;
}

/* A file operation's status line: the text with the name (the delete
 * confirmation also colours slot 0x9f7). */
static void draw_file_status(fe2_commander_state *s, const fe2_commander_services *game, uint16_t text, uint8_t colour,
                             const char *name, int confirm)
{
    fe2_call_args args;
    game->clear_panel(game->opaque);
    s->format_colours[SLOT_9E9] = s->colours[COLOUR_1];
    if (confirm) s->format_colours[SLOT_9F7] = s->colours[COLOUR_1];
    fe2_commander_text_args(&args);
    fe2_args_string(&args, SUBJECT_PARAMETER, name);
    game->draw_text(game->opaque, text, colour, 0x64, 0x46, &args);
    game->present(game->opaque);
}

/* Read `count` bytes of the file into the buffer; a DOS error is kept. */
static int read_commander(fe2_commander_state *s, const fe2_commander_services *game, const char *name, uint16_t count)
{
    const fe2_dos_platform *dos = game->dos;
    char path[PATH_BYTES];
    uint16_t handle, done;
    int error;
    draw_file_status(s, game, LOADING_TEXT, s->colours[COLOUR_10], name, 0);
    full_path(s, name, path);
    if ((error = dos->open_file(dos->opaque, path, 0, &handle)) != 0) {
        s->dos_error = (uint16_t)error;
        return 0;
    }
    error = dos->read_file(dos->opaque, handle, file_buffer, count, &done);
    if (error) s->dos_error = (uint16_t)error;
    dos->close_file(dos->opaque, handle);
    return error == 0;
}

/* Create the file (an existing one is error 0x50) and write `length`
 * bytes; a short write is error 8. */
static int write_commander(fe2_commander_state *s, const fe2_commander_services *game, const char *name, uint16_t length)
{
    const fe2_dos_platform *dos = game->dos;
    char path[PATH_BYTES];
    uint16_t handle, done;
    int error;
    draw_file_status(s, game, SAVING_TEXT, s->colours[COLOUR_5], name, 0);
    full_path(s, name, path);
    if (dos->open_file(dos->opaque, path, 0, &handle) == 0) {
        dos->close_file(dos->opaque, handle);
        s->dos_error = ERROR_EXISTS;
        return 0;
    }
    if ((error = dos->create_file(dos->opaque, path, 0, &handle)) != 0) {
        s->dos_error = (uint16_t)error;
        return 0;
    }
    error = dos->write_file(dos->opaque, handle, file_buffer, length, &done);
    if (!error && done < length) error = ERROR_SHORT_WRITE;
    if (error) s->dos_error = (uint16_t)error;
    dos->close_file(dos->opaque, handle);
    return error == 0;
}

/* A commander file must be 0x258 bytes to the buffer limit. Loading one
 * restarts the game with it; a bad one is reported. */
int fe2_commander_load(fe2_commander_state *s, const fe2_commander_services *game, const char *name, uint32_t size)
{
    fe2_save_image image;
    if ((size >> 16) != 0 || (uint16_t)size > s->buffer_limit || (uint16_t)size < FE2_COMMANDER_MINIMUM_BYTES) {
        game->clear_panel(game->opaque);
        fe2_commander_show_error(s, game, INVALID_FILE);
        return FE2_COMMANDER_CONTINUE;
    }
    s->selected_size = size;
    fe2_commander_remember(s, name, size);
    if (!read_commander(s, game, name, (uint16_t)size)) {
        fe2_commander_show_dos_error(s, game);
        return FE2_COMMANDER_CONTINUE;
    }
    image = game->save_image(game->opaque);
    if (!fe2_commander_decode(&image, s->buffer_limit, file_buffer, (uint16_t)size)) {
        game->clear_panel(game->opaque);
        fe2_commander_show_error(s, game, INVALID_FILE);
        return FE2_COMMANDER_CONTINUE;
    }
    fe2_commander_read_image(s, &image);
    fe2_commander_next_extension(s);
    fe2_commander_write_image(s, &image);
    game->check_music(game->opaque);
    game->restart_game(game->opaque);
    return FE2_COMMANDER_RESTARTED;
}

/* Before saving, the records of free object handles are cleared. */
static void prepare_save(const fe2_commander_services *game, const fe2_save_image *image)
{
    int handle;
    game->post_event(game->opaque, SAVE_EVENT);
    for (handle = LAST_HANDLE; handle >= 0; --handle)
        if (image->objects[handle] == 0) memset(game->object_record(game->opaque, (uint8_t)handle), 0, OBJECT_RECORD_BYTES);
}

void fe2_commander_save(fe2_commander_state *s, const fe2_commander_services *game)
{
    fe2_save_image image = game->save_image(game->opaque);
    char name[FILE_NAME_BYTES];
    uint16_t length;
    prepare_save(game, &image);
    fe2_commander_write_image(s, &image);
    length = fe2_commander_encode(&image, s->buffer_limit, file_buffer);
    strcpy(name, s->commander_name);
    strcat(name, s->extension);
    fe2_commander_remember(s, name, length);
    if (!write_commander(s, game, name, length)) {
        fe2_commander_show_dos_error(s, game);
        return;
    }
    fe2_commander_enumerate(s, game);
    fe2_commander_next_extension(s);
    fe2_commander_draw_save_dialog(s, game);
}

/* Delete after a 'y'; any other key cancels (and lists again). */
void fe2_commander_delete(fe2_commander_state *s, const fe2_commander_services *game, const char *name)
{
    const fe2_dos_platform *dos = game->dos;
    char path[PATH_BYTES];
    uint8_t key;
    int error;
    draw_file_status(s, game, DELETE_TEXT, s->colours[COLOUR_10], name, 1);
    while (!game->poll_key(game->opaque, &key)) {}
    if (key == KEY_YES) {
        full_path(s, name, path);
        if ((error = dos->delete_file(dos->opaque, path)) != 0) {
            s->dos_error = (uint16_t)error;
            fe2_commander_show_dos_error(s, game);
            return;
        }
    }
    fe2_commander_enumerate(s, game);
    fe2_commander_draw_dialog(s, game);
}

/* Edit the commander name in its box (eight characters). */
void fe2_commander_edit_name(fe2_commander_state *s, const fe2_commander_services *game)
{
    const uint16_t *box = fe2_commander_filename_widgets + 5;
    fe2_call_args args;
    fe2_commander_draw_save_dialog(s, game);
    s->edit_right = box[2];
    s->edit_bottom = box[3];
    s->line_colour = 0;
    game->draw_edit_box(game->opaque, box[0], box[1], box[2]);
    game->present(game->opaque);
    fe2_args_clear(&args);
    fe2_args_string(&args, NAME_PARAMETER, s->commander_name);
    game->edit_text(game->opaque, s->commander_name, EDIT_LENGTH, s->colours[COLOUR_3],
                    (uint16_t)(box[0] + 1), (uint16_t)(box[1] + 1), &args);
    fe2_commander_copy_upper_name(s);
    fe2_commander_draw_save_dialog(s, game);
}
