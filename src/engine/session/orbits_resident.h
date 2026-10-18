/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
/* fe2_orbit_state <-> the session (s), its objects layer (o) and its resident globals. */
static void orbits_resident_load(fe2_session *s, fe2_objects_state *o, fe2_orbit_state *st)
{
    (void)s; (void)o; (void)st;
    st->frame_step = (uint32_t)s->simulation_step;
    st->basis_angle[0] = (uint16_t)o->effect_seed[0];
    st->basis_angle[1] = (uint16_t)o->effect_seed[1];
    st->reference_index = (uint8_t)s->resident.body_reference_index;
    st->player_handle = (uint8_t)o->player;
    st->flight_state = (uint8_t)s->resident.flight_mode;
}
static void orbits_resident_store(fe2_session *s, fe2_objects_state *o, const fe2_orbit_state *st)
{
    (void)s; (void)o; (void)st;
    s->simulation_step = st->frame_step;
    o->effect_seed[0] = st->basis_angle[0];
    o->effect_seed[1] = st->basis_angle[1];
    s->resident.body_reference_index = st->reference_index;
    o->player = st->player_handle;
    s->resident.flight_mode = st->flight_state;
}
