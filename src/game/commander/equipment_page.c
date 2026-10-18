/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* ship equipment status page. */
#include "status_internal.h"

enum {
    TITLE = 0x9c02, DRIVE_TEXT = 0x9c0f, INSTALLED_TEXT = 0x9c0a,
    FIRST_INSTALLED_NAME = 0x986f,
    COLUMN_TOP = 0x2f, COLUMN_BOTTOM = 0x77, FIRST_COLUMN_X = 5, SECOND_COLUMN_X = 0x60,
    EQUIPMENT_STEP = 9,
    DRIVE_X = 5, DRIVE_Y = 0x0f,
    INSTALLED_X = 5, INSTALLED_Y = 0x7d, INSTALLED_STEP = 10,
    HULL_PERCENT_SCALE = 250,
    SHIELD_GENERATOR_CAPACITY_SHIFT = 6,
    DRIVE_SUMMARY_REQUEST = 1
};

/* Equipment item i is bit (i + 16) mod 32 of the fitted and damaged words. */
static uint32_t by_item(uint32_t bits)
{
    return (bits >> 16) | (bits << 16);
}

/* Fitted or damaged items in up to two columns; damaged ones dimmed. The
 * original tests the damage bit through a copy shifted one place, so the
 * last item (bit 31) always shows dimmed. */
static void draw_equipment_list(fe2_commander_status *status, const fe2_game_services *game,
                                fe2_call_args *args)
{
    const fe2_object *ship = status->ship;
    uint32_t present = by_item(ship->as.craft.equipment | ship->as.craft.equipment_damaged);
    uint32_t damaged = by_item(ship->as.craft.equipment_damaged);
    uint16_t x = FIRST_COLUMN_X, y = COLUMN_TOP;
    unsigned item;

    for (item = 0; item < 32 && (present >> item) != 0; ++item) {
        int dimmed = item == 31 || ((damaged >> item) & 1u);
        if (((present >> item) & 1u) == 0) continue;
        if (y >= COLUMN_BOTTOM) {
            x = SECOND_COLUMN_X;
            y = COLUMN_TOP;
        }
        fe2_args_word(args, PARAM_STYLE, STYLE_PLAIN);
        game->draw_text(game->opaque, fe2_status_equipment_text[item],
                        dimmed ? status->dim_colour : status->text_colour, x, y, args);
        y = (uint16_t)(y + EQUIPMENT_STEP);
    }
}

/* Hull condition: the original's unchecked DIV faults on a zero or too-small
 * hull mass. */
static int hull_percent(const fe2_object *ship, uint16_t hull_mass, uint16_t *percent)
{
    uint32_t product = (uint32_t)HULL_PERCENT_SCALE * ship->hull;
    if (hull_mass == 0 || product / hull_mass > 0xffffu) return FE2_STATUS_DIVIDE_FAULT;
    *percent = (uint16_t)(product / hull_mass);
    return FE2_STATUS_OK;
}

static void draw_installed_items(const fe2_object *ship, const fe2_game_services *game,
                                 fe2_call_args *args)
{
    uint16_t y = INSTALLED_Y;
    int count = (int8_t)ship->as.craft.installed_count;
    int item = 0;

    if (ship->as.craft.installed_count == 0) return;
    do {
        uint8_t installed = ship->as.craft.installed[item];
        fe2_args_word(args, PARAM_2, fe2_status_installed_text[(installed >> 3) & 0x0f]);
        fe2_args_word(args, PARAM_1, (uint16_t)(FIRST_INSTALLED_NAME + item));
        fe2_status_draw_line(game, args, INSTALLED_TEXT, INSTALLED_X, y);
        y = (uint16_t)(y + INSTALLED_STEP);
    } while (++item < count && item < FE2_INSTALLED_ITEMS);
}

int fe2_status_draw_equipment_page(fe2_commander_status *status, const fe2_game_services *game)
{
    const fe2_object *ship = status->ship;
    fe2_call_args args;
    uint16_t percent, first, second;
    int result;

    fe2_args_clear(&args);
    game->clear_panel(game->opaque);
    fe2_args_word(&args, PARAM_1, game->ship_type(game->opaque, ship->model)->name_text);
    fe2_status_draw_heading(status, game, &args, TITLE);
    draw_equipment_list(status, game, &args);

    result = hull_percent(ship, game->ship_type(game->opaque, ship->model)->hull_mass, &percent);
    if (result != FE2_STATUS_OK) return result;
    fe2_args_dword(&args, PARAM_1, 1);
    fe2_args_object(&args, PARAM_SUBJECT, ship);
    game->drive_summary(game->opaque, DRIVE_SUMMARY_REQUEST, &args, &first, &second);
    fe2_args_dword(&args, PARAM_10, (uint16_t)(ship->as.craft.shield_capacity >> SHIELD_GENERATOR_CAPACITY_SHIFT));
    fe2_args_dword(&args, PARAM_1, percent);
    fe2_args_word(&args, PARAM_11, first);
    fe2_args_dword(&args, PARAM_3, second);
    fe2_status_draw_line(game, &args, DRIVE_TEXT, DRIVE_X, DRIVE_Y);

    draw_installed_items(ship, game, &args);
    fe2_status_draw_balance(status, game, &args);
    fe2_status_draw_preview_frame(game);
    return FE2_STATUS_OK;
}
