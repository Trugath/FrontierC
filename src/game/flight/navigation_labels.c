/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The time point, the target marker and labels, and click selection. */
#include "navigation_internal.h"

enum {
    TIME_POINT_TEXT = 0, TEXT_SELECTOR_MASK = 0x3fe,
    TIME_PARAMETER = 1, DAY_PARAMETER = 2,
    TIME_POINT_X = 0x0a, TIME_POINT_Y = 0x9e,
    MARKER_OFFSET = 8, MARKER_BLINK = 0x100,      /* time of day bit 8 hides it */
    LABEL_CLEARANCE = 10,
    CLICK_X_OFFSET = 3, CLICK_Y_OFFSET = 4, CLICK_DISTANCE = 8,
    PICK_EFFECT = 0x15, FOLLOWING_STATE = 4
};

static const fe2_text_table *directory(void)
{
    return (const fe2_text_table *)fe2_navigation_text_directory;
}

void fe2_navigation_time_point(const fe2_navigation_services *game, uint16_t selector,
                               fe2_text_out *out)
{
    game->format_table_text(game->opaque, selector & TEXT_SELECTOR_MASK, directory(), 0, out);
}

void fe2_navigation_render_targets(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    fe2_call_args args;
    fe2_text_out line;
    fe2_args_clear(&args);
    fe2_args_dword(&args, TIME_PARAMETER, (uint32_t)s->time);
    fe2_args_dword(&args, DAY_PARAMETER, (uint32_t)(s->time >> 32));
    line.buffer = s->stack_segment;
    line.at = FE2_NAV_TEXT_LINE;
    game->format_table_text(game->opaque, TIME_POINT_TEXT, directory(), &args, &line);
    game->draw_string(game->opaque, s->stack_segment + FE2_NAV_TEXT_LINE, s->colours[TIME_COLOUR],
                      TIME_POINT_X, TIME_POINT_Y);
    if (!(s->flags & FE2_NAV_HIDE_LABELS)) {
        fe2_navigation_pass pass;
        pass.state = s;
        pass.game = game;
        game->visit_projected(game->opaque, s->map_objects, fe2_navigation_label_object, &pass);
    }
}

/* Distance of the original: each 16-bit difference made positive (0x8000
 * stays), summed in 16 bits. */
static uint16_t distance(uint16_t ax, uint16_t ay, uint16_t bx, uint16_t by)
{
    uint16_t dx = (uint16_t)(ax - bx), dy = (uint16_t)(ay - by);
    if (dx & 0x8000u) dx = (uint16_t)(0u - dx);
    if (dy & 0x8000u) dy = (uint16_t)(0u - dy);
    return (uint16_t)(dx + dy);
}

/* the autopilot target's blinking marker; body names, except
 * where they would overlap their parent's position. */
void fe2_navigation_label_object(void *pass, fe2_object *o, uint16_t x, uint16_t y)
{
    fe2_navigation_state *s = ((fe2_navigation_pass *)pass)->state;
    const fe2_navigation_services *game = ((fe2_navigation_pass *)pass)->game;
    if (o->handle == s->autopilot_target && !(s->time_of_day & MARKER_BLINK))
        game->draw_string(game->opaque, fe2_navigation_target_marker, s->colours[TIME_COLOUR],
                          (uint16_t)(x - MARKER_OFFSET), y);
    if (o->role != ROLE_BODY) return;
    if (o->parent_handle != 0) {
        const fe2_object *parent = game->lookup_object(game->opaque, o->parent_handle, 0);
        if (distance(parent->screen_x, parent->screen_y, x, y) < LABEL_CLEARANCE) return;
    }
    game->draw_name(game->opaque, o, s->colours[LABEL_COLOUR], x, y);
}

/* the first object within 8 of the click. While picking, it becomes
 * the autopilot target (followed by the ship in flight state 4); otherwise a
 * visible body becomes the selection. */
void fe2_navigation_click_object(void *pass, fe2_object *o, uint16_t x, uint16_t y)
{
    fe2_navigation_state *s = ((fe2_navigation_pass *)pass)->state;
    const fe2_navigation_services *game = ((fe2_navigation_pass *)pass)->game;
    if (s->selection_found) return;
    if (distance((uint16_t)(x - CLICK_X_OFFSET), (uint16_t)(y + CLICK_Y_OFFSET), s->click_x,
                 s->click_y) > CLICK_DISTANCE)
        return;
    if (s->flags & FE2_NAV_TARGET_PICKING) {
        s->selection_found = 0xff;
        s->autopilot_target = o->handle;
        game->play_effect(game->opaque, PICK_EFFECT);
        if (s->flight_state == FOLLOWING_STATE) {
            s->ship->behaviour = BEHAVIOUR_FOLLOW;
            s->ship->behaviour_target = o->handle;
        }
        return;
    }
    if ((o->flags & OBJECT_HIDDEN) || o->role != ROLE_BODY) return;
    s->selection_found = 0xff;
    s->camera.parent_handle = o->handle;
}
