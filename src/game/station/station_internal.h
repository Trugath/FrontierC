/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_STATION_INTERNAL_H
#define FE2_STATION_INTERNAL_H

#include "station.h"
#include "station_tables.h"

/* Parameters (the argument block slots). */
enum {
    P0, P1, P2, P3, P4, P5, P6,
    P_STYLE = 7,
    P_SUBJECT = 8,     /* the current object */
    P_RECORD = 9,
    P_NAME = 10,
    P_TEXT = 11,
    P_LIST = 12
};

enum {
    STATION_FEE_TIER_LO = 0xb3,
    STATION_FEE_TIER_MID = 0xcc,
    STATION_FEE_TIER_HI = 0xee,
    STATION_QUEUE_BYTES = 0x70,
    STATION_MESSAGE_BYTES = 0x1c,
    STATION_ROW_BYTES = 0x18,
    STATION_UI_SELECTION = 1,
    STATION_UI_MESSAGE = 2,
    STATION_MESSAGE_TIME = 0x2f54d,
    STATION_SELECTION_TIME = 0x54f42,
    STATION_NAME_OFFSET = 0xf6,           /* an object's name: its reference + 0xf6 */
    STATION_TEXT_BASE = 0x9800            /* the first handler text */
};

/* The translation units' context. `stop` is set once the session restarts
 * (0x40 does not return in the original). */
typedef struct station_context {
    fe2_station_state *s;
    const fe2_station_services *g;
    fe2_call_args *a;
    int stop;
} station_context;

/* Parameters. Word, byte and high-word stores keep the other bytes. */
uint32_t station_get(const station_context *c, unsigned n);
void station_set(station_context *c, unsigned n, uint32_t value);
void station_set_word(station_context *c, unsigned n, uint16_t value);
void station_set_high(station_context *c, unsigned n, uint16_t value);
void station_set_byte(station_context *c, unsigned n, uint8_t value);
void station_set_table(station_context *c, unsigned n, const void *table);
static inline uint16_t station_word(const station_context *c, unsigned n) { return (uint16_t)station_get(c, n); }

/* Records. */
fe2_object *station_lookup_object(const station_context *c, fe2_ref ref);
fe2_ref station_name_ref(fe2_ref object);
uint32_t station_ror(uint32_t value, unsigned bits);
uint32_t station_rol(uint32_t value, unsigned bits);
const fe2_station_model *station_lookup_model(const station_context *c, uint16_t model);
fe2_station_row *station_row_at(const station_context *c, uint16_t ref_offset);
uint16_t station_row_offset(const station_context *c, unsigned index);
uint16_t station_market_word(const uint8_t *market, unsigned at);
void station_set_market_word(uint8_t *market, unsigned at, uint16_t value);

/* Screen and list (screen.c). */
void station_reset_rows(station_context *c);
void station_start_selection(station_context *c);
void station_append(station_context *c, uint16_t text);
void station_open_screen(station_context *c);
void station_draw_frame(station_context *c, uint16_t action);
void station_draw_default(station_context *c);
void station_draw_board(station_context *c, uint16_t heading);
void station_draw_status_list(station_context *c, uint16_t heading);
void station_draw_list(station_context *c, uint16_t action);
void station_draw_status(station_context *c);
void station_draw_balance(station_context *c);
void station_show_portrait(station_context *c);
void station_show_result(station_context *c, uint16_t text, uint16_t action);
void station_dispatch_action(station_context *c);

/* Messages (message.c). */
void station_show_message(station_context *c, uint16_t text);
void station_format_message(station_context *c, uint16_t text);
void station_post_message(station_context *c, uint16_t text);
void station_set_message(station_context *c, uint16_t text);
void station_show_current(station_context *c);
void station_show_heading(station_context *c, uint16_t text);
void station_pump_messages(station_context *c);

/* Selection (selection.c). */
void station_reset_selection_keys(station_context *c);
void station_select_row(station_context *c, uint16_t index);
void station_run_handler(station_context *c, uint16_t text);
void station_resume_action(station_context *c);

/* Handlers by family. Each takes the selector index (text - 0x9800). */
void station_service_handler(station_context *c, unsigned index);   /* services.c */
void station_fitting_handler(station_context *c, unsigned index);   /* fitting.c */
void station_yard_handler(station_context *c, unsigned index);      /* shipyard.c */
void station_board_handler(station_context *c, unsigned index);     /* bulletin.c */
void station_mission_handler(station_context *c, unsigned index);   /* missions.c */
void station_talk_handler(station_context *c, unsigned index);      /* conversation.c */
void station_charge_handler(station_context *c, unsigned index);    /* charges.c */
void station_trade_handler(station_context *c, unsigned index);     /* trade.c */

/* Screens and shared steps. */
void station_show_fitting(station_context *c);
void station_open_yard_list(station_context *c);
void station_show_upgrades(station_context *c);
void station_open_shipyard(station_context *c);
void station_open_ship_offer(station_context *c);
void station_show_repairs(station_context *c);
void station_show_service_list(station_context *c);
void station_open_bulletin(station_context *c);
void station_open_missions(station_context *c);
void station_show_missions(station_context *c);
void station_show_responses(station_context *c);
void station_open_trade(station_context *c);
void station_open_list(station_context *c, uint16_t kind);
void station_show_trade(station_context *c);
void station_process_deferred(station_context *c);
void station_request_launch(station_context *c);
void station_reply(station_context *c);
void station_end_reply(station_context *c);
void station_report_dealing(station_context *c);
void station_clear_board_entry(station_context *c);
void station_end_mission_offer(station_context *c);
void station_remove_request(station_context *c);
int station_find_mission(station_context *c);
void station_generate_system(station_context *c, enum fe2_station_work work);
void station_buy_drive(station_context *c, uint16_t text);
void station_preview_frame(station_context *c);
void station_prepare_preview(station_context *c, uint16_t model);

/* Fees (events.c). */
uint32_t station_landing_charge(int16_t seed);
uint32_t station_berthing_charge(int16_t seed);

#endif
