/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native session adapter; keep aligned with the named overlay state and services. */
/* fe2_market_state <-> the session (s), its objects layer (o) and its resident globals. */
static void market_resident_load(fe2_session *s, fe2_objects_state *o, fe2_market_state *st)
{
    (void)s; (void)o; (void)st;
    st->market_count = (uint16_t)s->resident.market_count;
    st->trade_codes[0] = (uint8_t)s->resident.trade_codes_0;
    st->trade_codes[1] = (uint8_t)s->resident.trade_codes_1;
    st->trade_codes[2] = (uint8_t)s->resident.trade_codes_2;
    st->trade_codes[3] = (uint8_t)s->resident.trade_codes_3;
    st->trade_codes[4] = (uint8_t)s->resident.trade_codes_4;
    st->trade_codes[5] = (uint8_t)s->resident.trade_codes_5;
    st->trade_codes[6] = (uint8_t)s->resident.trade_codes_6;
    st->trade_codes[7] = (uint8_t)s->resident.trade_codes_7;
    st->trade_codes[8] = (uint8_t)s->resident.trade_codes_8;
    st->trade_codes[9] = (uint8_t)s->resident.trade_codes_9;
    st->trade_codes[10] = (uint8_t)s->resident.trade_codes_10;
    st->trade_codes[11] = (uint8_t)s->resident.trade_codes_11;
    st->trade_codes[12] = (uint8_t)s->resident.trade_codes_12;
    st->trade_codes[13] = (uint8_t)s->resident.trade_codes_13;
    st->trade_codes[14] = (uint8_t)s->resident.trade_codes_14;
    st->trade_codes[15] = (uint8_t)s->resident.trade_codes_15;
    st->trade_codes[16] = (uint8_t)s->resident.trade_codes_16;
    st->trade_codes[17] = (uint8_t)s->resident.trade_codes_17;
    st->trade_codes[18] = (uint8_t)s->resident.trade_codes_18;
    st->trade_codes[19] = (uint8_t)s->resident.trade_codes_19;
    st->trade_codes[20] = (uint8_t)s->resident.trade_codes_20;
    st->trade_codes[21] = (uint8_t)s->resident.trade_codes_21;
    st->trade_codes[22] = (uint8_t)s->resident.trade_codes_22;
    st->trade_codes[23] = (uint8_t)s->resident.trade_codes_23;
    st->trade_codes[24] = (uint8_t)s->resident.trade_codes_24;
    st->trade_codes[25] = (uint8_t)s->resident.trade_codes_25;
    st->trade_codes[26] = (uint8_t)s->resident.trade_codes_26;
    st->trade_codes[27] = (uint8_t)s->resident.trade_codes_27;
    st->trade_codes[28] = (uint8_t)s->resident.trade_codes_28;
    st->trade_codes[29] = (uint8_t)s->resident.trade_codes_29;
    st->trade_codes[30] = (uint8_t)s->resident.trade_codes_30;
    st->resource_richness = (uint16_t)s->resident.system_seed;
    st->allegiance = (uint16_t)s->resident.legal_allegiance;
    st->day = (uint32_t)s->pools.clock[1];
    st->random_state_seed = (uint32_t)s->noise[0];
    st->current_system = (uint32_t)s->resident.current_system;
    st->player = (uint8_t)o->player;
    st->player_slot = (uint8_t)s->resident.player_contact_class;
    st->flight_state = (uint8_t)s->resident.flight_mode;
    st->event_base = (uint32_t)s->resident.event_base;
    st->local_offence_fine = (uint32_t)s->resident.local_offence_fine;
    st->music_enabled = (uint8_t)s->resident.music_enabled;
    st->current_music = (uint16_t)s->current_music;
    st->objects = (fe2_object_list *)s->pools.root;
}
static void market_resident_store(fe2_session *s, fe2_objects_state *o, const fe2_market_state *st)
{
    (void)s; (void)o; (void)st;
    s->resident.market_count = st->market_count;
    s->resident.trade_codes_0 = st->trade_codes[0];
    s->resident.trade_codes_1 = st->trade_codes[1];
    s->resident.trade_codes_2 = st->trade_codes[2];
    s->resident.trade_codes_3 = st->trade_codes[3];
    s->resident.trade_codes_4 = st->trade_codes[4];
    s->resident.trade_codes_5 = st->trade_codes[5];
    s->resident.trade_codes_6 = st->trade_codes[6];
    s->resident.trade_codes_7 = st->trade_codes[7];
    s->resident.trade_codes_8 = st->trade_codes[8];
    s->resident.trade_codes_9 = st->trade_codes[9];
    s->resident.trade_codes_10 = st->trade_codes[10];
    s->resident.trade_codes_11 = st->trade_codes[11];
    s->resident.trade_codes_12 = st->trade_codes[12];
    s->resident.trade_codes_13 = st->trade_codes[13];
    s->resident.trade_codes_14 = st->trade_codes[14];
    s->resident.trade_codes_15 = st->trade_codes[15];
    s->resident.trade_codes_16 = st->trade_codes[16];
    s->resident.trade_codes_17 = st->trade_codes[17];
    s->resident.trade_codes_18 = st->trade_codes[18];
    s->resident.trade_codes_19 = st->trade_codes[19];
    s->resident.trade_codes_20 = st->trade_codes[20];
    s->resident.trade_codes_21 = st->trade_codes[21];
    s->resident.trade_codes_22 = st->trade_codes[22];
    s->resident.trade_codes_23 = st->trade_codes[23];
    s->resident.trade_codes_24 = st->trade_codes[24];
    s->resident.trade_codes_25 = st->trade_codes[25];
    s->resident.trade_codes_26 = st->trade_codes[26];
    s->resident.trade_codes_27 = st->trade_codes[27];
    s->resident.trade_codes_28 = st->trade_codes[28];
    s->resident.trade_codes_29 = st->trade_codes[29];
    s->resident.trade_codes_30 = st->trade_codes[30];
    s->resident.system_seed = st->resource_richness;
    s->resident.legal_allegiance = st->allegiance;
    s->pools.clock[1] = st->day;
    s->noise[0] = st->random_state_seed;
    s->resident.current_system = st->current_system;
    o->player = st->player;
    s->resident.player_contact_class = st->player_slot;
    s->resident.flight_mode = st->flight_state;
    s->resident.event_base = st->event_base;
    s->resident.local_offence_fine = st->local_offence_fine;
    s->resident.music_enabled = st->music_enabled;
    s->current_music = st->current_music;
    s->pools.root = (fe2_object_pool *)st->objects;
}
