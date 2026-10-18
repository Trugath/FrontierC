/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_STATUS_STATUS_H
#define FE2_STATUS_STATUS_H

/*
 * Commander status module (status): status pages, contracts, crew, cargo
 * and military standing.
 *
 * Every entry works on the commander state and calls the rest of the game
 * through `fe2_game_services`. Entries that can draw a page return
 * FE2_STATUS_DIVIDE_FAULT where the original equipment page executes an
 * unchecked 16-bit division that faults (zero or too-small hull mass);
 * otherwise they return FE2_STATUS_OK.
 */
#include "game_services.h"

enum { FE2_STATUS_OK = 0, FE2_STATUS_DIVIDE_FAULT = -2 };

/* Resident events dispatched to status (AX of vector 0x76). */
enum {
    FE2_EVENT_MILITARY_ARRIVAL = 0x02,
    FE2_EVENT_SYSTEM_ENTERED = 0x05,
    FE2_EVENT_NEW_DAY = 0x0f,
    FE2_EVENT_PAY_CREW = 0x10,
    FE2_EVENT_REDRAW_A = 0x16,
    FE2_EVENT_REDRAW_B = 0x17,
    FE2_EVENT_REDRAW_C = 0x19
};

/* 0x6e: clear contracts, crew, wages and offences for a new commander. */
void fe2_status_initialize(fe2_commander_status *status);

/* 0x72: open the status screen (first call) and draw the current page. */
int fe2_status_draw_screen(fe2_commander_status *status, const fe2_game_services *game);
/* Redraw the page already on screen. Opening the screen advances once;
 * painting it again after the 3D view must not turn the page. */
int fe2_status_refresh(fe2_commander_status *status, const fe2_game_services *game);

/* 0x8e: return to the status screen from another panel. */
void fe2_status_resume(fe2_commander_status *status, const fe2_game_services *game);

/* 0x76: game events. */
int fe2_status_event(fe2_commander_status *status, const fe2_game_services *game,
                     uint16_t event);

/* 0x7a: one key from the status screen. */
void fe2_status_input(fe2_commander_status *status, const fe2_game_services *game);

/* 0x86: possibly create a child object (for example cargo) beside `source`. */
void fe2_status_spawn_child(const fe2_game_services *game, fe2_object *source);

/* 0x8a: accept a contract; may create related objects. Callers pass the
 * contract as it appears in the argument block. */
void fe2_status_add_contract(fe2_commander_status *status, const fe2_game_services *game,
                             const fe2_contract *contract);

/* 0x92: format status text `selector` through the resident text formatter. */
void fe2_status_format_text(const fe2_game_services *game, uint16_t selector);

/* 0x96: periodic arrival scan for passengers, parcels and military missions. */
void fe2_status_scan_arrivals(fe2_commander_status *status, const fe2_game_services *game);

/* visitor: record the handle of an object named like the mission target. */
void fe2_status_match_target(fe2_commander_status *status, const fe2_military_contract *mission,
                             const fe2_object *object);

/* Context status passes to the object traversal service with
 * fe2_status_target_visitor (the original passes the mission in 0x7e0). */
typedef struct fe2_target_search {
    fe2_commander_status *status;
    const fe2_military_contract *mission;
} fe2_target_search;
void fe2_status_target_visitor(void *search, const fe2_object *object);

#endif
