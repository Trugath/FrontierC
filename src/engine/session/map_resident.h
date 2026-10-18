/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
/* fe2_map_state <-> the session (s), its objects layer (o) and its resident globals. */
static void map_resident_load(fe2_session *s, fe2_objects_state *o, fe2_map_state *st)
{
    (void)s; (void)o; (void)st;
    st->selection_reference = (uint32_t)s->resident.jump_selection_reference;
    st->selection_distance = (uint32_t)s->resident.selected_jump_distance;
    st->selection_status = (uint8_t)s->resident.jump_selection_status;
    st->status_colour = (uint8_t)s->resident.display_colour_1;
    st->name_colour = (uint8_t)s->resident.display_colour_4;
    st->classification_colour = (uint8_t)s->resident.display_colour_2;
    st->information_colour = (uint8_t)s->resident.display_colour_5;
}
static void map_resident_store(fe2_session *s, fe2_objects_state *o, const fe2_map_state *st)
{
    (void)s; (void)o; (void)st;
    s->resident.jump_selection_reference = st->selection_reference;
    s->resident.selected_jump_distance = st->selection_distance;
    s->resident.jump_selection_status = st->selection_status;
    s->resident.display_colour_1 = st->status_colour;
    s->resident.display_colour_4 = st->name_colour;
    s->resident.display_colour_2 = st->classification_colour;
    s->resident.display_colour_5 = st->information_colour;
}
