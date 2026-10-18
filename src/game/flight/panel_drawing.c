/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* panel modes, the clock and date, the flight status message
 * and the three double-line gauges. */
#include "panels_internal.h"

enum {
    BACKGROUND_SPRITE = 0x50, STATUS_SPRITE = 0x51, GAUGE_SPRITE = 0x55,
    STATUS_X = 0x100, STATUS_TEXT_X = 0x10b, GAUGE_SPRITE_X = 0xea,
    CLOCK_TEXT = 0x3010, CLOCK_X = 1, DATE_TEXT = 0x300d, DATE_X = 0x20,
    STATUS_TEXT = 0x861f, STATUS_MESSAGE_SELECTOR = 2,
    MODE_BACKGROUND = 3, MODE_STATUS = 4,
    GAUGE_TOP = 0xb1, GAUGE_WIDTH = 0x0f, GAUGE_HEIGHT = 0x0a,
    FUEL_GAUGE_X = 0xfb, BUSY_GAUGE_X = 0x100, THIRD_GAUGE_X = 0x105
};

static void text(fe2_panel_state *s, const fe2_panel_services *game, uint16_t selector,
                 uint32_t value, uint16_t x)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_dword(&args, TEXT_VALUE_PARAMETER, value);
    fe2_args_word(&args, TEXT_STYLE_PARAMETER, 0);
    game->draw_text(game->opaque, selector, fe2_panels_palette_indices[TEXT_COLOUR], x, TEXT_Y, &args);
    (void)s;
}


void fe2_panels_draw_clock(fe2_panel_state *s, const fe2_panel_services *game)
{
    text(s, game, CLOCK_TEXT, s->time_of_day, CLOCK_X);
    s->pointer_redraw = 1;         /* the clock changed: redraw the pointer at the next tick */
}


void fe2_panels_draw_date(fe2_panel_state *s, const fe2_panel_services *game)
{
    text(s, game, DATE_TEXT, s->day, DATE_X);
}

/* The flight status message: docked, landed,.... */
static uint16_t status_message(const fe2_panel_state *s)
{
    static const uint8_t states[5] = {0x04, 0x60, 0x54, 0x68, 0x48};
    unsigned i;
    if (s->ship_status & 1u) return 0x8619;
    for (i = 0; i < 5; ++i)
        if (s->flight_state == states[i]) return (uint16_t)(0x8618 - i);
    return 0x8613;
}

/* the status panel keeps the caller's message argument. */
static void draw_status(fe2_panel_state *s, const fe2_panel_services *game, uint16_t argument)
{
    fe2_call_args args;
    game->draw_sprite(game->opaque, STATUS_SPRITE, STATUS_X, PANEL_Y);
    fe2_args_clear(&args);
    fe2_args_word(&args, TEXT_VALUE_PARAMETER, status_message(s));
    fe2_args_word(&args, TEXT_STYLE_PARAMETER, 0);
    fe2_args_word(&args, STATUS_MESSAGE_SELECTOR, argument);
    game->draw_text(game->opaque, STATUS_TEXT, fe2_panels_palette_indices[TEXT_COLOUR], STATUS_TEXT_X,
                    TEXT_Y, &args);
    game->install_gauges(game->opaque, fe2_panels_gauges);
}


void fe2_panels_draw_mode(fe2_panel_state *s, const fe2_panel_services *game, int16_t mode,
                       uint16_t argument)
{
    if (mode == MODE_STATUS) {
        draw_status(s, game, argument);
        return;
    }
    if (mode != MODE_BACKGROUND) draw_status(s, game, argument);
    game->draw_sprite(game->opaque, BACKGROUND_SPRITE, 0, PANEL_Y);
    fe2_panels_draw_clock(s, game);
    fe2_panels_draw_date(s, game);
}

/* a gauge of `fraction` (of 65536) as two adjacent lines from
 * (x, y) back by the fraction of the width and height. */
static void double_line(const fe2_panel_services *game, uint16_t fraction, uint16_t x, uint16_t y)
{
    uint16_t x1 = (uint16_t)(x - ((uint32_t)fraction * GAUGE_WIDTH >> 16));
    uint16_t y1 = (uint16_t)(y - ((uint32_t)fraction * GAUGE_HEIGHT >> 16));
    game->draw_line(game->opaque, x, y, x1, y1);
    game->draw_line(game->opaque, (uint16_t)(x + 1u), y, (uint16_t)(x1 + 1u), y1);
}

static void set_line_colour(fe2_panel_state *s, uint8_t colour)
{
    s->line_colours = (uint16_t)(colour << 8 | colour);
}

/* fuel (scaled by drive class: bits 8..23 of fuel * scale),
 * equipment busy time and the third resident value. Drive classes above 14
 * read code bytes as scales in the original. */
void fe2_panels_gauges(fe2_panel_state *s, const fe2_panel_services *game)
{
    const fe2_object *ship = s->ship;
    uint8_t drive = ship->as.craft.drive;
    uint32_t fuel = (uint32_t)(uint16_t)(ship->fuel >> 16) *
                    (drive < 15 ? fe2_panels_drive_gauge_scales[drive] : 0u);

    game->draw_sprite_over(game->opaque, GAUGE_SPRITE, GAUGE_SPRITE_X, TEXT_Y);
    set_line_colour(s, fe2_panels_palette_indices[GAUGE_FUEL_COLOUR]);
    double_line(game, (uint16_t)(fuel >> 8), FUEL_GAUGE_X, GAUGE_TOP);
    set_line_colour(s, fe2_panels_palette_indices[GAUGE_SECOND_COLOUR]);
    double_line(game, s->ship->busy_remaining, BUSY_GAUGE_X, GAUGE_TOP);
    set_line_colour(s, fe2_panels_palette_indices[TEXT_COLOUR]);
    double_line(game, s->third_gauge, THIRD_GAUGE_X, GAUGE_TOP);
}
