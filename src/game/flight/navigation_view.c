/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The view camera: its basis from the view angles and its offset from the
 * scale, then the map render and the projection pass. */
#include "navigation_internal.h"

enum { ANGLE_SHIFT = 6, OFFSET_EXPONENT_BIAS = 31, SELECTION_MARGIN = 10 };

void fe2_navigation_camera_args(fe2_navigation_state *s, fe2_call_args *args)
{
    fe2_args_clear(args);
    fe2_args_object(args, CAMERA_PARAMETER, &s->camera);
}

/* the selected object's model radii plus a margin, as an exponent
 * bound for the scale. */
uint16_t fe2_navigation_selected_scale(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    fe2_call_args args;
    const fe2_object *selected;
    fe2_navigation_model model;
    fe2_args_clear(&args);
    fe2_args_objects(&args, OBJECTS_PARAMETER, s->map_objects);
    selected = game->lookup_object(game->opaque, s->camera.parent_handle, &args);
    model = game->model(game->opaque, selected->model);
    return (uint16_t)(model.radius[0] + model.radius[1] + SELECTION_MARGIN);
}

static uint32_t sign_of(uint32_t value) { return (value & 0x80000000u) ? 0xffffffffu : 0; }

/* Arithmetic right shift of a signed dword by one. */
static uint32_t halve(uint32_t value) { return (value >> 1) | (value & 0x80000000u); }

/*
 * Camera offset component: the basis' third column times the negated scale
 * mantissa, as a 64-bit value. The exponent's distance from 31 shifts it:
 * - left across all 64 bits;
 * - right through the low dword only, where the high dword stays the sign.
 */
static void offset_component(int16_t basis, uint16_t mantissa, int16_t shift, uint32_t *low,
                             uint32_t *high)
{
    uint32_t product = (uint32_t)((int32_t)basis * (int16_t)(uint16_t)(0u - mantissa));
    uint32_t sign = sign_of(product);
    for (; shift < 0; ++shift) product = halve(product);
    for (; shift > 0; --shift) {
        sign = sign << 1 | product >> 31;
        product <<= 1;
    }
    *low = product;
    *high = sign;
}

void fe2_navigation_build_view(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    fe2_call_args args;
    unsigned row;
    int16_t shift = (int16_t)(uint16_t)(s->scale.exponent - OFFSET_EXPONENT_BIAS);
    s->view_angle[0] = game->view_angles(game->opaque, &s->view_angle[1]);
    s->basis_angle[0] = (uint16_t)(s->view_angle[1] << ANGLE_SHIFT);
    s->basis_angle[1] = (uint16_t)(0u - (uint16_t)(s->view_angle[0] << ANGLE_SHIFT));
    game->basis_b(game->opaque, s->camera.orientation);
    for (row = 0; row < 3; ++row)
        offset_component(s->camera.orientation[3 * row + 2], s->scale.mantissa, shift,
                         &s->camera.position_low[row], &s->camera.position_high[row]);
    s->camera.render_state = 0;
    fe2_navigation_camera_args(s, &args);
    fe2_args_objects(&args, OBJECTS_PARAMETER, s->map_objects);
    game->render_map(game->opaque, &args);
    if (!(s->flags & FE2_NAV_HIDE_PROJECTIONS)) {
        fe2_navigation_pass pass;
        pass.state = s;
        pass.game = game;
        game->visit_objects(game->opaque, s->map_objects, fe2_navigation_project_object, &pass);
    }
}
