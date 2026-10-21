/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "gameplay/harness.h"

int fe2_check_scanner(void)
{
    unsigned i;
    fe2_video *v = fe2_render_runtime_video(runtime);
    uint8_t *page = v->pages[v->drawing];
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_byte(&args, 8, 0x35);
    session.resident.line_colour = 0x71; /* Must not supply marker colour. */
    memset(page, 0, FE2_VIDEO_PAGE_BYTES);
    game.panels.host.draw_marker(&game, 160, 180, 4, &args);
    assert(page[175 * 320 + 159] == 0x35 && page[180 * 320 + 160] == 0x35);
    assert(page[175 * 320 + 161] == 0x35 && page[176 * 320 + 162] == 0x35);
    assert(!page[177 * 320 + 161] && !page[174 * 320 + 159]);
    /* Downward stems preserve all four scanner grid palette indices. */
    memset(page, 0, FE2_VIDEO_PAGE_BYTES);
    for (i = 0; i < 4; ++i) page[(180 + i) * 320 + 159] = (uint8_t)(0x28 + i);
    game.panels.host.draw_marker(&game, 160, 180, (uint16_t)-4, &args);
    for (i = 0; i < 4; ++i) assert(page[(180 + i) * 320 + 159] == 0x28 + i);
    assert(page[185 * 320 + 160] == 0x35 && page[184 * 320 + 162] == 0x35);
    assert(!page[186 * 320 + 160]);
    memset(page, 0, FE2_VIDEO_PAGE_BYTES);
    fe2_args_word(&args, 3, 0xffff);
    game.panels.host.draw_marker(&game, 160, 170, 4, &args);
    assert(!page[167 * 320 + 159] && page[168 * 320 + 159] == 0x35);
    fe2_args_word(&args, 3, 0);
    game.panels.host.draw_marker(&game, 160, 170, 4, &args);
    assert(page[165 * 320 + 159] == 0x35);
    game.panels.host.draw_marker(&game, 160, 197, (uint16_t)-4, &args);
    assert(page[198 * 320 + 160] == 0x35 && !page[199 * 320 + 160]);
    /* Service 210h trusts cockpit rows and excludes the endpoint;
     * service 90h clips to the view before rasterizing. */
    memset(page, 0, FE2_VIDEO_PAGE_BYTES);
    game.panels.host.draw_line(&game, 10, 180, 14, 180);
    for (i = 10; i < 14; ++i) assert(page[180 * 320 + i] == 0x71);
    assert(!page[180 * 320 + 14]);
    game.panels.host.draw_line(&game, 20, 180, 20, 180);
    assert(!page[180 * 320 + 20]);
    game.flight.host.draw_clipped_line(&game, 30, 160, 30, 180);
    /* Clipping the second endpoint swaps direction: draw 167..161. */
    assert(!page[160 * 320 + 30] && page[161 * 320 + 30] == 0x71);
    assert(page[167 * 320 + 30] == 0x71 && !page[168 * 320 + 30]);
    {
        uint8_t view = game.flight.state.view_index;
        session.render_mode = 0;
        page[0] = 5; page[199 * 320] = 6;
        game.menu.host.restore_view(&game);
        assert(session.render_mode == 0xff && host.colour.restored == 0xff);
        assert(page[0] == 0x7f && page[199 * 320] == 6);
        assert(game.flight.state.view_index == view);
    }
    printf("Scanner: stems, caps, colour, clipping and preservation passed\n");
    fe2_render_runtime_destroy(runtime);
    return 0;
}

int fe2_check_dust(void)
{
    unsigned pass, slot, particles = 0;
    fe2_object *moving = (fe2_object *)ship;
    /* Steady slow flight in open space, without turning or changing view.
     * The dust roll has to advance every frame or the same seed repeats. */
    moving->parent_handle = 0x72; moving->station_slot = 0;
    memset(moving->orientation, 0, sizeof moving->orientation);
    moving->orientation[0] = moving->orientation[4] = moving->orientation[8] = 0x4000;
    memset(moving->position_low, 0, sizeof moving->position_low);
    memset(moving->position_high, 0, sizeof moving->position_high);
    moving->position_high[2] = 0x10000;
    moving->render_state = 0;
    moving->velocity[0] = moving->velocity[1] = 0;
    moving->velocity[2] = 0x2000;
    session.resident.flight_mode = FE2_FLIGHT_MANUAL;
    session.resident.preference_byte_0 = 0xff;
    for (pass = 0; pass < 120; ++pass) frame();
    for (slot = 1; slot < FE2_POOL_SLOTS; ++slot)
        if (session.objects.status[slot] && session.objects.records[slot] &&
            session.objects.records[slot]->model == 0x76) ++particles;
    printf("Space dust: %u particles while flying straight\n", particles);
    assert(particles >= 8);
    assert(game.flight.state.view_index == 0);
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_views(void)
{
    unsigned view, pass;
    for (view = 0; view < 12; ++view) {
        fprintf(stderr, "cycling view %u\n", view);
        fe2_gameplay_key(&game, 0x3b, 0, 0); frame();
        fe2_gameplay_key(&game, 0x3b, 0, 1);
        fe2_gameplay_key(&game, 0x48, 1, 0);
        fe2_gameplay_key(&game, 0x4b, 1, 0);
        for (pass = 0; pass < 300; ++pass) frame();
        fe2_gameplay_key(&game, 0x48, 1, 1);
        fe2_gameplay_key(&game, 0x4b, 1, 1);
    }
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_hud(void)
{
    fe2_video *video;
    int16_t parked[9], coast[9], steered[9];
    int drift, turned;
    unsigned y;
    const uint8_t *page;
    video = fe2_render_runtime_video(runtime);
    page = video->pages[video->drawing];
    /* Docked mode icon 49h sits in the first bottom-right box. */
    printf("HUD icon pixel %u (want 44)\n", page[190u * 320u + 262u]);
    assert(page[190u * 320u + 262u] == 44);
    fe2_gameplay_key(&game, 0x41, 0, 0);
    frame(); fe2_gameplay_key(&game, 0x41, 0, 1);
    assert(session.resident.flight_mode != FE2_FLIGHT_DOCKED);
    fe2_gameplay_mouse(&game, 160, 100, 0);
    for (y = 0; y < 5; ++y) frame();
    memcpy(parked, ship->orientation, sizeof(parked));
    for (y = 0; y < 5; ++y) frame();
    memcpy(coast, ship->orientation, sizeof(coast));
    drift = basis_step(parked, coast);
    fe2_gameplay_mouse(&game, 20, 30, 0); frame();
    fe2_gameplay_mouse(&game, 300, 40, 0); frame();
    fe2_gameplay_mouse(&game, 280, 190, 0); frame();
    fe2_gameplay_mouse(&game, 40, 185, 0); frame();
    turned = basis_step(coast, ship->orientation);
    printf("Launch coast drift %d, pointer drift %d, spin %d %d %d\n",
        drift, turned, ship->spin[0], ship->spin[1], ship->spin[2]);
    assert(turned <= drift + 8);
    fe2_gameplay_mouse(&game, 40, 100, 2); frame();
    fe2_gameplay_mouse(&game, 90, 60, 2); frame();
    memcpy(steered, ship->orientation, sizeof(steered));
    printf("Secondary steer step %d\n", basis_step(coast, steered));
    assert(basis_step(coast, steered) > turned + 20);
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_fire(void)
{
    unsigned i;
    fe2_gameplay_key(&game, 0x39, 0, 0);
    for (i = 0; i < 100; ++i) frame();
    fe2_gameplay_key(&game, 0x39, 0, 1);
    frame();
    assert(game.objects.state.warnings & 4u);
    assert(session.resident.local_offence_fine > 0); /* Native M15 weapons offence. */
    assert(game.offences.state.offences[game.offences.state.legal_authority] & (1u << 3));
    assert(game.station.state.ui_flags & 2u);
    assert(game.station.state.message.text == 0x9e00 ||
           game.station.state.message.text == 0x9e01);
    assert(game.beams > 0);
    /* Commit publishes the alert into 0413h, which is the coarse-tick bit. */
    assert(session.metadata[15].flags == session.resident.police_flags);
    assert(session.metadata[15].update == session.resident.launch_phase);
    assert(session.resident.police_flags & 1u);
    {
        /* The fine wraps at 130 pixels from x=116 (colour 23h, shadow 00h).
         * Both lines stay left of the status panel. */
        fe2_video *video = fe2_render_runtime_video(runtime);
        const uint8_t *page = video->pages[video->drawing];
        unsigned y, x, first = 0, second = 0;
        int max_x = 0;
        for (y = 170; y <= 178; ++y) for (x = 110; x < 256; ++x)
            if (page[y * 320u + x] == 0x23) { ++first; if ((int)x > max_x) max_x = (int)x; }
        for (y = 180; y <= 188; ++y) for (x = 110; x < 256; ++x)
            if (page[y * 320u + x] == 0x23) ++second;
        printf("Comms wrap: line ink %u/%u, first-line max x %d\n", first, second, max_x);
        assert(first > 20 && second > 20 && max_x < 250);
    }
    {
        /* The station trickles one ship per coarse tick once 0414h, its
         * update byte, has climbed to FFFFh and back. */
        unsigned before = population(&session), slot, beside = 0;
        uint16_t seen_model[FE2_POOL_SLOTS];
        uint8_t seen_beh[FE2_POOL_SLOTS];
        fe2_object *source;
        memset(seen_model, 0, sizeof seen_model);
        for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
            fe2_object *o = session.objects.records[slot];
            if (o && session.objects.status[slot] && !(session.objects.status[slot] & FE2_SLOT_REUSABLE)) {
                seen_model[slot] = o->model;
                seen_beh[slot] = o->behaviour;
            }
        }
        for (i = 0; i < 3000 && population(&session) <= before; ++i) frame();
        source = session.resident.police_station;
        assert(source);
        for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
            fe2_object *o = session.objects.records[slot];
            int64_t gap = 0;
            unsigned k;
            if (!o || o->role != 11 || !session.objects.status[slot] ||
                (session.objects.status[slot] & FE2_SLOT_REUSABLE)) continue;
            for (k = 0; k < 3; ++k) {
                int64_t d = (int64_t)((uint64_t)o->position_high[k] << 32 | o->position_low[k]) -
                            (int64_t)((uint64_t)source->position_high[k] << 32 | source->position_low[k]);
                if (d < 0) d = -d;
                if (d > gap) gap = d;
            }
            if (o->parent_handle == source->handle || o->parent_handle == source->parent_handle)
                if (gap < 1000000000ll) ++beside;
        }
        printf("Police response: %u craft (%u before), %u beside the station\n",
            population(&session), before, beside);
        assert(population(&session) > before);
        assert(beside > 0);
        /* active_action.inc 0A3Fh then 0AFDh. A linked source (flag 20h)
         * keeps its parent's handle and station slot; any other source
         * docks the ship to itself at station slot FFh. */
        {
            unsigned placed = 0;
            int linked = (source->flags & 0x20) != 0;
            for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
                fe2_object *o = session.objects.records[slot];
                int64_t gap = 0;
                unsigned k;
                if (!o || o->role != 11 || !session.objects.status[slot] ||
                    (session.objects.status[slot] & FE2_SLOT_REUSABLE)) continue;
                for (k = 0; k < 3; ++k) {
                    int64_t d = (int64_t)((uint64_t)o->position_high[k] << 32 | o->position_low[k]) -
                                (int64_t)((uint64_t)source->position_high[k] << 32 | source->position_low[k]);
                    if (d < 0) d = -d;
                    if (d > gap) gap = d;
                }
                if (gap >= 1000000000ll) continue;
                if (linked) {
                    if (o->parent_handle == source->parent_handle &&
                        o->station_slot == source->station_slot) ++placed;
                } else if (o->parent_handle == source->handle && o->station_slot == 0xff) {
                    ++placed;
                }
            }
            printf("Police place: linked %d, %u ships on the assembly parent\n", linked, placed);
            assert(placed > 0);
            assert(session.metadata[15].flags == session.resident.police_flags);
            assert(session.metadata[15].update == session.resident.launch_phase);
            /* The slot vector has to clear the station. A discarded copy of
             * the market result leaves the kind byte (4 or 0x7c) as X, and
             * the ship is removed on the next contact pass. */
            {
                fe2_object *ship = 0;
                unsigned ship_slot = 0;
                int64_t clearance = 0;
                for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
                    fe2_object *o = session.objects.records[slot];
                    int64_t gap = 0;
                    unsigned k;
                    if (!o || o->role != 11 || !session.objects.status[slot] ||
                        (session.objects.status[slot] & FE2_SLOT_REUSABLE)) continue;
                    if (seen_model[slot] == o->model && seen_beh[slot] == o->behaviour) continue;
                    for (k = 0; k < 3; ++k) {
                        int64_t d = (int64_t)((uint64_t)o->position_high[k] << 32 | o->position_low[k]) -
                                    (int64_t)((uint64_t)source->position_high[k] << 32 | source->position_low[k]);
                        if (d < 0) d = -d;
                        if (d > gap) gap = d;
                    }
                    ship = o;
                    ship_slot = slot;
                    clearance = gap;
                }
                assert(ship);
                printf("Police clear: model %04x behaviour %02x, %lld from the station\n",
                    ship->model, ship->behaviour, (long long)clearance);
                assert(clearance > 1000);
                for (i = 0; i < 60; ++i) frame();
                assert(session.objects.records[ship_slot] == ship);
                assert(session.objects.status[ship_slot] &&
                       !(session.objects.status[ship_slot] & FE2_SLOT_REUSABLE));
                assert(ship->model != 0xbc && ship->behaviour != 0x64);
            }
        }
    }
    printf("Weapon held for 100 frames: offence recorded, fine message queued, %u beams\n",
        game.beams);
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_crash(void)
{
    unsigned i;
    fe2_object *craft = session.objects.records[ship->handle];
    fe2_object *parent = session.objects.records[craft->parent_handle];
    fe2_contact_report report;
    uint64_t radius;
    int64_t at;
    unsigned slot, explosions;
    uint16_t hull;
    assert(parent && parent->role == 1);
    radius = (uint64_t)parent->model_scale_a[0] | (uint64_t)parent->model_scale_a[1] << 16 |
             (uint64_t)parent->model_scale_a[2] << 32;
    at = (int64_t)(radius - (radius >> 3));
    if (at < 1) at = 1;
    fprintf(stderr, "parent model %04x flags %04x radius %llx\n", parent->model, parent->flags,
            (unsigned long long)radius);
    hull = craft->hull;
    /* Just inside the parent, resting and aligned: a landing, not a collision. */
    craft->cargo_type = 0xffff;
    craft->velocity[0] = craft->velocity[1] = craft->velocity[2] = 0;
    craft->position_low[0] = (uint32_t)(uint64_t)at;
    craft->position_high[0] = (uint32_t)((uint64_t)at >> 32);
    craft->position_low[1] = craft->position_low[2] = 0;
    craft->position_high[1] = craft->position_high[2] = 0;
    craft->station_slot = 0;
    memset(craft->orientation, 0, sizeof craft->orientation);
    craft->orientation[0] = craft->orientation[1] = craft->orientation[8] = 0x7f00;
    game.contact.frame_step = session.simulation_step;
    report = fe2_contact_scan(&game.contact, &game.contact_services, craft, 1);
    fprintf(stderr, "Slow scan flags %02x hull %u -> %u status %02x vel %d\n", report.flags, hull,
            craft->hull, session.objects.status[craft->handle], craft->velocity[0]);
    assert(session.objects.status[craft->handle] != 0 && craft->hull == hull);
    assert(report.flags & 0x40);
    /* The flight pass turns that contact into the landed mode. */
    session.resident.flight_mode = FE2_FLIGHT_MANUAL;
    craft->cargo_type = 0xffff;
    craft->velocity[0] = craft->velocity[1] = craft->velocity[2] = 0;
    craft->position_low[0] = (uint32_t)(uint64_t)at;
    craft->position_high[0] = (uint32_t)((uint64_t)at >> 32);
    craft->position_low[1] = craft->position_low[2] = 0;
    craft->position_high[1] = craft->position_high[2] = 0;
    craft->station_slot = 0;
    frame();
    fprintf(stderr, "Slow flight mode %02x flags %02x hull %u\n", game.flight.state.mode,
            game.flight.state.contact_status, craft->hull);
    assert(session.objects.status[craft->handle] != 0 && craft->hull == hull);
    assert(game.flight.state.mode == FE2_FLIGHT_MINING || game.flight.state.mode == FE2_FLIGHT_ENTER_MINING);
    /* Faster than the 258h landing sum: the flight scan destroys the ship. */
    session.resident.flight_mode = FE2_FLIGHT_MANUAL;
    craft->velocity[0] = 0x10000;
    craft->velocity[1] = craft->velocity[2] = 0;
    craft->position_low[0] = (uint32_t)(uint64_t)at;
    craft->position_high[0] = (uint32_t)((uint64_t)at >> 32);
    craft->position_low[1] = craft->position_low[2] = 0;
    craft->position_high[1] = craft->position_high[2] = 0;
    craft->station_slot = 0;
    frame();
    explosions = 0;
    for (slot = 1; slot < FE2_POOL_SLOTS; ++slot)
        if (session.objects.records[slot] && session.objects.records[slot]->model == 0xbc) ++explosions;
    fprintf(stderr, "Fast flight mode %02x flags %02x hull %u status %02x explosions %u\n",
            game.flight.state.mode, game.flight.state.contact_status, craft->hull,
            session.objects.status[ship->handle], explosions);
    assert(game.flight.state.mode != FE2_FLIGHT_LANDED && game.flight.state.mode != FE2_FLIGHT_DOCKED &&
           game.flight.state.mode != FE2_FLIGHT_MINING && game.flight.state.mode != FE2_FLIGHT_ENTER_MINING);
    assert(explosions > 0 || session.objects.status[ship->handle] == 0 || craft->hull < hull);
    /* The wreck smokes while its countdown is in BC00h..C7FFh. One more
     * frame has to spawn that effect and keep moving the wreck. */
    {
        fe2_object *wreck = 0;
        uint32_t at;
        unsigned smoke = 0;
        for (slot = 1; slot < FE2_POOL_SLOTS; ++slot)
            if (session.objects.records[slot] && session.objects.records[slot]->model == 0xbc)
                wreck = session.objects.records[slot];
        assert(wreck);
        wreck->size_mantissa = 0xc400;
        wreck->size_exponent = 0xffff;
        wreck->orbit[0] = wreck->orbit[1] = 0;
        wreck->velocity[0] = 0x10000;
        wreck->update_countdown = 0;
        at = wreck->position_low[0];
        /* Kind 2 spawns on half the frames. A zero seed draws first. */
        for (i = 0; i < 6 && !smoke; ++i) {
            session.noise[0] = session.noise[1] = 0;
            frame();
            for (slot = 1; slot < FE2_POOL_SLOTS; ++slot)
                if (session.objects.records[slot] && session.objects.records[slot]->model == 0xc2) ++smoke;
        }
        fprintf(stderr, "Wreck smoke %u orbit %08x moved %u\n", smoke, wreck->orbit[0],
            wreck->position_low[0] != at);
        printf("Wreck smoke %u\n", smoke);
        assert(smoke > 0 && wreck->orbit[0] != 0);
    }
    printf("Planet impact: slow landing kept hull %u, fast impact explosions %u\n", hull, explosions);
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_view(void)
{
    unsigned ink_before, step;
    /* A fine message notifies the selected panel. That notice is not
     * "close the view": the cockpit must keep drawing the planet. */
    ink_before = view_ink();
    printf("Before offence: ink %u view %04x mode %02x\n", ink_before,
        game.flight.state.view_mode, session.resident.flight_mode);
    assert(ink_before > 10000);
    commit_offence(1);
    for (step = 0; step < 5; ++step) frame();
    printf("Launch clearance: ink %u view %04x mode %02x fine %u msg %04x\n",
        view_ink(), game.flight.state.view_mode, session.resident.flight_mode,
        session.resident.local_offence_fine, game.station.state.message.text);
    assert(session.resident.local_offence_fine > 0);
    assert((int8_t)game.flight.state.view_mode < 0);
    assert(view_ink() > 10000);
    if (session.resident.flight_mode == FE2_FLIGHT_DOCKED) {
        fe2_gameplay_key(&game, 0x41, 0, 0);
        frame();
        fe2_gameplay_key(&game, 0x41, 0, 1);
        for (step = 0; step < 10; ++step) frame();
        printf("Left without permission: ink %u view %04x mode %02x\n",
            view_ink(), game.flight.state.view_mode, session.resident.flight_mode);
        assert(session.resident.flight_mode != FE2_FLIGHT_DOCKED);
        assert((int8_t)game.flight.state.view_mode < 0);
        assert(view_ink() > 10000);
    }
    fe2_render_runtime_destroy(runtime);
    return 0;
}

int fe2_check_autopilot(void)
{
    unsigned i;
    /* Launch, then engage the way the autopilot key does: thrust off,
     * the station beside the ship as the target, mode 4 and behaviour
     * 20h. Docked mode 60h would rewrite that behaviour. The armed
     * station is held, then the timers dock it (74h, then 84h). */
    fe2_object *craft = session.objects.records[ship->handle];
    fe2_object *target = 0;
    unsigned slot;
    uint16_t hull;
    assert(craft);
    fe2_gameplay_mouse(&game, 263, 192, 1); frame(); frame();
    fe2_gameplay_mouse(&game, 263, 192, 0); frame();
    assert(session.resident.flight_mode != FE2_FLIGHT_DOCKED);
    for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
        fe2_object *o = session.objects.records[slot];
        if (!o || o->role != 1 || !(o->flags & 0x10) || o->parent_handle != craft->parent_handle)
            continue;
        target = o;
        break;
    }
    assert(target);
    station_frame();
    hull = craft->hull;
    craft->behaviour_target = target->handle;
    craft->behaviour = 0x20;
    craft->update_countdown = 0;
    game.flight.state.target = target->handle;
    game.flight.state.mode = FE2_FLIGHT_AUTOPILOT;
    game.flight.state.control_flags = (uint8_t)(game.flight.state.control_flags & (uint8_t)~FE2_CONTROL_THRUST);
    session.resident.navigation_target = target->handle;
    session.resident.flight_mode = FE2_FLIGHT_AUTOPILOT;
    session.resident.ship_control_flags = game.flight.state.control_flags;
    game.menu.state.time_exponent = 3;
    session.resident.game_time_exponent = 3;
    for (i = 0; i < 100 && craft->behaviour == 0x20; ++i) frame();
    assert(game.motion.time_exponent == 3);
    assert(game.motion.game_time_exponent == 3);
    game.menu.state.time_exponent = 0;
    session.resident.game_time_exponent = 0;
    for (i = 0; i < 20000 && session.resident.flight_mode == FE2_FLIGHT_AUTOPILOT; ++i) frame();
    printf("Autopilot: behaviour %02x, parent %u, target %u, mode %02x, hull %u, frames %u\n",
        craft->behaviour, craft->behaviour_parent, target->handle,
        session.resident.flight_mode, craft->hull, i);
    assert(!session.error && !host.errors);
    assert(session.objects.status[craft->handle] != 0);
    assert(craft->hull == hull);
    assert(craft->behaviour != 0x80);
    assert(session.resident.flight_mode == FE2_FLIGHT_DOCKED || session.resident.flight_mode == FE2_FLIGHT_ENTER_DOCKED);
    assert(craft->behaviour == 0x74 || craft->behaviour == 0x84);
    fe2_render_runtime_destroy(runtime); return 0;
}
