/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_STATION_STATION_H
#define FE2_STATION_STATION_H

/*
 * Station interactions (station): the docked-station screens and what they
 * do. Landing and berthing fees, the message line and its queue, the
 * interaction list (45 selectable rows with their text and values), the
 * trade, equipment fitting, shipyard, repair, bulletin board, mercenary
 * mission and conversation actions, deferred charges (fines) and the ship
 * preview of the shipyard.
 *
 * Records use opaque reference tokens (`fe2_ref`), resolved through services.
 * Persistent `fe2_call_args` hold screen parameters between calls. Operations
 * outside this module are provided by `fe2_station_services`; reference tokens
 * are identifiers, not native C addresses.
 */
#include <stdint.h>

#include "game/call_args.h"
#include "game/object.h"
#include "game/text_table.h"

typedef uint32_t fe2_ref;

enum {
    FE2_STATION_ROWS = 45,            /* interaction list rows (0x46cf, 24 bytes each) */
    FE2_STATION_QUEUE = 4,            /* queued messages (0x4b77, 0x1c bytes each) */
    FE2_STATION_MISSIONS = 6,         /* mission records (0x4fe3, 0x2a bytes each) */
    FE2_STATION_CONTRACTS = 59,       /* accepted contracts (0x4c1d, 16 bytes each) */
    FE2_STATION_PASSENGERS = 16,      /* passenger records (0x50e3, 6 bytes each) */
    FE2_STATION_REQUESTS = 25,        /* timed requests (0x43a7, 24 bytes each, then a zero dword) */
    FE2_STATION_COMMODITIES = 31,
    FE2_STATION_NAME = 20,
    FE2_STATION_PARKED = 11,          /* saved parameter words */

    /* Event selectors (0x76). */
    FE2_STATION_EVENT_LANDING = 5,
    FE2_STATION_EVENT_BERTHING = 0x0f,
    FE2_STATION_EVENT_REFRESH = 0x19,

    /* Flight modes  the module tests. */
    FE2_STATION_MODE_LAUNCH = 0x08,
    FE2_STATION_MODE_DOCKING = 0x34,
    FE2_STATION_MODE_48 = 0x48,
    FE2_STATION_MODE_4C = 0x4c,
    FE2_STATION_MODE_BERTH_A = 0x54,
    FE2_STATION_MODE_HYPERSPACE = 0x58,
    FE2_STATION_MODE_BERTH_B = 0x60,

    /* Actions : the screen the list belongs to. */
    FE2_STATION_ACTION_CONVERSATION = 2,
    FE2_STATION_ACTION_BULLETIN = 4,
    FE2_STATION_ACTION_TRADE = 6,
    FE2_STATION_ACTION_FITTING = 8,
    FE2_STATION_ACTION_SHIPYARD = 0x0a,
    FE2_STATION_ACTION_SHIP = 0x0c,
    FE2_STATION_ACTION_MISSIONS = 0x0e,
    FE2_STATION_ACTION_REPAIRS = 0x10
};

/* A row of the interaction list: the text and its format values. */
typedef struct fe2_station_row {
    uint16_t text;              /* ; zero ends the list */
    uint16_t text_high;         /* (0x7d0's low word when appended) */
    uint32_t value[4];          /* parameters 1..4 */
    uint32_t subject;           /* the subject  */
} fe2_station_row;

/* A message: shown now (0x4b0f..) or queued (0x4b77 + 0x1c n). */
typedef struct fe2_station_message {
    uint16_t text;
    uint16_t pad;               /* kept by a queued message's word store */
    uint32_t value[3];          /* parameters 1..3 */
    uint32_t style;             /* parameter 7 : negative, no sound */
    uint32_t subject;           /* parameter 8 */
    uint32_t kind;              /* parameter 10 */
} fe2_station_message;

/* A mission record (0x4fe3, 0x2a bytes). */
typedef struct fe2_station_mission {
    uint8_t kind;
    uint8_t seen;
    uint32_t system;
    uint32_t seed;
    uint32_t due;
    uint16_t model;             /* ship name text selector */
    uint16_t reward;            /* minimum crew */
    char name[FE2_STATION_NAME];
    uint32_t origin;            /* the current system when accepted */
} fe2_station_mission;

/* An accepted contract (0x4c1d, 16 bytes): delivery cargo and the like. */
typedef struct fe2_station_contract {
    uint16_t text;
    uint16_t detail;            /* : high byte bit 5 no cabins; low byte / 4 cabins - 1 */
    uint32_t value[2];          /* , +8 */
    uint32_t seed;
} fe2_station_contract;

/* A passenger (0x50e3, 6 bytes). */
typedef struct fe2_station_passenger {
    uint32_t seed;
    uint16_t fare;
} fe2_station_passenger;

/* A timed request (0x43a7, market's 24-byte records). */
typedef struct fe2_station_request {
    uint32_t head;              /* the low word is the text; zero ends the list */
    uint32_t value[4];
    uint32_t seed;
} fe2_station_request;

/* The ship preview of the shipyard. */
typedef struct fe2_station_preview {
    uint16_t angle[2];
    uint32_t position[3];
    uint32_t seed;
    char name[8];               /* generated registration */
    uint8_t spare[12];
    uint16_t scale;
    uint16_t model;
    uint16_t crew;
} fe2_station_preview;

/* References of the module's resident buffers (the original's addresses). */
typedef struct fe2_station_refs {
    fe2_ref rows;               /* the interaction list */
    fe2_ref preview_name;
} fe2_station_refs;

/* Stack work records the original builds (their references come from the
 * `work_record` service). */
enum fe2_station_work {
    FE2_STATION_WORK_MISSION,       /* the generated mission (0xf44 bytes) */
    FE2_STATION_WORK_OFFER,         /* the same, built by a mission offer */
    FE2_STATION_WORK_PREVIEW,       /* the preview ship record */
    FE2_STATION_WORK_REGISTRATION,  /* generated registration */
    FE2_STATION_WORK_SHIP_NAME      /* generated ship name */
};

typedef struct fe2_station_state {
    fe2_station_refs refs;

    /* Screen and list. */
    uint8_t screen_active;
    uint8_t ui_flags;                  /* bit 0 selection, bit 1 message */
    int8_t view_mode;                  /* negative: the interaction view */
    uint8_t input_lock;                /* nonzero: the answer key 0xf4 */
    uint16_t key_word;
    uint8_t key_latch;
    uint8_t colours[4];
    uint8_t message_colours[3];
    uint8_t list_colours[3];
    uint8_t line_colour;
    fe2_station_row rows[FE2_STATION_ROWS];
    uint32_t rows_end;                 /* the end marker after a full list */
    uint32_t message_timer;
    fe2_station_message message;       /* (0x4b11 is not written) */
    uint32_t selection_timer;
    uint16_t first_row;
    uint16_t row_count;
    uint16_t heading;
    uint16_t screen_title;             /* column titles from the action screen; 0 if none */
    uint8_t message_flags;             /* bit 0 */
    uint8_t ship_list;                 /* nonzero: the ship list of a dealer */
    uint16_t key_count;
    uint16_t action;
    uint16_t list_kind;
    uint32_t identity;
    uint16_t portrait;
    uint8_t *market;                   /* market's market record of the station */
    uint32_t saved[7];                 /* a bulletin entry's values */
    uint32_t deferred;                 /* fines owed */
    uint16_t portrait_timer;
    uint8_t refit;                     /* nonzero: the ship changed */
    uint8_t board_kind;
    uint16_t tech_level;
    uint8_t chosen;                    /* the handle a search chose */
    uint16_t offer_model;
    uint16_t queue_cursor;             /* byte offset, 0x1c per message */
    fe2_station_message queue[FE2_STATION_QUEUE];
    /* read as a fifth queued message when the queue moves. */
    uint16_t event_word;               /* the bulletin page */
    uint16_t queue_tail_padding;
    uint32_t queue_tail_value;
    fe2_station_preview preview;       /*. (with 0x4b67, 0x4b6f) */
    uint16_t contract_count;
    fe2_station_contract contracts[FE2_STATION_CONTRACTS];
    uint16_t drive_index;
    uint16_t mission_count;
    fe2_station_mission missions[FE2_STATION_MISSIONS];
    uint16_t passenger_limit;
    uint16_t passenger_count;
    fe2_station_passenger passengers[FE2_STATION_PASSENGERS];
    fe2_station_request requests[FE2_STATION_REQUESTS];
    uint32_t requests_end;             /* the zero after a full list */
    uint32_t parked[FE2_STATION_PARKED];  /* saved screen parameters */
    fe2_ref parked_ref;                /* (also a list: parameter 12) */
    uint16_t selected_text;            /* the deferred-charge notice */
    uint8_t notice_wait;               /* the deferred-charge notice is on screen; the host pumps keys */

    /* Game. */
    uint8_t flight_state;
    uint8_t game_flags;
    uint8_t expected_letter;           /* ownership question: the answer */
    uint32_t check_value;
    uint32_t check_elapsed;
    int8_t search_counter;
    uint8_t player_handle;
    uint8_t reference_handle;
    uint8_t motion_flags;
    fe2_ref ship;
    fe2_ref station;
    uint8_t countdown;
    uint8_t target;
    uint8_t mining_mode_active;
    uint8_t release_suppressed;
    uint32_t shared_countdown;
    uint32_t current_system;
    uint8_t fined_handle;
    uint32_t balance;
    uint16_t capacity;                 /* free mass of the hold */
    uint16_t cabins;
    uint16_t cargo_used;
    uint16_t cabins_used;
    uint16_t cargo[FE2_STATION_COMMODITIES];
    uint32_t jump_limit;
    uint32_t visited[3];
    int16_t rating;
    uint16_t service_timer[2];
    uint32_t service_day[2];
    uint16_t saved_limits[6];
    uint8_t misjump_flag;
    uint8_t trade_codes[FE2_STATION_COMMODITIES];
    int16_t system_seed;
    uint16_t allegiance;
    uint32_t visited_days[3];
    uint32_t time_of_day;
    uint32_t day;
    uint8_t redraw_flags;
    uint16_t basis_angle[2];
    uint16_t light[3];
    uint16_t shades[8];
    uint32_t frame_step;
    uint32_t real_step;
    uint32_t time_step;
    fe2_ref objects;
    uint32_t mission_flags;
    uint16_t police_count;
    int16_t question[3];               /* offences's manual word, line and page */
} fe2_station_state;

/* The flight parameter words of a model (0x50: the descriptor + its word
 * at +0x1c). */
typedef struct fe2_station_model {
    uint16_t word[11];
    fe2_ref ref;                /* opaque parameter reference */
} fe2_station_model;

enum fe2_station_model_word {
    FE2_MODEL_MAIN_THRUST,      /* +0 */
    FE2_MODEL_RETRO_THRUST,            /* +2 */
    FE2_MODEL_GUN_MOUNT_FLAGS,            /* +4: bit 8 accepts a military drive; low nibble a class */
    FE2_MODEL_HULL_MASS,             /* +6 */
    FE2_MODEL_INTERNAL_CAPACITY,         /* +8 */
    FE2_MODEL_PRICE,
    FE2_MODEL_PREVIEW_SIZE,
    FE2_MODEL_NAME_TEXT,             /* ship name text selector */
    FE2_MODEL_REQUIRED_CREW,       /* minimum crew */
    FE2_MODEL_MISSILE_PYLONS,            /* missile pylons */
    FE2_MODEL_DRIVE_OPTIONS
};

/* market's answer to a market request (overlay 0x38). */
typedef struct fe2_station_market_reply {
    uint16_t result;            /* AX */
    uint8_t *market;            /* DI: the market record (request 0) */
    fe2_ref ref;                /* opaque record reference */
    uint32_t value;
} fe2_station_market_reply;

/* A generated name (names, overlay 0x48) and where the original keeps it. */
typedef struct fe2_station_name {
    char text[FE2_STATION_NAME];
    fe2_ref ref;
} fe2_station_name;

/* A bounded sample : the seed is advanced, the value returned. */
typedef struct fe2_station_sample {
    uint16_t result;            /* AX */
    uint32_t seed_a;            /* CX:BX */
    uint32_t seed_b;            /* SI:DI */
} fe2_station_sample;

/* A visit of the current list  calls back with each record. */
typedef void (*fe2_station_visitor)(void *context, fe2_ref object);

typedef struct fe2_station_services {
    void *opaque;

    /* Records. */
    fe2_object *(*object)(void *opaque, fe2_ref ref);         /* the record a reference names */
    fe2_ref (*lookup)(void *opaque, uint8_t handle, const fe2_call_args *args); /*, parameter 12 */
    const fe2_station_model *(*model)(void *opaque, uint16_t model);
    /* Parameter 11 is the callback. */
    void (*visit_objects)(void *opaque, fe2_ref list, fe2_station_visitor visit, void *context,
                          const fe2_call_args *args);
    void (*prepare_object)(void *opaque, const fe2_call_args *args);       /* 9 record, 8 template */
    void (*clear_objects)(void *opaque, const fe2_call_args *args);        /*, parameter 12 */
    void (*attach_object)(void *opaque, const fe2_call_args *args);        /*, parameter 12 */
    void (*set_slot_flags)(void *opaque, fe2_ref object, uint8_t flags);
    void (*move_object)(void *opaque, const fe2_call_args *args);          /* 0..2, 9 */
    void (*orient_object)(void *opaque, fe2_ref object);
    void (*render_object)(void *opaque, fe2_ref object, const fe2_call_args *args);
    fe2_ref (*work_record)(void *opaque, enum fe2_station_work work);

    /* Screen. */
    void (*select_controls)(void *opaque, uint16_t mode);
    void (*set_widgets)(void *opaque, const uint16_t *widgets);
    void (*show_panel)(void *opaque);
    void (*clear_regions)(void *opaque);
    void (*restore_view)(void *opaque);
    void (*open_view)(void *opaque);
    void (*clear_drawing)(void *opaque);
    void (*set_controls)(void *opaque, uint16_t layout_ax, uint16_t layout_bx, uint8_t buttons);
    void (*set_view)(void *opaque, uint16_t layout, uint8_t buttons);
    void (*clear_panel)(void *opaque);
    void (*clear_picture)(void *opaque);
    void (*draw_frame)(void *opaque, uint16_t style, uint16_t x, uint8_t y, uint8_t height,
                       uint16_t mode);
    void (*draw_line)(void *opaque, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void (*draw_icon)(void *opaque, uint16_t icon, uint16_t x, uint16_t y);
    void (*draw_sprite)(void *opaque, uint16_t sprite, uint16_t x, uint16_t y,
                        const fe2_call_args *args);
    void (*present)(void *opaque);
    /* Text `text` in `colour` at (x, y); parameter 7 the style. */
    void (*draw_text_shadowed)(void *opaque, uint16_t text, uint8_t colour, uint16_t x, uint16_t y,
                               const fe2_call_args *args);
    void (*draw_text_both)(void *opaque, uint16_t text, uint8_t colour, uint16_t x, uint16_t y,
                           const fe2_call_args *args);
    void (*draw_text)(void *opaque, uint16_t text, uint8_t colour, uint16_t x, uint16_t y,
                      const fe2_call_args *args);

    /* Input, time and events. */
    int (*poll_key)(void *opaque, uint16_t *key);                          /* nonzero with a key (AX) */
    void (*forward_key)(void *opaque, uint16_t key);
    void (*unregister_key)(void *opaque, uint16_t key);
    void (*post_event)(void *opaque, uint16_t event);
    void (*stop_time)(void *opaque);
    uint16_t (*random_below)(void *opaque, uint16_t bound);
    fe2_station_sample (*bounded_sample)(void *opaque, uint16_t bound, uint32_t seed_a,
                                         uint32_t seed_b);
    void (*play_effect)(void *opaque, uint16_t effect);
    void (*restart_session)(void *opaque, uint16_t mode);                  /* (does not return) */

    /* Other overlays (their 0x8a entry unless noted). */
    /* systems: generate (parameter 2 = 0) or instantiate (1) the system of the
     * seeds into the record at parameter 8; the seeds are its registers
     * (DX:AX, CX:BX) in and out. */
    void (*systems_system)(void *opaque, uint32_t *seed_a, uint32_t *seed_b, const fe2_call_args *args); /* 0x0 */
    uint16_t (*galactic_map_system)(void *opaque, uint16_t request, const fe2_call_args *args);  /* 0x10 */
    void (*flight_activate_view)(void *opaque);                                             /* 0x18: 0x72 */
    void (*flight_restore_capacity)(void *opaque);                                            /* 0x18: 0x8a */
    void (*panels_panel)(void *opaque, uint16_t request, uint16_t view, const fe2_call_args *args); /* 0x20 */
    fe2_station_market_reply (*market_market)(void *opaque, uint16_t request, uint8_t handle,
                                           const fe2_call_args *args);    /* 0x38 */
    void (*names_name)(void *opaque, uint16_t type, uint32_t seed, enum fe2_station_work buffer,
                     fe2_station_name *out);                                /* 0x48 */
    void (*commander_restore_time)(void *opaque);                                              /* 0x50 */
    void (*composite_portrait)(void *opaque, uint32_t identity, const fe2_call_args *args);   /* 0x68 */
    void (*status_contract)(void *opaque, const fe2_call_args *args);                       /* 0x70 */
    void (*offences_police)(void *opaque, uint16_t request, const fe2_call_args *args);       /* 0x78 */
    void (*hyperspace_ship)(void *opaque, uint16_t request, uint16_t model, const fe2_call_args *args); /* 0x80 */
    uint16_t (*mining_service)(void *opaque, uint16_t request, const fe2_call_args *args);  /* 0x88 */
    void (*interaction_text_text)(void *opaque, uint16_t selector, fe2_text_out *out);                /* 0x90 +0x28 */
} fe2_station_services;

/* 0x6e. */
void fe2_station_initialize(fe2_station_state *state, fe2_call_args *args);
/* 0x72: the station screen (docked) or the flight message panel. */
void fe2_station_draw(fe2_station_state *state, const fe2_station_services *game, fe2_call_args *args);
/* Redraw the open station screen after the 3D view. */
void fe2_station_refresh(fe2_station_state *state, const fe2_station_services *game, fe2_call_args *args);
/* 0x76: landing fee (5), berthing fee (0xf), refresh (0x19). */
void fe2_station_event(fe2_station_state *state, const fe2_station_services *game, fe2_call_args *args,
                       uint16_t selector);
/* 0x7a: a key on the station screen. */
void fe2_station_input(fe2_station_state *state, const fe2_station_services *game, fe2_call_args *args);
/* 0x7e: the selection menu and message line in flight. */
void fe2_station_select(fe2_station_state *state, const fe2_station_services *game, fe2_call_args *args);
/* 0x8a: show or queue message `text`. */
void fe2_station_show_message(fe2_station_state *state, const fe2_station_services *game,
                              fe2_call_args *args, uint16_t text);
/* 0x8e: leave the screen (0), redisplay the message (positive) or mark it
 * (negative); returns AX for the positive request (1 shown, 0 none). */
uint16_t fe2_station_resume(fe2_station_state *state, const fe2_station_services *game,
                            fe2_call_args *args, int16_t request);
/* 0x92: interaction text `selector` (interaction). */
void fe2_station_format_text(const fe2_station_services *game, uint16_t selector, fe2_text_out *out);

/* The visits' callbacks (parameter 11); `context` is the screen's pass. */
void fe2_station_offer_visitor(void *context, fe2_ref object);
void fe2_station_search_visitor(void *context, fe2_ref object);
void fe2_station_target_visitor(void *context, fe2_ref object);

/* The same callbacks on their own. */
void fe2_station_offer_owner(fe2_station_state *state, const fe2_station_services *game,
                             fe2_call_args *args, fe2_ref object);
void fe2_station_count_police(fe2_station_state *state, fe2_ref object, fe2_object *record);
void fe2_station_choose_target(fe2_station_state *state, fe2_call_args *args, fe2_ref object,
                               fe2_object *record);

#endif
