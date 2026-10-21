/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "gameplay/harness.h"

int fe2_check_scene(void)
{
    fe2_object camera = {0}, object = *game.flight.state.ship;
    const uint8_t *descriptor = resources.models[0][object.model / 2].data;
    fe2_scaled reduced;
    int64_t distance[3] = {0, 0, 1 << 24};
    unsigned threshold = fe2_object_data_word(descriptor + 0x14), before, k;
    memset(session.objects.status, 0, sizeof session.objects.status);
    object.handle = 2; object.parent_handle = object.station_slot = 0;
    memset(object.position_low, 0, sizeof object.position_low);
    memset(object.position_high, 0, sizeof object.position_high);
    memset(object.orientation, 0, sizeof object.orientation);
    object.orientation[0] = object.orientation[4] = object.orientation[8] = 32767;
    memcpy(camera.orientation, object.orientation, sizeof camera.orientation);
    object.position_low[2] = 1 << 24; object.update_countdown = 0; object.frame_state = 0;
    session.objects.records[2] = &object; session.objects.status[2] = FE2_SLOT_PROJECTED;
    reduced = fe2_motion_scale_wide(distance);
    for (k = 0; k < 2; ++k) {
        object.model_parameter = (uint16_t)(reduced.shift - threshold + (k == 0));
        before = game.distant;
        assert(!fe2_render_runtime_begin(runtime, 127));
        assert(!fe2_gameplay_render_scene(&game, &camera));
        assert(object.screen_x != 0x8000 && object.screen_y != 0x8000);
        assert(game.distant == before + k);
        assert(!fe2_render_runtime_draw(runtime));
        snapshot(k ? "scene-distant" : "scene-near");
        assert(!fe2_render_runtime_abort(runtime));
    }
    object.model_parameter = 0;
    object.frame_state = 1; before = object.model;
    assert(!fe2_render_runtime_begin(runtime, 127));
    assert(!fe2_gameplay_render_scene(&game, &camera));
    assert(!(object.frame_state & 1) && object.model == before && object.model_parameter == 0);
    assert(!fe2_render_runtime_draw(runtime)); assert(!fe2_render_runtime_abort(runtime));
    object.model = 0x48; /* Native craft with all four mount pairs. */
    object.frame_state = 2 | 4 | 8 | 16;
    game.flight.state.turret_angle[1] = -1;
    for (k = 0; k < 3; ++k) {
        object.frame_state |= 2;

        assert(!fe2_render_runtime_begin(runtime, 127));
        assert(!fe2_gameplay_render_scene(&game, &camera));
        assert(!(object.frame_state & (2 | (4u << k))));
        assert(!fe2_render_runtime_draw(runtime)); assert(!fe2_render_runtime_abort(runtime));
    }
    object.frame_state = 2 | 16;
    game.flight.state.turret_angle[1] = 0;
    assert(!fe2_render_runtime_begin(runtime, 127));
    assert(!fe2_gameplay_render_scene(&game, &camera));
    assert(!(object.frame_state & 18));
    assert(!fe2_render_runtime_draw(runtime)); assert(!fe2_render_runtime_abort(runtime));
    assert(game.flight.host.render_frame_model);
    before = game.frame_models;
    assert(!fe2_render_runtime_begin(runtime, 127));
    game.flight.host.render_frame_model(&game, &camera);
    assert(game.frame_models == before + 1);
    assert(!fe2_render_runtime_draw(runtime)); assert(!fe2_render_runtime_abort(runtime));
    printf("Scene: near/distant queue, temporary model, four attachment branches and frame model passed\n");
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_far(void)
{
    static const unsigned counts[] = {0, 1, 6, 7, 8, 31, 32};
    fe2_object camera = game.flight.state.frame, *body = NULL;
    fe2_scaled reduced;
    int64_t difference[3];
    unsigned k, cutoff;
    for (k = 1; k < FE2_POOL_SLOTS; ++k)
        if (session.objects.status[k] && session.records[k].role == 1) { body = &session.records[k]; break; }
    assert(body);
    game.motion_services.relative_position(&game, body, &camera, difference);
    reduced = fe2_motion_scale_wide(difference);
    cutoff = fe2_object_data_word(resources.models[0][body->model / 2].data + 0x10);
    for (k = 0; k < sizeof counts / sizeof counts[0]; ++k) {
        body->model_parameter = (uint16_t)(reduced.shift - cutoff - counts[k]);
        body->screen_x = 123; body->screen_y = 45;
        assert(!fe2_render_runtime_begin(runtime, 0));
        assert(!fe2_gameplay_render_scene(&game, &camera));
        assert(body->screen_x == 0x8000 && body->screen_y == 0x8000);
        assert(body->update_period == (counts[k] == 0 || counts[k] == 32 ? 1 :
            counts[k] < 7 ? 1u << counts[k] : 127));
        assert(!fe2_render_runtime_abort(runtime));
    }
    printf("Far objects: stale projection invalidation passed\n");
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_exit(void)
{
    unsigned i;
    unsigned renders = host.renders, ink = 0, widest = 0, at;
    int16_t first_basis[9];
    const uint8_t *page;
    assert(!fe2_session_exit_scene(&session, 1));
    assert(session.frontend == FE2_SESSION_EXIT_SCENE);
    assert(session.exit_record.model == 6);
    frame();
    memcpy(first_basis, session.exit_record.orientation, sizeof first_basis);
    /* Distance starts at 250000 and the face is readable once it reaches
     * the 35000 near clamp, about 220 frames at one subtick each. */
    for (i = 0; i < 280; ++i) {
        unsigned minx = 320, maxx = 0, pixels = 0;
        frame();
        page = fe2_render_runtime_video(runtime)->pages[fe2_render_runtime_video(runtime)->drawing];
        for (at = 0; at < 320u * 168u; ++at) if (page[at]) {
            unsigned x = at % 320u;
            if (x < minx) minx = x; if (x > maxx) maxx = x;
            ++pixels;
        }
        if (maxx >= minx && maxx - minx + 1 > widest) widest = maxx - minx + 1;
        if (pixels > ink) ink = pixels;
    }
    assert(basis_step(first_basis, session.exit_record.orientation) > 0);
    assert(widest > 60 && ink > 5000 && host.renders > renders && !host.errors);
    fe2_input_restore(&session.pointer, ' '); frame();
    assert(session.frontend == FE2_SESSION_INTRO);
    for (i = 0; i < 10; ++i) frame();
    printf("Exit scene: spinning tombstone, %u ink, %u rendered records, returned to intro\n",
        ink, host.renders - renders);
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_infophone(void)
{
    fe2_input_restore(&session.pointer, 0xf3); frame();
    assert(game.status.state.screen_active && session.metadata[14].draw);
    fe2_input_restore(&session.pointer, 0xf4); frame();
    assert(!game.status.state.screen_active && !session.metadata[14].draw);
    assert(game.station.state.screen_active && row_has(&game, 0x9818));
    fe2_input_restore(&session.pointer, 0xf1); frame();
    fe2_input_restore(&session.pointer, 0xf7); frame();
    assert(session.resident.flight_mode != 0x60);
    {
        fe2_video *video = fe2_render_runtime_video(runtime);
        unsigned picture, step;
        frame();
        picture = flight_picture(video->pages[video->drawing]);
        fe2_input_restore(&session.pointer, 0xf4); frame();
        assert(game.station.state.ui_flags & 1u);
        assert(row_has(&game, 0x9803));
        assert(picture > 20);
        assert(flight_picture(video->pages[video->drawing]) * 2 > picture);
        /* One subtick a frame; the list's time is 0x54f42. */
        for (step = 0; step < 400 && (game.station.state.ui_flags & 1u); ++step) frame();
        assert(!(game.station.state.ui_flags & 1u));
        printf("In-flight phone keeps the view (%u pixels) and times out after %u frames\n",
               picture, step);
    }
    fe2_input_restore(&session.pointer, 0xf3); frame();
    assert(game.status.state.screen_active && session.metadata[14].draw);
    fe2_input_restore(&session.pointer, 0xf4); frame();
    frame();
    assert(!game.status.state.screen_active && !session.metadata[14].draw);
    assert(game.station.state.ui_flags & 1u);
    assert(row_has(&game, 0x9803));
    assert((int8_t)session.resident.scanner_view_mode < 0);
    printf("Phone replaces the info page, docked and in flight\n");
    fe2_render_runtime_destroy(runtime); return 0;
}

int fe2_check_screens(void)
{
    unsigned i;
    fe2_video *video = fe2_render_runtime_video(runtime);
    const uint8_t *page;
    unsigned x, y, ink;
    printf("Cockpit flag %02x\n", session.resident.scanner_view_mode);
    assert((int8_t)session.resident.scanner_view_mode < 0);
    game.map_pixels = 0;
    /* A cockpit look must not tilt the chart. Opening it clears the stick. */
    session.pointer.redirected[0] = 400;
    session.pointer.redirected[1] = -200;
    fe2_input_restore(&session.pointer, 0xf2);
    frame();
    assert(session.pointer.redirected[0] == 0 && session.pointer.redirected[1] == 0);
    assert(game.map.state.mode == -1);
    assert(session.metadata[2].update && session.metadata[2].draw);
    assert(session.selected_overlay == 2);
    assert(game.map_pixels > 20);
    page = video->pages[video->drawing];
    ink = 0;
    for (y = 0; y < 168; ++y) for (x = 0; x < 320; ++x) if (page[y * 320u + x] >= 128) ++ink;
    printf("Galactic map: %u stars, %u on screen, selection %s\n", game.map_pixels, ink,
        game.map.state.selected_name);
    snapshot("galactic");
    assert(ink > 10);
    /* F10 is the galactic view. Its marker and scale bar used to hit
     * unbound drawing services and stop the session. */
    fe2_input_restore(&session.pointer, 0xfa); frame();
    assert(game.map.state.mode == 2 && !session.error && !host.errors && game.map.status == 0);
    fe2_input_restore(&session.pointer, 0xfa); frame();
    assert(game.map.state.mode == -1 && !session.error && !host.errors);
    {
        fe2_text_resources labels = resources.text;
        uint8_t rendered[256];
        char expected[256], first[256] = {0};
        unsigned label, different = 0;
        labels.value[2] = game.map.state.center_y >> 16;
        labels.value[3] = game.map.state.center_x >> 16;
        for (label = 0; label < 4; ++label) {
            labels.value[4] = label;
            assert(!fe2_map_name(&game.map.services, (uint16_t)labels.value[2],
                (uint16_t)labels.value[3], (uint16_t)label, expected, sizeof expected));
            assert(!fe2_text_expand(&labels, 0x8400, rendered, sizeof rendered, NULL));
            assert(!strcmp((char *)rendered, expected));
            if (!label) strcpy(first, expected);
            else different |= strcmp(first, expected) != 0;
        }
        assert(different);
        printf("Galactic labels use each star's sector and index\n");
    }
    {
        fe2_text_resources text = resources.text;
        uint8_t rendered[64];
        text.value[0] = 5;
        text.value[1] = (uint32_t)(int32_t)-3;
        assert(!fe2_text_expand(&text, 0x84ae, rendered, sizeof rendered, NULL));
        assert(!strcmp((char *)rendered, "Sector: 5,-3"));
        text.value[0] = 12;
        assert(!fe2_text_expand(&text, 0x84af, rendered, sizeof rendered, NULL));
        assert(!strcmp((char *)rendered, "Fuel Required: 12t"));
        text.value[0] = 0;
        assert(!fe2_text_expand(&text, 0x84af, rendered, sizeof rendered, NULL));
        assert(!strcmp((char *)rendered, "Fuel Required: -"));
        printf("Map status text uses the travel figures\n");
        game.flight.state.jump_range = 0x320;
        game.hyperspace.state.jump_range = 0x320;
        session.resident.jump_range = 0x320;
        frame();
        assert(game.map.state.marker_range == 0x320);
        game.flight.state.jump_range = 0;
        game.hyperspace.state.jump_range = 0;
        session.resident.jump_range = 0;
    }
    {
        fe2_map_details details;
        uint8_t ui_palette[128][3];
        memcpy(ui_palette, video->palette, sizeof ui_palette);
        const fe2_catalogue_record *record = fe2_system_find(game.catalogue.catalogue, game.map.state.selection_reference);
        assert(!game.map.services.system_details(game.map.services.opaque, game.map.state.selection_reference, 1, &details));
        assert(details.population_text == record->population_band && !memcmp(details.trade, record->trade_codes, 31));
        fe2_input_restore(&session.pointer, 0xf6); frame();
        assert(game.map.status == 0 && game.map.state.mode == 1 && game.map_page_valid);
        assert(!memcmp(ui_palette, video->palette, 127*3));
        assert(game.map.state.bodies.records && game.map.state.named_bodies > 0);
        for (i = 0; i < 5; ++i) frame();
        if (memcmp(video->pages[video->drawing], game.map_page, 53760)) {
            unsigned differences = 0, at;
            for (at = 0; at < 53760; ++at) if (video->pages[video->drawing][at] != game.map_page[at]) {
                if (differences++ < 5) fprintf(stderr, "Map changed at %u,%u: %u -> %u\n", at % 320, at / 320,
                    game.map_page[at], video->pages[video->drawing][at]);
            }
            fprintf(stderr, "Map differences %u, view %04x mode %d\n", differences, game.flight.state.view_mode, game.map.state.mode);
        }
        assert(!memcmp(video->pages[video->drawing], game.map_page, 53760));
        printf("System details: %u named bodies, page preserved\n", game.map.state.named_bodies);
        snapshot("system");
        fe2_input_restore(&session.pointer, 0xf7); frame();
        assert(game.map.status == 0);
        printf("Trade details: population %u, government %u\n", details.population_text, details.government);
        snapshot("trade");
        fe2_input_restore(&session.pointer, 0xf8); frame();
        assert(game.map.status == 0);
        printf("Government details rendered\n");
        snapshot("government");
        fe2_input_restore(&session.pointer, 0x81); frame();
        assert(game.map.status == 0);
        printf("Body details rendered\n");
        snapshot("body");
        fe2_input_restore(&session.pointer, 0xf6); frame();
        assert(game.map.state.mode < 0);
        assert(!memcmp(ui_palette, video->palette, 127*3));
    }
    fe2_input_restore(&session.pointer, 0xf1);
    frame();
    assert(game.map.state.mode == 0 && !session.metadata[2].update);
    fe2_input_restore(&session.pointer, 0xf3);
    frame();
    assert(game.status.state.screen_active);
    page = video->pages[video->drawing];
    ink = 0;
    for (y = 0; y < 168; ++y) for (x = 0; x < 320; ++x) if (page[y * 320u + x]) ++ink;
    printf("Commander status: page %u, ink %u\n", game.status.state.page, ink);
    assert(game.status.state.page == 0 && ink > 20);
    printf("Status preview: %u draws, %u changed pixels\n", game.previews, game.preview_pixels);
    assert(game.previews > 0 && game.preview_pixels > 0);
    snapshot("status");
    fe2_input_restore(&session.pointer, 0xf1);
    frame();
    assert(!game.status.state.screen_active);
    fe2_input_restore(&session.pointer, 0xf4);
    frame();
    assert(game.station.state.screen_active || (game.station.state.ui_flags & 1u));
    assert(game.station.state.row_count >= 1);
    page = video->pages[video->drawing];
    ink = 0;
    for (y = 0; y < 168; ++y) for (x = 0; x < 320; ++x) if (page[y * 320u + x]) ++ink;
    printf("Phone docked: %u rows, first %04x, panel ink %u\n", game.station.state.row_count,
        game.station.state.rows[0].text, ink);
    assert(ink > 20 && row_has(&game, 0x9818) && row_has(&game, 0x980a));
    fe2_input_restore(&session.pointer, 0xf1);
    frame();
    fe2_input_restore(&session.pointer, 0xf7);
    frame();
    assert(session.resident.flight_mode != 0x60);
    fe2_input_restore(&session.pointer, 0xf4);
    frame();
    assert(game.station.state.ui_flags & 1u);
    assert(row_has(&game, 0x9803));
    printf("Phone flight: %u rows, first %04x\n", game.station.state.row_count,
        game.station.state.rows[0].text);
    fe2_input_restore(&session.pointer, 0xf3); frame();
    assert(game.status.state.screen_active);
    fe2_input_restore(&session.pointer, 0xf4); frame();
    frame();
    assert(!game.status.state.screen_active && !session.metadata[14].draw);
    assert((game.station.state.ui_flags & 1u) && row_has(&game, 0x9803));
    assert((int8_t)session.resident.scanner_view_mode < 0);
    printf("Phone replaces the info page in flight\n");
    fe2_input_restore(&session.pointer, 0xf1);
    frame();
    click_icon(24, 192);
    printf("Map icon: selected %u, mode %d, panel %u, pointer %u,%u\n", session.selected_overlay,
           game.map.state.mode, game.panels.state.selection, session.pointer.x, session.pointer.y);
    assert(game.map.state.mode == -1);
    click_icon(8, 192);
    assert(game.map.state.mode == 0);
    click_icon(40, 192);
    assert(game.status.state.screen_active && game.status.state.page == 0);
    click_icon(8, 192);
    assert(!game.status.state.screen_active);
    click_icon(56, 192);
    assert(game.station.state.ui_flags & 1u);
    printf("Icon clicks: map, status, flight phone\n");
    fe2_render_runtime_destroy(runtime); return 0;
}
