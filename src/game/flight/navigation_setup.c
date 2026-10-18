/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Map set-up: initialization, the two map modes, the controls and the map's
 * object list (a filtered copy of the system, or a generated system). */
#include "navigation_internal.h"

enum {
    INITIAL_SCALE_MANTISSA = 0x7fff, INITIAL_SCALE_EXPONENT = 0x33,
    SAVED_SCALE_EXPONENT = 0x2a,
    MAP_PANEL_MODE = 5, POINTER_SCALE = 0x100,
    COPIED_CONTROLS = 0x0f03, GENERATED_CONTROLS = 0x030f, CONTROL_BUTTONS = 3,
    GENERATED_SELECTION = 0x72,
    SELECTABLE_HANDLE = 0x08,
    CONTROLS_SPRITE = 0x56, COPIED_SPRITE = 0x57,
    /* systems action parameters. */
    MODE_PARAMETER = 2, TIME_PARAMETER = 4, DAY_PARAMETER = 5, RECORDS_PARAMETER = 8,
    NAME_PARAMETER = 10
};

void fe2_navigation_initialize(fe2_navigation_state *s, uint8_t request, uint8_t mode)
{
    if (request != 1) return;
    s->flags = 0;
    s->time = fe2_navigation_clock(s);
    s->saved_scale.mantissa = INITIAL_SCALE_MANTISSA;
    s->saved_scale.exponent = SAVED_SCALE_EXPONENT;
    s->default_selection = mode == 1 ? 0x60 : mode == 2 ? 0x70 : 0x6f;
}

void fe2_navigation_restore(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    s->panel_view = 0;
    game->show_panel(game->opaque);
    game->restore_view(game->opaque);
    if (s->flags & FE2_NAV_COPIED_LIST) {
        s->default_selection = s->camera.parent_handle;
        s->saved_scale = s->scale;
    }
}

static void prepare_view(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    game->set_panel_mode(game->opaque, MAP_PANEL_MODE);
    s->flags &= (uint8_t)~FE2_NAV_TARGET_PICKING;
    s->scale.mantissa = INITIAL_SCALE_MANTISSA;
    s->scale.exponent = INITIAL_SCALE_EXPONENT;
    s->time = fe2_navigation_clock(s);
    game->set_pointer(game->opaque, POINTER_SCALE, POINTER_SCALE);
    s->panel_view = 0xffff;
    game->show_panel(game->opaque);
    game->set_widgets(game->opaque, fe2_navigation_slider_widgets);
}

/* The bottom controls. A selection whose handle is not selectable steps
 * back by one, unchecked. */
void fe2_navigation_draw_controls(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    if (!(game->handle_flags(game->opaque, s->map_objects, s->camera.parent_handle) & SELECTABLE_HANDLE))
        --s->camera.parent_handle;
    game->draw_sprite(game->opaque, CONTROLS_SPRITE, 0x100, 0xb2);
    if (s->flags & FE2_NAV_COPIED_LIST) game->draw_sprite(game->opaque, COPIED_SPRITE, 0x130, 0xbc);
}

static void clear_map_objects(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_objects(&args, OBJECTS_PARAMETER, s->map_objects);
    game->clear_objects(game->opaque, &args);
}

/* copy the system's objects, restore the saved scale, and filter. */
static void copy_map_objects(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    fe2_navigation_pass pass;
    clear_map_objects(s, game);
    game->copy_objects(game->opaque, s->map_objects, s->objects);
    s->scale = s->saved_scale;
    pass.state = s;
    pass.game = game;
    game->visit_objects(game->opaque, s->map_objects, fe2_navigation_filter_object, &pass);
}

/* generate the current system's records (systems mode 0), then
 * instantiate them at the current time (mode 1). */
static void generate_map_objects(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    uint8_t records[FE2_NAV_SYSTEM_RECORD_BYTES];
    fe2_system_seeds seeds;
    fe2_call_args args;
    clear_map_objects(s, game);
    seeds = game->current_system(game->opaque);
    fe2_args_clear(&args);
    fe2_args_template(&args, RECORDS_PARAMETER, records);
    fe2_args_word(&args, MODE_PARAMETER, 0);
    fe2_args_string(&args, NAME_PARAMETER, seeds.name);
    game->system_action(game->opaque, &seeds, &args);
    fe2_args_clear(&args);
    fe2_args_template(&args, RECORDS_PARAMETER, records);
    fe2_args_dword(&args, TIME_PARAMETER, s->time_of_day);
    fe2_args_dword(&args, DAY_PARAMETER, s->day);
    fe2_args_word(&args, MODE_PARAMETER, 1);
    game->system_action(game->opaque, 0, &args);
}

void fe2_navigation_select(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    s->flags |= FE2_NAV_COPIED_LIST;
    s->camera.parent_handle = s->default_selection;
    prepare_view(s, game);
    game->set_controls(game->opaque, COPIED_CONTROLS, CONTROL_BUTTONS);
    copy_map_objects(s, game);
    fe2_navigation_draw_controls(s, game);
}

void fe2_navigation_select_generated(fe2_navigation_state *s, const fe2_navigation_services *game)
{
    s->flags &= (uint8_t)~FE2_NAV_COPIED_LIST;
    s->camera.parent_handle = GENERATED_SELECTION;
    prepare_view(s, game);
    game->set_controls(game->opaque, GENERATED_CONTROLS, CONTROL_BUTTONS);
    generate_map_objects(s, game);
    fe2_navigation_draw_controls(s, game);
}

/* keep bodies and the special marker in progress 1; other markers
 * and convertible objects become plain markers; remove the rest. */
void fe2_navigation_filter_object(void *pass, fe2_object *o)
{
    const fe2_navigation_services *game = ((fe2_navigation_pass *)pass)->game;
    switch (o->role) {
    case ROLE_BODY:
        return;
    case ROLE_CONVERTIBLE:
        o->role = ROLE_MARKER;
        if (o->behaviour != BEHAVIOUR_FIXED) o->station_slot = 0;
        return;
    case ROLE_SPECIAL:
        if (o->behaviour == BEHAVIOUR_FIXED || o->behaviour == BEHAVIOUR_EXCLUDED) break;
        if (o->model == SPECIAL_MODEL) {
            if (o->cargo_type == 1) return;
            break;
        }
        o->role = ROLE_MARKER;
        o->station_slot = 0;
        return;
    default:
        break;
    }
    game->remove_object(game->opaque, o);
}
