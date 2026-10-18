/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Flight initialization, view entry and view changes, the
 * text directory  and the shared service wrappers. */
#include "flight_internal.h"

void flight_effect(flight_context *c, uint16_t effect)
{
    c->g->play_effect(c->g->opaque, effect);
}

/* station messages last until replaced (style 0xffff). */
void flight_message(flight_context *c, uint16_t text, fe2_call_args *args)
{
    fe2_call_args none;
    if (args == 0) {
        fe2_args_clear(&none);
        args = &none;
    }
    fe2_args_word(args, P_MODE, STYLE_MESSAGE);
    c->g->station_message(c->g->opaque, text, args);
}

fe2_object *flight_lookup(flight_context *c, uint8_t handle)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_objects(&args, P_OBJECTS, c->s->current_objects);
    return c->g->lookup(c->g->opaque, handle, &args);
}

void flight_select_root(flight_context *c, fe2_call_args *args)
{
    c->s->current_objects = c->s->objects;
    fe2_args_objects(args, P_OBJECTS, c->s->objects);
}

void flight_clear_regions(flight_context *c)
{
    int i;
    for (i = 14; i >= 0; --i) c->g->remove_region(c->g->opaque, fe2_flight_resource_selectors[i]);
}

/* The next view: front, rear, then the turrets a hull with more than two
 * mounts has (the bottom one only while 0x2129 is not negative), then the
 * external view. */
void flight_cycle_view(flight_context *c)
{
    fe2_flight_state *s = c->s;
    uint8_t view = (uint8_t)(s->view_index + 1);
    if (view == 3) ++view;
    else if (view == 2) {
        if (s->ship->as.flight.capacity_class <= 2) view = (uint8_t)(view + 2);
        else if (s->turret_angle[1] >= 0) ++view;
    }
    if (view > 4) view = 0;
    s->view_index = view;
    flight_refresh_mode_display(c, 0);
}

void flight_enter_cockpit(flight_context *c)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    g->set_panel_mode(g->opaque, 3);
    g->show_panel(g->opaque);
    flight_clear_widget_flag(c);
    s->view_mode = 0xffff;
    g->set_pointer(g->opaque, s->axis[0], s->axis[1]);
    flight_refresh_mode_display(c, 0);
    g->set_controls(g->opaque, 0x303, 0x17);
}

void fe2_flight_initialize(fe2_flight_state *state, const fe2_flight_services *game, uint16_t config)
{
    flight_context c;
    fe2_call_args args;
    fe2_object_list *saved;
    c.s = state;
    c.g = game;
    c.subject = 0;
    if ((uint8_t)config != 1) return;
    state->starting_config = config;
    {
        fe2_flight_drive_reply reply;
        game->hyperspace_query(game->opaque, 0, 0, &reply);
    }
    state->control_flags = 0;
    state->motion_flags = 0;
    state->axis[0] = 0;
    state->axis[1] = 0x200;
    state->external_angle[0] = 0;
    state->external_angle[1] = 0x200;
    state->external_distance = 0xff400000u;
    if (!flight_starting_ship(&c)) return;
    saved = state->current_objects;
    fe2_args_clear(&args);
    fe2_args_word(&args, P2, 0x8600);
    game->panels_panel(game->opaque, 2, &args);
    state->current_objects = saved;
    flight_enter_cockpit(&c);
}

void fe2_flight_activate_view(fe2_flight_state *state, const fe2_flight_services *game)
{
    flight_context c;
    c.s = state;
    c.g = game;
    c.subject = 0;
    if (cockpit_shown(state)) flight_cycle_view(&c);
    else flight_enter_cockpit(&c);
}

void fe2_flight_change_view(fe2_flight_state *state, const fe2_flight_services *game, uint16_t request)
{
    flight_context c;
    c.s = state;
    c.g = game;
    c.subject = 0;
    if (request == 0) {
        state->view_mode = 0x7f;
        state->redraw_flags |= 1;
    } else if ((int16_t)request > 0) {
        flight_refresh_widgets(&c);
    } else {
        state->control_flags &= (uint8_t)~FE2_CONTROL_WIDGETS;
        flight_clear_regions(&c);
    }
}

void fe2_flight_text(const fe2_flight_services *game, uint16_t selector, fe2_text_out *out)
{
    game->format_table_text(game->opaque, selector & 0x3fe,
                            (const fe2_text_table *)(const void *)fe2_flight_text_directory, out);
}
