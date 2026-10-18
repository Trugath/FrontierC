/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_STATUS_STATUS_INTERNAL_H
#define FE2_STATUS_STATUS_INTERNAL_H

/* Shared declarations between the status translation units. */
#include "status.h"
#include "status_tables.h"

/* Named parameter slots used by commander services. */
enum {
    PARAM_0, PARAM_1, PARAM_2, PARAM_3, PARAM_4, PARAM_5, PARAM_6,
    PARAM_STYLE,    /* text style, message portrait or creation mode */
    PARAM_SUBJECT,  /* object or named subject */
    PARAM_RESULT,   /* object operated on or created */
    PARAM_10, PARAM_11,
    PARAM_OBJECTS   /* system object list */
};

enum {
    TEXT_COLOUR_WHITE = 0x0f,
    LIST_ICON = 0x70,
    STYLE_PLAIN = 0x0000,
    STYLE_MESSAGE = 0xffff,
    TRACK_FANFARE = 7
};

/* Equivalent of `x >> n` for signed values, independent of the compiler. */
int32_t fe2_status_sar32(int32_t value, unsigned count);
int16_t fe2_status_sar16(int16_t value, unsigned count);

/* page_layout.c */
void fe2_status_draw_line(const fe2_game_services *game, fe2_call_args *args, uint16_t text,
                       uint16_t x, uint16_t y);
void fe2_status_draw_heading(fe2_commander_status *status, const fe2_game_services *game,
                          fe2_call_args *args, uint16_t text);
void fe2_status_draw_balance(const fe2_commander_status *status, const fe2_game_services *game,
                          fe2_call_args *args);
void fe2_status_draw_preview_frame(const fe2_game_services *game);
void fe2_status_render_preview(fe2_commander_status *status, const fe2_game_services *game);
void fe2_status_draw_page_sprite(const fe2_game_services *game, uint8_t page);

/* Pages; only the equipment page can fail. */
int fe2_status_draw_equipment_page(fe2_commander_status *status, const fe2_game_services *game);
void fe2_status_draw_profile_page(fe2_commander_status *status, const fe2_game_services *game);
void fe2_status_draw_crew_page(fe2_commander_status *status, const fe2_game_services *game);
void fe2_status_draw_cargo_page(fe2_commander_status *status, const fe2_game_services *game);
void fe2_status_draw_passenger_page(fe2_commander_status *status, const fe2_game_services *game);
void fe2_status_draw_mission_page(fe2_commander_status *status, const fe2_game_services *game);
void fe2_status_draw_mining_page(fe2_commander_status *status, const fe2_game_services *game);

/* status_screen.c */
int fe2_status_select_page(fe2_commander_status *status, const fe2_game_services *game,
                        uint8_t page);
void fe2_status_redraw_crew_page(fe2_commander_status *status, const fe2_game_services *game);
void fe2_status_redraw_cargo_page(fe2_commander_status *status, const fe2_game_services *game);

/* military_rank.c */
typedef enum fe2_navy { FE2_NAVY_FEDERAL, FE2_NAVY_IMPERIAL } fe2_navy;
uint16_t fe2_status_rank_text(const fe2_commander_status *status, fe2_navy navy);
/* Grant `permit` (negative: none) and add `delta` points; announces a new
 * permit, promotion or demotion. `args` holds the caller's parameters (the
 * caller sets 0 and 4 to the delta and permit as it passes them); parameter 1
 * receives the resulting rank text, as the original leaves it there. */
void fe2_status_award(fe2_commander_status *status, const fe2_game_services *game,
                   fe2_call_args *args, fe2_navy navy, int16_t permit, int16_t delta);

/* contracts.c / missions.c */
void fe2_status_contracts_changed(const fe2_game_services *game);
void fe2_status_settle_missions(fe2_commander_status *status, const fe2_game_services *game);
void fe2_status_place_mission_targets(fe2_commander_status *status, const fe2_game_services *game);
void fe2_status_check_deadlines(fe2_commander_status *status, const fe2_game_services *game);

#endif
