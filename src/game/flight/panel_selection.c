/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* palette set-up, events, panel selection and
 * refresh. */
#include "panels_internal.h"

enum {
    START_GAME = 1, EVENT_CLOCK = 0x0f, EVENT_MODE = 3,
    CLOCK_SPRITE = 0x54, REFRESH_SPRITE = 0x53, DEFAULT_SPRITE = 0x4e, ACTIVE_SPRITE = 0x4f,
    SELECTION_X = 0x40, PARTIAL_REFRESH = 0x80,
    NOTIFY_CLOSE = 1, NOTIFY_FORCED = 0xffff
};

/* Up to this clock value the panel is always redrawn in full. */
#define FULL_REFRESH_UNTIL UINT32_C(0x1845c)

/* With the direction flag set the original copies backwards
 * through code; the port assumes it clear. */
void fe2_panels_initialize(fe2_panel_state *s, uint8_t request)
{
    unsigned i;
    if (request != START_GAME) return;
    s->selection = 0;
    s->default_selected = 0;
    for (i = 0; i < sizeof s->colours; ++i) s->colours[i] = fe2_panels_palette_indices[i];
}


void fe2_panels_event(fe2_panel_state *s, const fe2_panel_services *game, uint16_t event)
{
    if ((event != EVENT_CLOCK && event != EVENT_MODE) || !s->enabled) return;
    game->draw_sprite(game->opaque, CLOCK_SPRITE, 0, TEXT_Y);
    fe2_panels_draw_clock(s, game);
    fe2_panels_draw_date(s, game);
}

static void draw_selection(fe2_panel_state *s, const fe2_panel_services *game, uint16_t sprite)
{
    game->draw_sprite(game->opaque, sprite, SELECTION_X, PANEL_Y);
    game->selection_changed(game->opaque);
    fe2_panels_draw_date(s, game);
}

/* requests below 2 change the selection. Zero closes the selected
 * panel (its overlay may refuse); other values keep a different selected
 * panel after notifying it, or select `overlay`. */
void fe2_panels_select(fe2_panel_state *s, const fe2_panel_services *game, int16_t request,
                       uint16_t overlay, uint16_t argument)
{
    uint16_t old = s->selection;

    if (request >= 2) {
        fe2_panels_draw_mode(s, game, request, argument);
        return;
    }
    s->selection_argument = (uint8_t)argument;
    if (old == 0 || old == overlay) {
        if (request != 0) {
            s->selection = overlay;
            s->default_selected = 0;
            draw_selection(s, game, ACTIVE_SPRITE);
            return;
        }
        s->selection = 0;
    } else if (request != 0) {
        game->notify_panel(game->opaque, old, NOTIFY_FORCED);
        s->default_selected = 0;
        draw_selection(s, game, ACTIVE_SPRITE);
        return;
    } else {
        s->selection = 0;
        if (game->notify_panel(game->opaque, old, NOTIFY_CLOSE) != 0) return;
    }
    s->default_selected = 0xff;
    draw_selection(s, game, DEFAULT_SPRITE);
}


void fe2_panels_refresh(fe2_panel_state *s, const fe2_panel_services *game)
{
    if (!s->enabled) return;
    if (s->simulation_time_step < FULL_REFRESH_UNTIL || !(s->refresh_style & PARTIAL_REFRESH)) {
        game->draw_sprite(game->opaque, REFRESH_SPRITE, 0, TEXT_Y);
        fe2_panels_draw_clock(s, game);
    } else {
        fe2_call_args args;
        game->draw_sprite_over(game->opaque, REFRESH_SPRITE, 0, TEXT_Y);
        fe2_args_clear(&args);
        fe2_args_dword(&args, TEXT_VALUE_PARAMETER, s->time_of_day);
        fe2_args_word(&args, TEXT_STYLE_PARAMETER, 0);
        game->draw_text_over(game->opaque, 0x3010, fe2_panels_palette_indices[TEXT_COLOUR], 1, TEXT_Y,
                             &args);
    }
    if (s->refresh_style & PARTIAL_REFRESH) return;
    fe2_panels_gauges(s, game);
    game->present(game->opaque);
}
