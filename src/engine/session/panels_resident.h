/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
/* fe2_panel_state <-> the session (s), its objects layer (o) and its resident globals. */
static void panels_resident_load(fe2_session *s, fe2_objects_state *o, fe2_panel_state *st)
{
    (void)s; (void)o; (void)st;
    st->enabled = (uint8_t)s->drawing_flag;
    st->selection = (uint16_t)s->panel_selection;
    st->refresh_style = (uint8_t)s->render_mode;
    st->ship = (fe2_object *)o->player_record;
    st->line_colours = (uint16_t)s->resident.line_colour;
    st->time_of_day = (uint32_t)s->pools.clock[0];
    st->day = (uint32_t)s->pools.clock[1];
    st->third_gauge = (uint16_t)s->resident.heat_gauge;
    st->simulation_time_step = (uint32_t)s->time_step;
    st->ship_status = (uint8_t)s->resident.ship_control_flags;
    st->flight_state = (uint8_t)s->resident.flight_mode;
    st->scanner_mode = (uint8_t)s->resident.scanner_view_mode;
    st->colours[0] = (uint8_t)s->resident.display_colour_0;
    st->colours[1] = (uint8_t)s->resident.display_colour_1;
    st->colours[2] = (uint8_t)s->resident.display_colour_2;
    st->colours[3] = (uint8_t)s->resident.display_colour_3;
    st->colours[4] = (uint8_t)s->resident.display_colour_4;
    st->colours[5] = (uint8_t)s->resident.display_colour_5;
    st->colours[6] = (uint8_t)s->resident.display_colour_6;
    st->colours[7] = (uint8_t)s->resident.display_colour_7;
    st->colours[8] = (uint8_t)s->resident.display_colour_8;
    st->colours[9] = (uint8_t)s->resident.display_colour_9;
    st->colours[10] = (uint8_t)s->resident.display_colour_10;
    st->objects = (fe2_object_list *)s->pools.root;
}
static void panels_resident_store(fe2_session *s, fe2_objects_state *o, const fe2_panel_state *st)
{
    (void)s; (void)o; (void)st;
    s->drawing_flag = st->enabled;
    s->panel_selection = st->selection;
    s->render_mode = st->refresh_style;
    o->player_record = st->ship;
    s->resident.line_colour = st->line_colours;
    s->pools.clock[0] = st->time_of_day;
    s->pools.clock[1] = st->day;
    s->resident.heat_gauge = st->third_gauge;
    s->time_step = st->simulation_time_step;
    s->resident.ship_control_flags = st->ship_status;
    s->resident.flight_mode = st->flight_state;
    s->resident.scanner_view_mode = st->scanner_mode;
    s->resident.display_colour_0 = st->colours[0];
    s->resident.display_colour_1 = st->colours[1];
    s->resident.display_colour_2 = st->colours[2];
    s->resident.display_colour_3 = st->colours[3];
    s->resident.display_colour_4 = st->colours[4];
    s->resident.display_colour_5 = st->colours[5];
    s->resident.display_colour_6 = st->colours[6];
    s->resident.display_colour_7 = st->colours[7];
    s->resident.display_colour_8 = st->colours[8];
    s->resident.display_colour_9 = st->colours[9];
    s->resident.display_colour_10 = st->colours[10];
    s->pools.root = (fe2_object_pool *)st->objects;
}
