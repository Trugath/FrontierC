/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
/* fe2_offence_state <-> the session (s), its objects layer (o) and its resident globals. */
static void offences_resident_load(fe2_session *s, fe2_objects_state *o, fe2_offence_state *st)
{
    (void)s; (void)o; (void)st;
    st->police_flags = (uint8_t)s->resident.police_flags;
    st->launch_phase = (uint8_t)s->resident.launch_phase;
    st->police_source = (fe2_object *)s->resident.police_station;
    st->local_fine = (uint32_t)s->resident.local_offence_fine;
    st->legal_authority = (uint16_t)s->resident.legal_allegiance;
    st->offence_score[0] = (uint16_t)s->resident.offence_score_0;
    st->offence_score[1] = (uint16_t)s->resident.offence_score_1;
    st->offence_score[2] = (uint16_t)s->resident.offence_score_2;
    st->player_handle = (uint8_t)o->player;
    st->fined_handle = (uint8_t)s->resident.fined_handle;
    st->alert_music = (uint8_t)s->resident.alert_music_enabled;
    st->frame_step = (uint32_t)s->simulation_step;
    st->smuggler = (fe2_object *)s->resident.reference_object;
    st->expected_letter = (uint8_t)s->resident.expected_letter;
    st->objects = (fe2_object_list *)s->pools.root;
    st->creation_objects = (fe2_object_list *)s->pools.saved;
}
static void offences_resident_store(fe2_session *s, fe2_objects_state *o, const fe2_offence_state *st)
{
    (void)s; (void)o; (void)st;
    s->resident.police_flags = st->police_flags;
    s->resident.launch_phase = st->launch_phase;
    s->resident.police_station = st->police_source;
    s->resident.local_offence_fine = st->local_fine;
    s->resident.legal_allegiance = st->legal_authority;
    s->resident.offence_score_0 = st->offence_score[0];
    s->resident.offence_score_1 = st->offence_score[1];
    s->resident.offence_score_2 = st->offence_score[2];
    o->player = st->player_handle;
    s->resident.fined_handle = st->fined_handle;
    s->resident.alert_music_enabled = st->alert_music;
    s->simulation_step = st->frame_step;
    s->resident.reference_object = st->smuggler;
    s->resident.expected_letter = st->expected_letter;
    s->pools.root = (fe2_object_pool *)st->objects;
    s->pools.saved = (fe2_object_pool *)st->creation_objects;
}
