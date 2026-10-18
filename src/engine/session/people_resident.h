/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
/* fe2_people_state <-> the session (s), its objects layer (o) and its resident globals. */
static void people_resident_load(fe2_session *s, fe2_objects_state *o, fe2_people_state *st)
{
    (void)s; (void)o; (void)st;
    st->event_flags = (uint8_t)s->pools.panel_flags;
    st->ship = (fe2_object *)o->player_record;
    st->station = (fe2_object *)s->resident.police_station;
    st->objects = (fe2_object_list *)s->pools.root;
    st->lookup_objects = (fe2_object_list *)s->pools.saved;
    st->player_handle = (uint8_t)o->player;
    st->motion_flags = (uint8_t)o->warnings;
    st->mode = (uint8_t)s->resident.flight_mode;
    st->game_flags = (uint8_t)s->pools.game_flags;
    st->police_flags = (uint8_t)s->resident.police_flags;
    st->launch_phase = (uint8_t)s->resident.launch_phase;
    st->basis_angle[0] = (uint16_t)o->effect_seed[0];
    st->basis_angle[1] = (uint16_t)o->effect_seed[1];
    st->time_of_day = (uint32_t)s->pools.clock[0];
    st->day = (uint32_t)s->pools.clock[1];
    st->system_seed = (uint16_t)s->resident.system_seed;
    st->market_count = (uint16_t)s->resident.market_count;
    st->flagged_count = (uint16_t)s->resident.station_count_copy;
    st->current_system = (uint32_t)s->resident.current_system;
    st->previous_system = (uint32_t)s->resident.previous_systems_2;
    st->jump_value = (uint32_t)s->resident.jump_value;
    st->capacity = (uint16_t)s->pools.player_capacity;
    st->cargo_used = (uint16_t)o->cargo_space;
    st->cargo[0] = (uint16_t)o->cargo[0];
    st->cargo[1] = (uint16_t)o->cargo[1];
    st->cargo[2] = (uint16_t)o->cargo[2];
    st->cargo[3] = (uint16_t)o->cargo[3];
    st->cargo[4] = (uint16_t)o->cargo[4];
    st->cargo[5] = (uint16_t)o->cargo[5];
    st->cargo[6] = (uint16_t)o->cargo[6];
    st->cargo[7] = (uint16_t)o->cargo[7];
    st->cargo[8] = (uint16_t)o->cargo[8];
    st->cargo[9] = (uint16_t)o->cargo[9];
    st->cargo[10] = (uint16_t)o->cargo[10];
    st->cargo[11] = (uint16_t)o->cargo[11];
    st->cargo[12] = (uint16_t)o->cargo[12];
    st->cargo[13] = (uint16_t)o->cargo[13];
    st->cargo[14] = (uint16_t)o->cargo[14];
    st->cargo[15] = (uint16_t)o->cargo[15];
    st->cargo[16] = (uint16_t)o->cargo[16];
    st->cargo[17] = (uint16_t)o->cargo[17];
    st->cargo[18] = (uint16_t)o->cargo[18];
    st->cargo[19] = (uint16_t)o->cargo[19];
    st->cargo[20] = (uint16_t)o->cargo[20];
    st->cargo[21] = (uint16_t)o->cargo[21];
    st->cargo[22] = (uint16_t)o->cargo[22];
    st->cargo[23] = (uint16_t)o->cargo[23];
    st->cargo[24] = (uint16_t)o->cargo[24];
    st->cargo[25] = (uint16_t)o->cargo[25];
    st->cargo[26] = (uint16_t)o->cargo[26];
    st->cargo[27] = (uint16_t)o->cargo[27];
    st->cargo[28] = (uint16_t)o->cargo[28];
    st->cargo[29] = (uint16_t)o->cargo[29];
    st->cargo[30] = (uint16_t)o->cargo[30];
    st->jump_distance = (uint16_t)s->resident.jump_distance_word;
    st->frame_step = (uint32_t)s->simulation_step;
    st->time_step = (uint32_t)s->time_step;
    st->music_preference = (uint8_t)s->resident.music_preference;
    st->music_enabled = (uint8_t)s->resident.alert_music_enabled;
    st->contract_count = (uint16_t)s->resident.mission_count;
    st->emitted_count = (uint8_t)s->resident.emitted_count;
}
static void people_resident_store(fe2_session *s, fe2_objects_state *o, const fe2_people_state *st)
{
    (void)s; (void)o; (void)st;
    s->pools.panel_flags = st->event_flags;
    o->player_record = st->ship;
    s->resident.police_station = st->station;
    s->pools.root = (fe2_object_pool *)st->objects;
    s->pools.saved = (fe2_object_pool *)st->lookup_objects;
    o->player = st->player_handle;
    o->warnings = st->motion_flags;
    s->resident.flight_mode = st->mode;
    s->pools.game_flags = st->game_flags;
    s->resident.police_flags = st->police_flags;
    s->resident.launch_phase = st->launch_phase;
    o->effect_seed[0] = st->basis_angle[0];
    o->effect_seed[1] = st->basis_angle[1];
    s->pools.clock[0] = st->time_of_day;
    s->pools.clock[1] = st->day;
    s->resident.system_seed = st->system_seed;
    s->resident.market_count = st->market_count;
    s->resident.station_count_copy = st->flagged_count;
    s->resident.current_system = st->current_system;
    s->resident.previous_systems_2 = st->previous_system;
    s->resident.jump_value = st->jump_value;
    s->pools.player_capacity = st->capacity;
    o->cargo_space = st->cargo_used;
    o->cargo[0] = st->cargo[0];
    o->cargo[1] = st->cargo[1];
    o->cargo[2] = st->cargo[2];
    o->cargo[3] = st->cargo[3];
    o->cargo[4] = st->cargo[4];
    o->cargo[5] = st->cargo[5];
    o->cargo[6] = st->cargo[6];
    o->cargo[7] = st->cargo[7];
    o->cargo[8] = st->cargo[8];
    o->cargo[9] = st->cargo[9];
    o->cargo[10] = st->cargo[10];
    o->cargo[11] = st->cargo[11];
    o->cargo[12] = st->cargo[12];
    o->cargo[13] = st->cargo[13];
    o->cargo[14] = st->cargo[14];
    o->cargo[15] = st->cargo[15];
    o->cargo[16] = st->cargo[16];
    o->cargo[17] = st->cargo[17];
    o->cargo[18] = st->cargo[18];
    o->cargo[19] = st->cargo[19];
    o->cargo[20] = st->cargo[20];
    o->cargo[21] = st->cargo[21];
    o->cargo[22] = st->cargo[22];
    o->cargo[23] = st->cargo[23];
    o->cargo[24] = st->cargo[24];
    o->cargo[25] = st->cargo[25];
    o->cargo[26] = st->cargo[26];
    o->cargo[27] = st->cargo[27];
    o->cargo[28] = st->cargo[28];
    o->cargo[29] = st->cargo[29];
    o->cargo[30] = st->cargo[30];
    s->resident.jump_distance_word = st->jump_distance;
    s->simulation_step = st->frame_step;
    s->time_step = st->time_step;
    s->resident.music_preference = st->music_preference;
    s->resident.alert_music_enabled = st->music_enabled;
    s->resident.mission_count = st->contract_count;
    s->resident.emitted_count = st->emitted_count;
}
