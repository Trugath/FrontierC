/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Shared page furniture: headings, the balance box and the ship preview. */
#include "status_internal.h"

enum {
    HEADING_X = 0x1e, HEADING_Y = 5,
    BALANCE_TEXT = 0x9c38, BALANCE_X = 0xbe, BALANCE_Y = 0x8a,
    PAGE_SPRITE_X = 0x20, PAGE_SPRITE_Y = 0xbc,
    PREVIEW_CLOCK_SHIFT = 10
};

static const fe2_rect preview_frame = {0xbd, 0x13d, 0x02, 0x82};

int32_t fe2_status_sar32(int32_t value, unsigned count)
{
    uint32_t bits = (uint32_t)value;
    if (count >= 32) return value < 0 ? -1 : 0;
    if (value >= 0) return (int32_t)(bits >> count);
    return (int32_t)~(~bits >> count);
}

int16_t fe2_status_sar16(int16_t value, unsigned count)
{
    return (int16_t)fe2_status_sar32(value, count);
}

/* plain white text line. */
void fe2_status_draw_line(const fe2_game_services *game, fe2_call_args *args, uint16_t text,
                       uint16_t x, uint16_t y)
{
    fe2_args_word(args, PARAM_STYLE, STYLE_PLAIN);
    game->draw_text(game->opaque, text, TEXT_COLOUR_WHITE, x, y, args);
}

/* clear the panel and draw the page title in the dimmed colour. */
void fe2_status_draw_heading(fe2_commander_status *status, const fe2_game_services *game,
                          fe2_call_args *args, uint16_t text)
{
    game->clear_panel(game->opaque);
    status->heading_colour = status->dim_colour;
    fe2_status_draw_line(game, args, text, HEADING_X, HEADING_Y);
}

/* Remaining space as the original builds it: the capacity sign-extended to a
 * dword, then only its low word reduced by the space in use. */
static uint32_t remaining(uint16_t capacity, uint16_t used)
{
    uint32_t extended = (uint32_t)(int32_t)(int16_t)capacity;
    return (extended & UINT32_C(0xffff0000)) | (uint16_t)(capacity - used);
}

/* cash, cargo space and cabins. */
void fe2_status_draw_balance(const fe2_commander_status *status, const fe2_game_services *game,
                          fe2_call_args *args)
{
    fe2_args_dword(args, PARAM_1, status->cash);
    fe2_args_dword(args, PARAM_2, remaining(status->cargo_capacity, status->cargo_used));
    fe2_args_dword(args, PARAM_3, remaining(status->cabin_capacity, status->cabins_used));
    fe2_args_dword(args, PARAM_10, (uint32_t)(int32_t)(int16_t)status->cargo_used);
    fe2_args_dword(args, PARAM_11, (uint32_t)(int32_t)(int16_t)status->cabins_used);
    fe2_status_draw_line(game, args, BALANCE_TEXT, BALANCE_X, BALANCE_Y);
}


void fe2_status_draw_preview_frame(const fe2_game_services *game)
{
    game->draw_frame(game->opaque, &preview_frame, 0);
}

/* frame, then render the commander's ship from a temporary camera. */
void fe2_status_render_preview(fe2_commander_status *status, const fe2_game_services *game)
{
    fe2_object camera = {0};
    fe2_call_args args;
    unsigned axis;

    fe2_status_draw_preview_frame(game);
    status->redraw_flags &= (uint8_t)~1u;
    fe2_args_clear(&args);
    fe2_args_object(&args, PARAM_RESULT, &camera);
    fe2_args_object(&args, PARAM_SUBJECT, status->ship);
    fe2_args_word(&args, PARAM_1, 0);
    game->prepare_camera(game->opaque, &camera, &args);
    for (axis = 0; axis < 3; ++axis) {
        camera.camera_target[axis] = 0;
        camera.camera_offset[axis] = status->preview_distance[axis];
    }
    camera.camera_roll = 0;
    camera.as.craft.camera_flags = 0;
    camera.clock = (uint16_t)(status->time_of_day >> PREVIEW_CLOCK_SHIFT);
    status->view_angle[0] = status->preview_angle[0];
    status->view_angle[1] = status->preview_angle[1];
    game->orient_camera(game->opaque, &camera);
    game->render_scene(game->opaque, &camera);
}

/* page sprite word read at byte offset `page` (odd pages straddle). */
void fe2_status_draw_page_sprite(const fe2_game_services *game, uint8_t page)
{
    unsigned offset = page < sizeof fe2_status_page_sprite_bytes - 1 ? page : 0;
    uint16_t sprite = (uint16_t)(fe2_status_page_sprite_bytes[offset] |
                                 fe2_status_page_sprite_bytes[offset + 1] << 8);
    game->draw_sprite(game->opaque, sprite, PAGE_SPRITE_X, PAGE_SPRITE_Y);
}
