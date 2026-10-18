/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
/* fe2_intro_state <-> the session (s), its objects layer (o) and its resident globals. */
static void intro_resident_load(fe2_session *s, fe2_objects_state *o, fe2_intro_state *st)
{
    (void)s; (void)o; (void)st;
    st->time = (uint32_t)s->pools.clock[0];
    st->overlay_mode = (uint8_t)s->drawing_flag;
    st->flags = (uint8_t)s->pools.game_flags;
    st->render_vector[0] = (uint16_t)s->light[0];
    st->render_vector[1] = (uint16_t)s->light[1];
    st->render_vector[2] = (uint16_t)s->light[2];
    st->classification[0] = (uint32_t)s->resident.classification_0;
    st->classification[1] = (uint32_t)s->resident.classification_1;
    st->classification[2] = (uint32_t)s->resident.classification_2;
    st->classification[3] = (uint32_t)s->resident.classification_3;
    st->return_mode = (uint8_t)s->input.keys[57];
}
static void intro_resident_store(fe2_session *s, fe2_objects_state *o, const fe2_intro_state *st)
{
    (void)s; (void)o; (void)st;
    s->pools.clock[0] = st->time;
    s->drawing_flag = st->overlay_mode;
    s->pools.game_flags = st->flags;
    s->light[0] = st->render_vector[0];
    s->light[1] = st->render_vector[1];
    s->light[2] = st->render_vector[2];
    s->resident.classification_0 = st->classification[0];
    s->resident.classification_1 = st->classification[1];
    s->resident.classification_2 = st->classification[2];
    s->resident.classification_3 = st->classification[3];
    s->input.keys[57] = st->return_mode;
}
