/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_COMMANDER_INTERNAL_H
#define FE2_COMMANDER_INTERNAL_H

#include "preferences.h"

enum {
    /* Palette entries (state->colours) and formatter colour slots. */
    COLOUR_1 = 1, COLOUR_2 = 2, COLOUR_3 = 3, COLOUR_4 = 4, COLOUR_5 = 5, COLOUR_10 = 10,
    SLOT_9E9 = 0, SLOT_9EA = 1, SLOT_9EB = 2, SLOT_9F7 = 0x0e,
    /* Parameters. */
    STYLE_PARAMETER = 7, CODE_PARAMETER = 1, PANEL_TEXT_PARAMETER = 2, OBJECTS_PARAMETER = 12,
    SUBJECT_PARAMETER = 10, SECOND_SUBJECT_PARAMETER = 11, NAME_PARAMETER = 8,
    /* Time. */
    NORMAL_STEP = 0x3e2,
    ICON_PAUSED = 0x5d, ICON_TIME_STOPPED = 0x5e, ICON_NORMAL = 0x5f,
    TIME_ICON_Y = 0xb3,
    EFFECT_MENU = 0x14
};

static inline char *fe2_commander_end(char *text) { while (*text) ++text; return text; }

/* preferences.c */
void fe2_commander_enter_preferences(fe2_commander_state *s, const fe2_commander_services *game);
void fe2_commander_apply_detail(fe2_commander_state *s);
void fe2_commander_draw_preferences(fe2_commander_state *s, const fe2_commander_services *game);
int fe2_commander_preference_key(fe2_commander_state *s, const fe2_commander_services *game, uint8_t key);
void fe2_commander_copy_upper_name(fe2_commander_state *s);
/* time_control.c */
int fe2_commander_pause(fe2_commander_state *s, const fe2_commander_services *game);
int fe2_commander_publish_time(fe2_commander_state *s, const fe2_commander_services *game, uint16_t icon);
/* file_dialogs.c */
int fe2_commander_file_key(fe2_commander_state *s, const fe2_commander_services *game, uint8_t key);
void fe2_commander_open_dialog(fe2_commander_state *s, const fe2_commander_services *game, uint8_t mode);
void fe2_commander_enumerate(fe2_commander_state *s, const fe2_commander_services *game);
void fe2_commander_draw_dialog(fe2_commander_state *s, const fe2_commander_services *game);
void fe2_commander_draw_save_dialog(fe2_commander_state *s, const fe2_commander_services *game);
void fe2_commander_show_error(fe2_commander_state *s, const fe2_commander_services *game, uint16_t text);
void fe2_commander_show_dos_error(fe2_commander_state *s, const fe2_commander_services *game);
void fe2_commander_text_args(fe2_call_args *args);
/* image.c: commander's own fields in the resident image (the file format). */
void fe2_commander_read_image(fe2_commander_state *s, const fe2_save_image *image);
void fe2_commander_write_image(const fe2_commander_state *s, fe2_save_image *image);
/* commander_files.c */
int fe2_commander_load(fe2_commander_state *s, const fe2_commander_services *game, const char *name, uint32_t size);
void fe2_commander_save(fe2_commander_state *s, const fe2_commander_services *game);
void fe2_commander_delete(fe2_commander_state *s, const fe2_commander_services *game, const char *name);
void fe2_commander_edit_name(fe2_commander_state *s, const fe2_commander_services *game);
void fe2_commander_next_extension(fe2_commander_state *s);
void fe2_commander_remember(fe2_commander_state *s, const char *name, uint32_t size);

#endif
