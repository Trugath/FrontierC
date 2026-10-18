/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
/* fe2_navigation_state <-> the session (s), its objects layer (o) and its resident globals. */
static void navigation_resident_load(fe2_session *s, fe2_objects_state *o, fe2_navigation_state *st)
{
    (void)s; (void)o; (void)st;
    st->default_selection = (uint8_t)s->resident.map_default_selection;
    st->saved_scale.mantissa = (uint16_t)s->resident.map_saved_scale;
    st->saved_scale.exponent = (uint16_t)(s->resident.map_saved_scale >> 16);
    st->basis_angle[0] = (uint16_t)o->effect_seed[0];
    st->basis_angle[1] = (uint16_t)o->effect_seed[1];
    st->time_of_day = (uint32_t)s->pools.clock[0];
    st->day = (uint32_t)s->pools.clock[1];
    st->autopilot_target = (uint8_t)s->resident.navigation_target;
    st->selection_found = (uint8_t)s->resident.selection_found;
    st->flight_state = (uint8_t)s->resident.flight_mode;
    st->ship = (fe2_object *)o->player_record;
    st->click_x = (uint16_t)s->pointer.pressed[0];
    st->click_y = (uint16_t)s->pointer.pressed[1];
    st->pointer_x = (uint16_t)s->pointer.x;
    st->pointer_y = (uint16_t)s->pointer.y;
    st->last_key = (uint8_t)s->pointer.region[1];
    st->key_repeat = (uint8_t)s->pointer.age[1];
    st->frame_step = (uint32_t)s->render_step;
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
    st->map_objects = (fe2_object_list *)s->resident.map_objects;
    st->objects = (fe2_object_list *)s->pools.root;
}
static void navigation_resident_store(fe2_session *s, fe2_objects_state *o, const fe2_navigation_state *st)
{
    (void)s; (void)o; (void)st;
    s->resident.map_default_selection = st->default_selection;
    s->resident.map_saved_scale = (uint32_t)st->saved_scale.mantissa |
        ((uint32_t)st->saved_scale.exponent << 16);
    o->effect_seed[0] = st->basis_angle[0];
    o->effect_seed[1] = st->basis_angle[1];
    s->pools.clock[0] = st->time_of_day;
    s->pools.clock[1] = st->day;
    s->resident.navigation_target = st->autopilot_target;
    s->resident.selection_found = st->selection_found;
    s->resident.flight_mode = st->flight_state;
    o->player_record = st->ship;
    s->pointer.pressed[0] = st->click_x;
    s->pointer.pressed[1] = st->click_y;
    s->pointer.x = st->pointer_x;
    s->pointer.y = st->pointer_y;
    s->pointer.region[1] = st->last_key;
    s->pointer.age[1] = st->key_repeat;
    s->render_step = st->frame_step;
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
    s->resident.map_objects = st->map_objects;
    s->pools.root = (fe2_object_pool *)st->objects;
}
