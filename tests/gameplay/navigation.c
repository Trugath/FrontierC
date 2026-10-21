/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "gameplay/harness.h"

int fe2_check_arcturus(void)
{
    typedef struct { uint16_t y, x, first, count, extra; } catalog_entry;
    extern const catalog_entry map_catalogs[47];
    extern const char *const map_texts[261];
    unsigned catalog, text, index = 0;
    int found = 0;
    uint16_t sector_y = 0, sector_x = 0;
    fe2_map_query query;
    fe2_record_space space;
    fe2_system_generation generated = {0};
    fe2_system_request request = {0};
    fe2_system_record flight[256];
    uint8_t named = 0;
    uint32_t reference;
    for (catalog = 0; catalog < 47 && !found; ++catalog)
        for (text = 0; text < map_catalogs[catalog].count; ++text)
            if (!strcmp(map_texts[map_catalogs[catalog].first + text], "Arcturus")) {
                sector_y = map_catalogs[catalog].y;
                sector_x = map_catalogs[catalog].x;
                index = text;
                found = 1;
                break;
            }
    assert(found);
    reference = ((uint32_t)sector_x << 19) | ((uint32_t)(sector_y & 8191u) << 6) | (index & 63u);
    assert(!fe2_map_query_system(&game.map.state, &game.map.services, reference, 3, &query));
    assert(query.available && !strcmp(query.name, "Arcturus"));
    game.map.state.selected = query.position;
    assert(!fe2_map_select(&game.map.state, &game.map.services, query.index, query.descriptor));
    fe2_map_cached_selection(&game.map.state, &query);
    assert(!game.map.services.generate_system(game.map.services.opaque, &query, &space, &named));
    printf("Arcturus system page: class %u danger %u, %u named\n",
        query.classification, query.status, named);
    for (catalog = 0; catalog < space.count; ++catalog) {
        const fe2_system_record *record = &space.records[catalog];
        if (!fe2_record_dword(record, 0)) break;
        printf("  %s\n", fe2_record_name_const(record));
        assert(strcmp(fe2_record_name_const(record), "Arcturus 2"));
    }
    /* Arrival must use the same danger the system page used. Omitting it
     * builds "Arcturus 2" instead of Hooper. */
    request.seed_a = query.coordinate_mix;
    request.seed_b = query.table_value;
    request.key = query.reference;
    request.limit = query.classification;
    request.danger = query.status;
    request.root_name = query.name;
    memset(flight, 0, sizeof flight);
    fe2_systems_action(&generated, &game.systems.services, (fe2_record_space){flight, 256}, &request);
    for (catalog = 0; catalog < space.count; ++catalog) {
        const fe2_system_record *record = &space.records[catalog];
        const char *name;
        unsigned slot;
        int matched = 0;
        if (!fe2_record_dword(record, 0)) break;
        name = fe2_record_name_const(record);
        for (slot = 0; slot < 256; ++slot) {
            if (!fe2_record_dword(&flight[slot], 0)) break;
            if (!strcmp(name, fe2_record_name_const(&flight[slot]))) matched = 1;
        }
        assert(matched);
    }
    fe2_hyperspace_prepare(&game.hyperspace.state, &game.hyperspace.services);
    assert(!session.error);
    for (catalog = 0; catalog < space.count; ++catalog) {
        const fe2_system_record *record = &space.records[catalog];
        const char *name;
        unsigned slot;
        int matched = 0;
        if (!fe2_record_dword(record, 0)) break;
        name = fe2_record_name_const(record);
        for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
            fe2_object *object = session.objects.records[slot];
            if (!session.objects.status[slot] || !object || object->role != 1) continue;
            if (!strcmp(object->name, name)) matched = 1;
            assert(strcmp(object->name, "Arcturus 2"));
        }
        assert(matched);
    }
    fe2_render_runtime_destroy(runtime);
    return 0;
}

int fe2_check_navigation(void)
{
    unsigned i;
    uint32_t before[3];
    int16_t before_basis[9];
    fe2_navigation_state *nav = &game.navigation.state;
    unsigned count = 0, on_screen = 0, target = 0;
    fe2_scalar scale;
    uint64_t map_time;
    fe2_object live[FE2_POOL_SLOTS], map_before[FE2_POOL_SLOTS];
    char generated_name[32];
    /* Selections 4/5 open the original load-commander screen at startup. */
    if (session.selected_overlay == 10) {
        fe2_gameplay_key(&game, 0x3b, 0, 0); frame(); fe2_gameplay_key(&game, 0x3b, 0, 1); frame();
        assert(session.selected_overlay == 3);
    }
    assert(session.metadata[2].activation == 0xf2 && session.metadata[5].activation == 0xf2);
    fe2_gameplay_key(&game, 0x3f, 0, 0); frame(); fe2_gameplay_key(&game, 0x3f, 0, 1);
    assert(session.selected_overlay == 3 && !session.metadata[5].draw);
    /* Original controls: F2 first opens the galaxy, then current-system orbits. */
    fe2_gameplay_key(&game, 0x3c, 0, 0); frame(); fe2_gameplay_key(&game, 0x3c, 0, 1); frame();
    assert(session.selected_overlay == 2 && game.map.state.mode == -1);
    fe2_gameplay_key(&game, 0x3c, 0, 0); frame(); fe2_gameplay_key(&game, 0x3c, 0, 1);
    for (i = 0; i < 5; ++i) frame();
    assert(session.selected_overlay == 5 && session.metadata[5].update && session.metadata[5].draw);
    assert(nav->flags & FE2_NAV_COPIED_LIST);
    for (i = 1; i < FE2_POOL_SLOTS; ++i) {
        if (!session.map_objects.status[i] || (session.map_objects.status[i] & FE2_SLOT_REUSABLE)) continue;
        ++count;
        assert(session.map_objects.records[i] != session.objects.records[i]);
        if (session.map_records[i].screen_x < 320 && session.map_records[i].screen_y < 168) ++on_screen;
    }
    printf("Navigation: %u copied records, %u visible, %u scenes, %u projections, centre %u\n",
        count, on_screen, game.navigation_scenes, game.navigation_projections, nav->camera.parent_handle);
    assert(count && on_screen && game.navigation_scenes >= 5 && game.navigation_projections);
    snapshot("navigation");
    for (i = 1; i < FE2_POOL_SLOTS; ++i) {
        fe2_object *o = session.map_objects.records[i];
        if (session.map_objects.status[i] & FE2_SLOT_PROJECTED && o->role == 1 &&
            o->screen_x >= 10 && o->screen_x < 310 && o->screen_y >= 10 && o->screen_y < 150) target = i;
    }
    assert(target);
    fe2_input_restore(&session.pointer, 0xfa); frame();
    assert(nav->flags & FE2_NAV_TARGET_PICKING);
    click_icon(session.map_records[target].screen_x, session.map_records[target].screen_y);
    assert(!(nav->flags & FE2_NAV_TARGET_PICKING) && session.resident.navigation_target == target);
    printf("Navigation target: %u\n", target);
    /* Changing map time must not write through the copied record pointers. */
    memcpy(live, session.records, sizeof live);
    memcpy(map_before, session.map_records, sizeof map_before);
    map_time = nav->time + UINT64_C(0x100000000);
    game.navigation.host.set_objects_time(&game, nav->map_objects, map_time);
    assert(!memcmp(live, session.records, sizeof live));
    count = 0;
    for (i = 1; i < FE2_POOL_SLOTS; ++i)
        if (session.map_objects.status[i] &&
            (memcmp(map_before[i].position_low, session.map_records[i].position_low, sizeof before) ||
             memcmp(map_before[i].position_high, session.map_records[i].position_high, sizeof before))) ++count;
    assert(count);
    nav->time = map_time;
    fe2_gameplay_key(&game, 0x4d, 1, 0); frame(); fe2_gameplay_key(&game, 0x4d, 1, 1);
    assert(nav->time > map_time && nav->time != fe2_session_time(&session));
    map_time = nav->time;
    fe2_gameplay_key(&game, 0x4b, 1, 0); frame(); fe2_gameplay_key(&game, 0x4b, 1, 1);
    assert(nav->time < map_time);
    map_time = nav->time;
    click_icon(316, 182);
    assert(nav->time > map_time);
    printf("Navigation time controls preserve live records\n");
    scale = nav->scale;
    fe2_input_restore(&session.pointer, 0xf8); frame();
    assert(memcmp(&scale, &nav->scale, sizeof scale));
    fe2_input_restore(&session.pointer, 0xf7); frame();
    fe2_input_restore(&session.pointer, 0xf9); frame();
    assert(nav->flags & FE2_NAV_OPTIONS_OPEN);
    fe2_input_restore(&session.pointer, 0xf9); frame();
    assert(nav->flags & FE2_NAV_HIDE_PROJECTIONS);
    count = game.navigation_projections;
    for (i = 0; i < 3; ++i) frame();
    assert(game.navigation_projections == count);
    click_icon(293, 194);
    assert(nav->flags & FE2_NAV_OPTIONS_OPEN);
    click_icon(310, 194);
    assert((nav->flags & FE2_NAV_HIDE_LABELS) && !(nav->flags & FE2_NAV_OPTIONS_OPEN));
    memcpy(before_basis, nav->camera.orientation, sizeof before_basis);
    fe2_gameplay_mouse(&game, 160, 90, 2); frame();
    fe2_gameplay_mouse(&game, 190, 105, 2); frame();
    fe2_gameplay_mouse(&game, 190, 105, 0); frame();
    assert(memcmp(before_basis, nav->camera.orientation, sizeof before_basis));
    assert(session.pointer.x == 190 && session.pointer.y == 105);
    scale = nav->scale;
    fe2_input_restore(&session.pointer, 0xf1); frame();
    assert(!session.metadata[5].update && !session.metadata[5].draw && session.selected_overlay == 3);
    click_icon(24, 193);
    assert(session.selected_overlay == 2);
    click_icon(24, 193);
    assert(!memcmp(&scale, &nav->scale, sizeof scale));
    fe2_input_restore(&session.pointer, 0xf1); frame();
    fe2_input_restore(&session.pointer, 0xf2); frame(); frame();
    if (selection == 1) {
        fe2_gameplay_key(&game, 0x50, 0, 0);
        for (i = 0; i < 10; ++i) frame();
        fe2_gameplay_key(&game, 0x50, 0, 1); frame();
        assert(!strcmp(game.map.state.selected_name, "BD 4523"));
    }
    snprintf(generated_name, sizeof generated_name, "%s", game.map.state.selected_name);
    fe2_input_restore(&session.pointer, 0xf6); frame();
    assert(game.map.state.mode == 1);
    fe2_input_restore(&session.pointer, 0xfa); frame(); frame();
    assert(session.selected_overlay == 5 && session.metadata[5].draw && !(nav->flags & FE2_NAV_COPIED_LIST));
    snapshot("navigation-generated");
    count = 0;
    for (i = 1; i < FE2_POOL_SLOTS; ++i) if (session.map_objects.status[i]) ++count;
    printf("Navigation generated system: %u records\n", count);
    assert(count && !game.text_failures);
    printf("Navigation system name: %s (selected %s)\n", session.map_records[114].name, generated_name);
    assert(!strncmp(session.map_records[114].name, generated_name, strlen(generated_name)));
    fe2_input_restore(&session.pointer, 0xf2); frame();
    assert(session.selected_overlay == 2 && !session.metadata[5].draw);
    fe2_input_restore(&session.pointer, 0xf1); frame();
    assert(session.selected_overlay == 3);
    for (i = 0; i < FE2_SESSION_OVERLAYS; ++i) assert(!session.locks[i]);
    printf("Navigation controls and return to flight passed\n");
    return 0;
}

int fe2_check_lave(void)
{
    unsigned i;
    uint32_t before[3];
    uint32_t throttle;
    fe2_station_state *st = &game.station.state;
    assert(selection == 0x201 && session.resident.flight_mode == 0x54);
    station_open();
    printf("Lave station: %u rows, first %04x\n", st->row_count, st->rows[0].text);
    assert(row_has(&game, 0x9818));
    station_row(1);
    printf("Lave launch: message %04x, mode %02x\n", st->message.text, session.resident.flight_mode);
    assert(st->message.text == 0x98a4);
    for (i = 0; i < 1000; ++i) frame();
    printf("Lave departure: mode %02x, station slot %u\n", session.resident.flight_mode, ship->station_slot);
    assert(session.resident.flight_mode <= 0x4c);
    memcpy(before, ship->position_low, sizeof before);
    throttle = session.resident.throttle;
    fe2_gameplay_key(&game, 0x1c, 0, 0);
    for (i = 0; i < 100; ++i) frame();
    fe2_gameplay_key(&game, 0x1c, 0, 1);
    assert(session.resident.throttle != throttle && memcmp(before, ship->position_low, sizeof before));
    printf("Lave flight: throttle and position changed\n");
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_jump(void)
{
    unsigned i;
    /* Hyperspace: lift off, leave the planet behind (the ship is moved
     * out rather than flown), select BD 4523 on the galactic map (pan
     * down; 7.02 light years, in the starting drive's 8), then F8. */
    fe2_object *ship;
    uint32_t from = game.hyperspace.state.current_system, target;
    unsigned k, bodies = 0, before = occupied(&session);
    fe2_input_restore(&session.pointer, 0xf7); frame();
    for (i = 0; i < 100; ++i) frame();
    ship = session.objects.records[game.objects.state.player];
    ship->position_high[2] += 0x100;
    for (i = 0; i < 50; ++i) frame();
    printf("Departure: mode %02x, measured %x\n", session.resident.flight_mode, session.resident.jump_clock);
    assert(session.resident.flight_mode == 0x48 && session.resident.jump_clock >= 0xb71b00);
    fe2_input_restore(&session.pointer, 0xf2);
    for (i = 0; i < 5; ++i) frame();
    fe2_gameplay_key(&game, 0x50, 0, 0);
    for (i = 0; i < 10; ++i) frame();
    fe2_gameplay_key(&game, 0x50, 0, 1);
    for (i = 0; i < 5; ++i) frame();
    target = game.map.state.selection_reference;
    printf("Target: %s, %u.%02u light years, status %02x\n", game.map.state.selected_name,
           session.resident.selected_jump_distance / 100, session.resident.selected_jump_distance % 100, session.resident.jump_selection_status);
    assert(!strcmp(game.map.state.selected_name, "BD 4523") && session.resident.jump_selection_status);
    fe2_input_restore(&session.pointer, 0xf1); frame();
    fe2_input_restore(&session.pointer, 0xf8);
    for (i = 0; i < 1000; ++i) frame();
    for (k = 1; k < FE2_POOL_SLOTS; ++k)
        if (session.objects.status[k] && session.objects.records[k]->role == 1 &&
            session.objects.records[k] != ship) ++bodies;
    printf("Arrival: %s, system %08x -> %08x, mode %02x, %u live objects (from %u), %u bodies, %u craft\n",
           game.place_name, from, game.hyperspace.state.current_system, session.resident.flight_mode,
           occupied(&session), before, bodies, population(&session));
    assert(!strcmp(game.place_name, "BD 4523"));
    assert(game.hyperspace.state.current_system == target && target != from);
    assert(session.resident.flight_mode != 0x48 && bodies > 0 && !session.error);
    /* Arrival stores the system-map scale (mantissa 0x7fff, exponent 0x33). */
    assert(session.resident.map_saved_scale == 0x337fffu);
    fe2_gameplay_key(&game, 0x3c, 0, 0); frame(); fe2_gameplay_key(&game, 0x3c, 0, 1); frame();
    fe2_gameplay_key(&game, 0x3c, 0, 0); frame(); fe2_gameplay_key(&game, 0x3c, 0, 1);
    for (i = 0; i < 5; ++i) frame();
    assert(session.selected_overlay == 5 && (game.navigation.state.flags & FE2_NAV_COPIED_LIST));
    assert(game.navigation.state.scale.mantissa == 0x7fff && game.navigation.state.scale.exponent == 0x33);
    printf("System map after jump: scale %04x exponent %02x\n",
           game.navigation.state.scale.mantissa, game.navigation.state.scale.exponent);
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_dest(void)
{
    /* Manual flight, a galactic destination, then the jump. Mode 00 is the
     * only cockpit mode that queries the drive for that destination. */
    unsigned step;
    fe2_object *ship;
    fe2_input_restore(&session.pointer, 0xf7); frame();
    for (step = 0; step < 40; ++step) frame();
    ship = session.objects.records[game.objects.state.player];
    ship->position_high[2] += 0x100;
    for (step = 0; step < 20; ++step) frame();
    fe2_input_restore(&session.pointer, 0xf7); frame();
    fe2_input_restore(&session.pointer, 0xf7); frame();
    fe2_input_restore(&session.pointer, 0xf7); frame();
    assert(session.resident.flight_mode == 0);
    fe2_input_restore(&session.pointer, 0xf2); frame();
    fe2_gameplay_key(&game, 0x50, 0, 0);
    for (step = 0; step < 10; ++step) frame();
    fe2_gameplay_key(&game, 0x50, 0, 1);
    for (step = 0; step < 3; ++step) frame();
    printf("Destination %s, %u.%02u ly, status %02x, mode %02x\n",
        game.map.state.selected_name, session.resident.selected_jump_distance / 100,
        session.resident.selected_jump_distance % 100, session.resident.jump_selection_status, session.resident.flight_mode);
    assert(!strcmp(game.map.state.selected_name, "BD 4523") && session.resident.jump_selection_status);
    assert(!session.error && !host.errors && game.map.status == 0);
    fe2_input_restore(&session.pointer, 0xf1); frame();
    fe2_input_restore(&session.pointer, 0xf8);
    for (step = 0; step < 40; ++step) frame();
    printf("Jump: %s mode %02x system %08x\n", game.place_name, session.resident.flight_mode,
        game.hyperspace.state.current_system);
    assert(!strcmp(game.place_name, "BD 4523"));
    assert(session.resident.flight_mode == 0x08 && !session.error && !host.errors);
    fe2_render_runtime_destroy(runtime); return 0;
}
