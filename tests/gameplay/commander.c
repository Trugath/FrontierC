/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "gameplay/harness.h"

void fe2_check_intro(void)
{
    unsigned i;
    fe2_game_host_intro_services(&host, &intro.host);
    assert(!fe2_session_bind_intro_overlay(&session, &intro));
    assert(!fe2_session_startup(&session) && !fe2_session_intro(&session));
    session.input_mask = 0xff;
    for (i = 0; i < 120000; ++i) frame();
    fe2_input_restore(&session.pointer, '1');
    fe2_session_ticks(&session, 4);
    assert(fe2_session_step(&session) == FE2_SESSION_STARTED);
}

int fe2_check_original(void)
{
    const char *name = strrchr(check_argv[3], '\\');
    unsigned k, selected = FE2_COMMANDER_ENTRIES;
    if (!name) name = strrchr(check_argv[3], '/');
    name = name ? name + 1 : check_argv[3];
    for (k = 0; k < 2; ++k) {
        fe2_gameplay_key(&game, 1, 0, 0); frame();
        fe2_gameplay_key(&game, 1, 0, 1); frame();
    }
    fe2_input_restore(&session.pointer, 0xf7); frame();
    for (k = 0; k < game.menu.state.entry_count; ++k)
        if (game.menu.state.entries[k].kind == FE2_ENTRY_FILE && !strcmp(game.menu.state.entries[k].name, name)) selected = k;
    assert(selected < FE2_COMMANDER_ENTRIES);
    fe2_input_restore(&session.pointer, (uint8_t)(0x80 + selected)); frame();
    assert(restarts == 1 && !session.error && !host.errors);
    ship = session.objects.records[game.objects.state.player];
    assert(ship && ship->role == 3);
    printf("Original commander: %u live objects, cash %u, player %u, model %x, render %x\n",
        occupied(&session), game.objects.state.bounty, ship->handle, ship->model, session.render_mode);
    for (k = 0; k < 120; ++k) frame();
    assert(!game.text_failures && !session.error && !host.errors);
    if (check_argc > 4) {
        static uint8_t encoded[FE2_COMMANDER_BUFFER_BYTES];
        char name[32];
        size_t length;
        FILE *input, *output;
        snprintf(name, sizeof name, "%s%s", game.menu.state.commander_name, game.menu.state.extension);
        for (k = 0; k < sizeof name && name[k]; ++k)
            if (name[k] >= 'a' && name[k] <= 'z') name[k] = (char)(name[k] - 'a' + 'A');
        for (k = 0; k < 2; ++k) {
            fe2_gameplay_key(&game, 1, 0, 0); frame();
            fe2_gameplay_key(&game, 1, 0, 1); frame();
        }
        fe2_input_restore(&session.pointer, 0xf8); frame();
        fe2_input_restore(&session.pointer, 0x0d); frame();
        input = fopen(name, "rb"); assert(input);
        length = fread(encoded, 1, sizeof encoded, input); fclose(input);
        output = fopen(check_argv[4], "wb");
        assert(output && length > 6 && fwrite(encoded, 1, length, output) == length);
        fclose(output);
        printf("Native commander exported: %zu bytes\n", length);
    }
    printf("Original commander continued for 120 native frames\n");
    fe2_render_runtime_destroy(runtime);
    return 0;
}

int fe2_check_commander(void)
{
    unsigned i;
    /* Commander files (M10): save in flight, change the game, load the
     * file back. The working directory is the DOS drive. */
    fe2_object *ship;
    uint32_t balance, position[3], system;
    unsigned live;
    static uint8_t saved_objects[sizeof game.image.objects], saved_resident[sizeof game.image.resident];
    static uint8_t saved_market[sizeof game.market.state.markets.bytes];
    uint16_t cargo;
    unsigned k, file = 0;
    FILE *f;
    uint8_t first[0x10000], second[0x10000];
    size_t first_length, second_length;
    remove("JAMESON._01"); remove("JAMESON._02");
    fe2_input_restore(&session.pointer, 0xf7); frame();
    for (i = 0; i < 100; ++i) frame();
    ship = session.objects.records[game.objects.state.player];
    game.objects.state.cargo[3] = 7;
    game.mining.state.records.bytes[sizeof game.mining.state.records.bytes - 1] = 0x5a;
    /* Opaque file bytes survive native load/save even though they have
     * no simulation field: the directory pad and saved stack slice. */
    game.image.objects[0x73] = 0xa5;
    for (k = 0; k < sizeof game.image.stack; ++k) game.image.stack[k] = (uint8_t)(k * 13u + 7u);
    {
        fe2_contract contract = {42, 0x1c, 1, 0xa91dc5c1, session.pools.clock[1] + 100, 0x12345678};
        fe2_session_status_add_contract(&game.status, &contract);
    }
    assert(game.status.state.contract_count == 1);
    /* Distinct from each other, and still above the failure roll. A save
     * must not clear the equipment counter where the uppercase name ends. */
    game.objects.state.equipment_threshold = 0x1e1;
    game.objects.state.drive_threshold = 0x1e2;
    game.objects.state.servicing = 0x10000000u;
    balance = game.objects.state.bounty; cargo = game.objects.state.cargo[3];
    for (k = 0; k < 3; ++k) position[k] = ship->position_high[k];
    live = occupied(&session);
    system = game.hyperspace.state.current_system;
    /* Escape twice opens the preferences; F8 the save dialog; Enter saves. */
    for (k = 0; k < 2; ++k) { fe2_gameplay_key(&game, 1, 0, 0); frame(); fe2_gameplay_key(&game, 1, 0, 1); frame(); }
    fe2_input_restore(&session.pointer, 0xf8); frame();
    {
        fe2_video *v = fe2_render_runtime_video(runtime);
        unsigned x, y, ink = 0;
        uint8_t colour = game.menu.state.colours[4];
        assert(game.menu.state.menu_mode == FE2_MENU_SAVE && colour);
        for (y = 20; y < 80; ++y) for (x = 0; x < 80; ++x)
            if (v->pages[v->drawing][y * 320u + x] == colour) ++ink;
        printf("Save dialog: name ink %u colour %u entries %u first %u %.8s\n", ink, colour,
            game.menu.state.entry_count, game.menu.state.entries[0].kind,
            game.menu.state.entries[0].name);
        assert(ink > 0);
    }
    fe2_input_restore(&session.pointer, 0x0d); frame();
    f = fopen("JAMESON._01", "rb");
    assert(f);
    first_length = fread(first, 1, sizeof first, f); fclose(f);
    assert(first_length > 6 && first[0] == 0x11 && first[1] == 0);
    memcpy(saved_objects, game.image.objects, sizeof saved_objects);
    memcpy(saved_resident, game.image.resident, sizeof saved_resident);
    memcpy(saved_market, game.market.state.markets.bytes, sizeof saved_market);
    printf("Saved: JAMESON._01, %zu bytes, balance %u, cargo %u, system %08x\n", first_length, balance, cargo,
           system);
    /* The same name is refused. The notice has to return on a key; the
     * frame is inside that wait, so the pump supplies it. */
    strcpy(game.menu.state.extension, "._01");
    host.pump = sentence_key;
    fe2_input_restore(&session.pointer, 0x0d); frame();
    host.pump = NULL;
    assert(game.menu.state.menu_mode == FE2_MENU_SAVE);
    f = fopen("JAMESON._01", "rb");
    assert(f);
    assert(fread(second, 1, sizeof second, f) == first_length);
    fclose(f);
    assert(!memcmp(first, second, first_length));
    printf("Save overwrite dismissed\n");
    /* The game changes: cash, cargo, the ship moves on. */
    game.objects.state.bounty = 5000; game.objects.state.cargo[3] = 0;
    game.status.state.contracts[0].fee = 999;
    game.status.state.contracts[0].destination = 0;
    memset(game.market.state.markets.bytes, 0, sizeof game.market.state.markets.bytes);
    game.mining.state.records.bytes[sizeof game.mining.state.records.bytes - 1] = 0;
    ship->position_high[0] += 3;
    fe2_input_restore(&session.pointer, 0xf7); frame();
    for (k = 0; k < game.menu.state.entry_count; ++k)
        if (game.menu.state.entries[k].kind == FE2_ENTRY_FILE &&
            !strcmp(game.menu.state.entries[k].name, "JAMESON._01")) file = k;
    assert(game.menu.state.entries[file].kind == FE2_ENTRY_FILE);
    fe2_input_restore(&session.pointer, (uint8_t)(0x80 + file));
    for (i = 0; i < 10; ++i) frame();
    ship = session.objects.records[game.objects.state.player];
    printf("Loaded: restarts %u, menu %d, balance %u, cargo %u, system %08x, position %s, %u live objects (%u)\n",
           restarts, game.menu.state.menu_mode, game.objects.state.bounty, game.objects.state.cargo[3],
           game.hyperspace.state.current_system,
           ship->position_high[0] == position[0] && ship->position_high[1] == position[1] &&
           ship->position_high[2] == position[2] ? "restored" : "lost", occupied(&session), live);
    assert(restarts == 1 && game.objects.state.bounty == balance && game.objects.state.cargo[3] == cargo);
    assert(occupied(&session) == live && ship->position_high[0] == position[0]);
    assert(game.hyperspace.state.current_system == system);
    assert(game.objects.state.equipment_threshold == 0x1e1);
    assert(game.objects.state.drive_threshold == 0x1e2);
    printf("Service counters restored\n");
    assert(game.status.state.contracts[0].fee == 42 && game.status.state.contracts[0].destination == 0xa91dc5c1);
    assert(game.status.state.contracts[0].client == 0x12345678 && game.status.state.contract_count == 1);
    printf("Commander contract restored\n");
    assert(!memcmp(saved_market, game.market.state.markets.bytes, sizeof saved_market));
    assert(game.mining.state.records.bytes[sizeof game.mining.state.records.bytes - 1] == 0x5a);
    printf("Market and mining records restored\n");
    assert(game.image.objects[0x73] == 0xa5);
    for (k = 0; k < sizeof game.image.stack; ++k) assert(game.image.stack[k] == (uint8_t)(k * 13u + 7u));
    /* The game goes on from the file, and saves the same image again. */
    for (k = 0; k < 2; ++k) { fe2_gameplay_key(&game, 1, 0, 0); frame(); fe2_gameplay_key(&game, 1, 0, 1); frame(); }
    fe2_input_restore(&session.pointer, 0xf8); frame();
    fe2_input_restore(&session.pointer, 0x0d); frame();
    f = fopen("JAMESON._02", "rb");
    assert(f);
    second_length = fread(second, 1, sizeof second, f); fclose(f);
    assert(game.image.objects[0x73] == 0xa5);
    for (k = 0; k < sizeof game.image.stack; ++k) assert(game.image.stack[k] == (uint8_t)(k * 13u + 7u));
    printf("Opaque commander bytes preserved through load and resave\n");
    /* Straight after the load the image is the file's, but for what the
     * frames since have moved on: the clocks and the frame generation,
     * expired transient records (reusable slots), the measured contact
     * distance, panel selection and message timers. */
    {
        unsigned n, slots = 0, cells = 0, others = 0;
        for (n = 0; n < FE2_POOL_SLOTS; ++n)
            if (saved_objects[n] != game.image.objects[n]) {
                ++slots;
                if (!(saved_objects[n] & FE2_SLOT_REUSABLE) || game.image.objects[n]) ++others;
            }
        for (n = 0; n < sizeof saved_resident; ++n) cells += saved_resident[n] != game.image.resident[n];
        printf("Image again: %u slots differ (%u not expired transients), %u resident bytes\n", slots, others,
               cells);
        assert(others == 0 && cells < 32);
    }
    printf("Saved again: JAMESON._02, %zu bytes\n", second_length);
    assert(second_length > 6 && second[0] == 0x11 && second[1] == 0);
    for (i = 0; i < 200; ++i) frame();
    printf("Flying on: %u live objects, mode %02x\n", occupied(&session), session.resident.flight_mode);
    assert(!session.error);
    remove("JAMESON._01"); remove("JAMESON._02");
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_ui(void)
{
    unsigned before;
    assert(fe2_pointer_regions_count(&session.regions) > 0);
    /* Escape pauses; a second press opens native preferences. */
    fe2_gameplay_key(&game, 1, 0, 0); frame();
    fe2_gameplay_key(&game, 1, 0, 1); frame();
    assert(session.time_step == 0);
    {
        fe2_video *v = fe2_render_runtime_video(runtime);
        const uint8_t *page = v->pages[v->drawing];
        printf("Time icon paused: %u\n", page[180u * 320u + 2]);
        assert(page[180u * 320u + 2] == 46);
    }
    fe2_gameplay_key(&game, 1, 0, 0); frame();
    fe2_gameplay_key(&game, 1, 0, 1); frame();
    assert(game.menu.state.menu_mode == FE2_MENU_PREFERENCES);
    {
        fe2_video *v = fe2_render_runtime_video(runtime);
        const uint8_t *page = v->pages[v->drawing];
        unsigned n, blue = 0;
        for (n = 0; n < 320u * 168u; ++n) if (page[n] == 0x7f) ++blue;
        printf("Preferences: backdrop %u, heading %u, body %u\n", blue,
            host.colour_map[1], host.colour_map[0x0f]);
        printf("Time icon stopped: %u/%u\n", page[179u * 320u + 10], page[180u * 320u + 2]);
        assert(page[179u * 320u + 10] == 47 && page[180u * 320u + 2] == 32);
        assert(blue >= 10000);
        assert(host.colour_map[1] == game.menu.state.colours[4]);
        assert(host.colour_map[0x0f] == game.menu.state.colours[1]);
    }
    /* Preference widgets hit-test through the native pointer list. */
    before = game.menu.state.preferences[0];
    fe2_gameplay_mouse(&game, 28, 22, 1); frame(); frame();
    fe2_gameplay_mouse(&game, 28, 22, 0); frame();
    printf("UI: preferences open, %zu hotspots, first preference %u -> %u\n",
        fe2_pointer_regions_count(&session.regions), before, game.menu.state.preferences[0]);
    assert(game.menu.state.preferences[0] != before);
    fe2_gameplay_key(&game, 0x3b, 0, 0); frame();
    fe2_gameplay_key(&game, 0x3b, 0, 1); frame();
    assert(game.menu.state.menu_mode == FE2_MENU_CLOSED);
    fe2_gameplay_mouse(&game, 25, 180, 1); frame(); frame();
    fe2_gameplay_mouse(&game, 25, 180, 0); frame();
    assert(session.time_step == 0x26d6);
    {
        fe2_video *v = fe2_render_runtime_video(runtime);
        const uint8_t *page = v->pages[v->drawing];
        printf("Time icon fast: %u\n", page[179u * 320u + 21]);
        assert(page[179u * 320u + 21] == 33);
    }
    fe2_gameplay_mouse(&game, 15, 180, 1); frame(); frame();
    fe2_gameplay_mouse(&game, 15, 180, 0); frame();
    assert(session.time_step == 994);
    {
        fe2_video *v = fe2_render_runtime_video(runtime);
        const uint8_t *page = v->pages[v->drawing];
        printf("Time icon normal: %u\n", page[179u * 320u + 10]);
        assert(page[179u * 320u + 10] == 33);
    }
    fe2_gameplay_mouse(&game, 263, 192, 1); frame(); frame();
    fe2_gameplay_mouse(&game, 263, 192, 0); frame();
    assert(session.resident.flight_mode != FE2_FLIGHT_DOCKED);
    printf("UI: mouse time acceleration, normal speed and launch passed\n");
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_shipinfo(void)
{
    unsigned i;
    for (i = 0; i < 8; ++i) {
        unsigned step;
        uint16_t angle = game.status.state.preview_angle[0];
        fe2_gameplay_key(&game, 0x3d, 0, 0); frame();
        fe2_gameplay_key(&game, 0x3d, 0, 1);
        for (step = 0; step < 30; ++step) frame();
        printf("Ship information page %u active %u angle %u -> %u\n", game.status.state.page,
            game.status.state.screen_active, angle, game.status.state.preview_angle[0]);
        assert(game.status.state.screen_active);
        /* Contracts leave the preview alone. Every other page turns it. */
        if (game.status.state.page == 0x0a) assert(game.status.state.preview_angle[0] == angle);
        else assert(game.status.state.preview_angle[0] != angle);
    }
    {
        fe2_object *ship;
        uint16_t held, index;
        unsigned region;
        int found = 0;
        /* An empty hold skips the cargo page, so open it directly. The
         * refuel hotspot is the last widget on that page. */
        game.status.state.page = 6;
        frame();
        assert(game.status.state.page == 6 && game.status.state.screen_active);
        for (region = 0; region < FE2_POINTER_REGIONS &&
             session.regions.region[region].left != FE2_POINTER_REGION_END; ++region)
            if (session.regions.region[region].id == 0x66) found = 1;
        assert(found);
        ship = game.status.state.ship;
        index = ship->as.craft.drive >= 10 ? 20 : 19;
        ship->fuel = 0;
        game.objects.state.cargo[index] = (uint16_t)(game.objects.state.cargo[index] + 1);
        held = game.objects.state.cargo[index];
        /* An empty tank zeroes the live limits and latches the warning.
         * The copy taken at creation is what refuel writes back. */
        for (region = 0; region < 6; ++region) {
            uint16_t word = (uint16_t)(0x1111u * (region + 1));
            game.objects.state.lost_controls[region] = word;
            ((uint16_t *)game.flight.state.saved_limits)[region] = word;
        }
        memset(ship->as.flight.control_limits, 0, sizeof ship->as.flight.control_limits);
        game.objects.state.warnings |= 1u;
        fe2_input_restore(&session.pointer, 0x66); frame();
        printf("Refuelled: cargo %u %u -> %u, fuel %08x\n", index, held,
               game.objects.state.cargo[index], ship->fuel);
        assert(game.objects.state.cargo[index] == (uint16_t)(held - 1));
        assert(ship->fuel == 0x20000000u);
        assert(!(game.objects.state.warnings & 1u));
        for (region = 0; region < 6; ++region)
            assert(((uint16_t *)ship->as.flight.control_limits)[region] ==
                   (uint16_t)(0x1111u * (region + 1)));
        printf("Engines restored\n");
    }
    {
        uint8_t letter;
        game.market.state.markets.bytes[FE2_PROFILE_WORD_AT] = 8;
        game.market.state.markets.bytes[FE2_PROFILE_WORD_AT + 1] = 0;
        session.resident.trade_codes_0 = 0x80;
        game.objects.state.cargo[0] = 3;
        assert(fe2_session_broadcast(&session, 5) == 0);
        letter = game.offences.state.expected_letter;
        printf("Customs: letter %c law %u\n", letter, game.offences.state.law_level);
        assert(letter >= 'A' && letter <= 'Z');
        assert(game.offences.state.law_level == 8);
        assert(!session.error);
    }
    fe2_render_runtime_destroy(runtime); return 0;
}
