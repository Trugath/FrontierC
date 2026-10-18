/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_STATION_TABLES_H
#define FE2_STATION_TABLES_H

#include <stdint.h>

/* Handler names accepted by the editable reply routing table. */
enum {
    STATION_REPLY_SERVICE, STATION_REPLY_FITTING, STATION_REPLY_YARD,
    STATION_REPLY_TRADE, STATION_REPLY_CHARGE, STATION_REPLY_TALK,
    STATION_REPLY_MISSION, STATION_REPLY_BOARD
};
extern const uint8_t fe2_station_reply_routes[115];
typedef struct {
    uint32_t system_key;
    uint8_t mission_flag_bit;
} fe2_station_mission_destination;
extern const fe2_station_mission_destination fe2_station_mission_destinations[];
extern const unsigned fe2_station_mission_destination_count;

/* Typed native resources generated from data/interactions. */
extern const uint8_t fe2_station_colours[7];
typedef struct {
    uint16_t row_limit, row_spacing, text_x, page_size, continue_text;
} fe2_station_display;
extern const fe2_station_display fe2_station_displays[9];
extern const uint16_t fe2_station_pagination_text_x;
extern const uint16_t fe2_station_widgets_flight[26];
extern const uint16_t fe2_station_widgets_station[31];
extern const uint16_t fe2_station_widgets_list[156];
extern const uint16_t fe2_station_widgets_board[86];

enum { STATION_ITEMS = 45, STATION_REPAIR_COUNT = 18 };
typedef struct {
    uint16_t price, refund, mass;
    uint8_t handler, code, tech_level;
} fe2_station_item;
extern const fe2_station_item fe2_station_items[STATION_ITEMS];
extern const uint8_t fe2_station_template[286];

typedef struct {
    uint16_t trade_in_value, name_text, mass;
} fe2_station_drive;
extern const fe2_station_drive fe2_station_drives[15];
typedef struct {
    uint16_t name_text;
    int16_t cost;
} fe2_station_repair;
extern const fe2_station_repair fe2_station_repairs[STATION_REPAIR_COUNT];

#endif
