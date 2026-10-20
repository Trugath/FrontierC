/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_STATUS_GAME_SERVICES_H
#define FE2_STATUS_GAME_SERVICES_H

/*
 * Services the rest of the game provides to the commander status module.
 *
 * Text, messages and object services take numbered parameters
 * (game/call_args.h).
 */
#include "commander.h"
#include "game/call_args.h"
#include "game/text_table.h"

enum { FE2_TEXT_BYTES = 32 };

/* Ship model data (resident 0x50 descriptor and its ship statistics). */
typedef struct fe2_ship_type {
    uint16_t radius[2];      /* descriptor +0xa, +0xc */
    uint16_t scale;          /* descriptor +0x16 */
    uint16_t hull_mass;      /* statistics +6 */
    uint16_t size;           /* statistics +0xc */
    uint16_t name_text;      /* statistics +0xe */
} fe2_ship_type;

typedef struct fe2_rect {
    uint16_t left, right;
    uint8_t top, bottom;
} fe2_rect;

/* Status page widgets passed to the panel layout service. */
typedef struct fe2_widget_table fe2_widget_table;
/* status text directory passed to the resident text formatter. */

typedef void (*fe2_object_visitor)(void *context, const fe2_object *object);

typedef struct fe2_game_services {
    void *opaque;

    /* Panels and drawing. */
    void (*clear_panel)(void *opaque);
    void (*draw_text)(void *opaque, uint16_t text, uint8_t colour, uint16_t x,
                      uint16_t y, const fe2_call_args *args);
    void (*draw_icon)(void *opaque, uint16_t icon, uint16_t x, uint16_t y);
    void (*draw_sprite)(void *opaque, uint16_t sprite, uint16_t x, uint16_t y);
    void (*draw_frame)(void *opaque, const fe2_rect *frame, uint16_t style);
    void (*set_controls)(void *opaque, uint16_t layout, uint8_t buttons);
    void (*show_panel)(void *opaque);
    void (*present)(void *opaque);
    void (*set_widgets)(void *opaque, const fe2_widget_table *widgets);
    void (*set_panel_mode)(void *opaque, uint16_t mode);
    void (*open_panel)(void *opaque, uint16_t panel, const fe2_call_args *args); /* panels */

    /* Ship preview rendering through a temporary camera object. */
    void (*prepare_camera)(void *opaque, fe2_object *camera, const fe2_call_args *args);
    void (*orient_camera)(void *opaque, fe2_object *camera);
    void (*render_scene)(void *opaque, fe2_object *camera);

    /* Text. Generated strings are NUL-terminated within FE2_TEXT_BYTES. */
    void (*format_text)(void *opaque, uint16_t text, const fe2_call_args *args,
                        char out[FE2_TEXT_BYTES]);
    void (*generate_name)(void *opaque, uint16_t mode, uint32_t seed,
                          char out[FE2_TEXT_BYTES]);                       /* names */
    void (*show_message)(void *opaque, uint16_t text, const fe2_call_args *args); /* station */
    void (*format_table_text)(void *opaque, uint16_t text, const fe2_text_table *table);

    /* Game services. */
    uint32_t (*random)(void *opaque);
    uint16_t (*random_below)(void *opaque, uint16_t bound);
    const fe2_ship_type *(*ship_type)(void *opaque, uint16_t model);
    int (*poll_key)(void *opaque, uint8_t *key);
    void (*forward_key)(void *opaque);
    void (*post_event)(void *opaque, uint16_t event, const fe2_call_args *args);
    void (*select_music)(void *opaque, uint16_t track);
    void (*play_effect)(void *opaque, uint16_t effect);                     /* sound effect */
    void (*apply_impulse)(void *opaque, const fe2_call_args *args);
    /* In/out three-component dword vector (resident 0x324). */
    void (*square_vector)(void *opaque, uint32_t vector[3]);
    fe2_object *(*spawn_object)(void *opaque, const fe2_call_args *args);
    fe2_object *(*lookup_object)(void *opaque, uint8_t handle);
    void (*orient_object)(void *opaque, fe2_object *object);
    void (*visit_objects)(void *opaque, fe2_object_list *objects,
                          fe2_object_visitor visitor, void *context);
    /* market market service (BX) with request AX. Parameters 0..7 come back;
     * status uses service 9 for a child, with the option in parameter 7 and
     * the source in parameter 9, and reads the slot offset from 0..5. */
    void (*market_service)(void *opaque, uint16_t service, uint16_t request,
                           fe2_call_args *args);
    /* people creation with BX = `model` (the local authority for contract
     * company, the target model for escorts, 9 for child objects); the mode
     * is parameter 7. Returns the new object or null. The parameters come
     * back, with the new object in parameter 9. */
    fe2_object *(*create_object)(void *opaque, uint16_t model, fe2_call_args *args);
    /* drives drive summary (request 1) for the ship in parameter 8; its two
     * results (the original's 0x7c8 and 0x7d0) are the drive ranges shown. */
    void (*drive_summary)(void *opaque, uint16_t request, const fe2_call_args *args,
                          uint16_t *first, uint16_t *second);
} fe2_game_services;

#endif
