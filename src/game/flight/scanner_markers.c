/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* scanner markers. Objects related to the
 * selected one are projected through the viewing basis and drawn as
 * markers with stems on the scanner. */
#include "panels_internal.h"

enum {
    SCANNER_SPRITE = 0x52, SCANNER_X = 0x50,
    HIDDEN_FLAG = 0x20, MARKERS_ENABLED = 0x08,
    NO_SCANNER_EQUIPMENT_BIT = 0x12, NO_SCANNER_EVENT = 0xf5,
    WIDE_EXPONENT = 0x11, MAX_EXPONENT = 12, MIN_EXPONENT = 9, PROJECTION_BITS = 0x22,
    COLOUR_VALUE_LIMIT = 300,
    CLIP_HEIGHT = 0x13, SCREEN_HALF_WIDTH = 0x4e, SCREEN_WIDTH = 0x9c, SCREEN_CENTRE = 0xa0,
    BASE_Y = 0xb4, PANEL_BOTTOM = 0xc7, PANEL_TOP = 0xa8,
    CLIP_PARAMETER = 3, COLOUR_PARAMETER = 8, VIEWER_PARAMETER = 8
};

typedef struct marker_pass {
    fe2_panel_state *state;
    const fe2_panel_services *game;
    fe2_scanner_view view;
} marker_pass;

static void visit(void *context, fe2_object *object)
{
    marker_pass *pass = context;
    fe2_panels_scanner_marker(pass->state, pass->game, &pass->view, object);
}


void fe2_panels_render_objects(fe2_panel_state *s, const fe2_panel_services *game,
                               uint16_t view_flags, fe2_object *viewer)
{
    if (!s->enabled) return;
    if (s->default_selected) {
        fe2_object *ship = s->ship, *linked;
        marker_pass pass;
        if (!((ship->as.craft.equipment >> NO_SCANNER_EQUIPMENT_BIT) & 1u))
            game->object_event(game->opaque, NO_SCANNER_EVENT);
        game->draw_sprite_over(game->opaque, SCANNER_SPRITE, SCANNER_X, PANEL_Y);
        pass.view.viewer = viewer;
        if (s->ship->station_slot != 0) {
            fe2_call_args args;
            fe2_args_clear(&args);
            fe2_args_object(&args, VIEWER_PARAMETER, s->ship);
            pass.view.viewer = s->ship;
            game->secondary_basis(game->opaque, s->ship, &args);
        }
        linked = game->linked_object(game->opaque, s->ship);
        pass.state = s;
        pass.game = game;
        pass.view.target = linked->parent_handle;
        pass.view.own = linked->handle;
        pass.view.flags = view_flags;
        game->visit_objects(game->opaque, s->objects, visit, &pass);
    }
    fe2_panels_gauges(s, game);
}

/* Bound on a signed 64-bit value's magnitude: negative values are
 * complemented without adding one. */
static uint64_t bound(uint32_t low, uint32_t high)
{
    uint64_t value = (uint64_t)high << 32 | low;
    return (high & 0x80000000u) ? ~value : value;
}

static uint32_t sar32(uint32_t value, unsigned count)
{
    while (count-- > 0) value = (value >> 1) | (value & 0x80000000u);
    return value;
}

/* Scale the three coordinates into 16-bit range. Returns the exponent of
 * the scaled words; positions too far to show give an exponent above 12. */
static int scale_down(fe2_wide_vector *p, int16_t coordinate[3])
{
    uint64_t magnitude = (bound(p->low[0], p->high[0]) | bound(p->low[1], p->high[1]) |
                          bound(p->low[2], p->high[2])) << 1;
    unsigned axis;
    int16_t exponent = 0;

    if (magnitude >> 32) {
        /* Far away: halve until the bound fits 32 bits. The original then
         * swaps the low words in (always rejected: exponent >= 17). */
        int16_t shifts = 0;
        for (; magnitude >> 32; magnitude >>= 1, ++shifts)
            for (axis = 0; axis < 3; ++axis) {
                uint64_t v = (uint64_t)p->high[axis] << 32 | p->low[axis];
                v = (v >> 1) | (v & UINT64_C(0x8000000000000000));
                p->low[axis] = (uint32_t)v;
                p->high[axis] = (uint32_t)(v >> 32);
            }
        for (axis = 0; axis < 3; ++axis) {
            uint16_t low = (uint16_t)p->low[axis], high = (uint16_t)(p->low[axis] >> 16);
            coordinate[axis] = (int16_t)(uint16_t)((int16_t)high >> 1);
            p->low[axis] = (uint32_t)low << 16 | (uint16_t)coordinate[axis];
        }
        return WIDE_EXPONENT + shifts;
    }
    {
        uint32_t m = (uint32_t)magnitude;
        while ((m >> 16) != 0 || (m & 0xffffu) >= 0x4000u) {
            ++exponent;
            m >>= 1;
        }
    }
    for (axis = 0; axis < 3; ++axis) {
        p->low[axis] = sar32(p->low[axis], (unsigned)exponent);
        coordinate[axis] = (int16_t)(uint16_t)p->low[axis];
    }
    return exponent;
}

/* Viewing basis: the viewer's own, or its alternate (+0x46) when a docked
 * ship looks at an object that is not its parent. */
static const int16_t *viewing_basis(const fe2_panel_state *s, const fe2_scanner_view *view)
{
    const fe2_object *ship = s->ship;
    if (ship->station_slot != 0 && view->viewer->station_slot == 0 &&
        view->viewer->handle != ship->parent_handle)
        return ship->alternate_orientation;
    return ship->orientation;
}

/* Column `column` of the word basis dotted with v, doubled. */
static uint32_t transform(const int16_t *basis, unsigned column, const int16_t v[3])
{
    uint32_t sum = (uint32_t)((int32_t)basis[column] * v[0]) +
                   (uint32_t)((int32_t)basis[column + 3] * v[1]) +
                   (uint32_t)((int32_t)basis[column + 6] * v[2]);
    return sum << 1;
}

static uint8_t marker_colour(const fe2_panel_services *game, const fe2_object *viewer)
{
    fe2_model_colour c = game->model_colour(game->opaque, viewer->model);
    if (!c.present || c.value >= COLOUR_VALUE_LIMIT) return fe2_panels_palette_indices[MARKER_COLOUR];
    /* Values 288..299 index past the colour table into code in the original. */
    return (c.value >> 4) < 18 ? fe2_panels_marker_colours[c.value >> 4] : 0;
}


void fe2_panels_scanner_marker(fe2_panel_state *s, const fe2_panel_services *game,
                               const fe2_scanner_view *view, fe2_object *candidate)
{
    fe2_wide_vector position;
    const int16_t *basis;
    int16_t coordinate[3], exponent, x, y, height;
    uint32_t screen[3];
    unsigned shift, axis;
    uint8_t colour;
    fe2_call_args args;

    if (view->target == candidate->parent_handle) {
        if (view->own == candidate->handle) return;
    } else if (view->target != candidate->handle) {
        return;
    }
    if ((candidate->flags & HIDDEN_FLAG) || !(view->flags & MARKERS_ENABLED)) return;

    game->relative_position(game->opaque, candidate, s->ship, &position);
    exponent = (int16_t)scale_down(&position, coordinate);
    if (exponent > MAX_EXPONENT) return;
    if (exponent <= MIN_EXPONENT) exponent = MIN_EXPONENT;

    basis = viewing_basis(s, view);
    shift = (unsigned)(PROJECTION_BITS - exponent);
    for (axis = 0; axis < 3; ++axis) screen[axis] = sar32(transform(basis, axis, coordinate), shift);

    colour = marker_colour(game, view->viewer);
    x = (int16_t)(uint16_t)screen[0];
    height = (int16_t)((int16_t)(uint16_t)screen[1] >> 2);
    y = (int16_t)(BASE_Y - ((int16_t)(uint16_t)screen[2] >> 3));
    if (height >= CLIP_HEIGHT || height <= -CLIP_HEIGHT) return;
    if ((uint16_t)(x + SCREEN_HALF_WIDTH) >= SCREEN_WIDTH) return;
    if (y >= PANEL_BOTTOM || y <= PANEL_TOP) return;
    fe2_args_clear(&args);
    fe2_args_word(&args, CLIP_PARAMETER, (s->scanner_mode & 0x80u) ? 0 : 0xffff);
    fe2_args_byte(&args, COLOUR_PARAMETER, colour);
    game->draw_marker(game->opaque, (uint16_t)(x + SCREEN_CENTRE), (uint16_t)y, (uint16_t)height,
                      &args);
}
