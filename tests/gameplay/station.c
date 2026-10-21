/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "gameplay/harness.h"

int fe2_check_fine(void)
{
    uint32_t day;
    assert(selection == 0x201 && session.resident.flight_mode == 0x54);
    assert(game.station.state.selected_text == 0x993b);
    session.resident.local_offence_fine = 5000;
    station_open();
    day = session.pools.clock[1];
    host.pump = sentence_key;
    station_row(1);
    printf("Fine launch: frontend %u day %u -> %u failures %u errors %u\n",
        session.frontend, day, session.pools.clock[1], game.text_failures, host.errors);
    assert(session.frontend == FE2_SESSION_EXIT_SCENE);
    assert(session.pools.clock[1] > day && !session.error && !host.errors);
    fe2_input_restore(&session.pointer, ' ');
    frame();
    assert(session.frontend == FE2_SESSION_INTRO);
    printf("Unpaid fine: arrest notice, then the exit scene\n");
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_deliver(void)
{
    fe2_contract contract;
    uint32_t cash, system = session.resident.current_system;
    unsigned n;
    memset(&contract, 0, sizeof contract);
    contract.fee = 42; contract.group = 0x1c; contract.kind = 1;
    contract.destination = system; contract.deadline = session.pools.clock[1] + 1000;
    contract.client = 0x12345678;
    fe2_session_status_add_contract(&game.status, &contract);
    assert(game.status.state.contract_count == 1);
    assert(!fe2_session_broadcast(&session, 5));
    cash = game.status.state.cash;
    assert(session.metadata[14].flags & 1u);
    assert(game.offences.state.expected_letter >= 'A' && game.offences.state.expected_letter <= 'Z');
    assert(game.station.state.question[0] == (int16_t)game.offences.state.challenge[0]);
    assert(game.station.state.question[1] == (int16_t)game.offences.state.challenge[1]);
    assert(game.station.state.question[2] == (int16_t)game.offences.state.challenge[2]);
    for (n = 0; n < 800 && game.status.state.contract_count; ++n) frame();
    printf("Parcel delivered: frames %u cash %u -> %u contracts %u letter %c\n",
           n, cash, game.status.state.cash, game.status.state.contract_count,
           game.offences.state.expected_letter);
    assert(n < 800 && game.status.state.contract_count == 0);
    assert(game.status.state.cash == cash + 420);
    assert(!session.error && !host.errors);
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_spawn(void)
{
    unsigned i;
    /* A canister takes the station's slot offset from market service 9,
     * then people creation mode 7. Contract company is creation mode 1
     * with the authority in BX. */
    fe2_object *station = 0, *child = 0, *company;
    fe2_call_args args;
    unsigned slot, tries, child_slot = 0;
    int64_t clearance;
    for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
        fe2_object *o = session.objects.records[slot];
        if (o && o->role == 1 && (o->flags & 0x40u) && o->level < 0x11) { station = o; break; }
    }
    assert(station);
    for (tries = 0; tries < 400 && !child; ++tries) {
        uint16_t seen_model[FE2_POOL_SLOTS];
        uint32_t seen_pos[FE2_POOL_SLOTS];
        uint16_t seen_status[FE2_POOL_SLOTS];
        memset(seen_model, 0, sizeof seen_model);
        memset(seen_pos, 0, sizeof seen_pos);
        memset(seen_status, 0, sizeof seen_status);
        for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
            fe2_object *o = session.objects.records[slot];
            seen_status[slot] = session.objects.status[slot];
            if (o) { seen_model[slot] = o->model; seen_pos[slot] = o->position_low[0]; }
        }
        fe2_status_spawn_child(&game.status.services, station);
        assert(!session.error);
        for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
            fe2_object *o = session.objects.records[slot];
            if (!o || o->role != 11 || !session.objects.status[slot] ||
                (session.objects.status[slot] & FE2_SLOT_REUSABLE)) continue;
            if (seen_status[slot] && !(seen_status[slot] & FE2_SLOT_REUSABLE) &&
                seen_model[slot] == o->model && seen_pos[slot] == o->position_low[0]) continue;
            child = o;
            child_slot = slot;
        }
    }
    assert(child);
    clearance = station_clearance(child, station, 1);
    printf("Canister %s: model %04x clearance %lld\n",
        station->name, child->model, (long long)clearance);
    assert(clearance > 1000);
    for (i = 0; i < 60; ++i) frame();
    assert(!session.error);
    assert(session.objects.records[child_slot] == child);
    assert(session.objects.status[child_slot] &&
           !(session.objects.status[child_slot] & FE2_SLOT_REUSABLE));
    assert(child->model != 0xbc && child->behaviour != 0x64);
    fe2_args_clear(&args);
    fe2_args_word(&args, 7, 1);
    company = game.status.services.create_object(game.status.services.opaque, 0x3c, &args);
    assert(company && company->behaviour_parent == 0x3c);
    assert(!session.error);
    printf("Company: model %04x parent %02x\n", company->model, company->behaviour_parent);
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_ports(void)
{
    unsigned i;
    /* slot_vector's six banks: four rotating shapes, a fixed city, and a
     * free orbital body. Each must put a ship outside the station and
     * leave it there. */
    static const struct { const char *name; uint16_t flags; uint16_t shape; } ports[] = {
        { "rotating shape 1", 0x60, 1 },
        { "rotating shape 3", 0x60, 3 },
        { "rotating other", 0x60, 5 },
        { "rotating shape 0", 0x60, 0 },
        { "fixed", 0x20, 3 },
        { "free", 0x00, 3 }
    };
    fe2_object *found[6] = {0, 0, 0, 0, 0, 0};
    fe2_object *donor = 0;
    unsigned n, slot;
    for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
        fe2_object *o = session.objects.records[slot];
        unsigned kind;
        if (!o || o->role != 1 || !(o->flags & 0x10u)) continue;
        if (o->flags & 0x40u) {
            kind = o->as.body.shape == 1 ? 0u : o->as.body.shape == 3 ? 1u :
                   o->as.body.shape != 0 ? 2u : 3u;
            if (!donor) donor = o;
        } else if (o->flags & 0x20u) {
            kind = 4u;
        } else {
            kind = 5u;
        }
        if (!found[kind]) found[kind] = o;
    }
    assert(donor && found[4] && found[5]);
    for (n = 0; n < 4; ++n) if (!found[n]) found[n] = donor;
    for (n = 0; n < sizeof ports / sizeof ports[0]; ++n) {
        fe2_object *station = found[n];
        uint16_t saved_flags = station->flags, saved_shape = station->as.body.shape;
        uint16_t saved_radius = station->as.body.radius_class;
        uint8_t saved_door = station->as.body.docking_state;
        uint16_t saved_progress = station->as.body.progress;
        int synthesize = station == donor &&
            (((station->flags & 0x60u) != ports[n].flags) || station->as.body.shape != ports[n].shape);
        uint16_t seen_model[FE2_POOL_SLOTS];
        uint32_t seen_pos[FE2_POOL_SLOTS];
        uint16_t seen_status[FE2_POOL_SLOTS];
        fe2_object *ship = 0;
        unsigned ship_slot = 0, launched = 0, tries;
        int linked = (ports[n].flags & 0x20u) != 0;
        int trickle = (ports[n].flags & 0x40u) != 0;
        int64_t clearance = 0;
        memset(seen_model, 0, sizeof seen_model);
        memset(seen_pos, 0, sizeof seen_pos);
        memset(seen_status, 0, sizeof seen_status);
        for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
            fe2_object *o = session.objects.records[slot];
            seen_status[slot] = session.objects.status[slot];
            if (o) { seen_model[slot] = o->model; seen_pos[slot] = o->position_low[0]; }
        }
        if (synthesize) {
            station->flags = (uint16_t)((saved_flags & ~0x60u) | ports[n].flags | 0x10u);
            station->as.body.shape = ports[n].shape;
        }
        station->as.body.radius_class = trickle ? 16u : 1u;
        station->as.body.docking_state = 0;
        station->as.body.docking_claim = 0;
        station->as.body.docking_timer = 0;
        station->as.body.progress = trickle ? 0xffffu : 0;
        session.resident.police_station = station;
        session.resident.launch_phase = 0;
        session.metadata[15].flags = (uint8_t)(session.metadata[15].flags | 1u);
        for (tries = 0; tries < (trickle ? 80u : 1u) && !ship; ++tries) {
            session.overlays[15].coarse_tick(session.overlays[15].context, &session, 1);
            assert(!session.error);
            for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
                fe2_object *o = session.objects.records[slot];
                if (!o || o->role != 11 || !session.objects.status[slot] ||
                    (session.objects.status[slot] & FE2_SLOT_REUSABLE)) continue;
                if (seen_status[slot] && !(seen_status[slot] & FE2_SLOT_REUSABLE) &&
                    seen_model[slot] == o->model && seen_pos[slot] == o->position_low[0]) continue;
                ship = o;
                ship_slot = slot;
                ++launched;
            }
        }
        assert(ship);
        clearance = station_clearance(ship, station, linked);
        printf("Port %s (%s): %u ships, clearance %lld, behaviour %02x\n",
            ports[n].name, station->name, launched, (long long)clearance, ship->behaviour);
        assert(clearance > 1000);
        for (i = 0; i < 400; ++i) frame();
        printf("  flying: behaviour %02x door %02x scalar %u\n",
            ship->behaviour, station->as.body.docking_state, station->cargo_type);
        assert(session.objects.records[ship_slot] == ship);
        assert(session.objects.status[ship_slot] &&
               !(session.objects.status[ship_slot] & FE2_SLOT_REUSABLE));
        assert(ship->model != 0xbc && ship->behaviour != 0x64);
        /* A batch waits in the bay until the door scalar reaches zero and
         * the state becomes 0xfe. The scalar has to be moving. */
        if (!trickle && ship->behaviour == 0x7c) {
            assert(station->as.body.docking_state == 0xff ||
                   station->as.body.docking_state == 0xfe ||
                   station->as.body.docking_state == 0);
            assert(station->cargo_type < 0xffffu);
        }
        station->flags = saved_flags;
        station->as.body.shape = saved_shape;
        station->as.body.radius_class = saved_radius;
        station->as.body.docking_state = saved_door;
        station->as.body.progress = saved_progress;
    }
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_police(void)
{
    unsigned i;
    fe2_object *station = 0;
    unsigned before_count, slot;
    for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
        fe2_object *o = session.objects.records[slot];
        if (o && o->role == 1) { station = o; break; }
    }
    assert(station);
    station->flags = (uint16_t)(station->flags | 0x10u);
    station->as.body.radius_class = 2;
    station->as.body.docking_state = 0;
    station->as.body.progress = 0;
    session.resident.police_station = station;
    session.resident.launch_phase = 0;
    session.metadata[15].flags = (uint8_t)(session.metadata[15].flags | 1u);
    before_count = occupied(&session);
    assert(session.overlays[15].coarse_tick);
    session.overlays[15].coarse_tick(session.overlays[15].context, &session, 1);
    assert(!session.error);
    assert(session.metadata[15].flags == session.resident.police_flags);
    assert(session.metadata[15].update == session.resident.launch_phase);
    printf("Police launch: %u live objects before, %u after, %u patrol ships\n",
        before_count, occupied(&session), patrols(&session));
    assert(occupied(&session) > before_count);
    assert(patrols(&session) > 0);
    /* The patrol flies under M11's steering (180h/184h, 1A4h..1B4h). */
    {
        fe2_object *patrol = 0;
        int64_t before[3];
        unsigned k, moved = 0;
        for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
            fe2_object *o = session.objects.records[slot];
            if (o && session.objects.status[slot] && o->behaviour == 0x7c) { patrol = o; break; }
        }
        assert(patrol);
        for (k = 0; k < 3; ++k) before[k] = (int64_t)((uint64_t)patrol->position_high[k] << 32 | patrol->position_low[k]);
        for (i = 0; i < 600; ++i) frame();
        for (k = 0; k < 3; ++k)
            moved |= (int64_t)((uint64_t)patrol->position_high[k] << 32 | patrol->position_low[k]) != before[k];
        printf("Police patrol: behaviour %02x, moved %u\n", patrol->behaviour, moved);
        assert(moved);
    }
    /* Interception (behaviour 10h, closing on its parent): M11 steers
     * through 180h, 1A4h and 1ACh every update, and the patrol closes in.
     * The player is still docked (84h stops it), so the target is a body. */
    {
        fe2_object *patrol = 0, *player = 0;
        for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
            fe2_object *o = session.objects.records[slot];
            if (o && session.objects.status[slot] && o->role == 1 && o->behaviour != 0x84) { player = o; break; }
        }
        int64_t gap[2];
        unsigned pass;
        for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
            fe2_object *o = session.objects.records[slot];
            if (o && session.objects.status[slot] && o->role == 11 && o != player) { patrol = o; break; }
        }
        assert(patrol && player);
        /* Stage a separated interception instead of depending on where
         * the preceding free patrol happens to end. Native distance
         * scheduling changes that position and can leave it at standoff. */
        patrol->parent_handle = player->parent_handle;
        patrol->station_slot = player->station_slot;
        memcpy(patrol->position_low, player->position_low, sizeof patrol->position_low);
        memcpy(patrol->position_high, player->position_high, sizeof patrol->position_high);
        patrol->position_high[2] += 64;
        memset(patrol->velocity, 0, sizeof patrol->velocity);
        patrol->velocity[2] = -0x1000000;
        memset(patrol->rotation_rates, 0, sizeof patrol->rotation_rates);
        memset(patrol->orientation, 0, sizeof patrol->orientation);
        patrol->orientation[0] = patrol->orientation[8] = -32767;
        patrol->orientation[4] = 32767;
        patrol->render_state = 0;
        patrol->update_countdown = 0;
        fe2_pool_set_slot_flags(&session.pools, patrol, 0x4f);
        patrol->behaviour = 0x10;
        patrol->behaviour_parent = player->handle;
        for (pass = 0; pass < 2; ++pass) {
            int64_t v[3];
            fe2_motion_services *m = &game.motion_services;
            if (pass) for (i = 0; i < 600; ++i) frame();
            m->relative_position(m->opaque, patrol, player, v);
            gap[pass] = (v[0] < 0 ? -v[0] : v[0]) + (v[1] < 0 ? -v[1] : v[1]) + (v[2] < 0 ? -v[2] : v[2]);
        }
        printf("Interception: behaviour %02x, gap %lld -> %lld\n", patrol->behaviour,
               (long long)gap[0], (long long)gap[1]);
        assert(gap[1] < gap[0]);
        /* Escorting (00h): steered through the probe (184h, the scene
         * probe) while it gives more than 5, so the craft closes in. */
        patrol->behaviour = 0x00;
        patrol->behaviour_target = 0;
        for (pass = 0; pass < 2; ++pass) {
            int64_t v[3];
            fe2_motion_services *m = &game.motion_services;
            if (pass) for (i = 0; i < 600; ++i) frame();
            m->relative_position(m->opaque, patrol, player, v);
            gap[pass] = (v[0] < 0 ? -v[0] : v[0]) + (v[1] < 0 ? -v[1] : v[1]) + (v[2] < 0 ? -v[2] : v[2]);
        }
        printf("Escort: behaviour %02x, gap %lld -> %lld\n", patrol->behaviour,
               (long long)gap[0], (long long)gap[1]);
        assert(gap[1] < gap[0]);
        /* Holding (28h): 1B0h turns from the steering and stops the roll. */
        patrol->behaviour = 0x28;
        patrol->rotation_rates[1] = 0x1234;
        for (i = 0; i < 60; ++i) frame();
        printf("Hold: behaviour %02x, roll %d\n", patrol->behaviour, patrol->rotation_rates[1]);
        assert(patrol->behaviour == 0x28 && patrol->rotation_rates[1] == 0);
        /* Leaving the parent (1Ch): 1B4h takes the rates from the controls. */
        patrol->behaviour = 0x1c;
        for (i = 0; i < 60 && patrol->behaviour == 0x1c; ++i) frame();
        printf("Leaving: behaviour %02x, controls %d %d, rates %d %d\n", patrol->behaviour,
               patrol->controls[0], patrol->controls[1], patrol->rotation_rates[2], patrol->rotation_rates[0]);
        assert(patrol->behaviour == 0x1c);
        assert(patrol->rotation_rates[2] == (int16_t)(patrol->controls[0] >> 3));
        assert(patrol->rotation_rates[0] == (int16_t)(uint16_t)-(uint16_t)(int16_t)(patrol->controls[1] >> 2));
        /* Removal (50h from level 0Dh): 084h releases the craft's slot. */
        {
            unsigned craft = population(&session);
            patrol->behaviour = 0x50;
            patrol->level = 0x0d;
            for (i = 0; i < 60 && population(&session) == craft; ++i) frame();
            printf("Removal: %u craft -> %u\n", craft, population(&session));
            assert(!session.error && population(&session) == craft - 1);
        }
    }
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_shipyard(void)
{
    fe2_station_state *st = &game.station.state;
    unsigned k;
    station_open(); station_row(2); station_row(3);
    assert(st->row_count > 0 && st->rows[0].text == 0x9811);
    {
        /* Column titles (Buy, View, Ship, Cost) sit above the buttons. */
        const uint8_t *page = fe2_render_runtime_video(runtime)->pages[0];
        unsigned ink = 0, x, y;
        for (y = 2; y < 11; ++y)
            for (x = 120; x < 310; ++x)
                if (page[y * 320u + x] != 0x7f) ++ink;
        printf("Shipyard column titles: %u pixels\n", ink);
        assert(ink > 40);
    }
    /* A ship's offer (region 90h): the preview ship (work record, 0B8h)
     * and the registration through its reference; then buying it. */
    game.objects.state.bounty = 100000000;
    k = session.records[(uint8_t)st->ship].model;
    station_region(0x90);
    printf("Ship offer: preview model %u, registration %s\n", game.station_preview.model, st->preview.name);
    assert(st->message.text == 0x98d5 && game.station_preview.model == st->preview.model && st->preview.name[0]);
    printf("Ship preview: %u draws, %u changed pixels\n", game.previews, game.preview_pixels);
    assert(game.previews > 0 && game.preview_pixels > 0);
    assert(game.station_ship_valid);
    assert(!memcmp(fe2_render_runtime_video(runtime)->pages[0] + 22*320,
                   game.station_ship_pixels, sizeof game.station_ship_pixels));
    snapshot("shipyard");
    station_region(0x80);
    printf("Ship bought: model %u -> %u, %s, message %04x\n", k, session.records[(uint8_t)st->ship].model,
           session.records[(uint8_t)st->ship].name, st->message.text);
    assert(st->message.text == 0x98d7 && session.records[(uint8_t)st->ship].model == st->preview.model);
    assert(!strcmp(session.records[(uint8_t)st->ship].name, (const char *)st->preview.name));
    /* The new ship needs crew before launch (9921h). */
    station_open(); station_row(1);
    printf("Launch with the new ship: message %04x, crew %u of %u\n", st->message.text, st->passenger_count,
           st->passenger_limit);
    assert(st->message.text == 0x9921 && game.text_failures == 0);
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_loops(void)
{
    gameplay_loops();
    fe2_render_runtime_destroy(runtime);
    return 0;
}

int fe2_check_mission(void)
{
    /* Lave's board: the mission advert (9820h, row 6) searches its
     * target system, generated by M00 into the second pool (07FCh),
     * for a station; the contract is then taken. */
    fe2_station_state *st = &game.station.state;
    unsigned k, map = 0;
    game.objects.state.rating = 0xffff;
    station_open(); station_row(3);
    assert(st->row_count > 6 && st->rows[6].text == 0x9820);
    station_region(0x86);
    for (k = 1; k < FE2_POOL_SLOTS; ++k) map += session.map_objects.status[k] != 0;
    printf("Mission offer: message %04x, %u target system records, station %u\n", st->message.text, map,
           st->chosen);
    assert(st->message.text == 0x99ad && map > 0 && st->chosen && session.map_objects.status[st->chosen]);
    assert(session.map_records[st->chosen].flags & 0x10);
    station_region(0x80);
    printf("Mission taken: message %04x\n", st->message.text);
    assert(st->message.text == 0x99b3 && game.text_failures == 0);
    assert(session.resident.mission_count > 0);
    assert(game.status.state.military[0].target_system == st->missions[0].system);
    assert(game.status.state.military[0].target_seed == st->missions[0].seed);
    assert(game.status.state.military[0].deadline == st->missions[0].due);
    assert(game.flight.state.records && game.flight.state.records[0].system == st->missions[0].system);
    printf("Mission shared with commander status and flight\n");
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_station(void)
{
    /* The docked station's screens (F4, then keys 1..4 pick rows). */
    fe2_station_state *st = &game.station.state;
    uint32_t balance;
    uint16_t cargo;
    station_open();
    printf("Station: %u rows, first %04x\n", st->row_count, st->rows[0].text);
    assert(st->row_count == 4 && st->rows[3].text == 0x980c);
    {
        /* 1DCh fills the menu with 7Fh. The portrait follows once its
         * wait (4B69h, 8000h) has been counted down. */
        fe2_video *v = fe2_render_runtime_video(runtime);
        unsigned waits, ink_count = 0, blue = 0, n, x, y;
        for (waits = 0; waits < 80 && st->portrait_timer; ++waits) frame();
        assert(st->portrait_timer == 0);
        for (n = 0; n < 320u * 168u; ++n) if (v->pages[v->drawing][n] == 0x7f) ++blue;
        for (y = 30; y < 130; y += 2)
            for (x = 8; x < 110; x += 2)
                if (v->pages[v->drawing][y * 320 + x] >= 0x80) ++ink_count;
        printf("Station portrait: backdrop %u, %u ink samples\n", blue, ink_count);
        assert(blue >= 20000 && ink_count >= 20);
        /* Closing the phone must put the scene's DAC entries back.
         * The portrait wrote 80h+ over them after they were assigned. */
        st->screen_active = 0;
        frame();
        {
            fe2_palette_cache *cache = fe2_render_runtime_palette(runtime);
            unsigned slot, live = 0, bad = 0;
            for (slot = 0; slot < FE2_PALETTE_SLOTS; ++slot) {
                const fe2_palette_slot *s = &cache->slots[slot];
                uint8_t red, green, blue;
                if (s->state < FE2_PALETTE_REPLACEABLE || s->dac < 0x80u) continue;
                ++live;
                red = (uint8_t)(((s->rgb12 >> 8) & 0xfu) * 4u + 3u);
                green = (uint8_t)(((s->rgb12 >> 4) & 0xfu) * 4u + 3u);
                blue = (uint8_t)((s->rgb12 & 0xfu) * 4u + 3u);
                if (v->palette[s->dac][0] != red || v->palette[s->dac][1] != green ||
                    v->palette[s->dac][2] != blue) ++bad;
            }
            printf("Front view palette: %u entries, %u stale\n", live, bad);
            assert(live > 0 && bad == 0 && !host.dac_clobbered);
        }
        st->screen_active = 0xff;
        session.metadata[12].update = 1;
    }
    /* The market (980Ch): one row per commodity; a row buys one. */
    station_row(4);
    printf("Market: %u rows\n", st->row_count);
    assert(st->row_count > 10 && st->rows[0].text == 0x9831);
    balance = st->balance; cargo = st->cargo_used;
    station_row(1);
    printf("Bought: balance %u -> %u, cargo %u -> %u\n", balance, st->balance, cargo, st->cargo_used);
    assert(st->balance < balance && st->cargo_used == cargo + 1);
    /* Region code 90h: the first row with bit 10h, which sells. */
    balance = st->balance; cargo = st->cargo_used;
    station_region(0x90);
    printf("Sold: balance %u -> %u, cargo %u -> %u\n", balance, st->balance, cargo, st->cargo_used);
    assert(st->balance > balance && st->cargo_used == cargo - 1);
    /* The bulletin board (980Bh): the market's adverts, and a reply. */
    station_open(); station_row(3);
    printf("Bulletin board: %u adverts\n", st->row_count);
    assert(st->row_count > 0);
    station_row(1);
    printf("Advert: %u replies, first %04x\n", st->row_count, st->rows[0].text);
    assert(st->row_count > 0);
    /* The shipyard (980Ah): fitting (980Dh) and the ships for sale (980Fh). */
    station_open(); station_row(2);
    assert(st->rows[0].text == 0x980d && st->rows[2].text == 0x980f);
    station_row(1);
    printf("Fitting: %u rows\n", st->row_count);
    assert(st->row_count > 0);
    /* Equipment (region 84h, the fourth item) with enough cash: the
     * first rows refuse (no pylon free, already fitted). */
    game.objects.state.bounty = 1000000;
    station_region(0x81);
    printf("Equipment refused: message %04x\n", st->message.text);
    assert(st->message.text == 0x98e1 && st->balance == 1000000);
    station_region(0x84);
    printf("Equipment fitted: balance 1000000 -> %u, message %04x\n", st->balance, st->message.text);
    assert(st->message.text == 0x98e9 && st->balance < 1000000);
    /* Servicing (980Eh): the deluxe service and the standard one. */
    station_open(); station_row(2); station_row(2);
    printf("Servicing: %u rows, first %04x\n", st->row_count, st->rows[0].text);
    assert(st->row_count == 3 && st->rows[0].text == 0x9815);
    balance = st->balance;
    station_row(1);
    printf("Deluxe service: balance %u -> %u, message %04x\n", balance, st->balance, st->message.text);
    assert(st->message.text == 0x98db && st->balance < balance);
    station_open(); station_row(2); station_row(2);
    balance = st->balance;
    station_row(2);
    printf("Service: balance %u -> %u, message %04x\n", balance, st->balance, st->message.text);
    assert(st->message.text == 0x98dc && st->balance < balance);
    station_open(); station_row(2); station_row(3);
    printf("Ships for sale: %u\n", st->row_count);
    assert(st->row_count > 0 && st->rows[0].text == 0x9811);
    /* Launch (9818h): this station's port grants clearance (207Bh), and
     * F7 then lifts off. */
    station_open(); station_row(1);
    printf("Launch: message %04x, clearance %u\n", st->message.text, session.resident.mining_view_flag);
    assert(st->message.text == 0x98a4 && session.resident.mining_view_flag);
    fe2_input_restore(&session.pointer, 0xf1); frame();
    fe2_input_restore(&session.pointer, 0xf7); frame();
    printf("Lift-off: mode %02x\n", session.resident.flight_mode);
    assert(session.resident.flight_mode != 0x60);
    /* Every row expanded, including the adverts' system names (M02's
     * 85FFh through its text entry). */
    printf("Text failures: %u\n", game.text_failures);
    assert(game.text_failures == 0);
    fe2_render_runtime_destroy(runtime); return 0;
}
