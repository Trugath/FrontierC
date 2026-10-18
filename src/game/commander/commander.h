/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_STATUS_COMMANDER_H
#define FE2_STATUS_COMMANDER_H

/*
 * Game state read and written by the commander status module (status).
 *
 * This header describes native state. The commander codec owns serialized
 * record layouts; fields here do not imply a byte offset or packed C layout.
 */
#include <stdint.h>
#include "game/flight_mode.h"

#include "game/object.h"

enum {
    FE2_MAX_CONTRACTS = 60,
    FE2_MAX_MILITARY_CONTRACTS = 6,
    FE2_MAX_CREW = 20,
    FE2_MAX_MINING_UNITS = 8,
    FE2_CARGO_TYPES = 31,
    FE2_PERMIT_WORDS = 4,            /* bitset */
    FE2_AUTHORITIES = 3,             /* Interpol, Imperial, Federal */
    FE2_NAME_BYTES = 20,
    FE2_COMMANDER_NAME_BYTES = 16
};

/* Passenger, parcel or delivery contract state. */
typedef struct fe2_contract {
    uint16_t fee;            /* paid as 10 * fee on delivery */
    uint8_t group;           /* bits 2..7 group size - 1; 0x1c parcel; bit 6 imperial */
    uint8_t kind;            /* bits 0..4 kind (0x12 = goods delivery) */
    uint32_t destination;    /* system key */
    uint32_t deadline;       /* game day */
    uint32_t client;         /* client name seed; zero for military passengers */
} fe2_contract;

enum {
    FE2_CONTRACT_PARCEL = 0x1c,         /* group byte */
    FE2_CONTRACT_GROUP_IMPERIAL = 0x40, /* group byte */
    FE2_CONTRACT_GOODS = 0x12,          /* kind & 0x1f */
    FE2_CONTRACT_KIND_MASK = 0x1f,
    FE2_CONTRACT_PREPAID = 0x20,        /* kind byte */
    FE2_CONTRACT_IMPERIAL = 0x40,       /* kind byte */
    FE2_CONTRACT_EXPIRED = 0x80         /* kind byte */
};

/* Military mission state. */
typedef struct fe2_military_contract {
    uint8_t type;            /* low nibble mission 0..6, high nibble target class */
    int8_t outcome;          /* 0 pending, positive done, negative target destroyed */
    uint32_t target_system;
    uint32_t target_seed;    /* target name seed; preview angles for mission 3..6 */
    uint32_t deadline;       /* game day */
    uint8_t target_model;    /* model index; becomes object handle when matched */
    uint8_t quality;         /* 0xff marks a placed target; film quality */
    uint16_t reward;         /* paid as 10 * reward */
    char target_name[FE2_NAME_BYTES];
    uint32_t home_system;
} fe2_military_contract;

/* Crew member state. */
typedef struct fe2_crew_member {
    uint32_t name_seed;
    int16_t salary;
} fe2_crew_member;

/* Mining machine state needed by the commander display. */
typedef struct fe2_mining_unit {
    uint32_t system;         /* zero while not deployed */
    uint8_t target;          /* navigation target index */
    uint8_t prospect;        /* prospect text index */
    char name[FE2_NAME_BYTES];
} fe2_mining_unit;

/* Commodities with a role of their own. */
enum {
    FE2_CARGO_HYDROGEN_FUEL = 19,
    FE2_CARGO_MILITARY_FUEL = 20,
    FE2_CARGO_RADIOACTIVES = 29
};

enum {
    FE2_PAGE_EQUIPMENT = 0, FE2_PAGE_PROFILE = 2, FE2_PAGE_CREW = 4,
    FE2_PAGE_CARGO = 6, FE2_PAGE_PASSENGERS = 8, FE2_PAGE_CONTRACTS = 0x0a,
    FE2_PAGE_MINING = 0x0c
};


typedef struct fe2_commander_status {
    /* Status screen (status-owned). */
    uint8_t screen_active;
    uint8_t page;               /* requested page */
    uint8_t next_page;          /* page the next draw shows */
    uint8_t arrival_countdown;  /* scan when it drops below zero */
    uint8_t matched_handle;     /* found by the target-name callback */
    uint16_t preview_angle[2];  /* rotating ship preview */
    uint32_t preview_distance[3]; /* camera offset */

    /* Contracts, crew and mining (status-owned). */
    uint16_t contract_count;
    fe2_contract contracts[FE2_MAX_CONTRACTS + 1];
    uint16_t military_count;
    fe2_military_contract military[FE2_MAX_MILITARY_CONTRACTS];
    uint16_t crew_needed;
    uint16_t crew_count;
    fe2_crew_member crew[FE2_MAX_CREW + 1];
    uint32_t unpaid_wages;
    uint32_t permits[FE2_PERMIT_WORDS];
    uint16_t offences[FE2_AUTHORITIES];   /* bit n: offence n (offences) */
    uint32_t criminal_record[FE2_AUTHORITIES]; /* Interpol, imperial, federal */
    uint16_t mining_count;
    fe2_mining_unit mining[FE2_MAX_MINING_UNITS];

    /* Commander and ship. */
    uint32_t cash;
    uint16_t cargo_capacity;
    uint16_t cabin_capacity;
    uint16_t cargo_used;
    uint16_t cabins_used;
    uint16_t cargo[FE2_CARGO_TYPES]; /* tonnes by commodity */
    uint32_t elite_points;
    uint16_t federal_points;
    uint16_t imperial_points;
    char commander_name[FE2_COMMANDER_NAME_BYTES];
    int16_t reliability;
    uint8_t registration[12];
    uint16_t legal_authority;   /* selected authority */
    fe2_object *ship;

    /* Game clock, location and flags. */
    uint32_t time_of_day;
    uint32_t day;
    uint32_t frame_time;
    uint32_t current_system;
    uint8_t local_authority;
    uint8_t flight_state;
    uint8_t navigation_target;
    uint8_t event_flags;        /* bit 0: arrival scan pending */
    uint8_t redraw_flags;       /* bit 0: panel redraw */
    uint8_t ship_flags;
    uint8_t text_colour;
    uint8_t dim_colour;
    uint8_t heading_colour;
    uint16_t view_angle[2];     /* shared preview/view angles */
    uint16_t palette_top[3];
    uint16_t palette_ramp[8];
    fe2_object_list *objects;
} fe2_commander_status;

#endif
