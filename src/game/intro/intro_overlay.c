/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* 0x7e: the timed overlays over the intro. */
#include "intro.h"

enum {
    OVERLAY_START = 0x15cb40, CREDITS_START = 0x936654,
    PANEL_SPRITE = 0xe0, PANEL_X = 0x40, PANEL_Y = 0xa9,
    CREDIT_COLOUR = 0x0f, TEXT_STYLE_PARAMETER = 7
};

/* Credits shown until their end time, then the start-menu text (commander
 * text selectors). */
static const struct credit {
    uint32_t until;
    uint16_t text, x, y;
} credits[] = {
    {0x964ff4, 0x9417, 0x1e, 0x0f},   /* copyright */
    {0x993994, 0x9418, 0x0a, 0x37},   /* development */
    {0x9c2334, 0x9419, 0x0a, 0x01},   /* graphics and documentation */
};
static const struct credit start_menu = {0, 0x941a, 0x28, 0x14};

void fe2_intro_timed_overlay(fe2_intro_state *s, const fe2_intro_services *game)
{
    const struct credit *credit = &start_menu;
    fe2_call_args args;
    unsigned i;
    if (s->time < OVERLAY_START) return;
    if (s->overlay_mode == 0) {
        game->begin_overlay(game->opaque);
        game->draw_sprite(game->opaque, PANEL_SPRITE, PANEL_X, PANEL_Y);
        return;
    }
    if (s->time < CREDITS_START) return;
    for (i = 0; i < sizeof credits / sizeof credits[0]; ++i)
        if (s->time < credits[i].until) {
            credit = &credits[i];
            break;
        }
    if (credit == &start_menu) {
        s->flags &= (uint8_t)~FE2_INTRO_MUSIC_FLAG;
        game->check_music(game->opaque);
    }
    fe2_args_clear(&args);
    fe2_args_word(&args, TEXT_STYLE_PARAMETER, 0);
    game->draw_text(game->opaque, credit->text, CREDIT_COLOUR, credit->x, credit->y, &args);
}
