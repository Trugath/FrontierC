/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
/* fe2_hyperspace_state <-> the session (s), its objects layer (o) and its resident globals. */
static void hyperspace_resident_load(fe2_session *s, fe2_objects_state *o, fe2_hyperspace_state *st)
{
    (void)s; (void)o; (void)st;
    st->ship = (fe2_object *)o->player_record;
    st->player_handle = (uint8_t)o->player;
    st->flight_state = (uint8_t)s->resident.flight_mode;
    st->cargo_capacity = (uint16_t)s->pools.player_capacity;
    st->cargo_used = (uint16_t)o->cargo_space;
    st->hydrogen_fuel = (uint16_t)o->cargo[19];
    st->military_fuel = (uint16_t)o->cargo[20];
    st->radioactives = (uint16_t)o->cargo[29];
    st->rubbish = (uint16_t)o->cargo[30];
    st->jump_range = (uint16_t)s->resident.jump_range;
    st->jump_enabled = (uint8_t)s->resident.jump_selection_status;
    st->jump_clock = (uint32_t)s->resident.jump_clock;
    st->jump_distance = (uint32_t)s->resident.selected_jump_distance;
    st->jump_distance_word = (uint16_t)s->resident.jump_distance_word;
    st->jump_value = (uint32_t)s->resident.jump_selection_reference;
    st->jump_value_copy = (uint32_t)s->resident.jump_value;
    st->jump_time = (uint32_t)s->resident.jump_time;
    st->published_jump_time = (uint32_t)s->resident.jump_countdown;
    st->misjump_guard = (uint8_t)s->input.keys[56];
    st->time_of_day = (uint32_t)s->pools.clock[0];
    st->day = (uint32_t)s->pools.clock[1];
    st->local_record = (uint32_t)s->resident.local_offence_fine;
    st->criminal_record[0] = (uint32_t)s->resident.criminal_record_0;
    st->criminal_record[1] = (uint32_t)s->resident.criminal_record_1;
    st->criminal_record[2] = (uint32_t)s->resident.criminal_record_2;
    st->legal_authority = (uint16_t)s->resident.legal_allegiance;
    st->navigation_target = (uint8_t)s->resident.navigation_target;
    st->fined_handle = (uint8_t)s->resident.fined_handle;
    st->throttle = (uint32_t)s->resident.throttle;
    st->pool_flag_count = (uint16_t)s->pools.reusable_count;
    st->map_saved_scale = s->resident.map_saved_scale;
    st->map_default_selection = (uint8_t)s->resident.map_default_selection;
    st->simulation_time_step = (uint32_t)s->time_step;
    st->game_time_exponent = (uint16_t)s->resident.game_time_exponent;
    st->current_system = (uint32_t)s->resident.current_system;
    st->previous_systems[0] = (uint32_t)s->resident.previous_systems_0;
    st->previous_systems[1] = (uint32_t)s->resident.previous_systems_1;
    st->previous_systems[2] = (uint32_t)s->resident.previous_systems_2;
    st->station_count_copy = (uint16_t)s->resident.station_count_copy;
    st->emitted_limit = (uint8_t)s->resident.emitted_limit;
    st->reference_index = (uint8_t)s->resident.body_reference_index;
    st->objects = (fe2_object_list *)s->pools.root;
}
static void hyperspace_resident_store(fe2_session *s, fe2_objects_state *o, const fe2_hyperspace_state *st)
{
    (void)s; (void)o; (void)st;
    o->player_record = st->ship;
    o->player = st->player_handle;
    s->resident.flight_mode = st->flight_state;
    s->pools.player_capacity = st->cargo_capacity;
    o->cargo_space = st->cargo_used;
    o->cargo[19] = st->hydrogen_fuel;
    o->cargo[20] = st->military_fuel;
    o->cargo[29] = st->radioactives;
    o->cargo[30] = st->rubbish;
    s->resident.jump_range = st->jump_range;
    s->resident.jump_selection_status = st->jump_enabled;
    s->resident.jump_clock = st->jump_clock;
    s->resident.selected_jump_distance = st->jump_distance;
    s->resident.jump_distance_word = st->jump_distance_word;
    s->resident.jump_selection_reference = st->jump_value;
    s->resident.jump_value = st->jump_value_copy;
    s->resident.jump_time = st->jump_time;
    s->resident.jump_countdown = st->published_jump_time;
    s->input.keys[56] = st->misjump_guard;
    s->pools.clock[0] = st->time_of_day;
    s->pools.clock[1] = st->day;
    s->resident.local_offence_fine = st->local_record;
    s->resident.criminal_record_0 = st->criminal_record[0];
    s->resident.criminal_record_1 = st->criminal_record[1];
    s->resident.criminal_record_2 = st->criminal_record[2];
    s->resident.legal_allegiance = st->legal_authority;
    s->resident.navigation_target = st->navigation_target;
    s->resident.fined_handle = st->fined_handle;
    s->resident.throttle = st->throttle;
    s->pools.reusable_count = st->pool_flag_count;
    s->resident.map_saved_scale = st->map_saved_scale;
    s->resident.map_default_selection = st->map_default_selection;
    s->time_step = st->simulation_time_step;
    s->resident.game_time_exponent = st->game_time_exponent;
    s->resident.current_system = st->current_system;
    s->resident.previous_systems_0 = st->previous_systems[0];
    s->resident.previous_systems_1 = st->previous_systems[1];
    s->resident.previous_systems_2 = st->previous_systems[2];
    s->resident.station_count_copy = st->station_count_copy;
    s->resident.emitted_limit = st->emitted_limit;
    s->resident.body_reference_index = st->reference_index;
    s->pools.root = (fe2_object_pool *)st->objects;
}
