/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* time-step reset, 0xded resume, 0xe27 pause/preferences and
 * 0xebc the time-acceleration keys. */
#include "commander_internal.h"

enum {
    KEY_NORMAL = 0xc1, KEY_FASTEST = 0xc5, FIRST_FAST_ICON = 0x60,
    HYPERSPACE = 8, HYPERSPACE_STEP = 0xf880000, HYPERSPACE_EXPONENT = 0x12,
    NO_TIME_KEYS = 0x01, TIME_KEYS_WARN = 0x02,
    WARNING_TEXT = 0x98ac, WARNING_EFFECT = 0x1e, ACCELERATION_DRIFT = 0xc22e
};

/* Steps and exponents of keys 0xc2..0xc5. */
static const struct { uint32_t step; uint16_t exponent; } fast_steps[] = {
    {0x26d6, 3}, {0x1845c, 7}, {0xf2b98, 0x0a}, {0x97b3f0, 0x0c}
};

/* Publish the selected step to the game, draw its icon, then handle the
 * menu's input if a menu is open (the original falls through into it). */
int fe2_commander_publish_time(fe2_commander_state *s, const fe2_commander_services *game, uint16_t icon)
{
    s->game_time_step = s->time_step;
    s->game_time_exponent = s->time_exponent;
    game->draw_sprite(game->opaque, icon, 0, TIME_ICON_Y);
    return s->menu_mode != FE2_MENU_CLOSED ? fe2_commander_menu_input(s, game) : FE2_COMMANDER_CONTINUE;
}

int fe2_commander_pause(fe2_commander_state *s, const fe2_commander_services *game)
{
    s->time_step = 0;
    s->time_exponent = 0;
    s->frame_step = 0;
    return fe2_commander_publish_time(s, game, ICON_PAUSED);
}

int fe2_commander_restore_time_step(fe2_commander_state *s, const fe2_commander_services *game)
{
    if (s->game_time_step == NORMAL_STEP) return 0;
    s->time_step = NORMAL_STEP;
    s->time_exponent = 0;
    s->game_time_step = NORMAL_STEP;
    s->game_time_exponent = 0;
    s->frame_step = 0;
    s->frame_step_high = 0;
    game->draw_sprite(game->opaque, ICON_NORMAL, 0, TIME_ICON_Y);
    return 1;
}

void fe2_commander_resume(fe2_commander_state *s, const fe2_commander_services *game)
{
    s->menu_mode = FE2_MENU_CLOSED;
    s->time_step = NORMAL_STEP;
    s->time_exponent = 0;
    fe2_commander_publish_time(s, game, ICON_NORMAL);
    game->show_panel(game->opaque);
    game->restore_view(game->opaque);
}

/* With time running, pause; otherwise open the preferences (or, with a
 * menu open, refresh the cockpit and pause again). */
int fe2_commander_draw_if_paused(fe2_commander_state *s, const fe2_commander_services *game)
{
    int status;
    if (s->time_step != 0) return fe2_commander_pause(s, game);
    game->draw_sprite(game->opaque, ICON_TIME_STOPPED, 0, TIME_ICON_Y);
    if (s->menu_mode != FE2_MENU_CLOSED) {
        status = fe2_commander_menu_input(s, game);
        if (status != FE2_COMMANDER_CONTINUE) return status;
        game->refresh_cockpit(game->opaque);
        return fe2_commander_pause(s, game);
    }
    fe2_commander_enter_preferences(s, game);
    game->set_widgets(game->opaque, fe2_commander_preference_widgets);
    fe2_commander_draw_preferences(s, game);
    return FE2_COMMANDER_CONTINUE;
}

/* A key 0xc1..0xc5 selects a time step (in hyperspace, any of them selects
 * the hyperspace step). Choosing one plays the menu effect; with a menu
 * open the cockpit refreshes first. */
static int select_step(fe2_commander_state *s, const fe2_commander_services *game, uint16_t icon)
{
    game->play_effect(game->opaque, EFFECT_MENU);
    if (s->menu_mode != FE2_MENU_CLOSED) game->refresh_cockpit(game->opaque);
    return fe2_commander_publish_time(s, game, icon);
}

static int menu_or_continue(fe2_commander_state *s, const fe2_commander_services *game)
{
    return s->menu_mode != FE2_MENU_CLOSED ? fe2_commander_menu_input(s, game) : FE2_COMMANDER_CONTINUE;
}

static int time_key(fe2_commander_state *s, const fe2_commander_services *game, uint8_t key)
{
    if (key == KEY_NORMAL) {
        s->time_step = NORMAL_STEP;
        s->time_exponent = 0;
        return select_step(s, game, ICON_NORMAL);
    }
    if (key > KEY_NORMAL && key <= KEY_FASTEST) {
        unsigned speed = (unsigned)(key - KEY_NORMAL - 1);
        s->time_step = fast_steps[speed].step;
        s->time_exponent = fast_steps[speed].exponent;
        s->time_accumulator -= ACCELERATION_DRIFT;
        return select_step(s, game, (uint16_t)(FIRST_FAST_ICON + speed));
    }
    game->forward_key(game->opaque, key);
    return menu_or_continue(s, game);
}

int fe2_commander_time_input(fe2_commander_state *s, const fe2_commander_services *game)
{
    uint8_t key;
    if ((s->game_flags & NO_TIME_KEYS) || !game->poll_key(game->opaque, &key))
        return menu_or_continue(s, game);
    if (s->flight_state == HYPERSPACE) {
        if (key < KEY_NORMAL || key > KEY_FASTEST) {
            game->forward_key(game->opaque, key);
            return menu_or_continue(s, game);
        }
        s->time_step = HYPERSPACE_STEP;
        s->time_exponent = HYPERSPACE_EXPONENT;
        return select_step(s, game, ICON_NORMAL);
    }
    if ((s->panel_flags & TIME_KEYS_WARN) && key >= KEY_NORMAL) {
        fe2_call_args args;
        if (key > KEY_FASTEST) {
            game->forward_key(game->opaque, key);
            return menu_or_continue(s, game);
        }
        if (s->current_message == WARNING_TEXT) return FE2_COMMANDER_CONTINUE;
        game->play_effect(game->opaque, WARNING_EFFECT);
        fe2_args_clear(&args);
        fe2_args_word(&args, STYLE_PARAMETER, 0xffff);
        game->show_message(game->opaque, WARNING_TEXT, &args);
        return FE2_COMMANDER_CONTINUE;
    }
    return time_key(s, game, key);
}
