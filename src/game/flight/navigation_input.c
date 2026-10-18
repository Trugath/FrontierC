/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* 0x7a: the map's keys (options, target picking, clicks, zoom, time
 * stepping and the time slider), then the view rebuild. */
#include "navigation_internal.h"

enum {
    KEY_TIME_BACK = 3, KEY_TIME_FORWARD = 4, KEY_TIME_SLIDER = 0x80,
    KEY_ZOOM_IN = 0xf7, KEY_ZOOM_OUT = 0xf8, KEY_PROJECTIONS = 0xf9, KEY_LABELS = 0xfa,
    KEY_CLICK = 0xff,
    ZOOM_REPEAT_LIMIT = 0x14, ZOOM_RESET_MANTISSA = 0x4000,
    ZOOM_IN_FACTOR = 0x7998, ZOOM_OUT_FACTOR = 0x435d,
    MAX_SCALE_MANTISSA = 0x7fff, MAX_SCALE_EXPONENT = 0x3f,
    SLIDER_LEFT = 262, SLIDER_WIDTH = 58, SLIDER_CENTRE = 29, SLIDER_TOP = 178,
    SLIDER_BOTTOM = 188,
    TIME_STEP_SHIFT = 13,
    OPTIONS_SPRITE = 0x58, PROJECTIONS_SPRITE = 0x59, LABELS_SPRITE = 0x5a,
    TARGET_PROMPT = 0x8a01, PROMPT_PARAMETER = 7
};

static void open_options(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    s->flags |= FE2_NAV_OPTIONS_OPEN;
    game->draw_sprite(game->opaque, OPTIONS_SPRITE, 0x100, 0xbc);
    if (s->flags & FE2_NAV_HIDE_PROJECTIONS) game->draw_sprite(game->opaque, PROJECTIONS_SPRITE, 0x120, 0xbc);
    if (s->flags & FE2_NAV_HIDE_LABELS) game->draw_sprite(game->opaque, LABELS_SPRITE, 0x130, 0xbc);
}

static void pick_target(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    fe2_call_args args;
    s->flags |= FE2_NAV_TARGET_PICKING;
    fe2_args_clear(&args);
    fe2_args_word(&args, PROMPT_PARAMETER, 0xffff);
    game->show_message(game->opaque, TARGET_PROMPT, &args);
}

/* A click centres the map on (or picks as target) the first object near
 * it, then widens the scale to fit the selection. */
static void click(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    fe2_navigation_pass pass;
    uint16_t bound;
    s->selection_found = 0;
    pass.state = s;
    pass.game = game;
    game->visit_projected(game->opaque, s->map_objects, fe2_navigation_click_object, &pass);
    s->flags &= (uint8_t)~FE2_NAV_TARGET_PICKING;
    bound = fe2_navigation_selected_scale(s, game);
    if ((int16_t)bound >= (int16_t)s->scale.exponent) s->scale.exponent = bound;
    game->refresh_objects(game->opaque, s->map_objects);
}

static void advance_time(fe2_navigation_state *s, const fe2_navigation_services *game, uint64_t delta)
{
    s->time += delta;
    game->refresh_objects(game->opaque, s->map_objects);
    game->set_objects_time(game->opaque, s->map_objects, s->time);
}

/* Slider position x (-29..29 from the centre) moves the time by x squared
 * (copied view) or by the square of x squared (generated view), sign
 * restored. The product P is widened as sign(H):H:L:H and doubled twice,
 * so the change is (P << 18) + 4 * H. */
static int slider_delta(const fe2_navigation_state *s, uint64_t *delta)
{
    uint16_t x = (uint16_t)(s->pointer_x - SLIDER_LEFT);
    int16_t centred;
    uint32_t product;
    if ((int16_t)s->pointer_y < SLIDER_TOP || (int16_t)s->pointer_y > SLIDER_BOTTOM) return 0;
    if ((int16_t)x < 0 || (int16_t)x > SLIDER_WIDTH) return 0;
    centred = (int16_t)(x - SLIDER_CENTRE);
    product = (uint32_t)((int32_t)centred * centred);
    if (!(s->flags & FE2_NAV_COPIED_LIST)) {
        int16_t low = (int16_t)(uint16_t)product;
        product = (uint32_t)((int32_t)low * low);
    }
    if (centred < 0) product = 0u - product;
    *delta = ((uint64_t)product << 18) + ((uint64_t)(product >> 16) << 2);
    if (product & 0x80000000u) *delta += UINT64_C(0xfffc000000000000);
    return 1;
}

/* Time steps move by the frame step << 13 (in 32 bits). Backwards the
 * upper dword is always FFFFFFFFh, so a zero step moves back 2^32. */
static uint64_t step_delta(const fe2_navigation_state *s, int backwards)
{
    uint32_t step = s->frame_step << TIME_STEP_SHIFT;
    return backwards ? UINT64_C(0xffffffff00000000) | (uint32_t)(0u - step) : step;
}

static fe2_scalar scaled(const fe2_navigation_state *s, const fe2_navigation_services *game,
                         uint16_t mantissa, uint16_t exponent)
{
    fe2_scalar factor;
    factor.mantissa = mantissa;
    factor.exponent = exponent;
    return game->normalize_word(game->opaque, game->multiply(game->opaque, s->scale, factor));
}

/* Zooming in stops at the selected object's bound. */
static void zoom_in(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    fe2_scalar scale = scaled(s, game, ZOOM_IN_FACTOR, 0);
    if ((int16_t)scale.exponent < (int16_t)fe2_navigation_selected_scale(s, game)) return;
    s->scale = scale;
}

static void zoom_out(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    fe2_scalar scale = scaled(s, game, ZOOM_OUT_FACTOR, 1);
    if ((int16_t)scale.exponent > MAX_SCALE_EXPONENT) {
        scale.mantissa = MAX_SCALE_MANTISSA;
        scale.exponent = MAX_SCALE_EXPONENT;
    }
    s->scale = scale;
}

/* Returns nonzero when the key closes the options. */
static int handle_key(fe2_navigation_state *s, const fe2_navigation_services *game, uint8_t key)
{
    uint64_t delta;
    switch (key) {
    case KEY_PROJECTIONS:
        if (s->flags & FE2_NAV_OPTIONS_OPEN) {
            s->flags ^= FE2_NAV_HIDE_PROJECTIONS;
            return 1;
        }
        open_options(s, game);
        return 0;
    case KEY_LABELS:
        if (s->flags & FE2_NAV_OPTIONS_OPEN) s->flags ^= FE2_NAV_HIDE_LABELS;
        else if (s->flags & FE2_NAV_COPIED_LIST) pick_target(s, game);
        return 1;
    case KEY_CLICK:
        click(s, game);
        return 1;
    default:
        break;
    }
    /* Holding zoom-in starts again from the selected object's bound. */
    if (key == KEY_ZOOM_IN && key == s->last_key && s->key_repeat < ZOOM_REPEAT_LIMIT) {
        s->scale.exponent = fe2_navigation_selected_scale(s, game);
        s->scale.mantissa = ZOOM_RESET_MANTISSA;
    }
    switch (key) {
    case KEY_TIME_SLIDER:
        if (slider_delta(s, &delta)) advance_time(s, game, delta);
        return 0;
    case KEY_TIME_BACK:
    case KEY_TIME_FORWARD:
        advance_time(s, game, step_delta(s, key == KEY_TIME_BACK));
        return 0;
    case KEY_ZOOM_IN:
        zoom_in(s, game);
        return 1;
    case KEY_ZOOM_OUT:
        zoom_out(s, game);
        return 1;
    default:
        game->forward_key(game->opaque, key);
        return 0;
    }
}

void fe2_navigation_render(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    uint8_t key;
    if (game->poll_key(game->opaque, &key) && handle_key(s, game, key) &&
        (s->flags & FE2_NAV_OPTIONS_OPEN)) {
        s->flags &= (uint8_t)~FE2_NAV_OPTIONS_OPEN;
        fe2_navigation_draw_controls(s, game);
    }
    fe2_navigation_build_view(s, game);
}
