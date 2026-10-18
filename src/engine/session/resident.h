/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
#ifndef FE2_SESSION_RESIDENT_H
#define FE2_SESSION_RESIDENT_H
#include <stdint.h>
#include "game/object.h"

/* The resident globals two or more overlays share that the session and its
 * layers do not own, by data segment address. */
typedef struct fe2_resident {
    uint8_t scanner_view_mode;
    uint8_t police_flags;
    uint8_t launch_phase;
    fe2_object_list * map_objects;
    uint8_t message_colour_0;
    uint8_t message_colour_1;
    uint8_t message_colour_2;
    uint8_t list_colour_0;
    uint8_t list_colour_1;
    uint8_t list_colour_2;
    uint32_t classification_0;
    uint32_t classification_1;
    uint32_t classification_2;
    uint32_t classification_3;
    uint8_t expected_letter;
    uint32_t event_base;
    uint8_t reference_handle;
    fe2_object * reference_object;
    uint32_t jump_clock;
    uint8_t flight_mode;
    uint8_t ship_control_flags;
    uint8_t countdown;
    uint8_t navigation_target;
    uint8_t selection_found;
    uint8_t mining_view_flag;
    uint8_t player_contact_class;
    uint8_t release_suppressed;
    uint32_t jump_countdown;
    uint32_t current_system;
    uint8_t fined_handle;
    uint16_t cabins;
    uint16_t cabins_used;
    uint16_t jump_range;
    uint16_t offence_score_0;
    uint16_t offence_score_1;
    uint16_t offence_score_2;
    uint32_t previous_systems_0;
    uint32_t previous_systems_1;
    uint32_t previous_systems_2;
    uint32_t jump_value;
    uint32_t throttle;
    uint32_t heat_timer;
    uint16_t heat_gauge;
    uint16_t rating;
    uint32_t service_day_0;
    uint32_t service_day_1;
    uint8_t body_reference_index;
    uint8_t jump_selection_status;
    uint32_t selected_jump_distance;
    uint32_t jump_selection_reference;
    /* System-map scale shared with navigation: low word mantissa, high word exponent. */
    uint32_t map_saved_scale;
    uint8_t map_default_selection;
    uint8_t trade_codes_0;
    uint8_t trade_codes_1;
    uint8_t trade_codes_2;
    uint8_t trade_codes_3;
    uint8_t trade_codes_4;
    uint8_t trade_codes_5;
    uint8_t trade_codes_6;
    uint8_t trade_codes_7;
    uint8_t trade_codes_8;
    uint8_t trade_codes_9;
    uint8_t trade_codes_10;
    uint8_t trade_codes_11;
    uint8_t trade_codes_12;
    uint8_t trade_codes_13;
    uint8_t trade_codes_14;
    uint8_t trade_codes_15;
    uint8_t trade_codes_16;
    uint8_t trade_codes_17;
    uint8_t trade_codes_18;
    uint8_t trade_codes_19;
    uint8_t trade_codes_20;
    uint8_t trade_codes_21;
    uint8_t trade_codes_22;
    uint8_t trade_codes_23;
    uint8_t trade_codes_24;
    uint8_t trade_codes_25;
    uint8_t trade_codes_26;
    uint8_t trade_codes_27;
    uint8_t trade_codes_28;
    uint8_t trade_codes_29;
    uint8_t trade_codes_30;
    uint16_t system_seed;
    uint16_t legal_allegiance;
    uint16_t market_count;
    uint16_t station_count_copy;
    uint8_t preference_byte_0;
    uint8_t preference_byte_1;
    uint8_t preference_byte_2;
    uint8_t preference_byte_3;
    uint8_t preference_byte_4;
    uint8_t preference_byte_5;
    uint8_t preference_byte_6;
    uint8_t preference_byte_7;
    uint8_t preference_byte_8;
    uint8_t preference_byte_9;
    uint8_t preference_byte_10;
    uint8_t preference_byte_11;
    uint8_t preference_byte_12;
    uint8_t preference_byte_13;
    uint8_t music_preference;
    uint8_t preference_byte_15;
    uint8_t music_enabled;
    uint8_t alert_music_enabled;
    uint32_t message_timer;
    uint32_t local_offence_fine;
    uint16_t contract_count;
    uint16_t mission_count;
    uint16_t passenger_limit;
    uint16_t passenger_count;
    uint32_t mission_flags;
    fe2_object * police_station;
    uint32_t criminal_record_0;
    uint32_t criminal_record_1;
    uint32_t criminal_record_2;
    uint8_t emitted_count;
    uint8_t emitted_limit;
    uint16_t mining_machine_count;
    uint16_t free_mining_machine_count;
    uint32_t jump_time;
    uint16_t jump_distance_word;
    uint16_t game_time_exponent;
    uint16_t line_colour;
    uint8_t display_colour_0;
    uint8_t display_colour_1;
    uint8_t display_colour_2;
    uint8_t display_colour_3;
    uint8_t display_colour_4;
    uint8_t display_colour_5;
    uint8_t display_colour_6;
    uint8_t display_colour_7;
    uint8_t display_colour_8;
    uint8_t display_colour_9;
    uint8_t display_colour_10;
} fe2_resident;

/* The original data segment image values. */
void fe2_resident_init(fe2_resident *resident);
#endif
