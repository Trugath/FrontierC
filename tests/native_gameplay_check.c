/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Real resources, system generation, player creation, cockpit and physics. */
#include "host/asset_bind.h"
#include "host/gameplay.h"
#include "game/object_data.h"
#include "pool/pool.h"
#include "session/intro_overlay.h"
#include "mining/mining.h"
#include "station/market.h"
#include <stddef.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "native_assets.h"

static fe2_asset_archive game_assets;

static int asset_path_beside_exe(char *out, size_t cap)
{
#ifdef _WIN32
    char module[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, module, MAX_PATH);
    char *slash, *other;
    size_t prefix;
    if (length == 0 || length >= MAX_PATH) return -1;
    slash = strrchr(module, '\\');
    other = strrchr(module, '/');
    if (other && (!slash || other > slash)) slash = other;
    if (!slash) return -1;
    prefix = (size_t)(slash - module) + 1;
    if (prefix + sizeof "frontier.dat" > cap) return -1;
    memcpy(out, module, prefix);
    memcpy(out + prefix, "frontier.dat", sizeof "frontier.dat");
    return 0;
#else
    char module[4096];
    ssize_t length = readlink("/proc/self/exe", module, sizeof module - 1);
    char *slash;
    size_t prefix;
    if (length < 0) return -1;
    module[length] = 0;
    slash = strrchr(module, '/');
    if (!slash) return -1;
    prefix = (size_t)(slash - module) + 1;
    if (prefix + sizeof "frontier.dat" > cap) return -1;
    memcpy(out, module, prefix);
    memcpy(out + prefix, "frontier.dat", sizeof "frontier.dat");
    return 0;
#endif
}

static fe2_gameplay game;
static fe2_game_host host;
static fe2_session session;
static fe2_session_intro_overlay intro;
static fe2_render_runtime_resources resources;
static unsigned frames, ink, restarts;
/* Optional review artifacts, captured after the native overlay has drawn. */
static void snapshot(const char *name)
{
    const char *directory = getenv("FE2_GAMEPLAY_CAPTURE");
    fe2_video *v;
    FILE *file;
    char path[2048];
    unsigned i, c;
    if (!directory || !*directory) return;
    assert(snprintf(path, sizeof path, "%s/%s.ppm", directory, name) < (int)sizeof path);
    file = fopen(path, "wb"); assert(file);
    v = fe2_render_runtime_video(game.host->runtime);
    fprintf(file, "P6\n320 200\n255\n");
    for (i = 0; i < 64000; ++i) for (c = 0; c < 3; ++c)
        fputc(v->palette[v->pages[v->drawing][i]][c] * 255 / 63, file);
    assert(!fclose(file));
}
/* The arrest screen waits inside the frame. One key ends it. */
static int sentence_key(void *context)
{
    (void)context;
    fe2_input_restore(&session.pointer, ' ');
    return 0;
}
static void present(void *p, const uint8_t *pixels, const uint8_t *palette)
{
    unsigned i; (void)p; (void)palette; ++frames;
    for (i = 0; i < 53760; ++i) if (pixels[i] && pixels[i] != 127) ++ink;
}
static unsigned occupied(const fe2_session *s)
{
    unsigned n = 0, slot;
    for (slot = 1; slot < FE2_POOL_SLOTS; ++slot)
        if (s->objects.status[slot] && !(s->objects.status[slot] & FE2_SLOT_REUSABLE)) ++n;
    return n;
}
/* Records M11 owns (role 11): the system's population. */
static unsigned population(const fe2_session *s)
{
    unsigned n = 0, slot;
    for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
        fe2_object *o = s->objects.records[slot];
        if (s->objects.status[slot] && !(s->objects.status[slot] & FE2_SLOT_REUSABLE) && o->role == 11) ++n;
    }
    return n;
}
static unsigned patrols(const fe2_session *s)
{
    unsigned n = 0, slot;
    for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
        fe2_object *o = s->objects.records[slot];
        if (o && o->behaviour == 0x7c) ++n;
    }
    return n;
}
static void frame(void)
{
    fe2_session_result result;
    fe2_session_ticks(&session, 4); result = fe2_session_step(&session);
    /* A restart (a commander loaded) ends its frame early. */
    if (result == FE2_SESSION_RESTARTED) ++restarts;
    else if (result != FE2_SESSION_FRAME) fprintf(stderr, "failed frame %u result=%u mode=%02x error=%u host=%u overlay=%u last=%u busy=%u\n",
        frames, result, session.resident.flight_mode, session.error, host.errors, session.current_overlay, session.last_overlay, session.busy);
    assert((result == FE2_SESSION_FRAME || result == FE2_SESSION_RESTARTED) && !session.error && !host.errors);
}
/* Pixels of the flight picture outside the comms list. The list is drawn
 * over that picture; a cleared view leaves only the menu glyphs. */
static unsigned flight_picture(const uint8_t *page)
{
    unsigned n = 0, x, y;
    for (y = 0; y < 168; ++y) for (x = 0; x < 320; ++x) {
        if (x < 220 && y < 140) continue;
        if (page[y * 320u + x] != 0x7f) ++n;
    }
    return n;
}
static int row_has(const fe2_gameplay *g, uint16_t text)
{
    unsigned i;
    for (i = 0; i < g->station.state.row_count && i < 16; ++i)
        if (g->station.state.rows[i].text == text) return 1;
    return 0;
}
/* F4 opens the station screen; key 1..4 picks a row (scan codes 2..5). */
static void station_open(void)
{
    fe2_input_restore(&session.pointer, 0xf4);
    frame();
}
/* A list region code (80h + row; bit 10h sells). */
static void station_region(uint8_t code)
{
    unsigned i;
    fe2_input_restore(&session.pointer, code);
    for (i = 0; i < 10; ++i) frame();
}
static void station_row(unsigned row)
{
    unsigned i;
    fe2_gameplay_key(&game, (uint8_t)(1 + row), 0, 0); frame();
    fe2_gameplay_key(&game, (uint8_t)(1 + row), 0, 1);
    for (i = 0; i < 10; ++i) frame();
}
/* Move onto a cockpit icon and click it. The hit is published at the end
 * of the press frame and the overlay reads it on the next one. */
static void click_icon(unsigned x, unsigned y)
{
    fe2_gameplay_mouse(&game, x, y, 0);
    frame();
    fe2_gameplay_mouse(&game, x, y, 1);
    frame();
    fe2_gameplay_mouse(&game, x, y, 0);
    frame();
}
static uint32_t timed_head(unsigned i)
{
    const uint8_t *head = game.market.state.markets.bytes + FE2_TIMED_AT + i * FE2_TIMED_RECORD_BYTES;
    return (uint32_t)head[0] | (uint32_t)head[1] << 8 | (uint32_t)head[2] << 16 | (uint32_t)head[3] << 24;
}
/* Assembly image 0de7 stores a request selector with `mov word [si], ax`.
 * A head whose low word is clear and whose high word is not still fails
 * `cmp dword [si], 0`, so the slot stays occupied and is never reused. */
static void plant_stale_request_head(void)
{
    unsigned i;
    for (i = 0; i < FE2_TIMED_RECORDS; ++i) {
        uint8_t *head;
        if (timed_head(i) != 0) continue;
        head = game.market.state.markets.bytes + FE2_TIMED_AT + i * FE2_TIMED_RECORD_BYTES;
        head[2] = 0xef;
        head[3] = 0xbe;
        return;
    }
}
static uint16_t mining_action(uint16_t action)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    assert(session.calls.mining);
    return session.calls.mining(session.calls.context[17], action, &args);
}
/* One docked-to-flight pass: trade, take a mission, fly, deploy the mining
 * record bugs the assembly actually has. */
static void gameplay_loops(void)
{
    fe2_station_state *st = &game.station.state;
    fe2_object *player;
    uint32_t balance, stale;
    uint16_t cargo, heat, model;
    unsigned i, slot = FE2_TIMED_RECORDS;
    uint8_t *row;
    /* Flight timers, image 0dd4: `shl bx, 7` is a 16-bit shift. An interval
     * of 512 shifts to zero, so the heat gauge does not cool. An interval
     * of 1 shifts to 128 and does cool, which shows the timer ran. */
    session.coarse_time = 0;
    session.pools.clock[0] = 49710u - session.time_step;
    session.resident.heat_gauge = 40000;
    frame();
    heat = session.resident.heat_gauge;
    printf("Heat step 1: %u -> %u\n", 40000, heat);
    assert(heat == 40000u - 128u);
    session.coarse_time = 0;
    session.pools.clock[0] = 512u * 49710u - session.time_step;
    session.resident.heat_gauge = 40000;
    frame();
    printf("Heat shift wraps: %u stays %u\n", 40000, session.resident.heat_gauge);
    assert(session.resident.heat_gauge == 40000);
    station_open();
    station_row(4);
    printf("Trading: %u rows\n", st->row_count);
    assert(st->row_count > 10 && st->rows[0].text == 0x9831);
    balance = st->balance;
    cargo = st->cargo_used;
    station_row(1);
    printf("Bought: balance %u -> %u, cargo %u -> %u\n", balance, st->balance, cargo, st->cargo_used);
    assert(st->balance < balance && st->cargo_used == cargo + 1);
    balance = st->balance;
    cargo = st->cargo_used;
    station_region(0x90);
    printf("Sold: balance %u -> %u, cargo %u -> %u\n", balance, st->balance, cargo, st->cargo_used);
    assert(st->balance > balance && st->cargo_used == cargo - 1);
    plant_stale_request_head();
    for (i = 0; i < FE2_TIMED_RECORDS; ++i)
        if ((timed_head(i) >> 16) == 0xbeefu) slot = i;
    assert(slot < FE2_TIMED_RECORDS);
    stale = timed_head(slot);
    game.objects.state.rating = 0xffff;
    station_open();
    station_row(3);
    assert(st->row_count > 6 && st->rows[6].text == 0x9820);
    printf("Mission requests keep a stale selector high word: slot %u head %08lx\n",
           slot, (unsigned long)timed_head(slot));
    assert(timed_head(slot) == stale);
    station_region(0x86);
    station_region(0x80);
    printf("Mission taken: message %04x, contracts %u\n", st->message.text, session.resident.mission_count);
    assert(st->message.text == 0x99b3 && session.resident.mission_count > 0);
    station_open();
    station_row(1);
    fe2_input_restore(&session.pointer, 0xf1);
    frame();
    fe2_input_restore(&session.pointer, 0xf7);
    frame();
    for (i = 0; i < 8; ++i) frame();
    printf("Lift-off: mode %02x\n", session.resident.flight_mode);
    assert(session.resident.flight_mode != 0x60);
    /* Mining compaction, image 0a05 and 0b36: words at 33h, 31h, ... 1 are
     * copied from the next record. Byte 0 stays, and the copy writes the
     * following record's first byte. */
    assert(!mining_action(FE2_MINING_ADD_SLOT));
    assert(!mining_action(FE2_MINING_ADD_SLOT));
    assert(!mining_action(FE2_MINING_ADD_SLOT));
    assert(game.mining.state.count == 3);
    row = game.mining.state.records.bytes;
    row[0] = row[1] = row[2] = row[3] = 0;
    row[0x34] = 0x11;
    row[0x35] = 0xbb;
    row[0x34 + 0x33] = 0xcc;
    row[0x68] = 0x22;
    assert(mining_action(FE2_MINING_REMOVE_SLOT) == 0xffff);
    printf("Mining compaction: kept %02x, spilled %02x into the next record\n", row[0], row[0x34]);
    assert(row[0] == 0 && row[1] == 0xbb && row[0x33] == 0xcc && row[0x34] == 0x22);
    /* Loading, image 0940: `cmp dx, cx` is signed. A free capacity above
     * 32767 looks negative, so a small hopper is treated as too big and the
     * transfer uses that capacity, truncated to a byte on the way out. */
    player = session.objects.records[game.objects.state.player];
    model = player->model;
    player->model = 0x78;
    game.mining.state.selected_machine = player->handle;
    row = game.mining.state.records.bytes;
    row[FE2_MINING_OBJECT] = player->handle;
    row[FE2_MINING_HOPPER] = 10;
    row[FE2_MINING_CATEGORY] = 0;
    assert(fe2_mining_category_cargo[0] < FE2_MINING_COMMODITIES);
    session.pools.player_capacity = 40000;
    game.objects.state.cargo_space = 0;
    assert(!mining_action(FE2_MINING_LOAD));
    printf("Mining hopper signed compare: moved %u, hopper byte now %u\n",
           game.objects.state.cargo_space, row[FE2_MINING_HOPPER]);
    assert(game.objects.state.cargo_space == 40000);
    assert(row[FE2_MINING_HOPPER] == (uint8_t)(10u - (uint8_t)40000u));
    player->model = model;
}
static int basis_step(const int16_t *a, const int16_t *b)
{
    int i, step = 0;
    for (i = 0; i < 9; ++i) {
        int diff = (int)a[i] - (int)b[i];
        if (diff < 0) diff = -diff;
        if (diff > step) step = diff;
    }
    return step;
}
static void station_frame_absolute(void *opaque, fe2_object *record, int64_t position[3])
{
    unsigned i;
    (void)opaque; (void)record;
    for (i = 0; i < 3; ++i) position[i] = 0;
}
static void station_frame_relative(void *opaque, fe2_object *source, fe2_object *reference, int64_t difference[3])
{
    unsigned i;
    (void)opaque; (void)source; (void)reference;
    for (i = 0; i < 3; ++i) difference[i] = 0;
}
static void station_frame_estimate(void *opaque, fe2_object *copy, const uint16_t time[4])
{
    (void)opaque; (void)copy; (void)time;
}
static void station_frame_notify(void *opaque, uint16_t event, fe2_object *record)
{
    (void)opaque; (void)event; (void)record;
}
static void station_frame_probe(void *opaque, fe2_object *record, fe2_object *target,
                                const fe2_motion_probe *request, fe2_motion_hit *result)
{
    (void)opaque; (void)record; (void)target; (void)request;
    memset(result, 0, sizeof *result);
}
static void station_frame_rebuild(void *opaque, fe2_object *record)
{
    (void)opaque; (void)record;
}
/* A station in another frame is joined only while time is accelerated.
 * With the exponent clear, the same offset leaves the parent unchanged. */
static void station_frame(void)
{
    fe2_object ship, station;
    fe2_object_pool pool;
    fe2_motion motion;
    fe2_motion_services services;
    fe2_feedback held;
    memset(&ship, 0, sizeof ship);
    memset(&station, 0, sizeof station);
    memset(&pool, 0, sizeof pool);
    memset(&motion, 0, sizeof motion);
    memset(&services, 0, sizeof services);
    services.absolute_position = station_frame_absolute;
    services.relative_position = station_frame_relative;
    services.estimate_update = station_frame_estimate;
    services.notify = station_frame_notify;
    services.probe = station_frame_probe;
    services.rebuild_secondary_basis = station_frame_rebuild;
    motion.pool = &pool;
    motion.time_exponent = 3;
    ship.source_offset[2] = 1;
    station.handle = 2;
    station.parent_handle = 4;
    station.flags = 0x20;
    held = fe2_motion_steer(&motion, &services, &ship, &station, 0);
    assert(held.shift == 0);
    assert(ship.parent_handle == 4);
    memset(&ship, 0, sizeof ship);
    ship.source_offset[2] = 1;
    motion.time_exponent = 0;
    (void)fe2_motion_steer(&motion, &services, &ship, &station, 0);
    assert(ship.parent_handle == 0);
}
int main(int argc, char **argv)
{
    fe2_session_services services = {0};
    fe2_render_runtime *runtime;
    const fe2_object *ship;
    uint32_t before[3], throttle;
    int16_t before_basis[9]; unsigned i;
    uint16_t selection = argc > 1 ? (uint16_t)strtoul(argv[1], NULL, 0) : 1;
    char asset_file[4096];
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(!asset_path_beside_exe(asset_file, sizeof asset_file));
    assert(!fe2_asset_archive_load(&game_assets, asset_file));
    assert(!fe2_asset_bind_install(&game_assets));
    /* The shared fields really must alias: different overlays use the two views. */
    assert(offsetof(fe2_craft_fields, drive) == offsetof(fe2_flight_fields, drive));
    assert(offsetof(fe2_craft_fields, equipment) == offsetof(fe2_flight_fields, equipment));
    assert(offsetof(fe2_body_fields, temperature) == offsetof(fe2_flight_fields, shield_capacity));
    assert(sizeof(fe2_craft_fields) == 40 && sizeof(fe2_flight_fields) == 40 && sizeof(fe2_body_fields) == 40);
    native_resources(&resources, NULL); runtime = fe2_render_runtime_create(&resources); assert(runtime);
    native_resources(&resources, fe2_render_runtime_graphics(runtime));
    for (i = 0; i < sizeof(native_palette) / 3; ++i)
        fe2_video_palette(fe2_render_runtime_video(runtime), (uint8_t)i,
            native_palette[i * 3], native_palette[i * 3 + 1], native_palette[i * 3 + 2]);
    fe2_game_host_init(&host, runtime, &resources, present, NULL);
    fe2_game_host_session_services(&host, &services); fe2_session_create(&session, &services);
    fe2_game_host_attach(&host, &session);
    if (argc > 2 && !strcmp(argv[2], "intro")) {
        fe2_game_host_intro_services(&host, &intro.host);
        assert(!fe2_session_bind_intro_overlay(&session, &intro));
        assert(!fe2_session_startup(&session) && !fe2_session_intro(&session));
        session.input_mask = 0xff;
        for (i = 0; i < 120000; ++i) frame();
        fe2_input_restore(&session.pointer, '1');
        fe2_session_ticks(&session, 4);
        assert(fe2_session_step(&session) == FE2_SESSION_STARTED);
    }
    assert(!fe2_gameplay_start(&game, &host, NULL, selection));
    ship = game.objects.state.player_record;
    assert(ship && ship->role == 3);
    if (argc > 3 && !strcmp(argv[2], "original")) {
        const char *name = strrchr(argv[3], '\\');
        unsigned k, selected = FE2_COMMANDER_ENTRIES;
        if (!name) name = strrchr(argv[3], '/');
        name = name ? name + 1 : argv[3];
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
        if (argc > 4) {
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
            output = fopen(argv[4], "wb");
            assert(output && length > 6 && fwrite(encoded, 1, length, output) == length);
            fclose(output);
            printf("Native commander exported: %zu bytes\n", length);
        }
        printf("Original commander continued for 120 native frames\n");
        fe2_render_runtime_destroy(runtime);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "scanner")) {
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
    if (selection == 1) {
        assert(ship->model == 0x20 && ship->as.craft.drive == 2);
        assert(game.bodies == 7 && !strcmp(session.records[114].name, "Ross 154"));
    }
    for (i = 0; i < 200; ++i) frame();
    assert(ink > 1000);
    {
        /* Highest preset (no detail flag): 1FD1h = 1, 1FD3h = 2, 1FD5h = 0.
         * Zero shifts keep model parts hidden until much closer. */
        uint16_t solid = 0, surface = 0;
        int16_t layers = -1;
        fe2_render_runtime_detail(runtime, &solid, &surface, &layers);
        assert(game.menu.state.detail[0] == 1 && game.menu.state.detail[1] == 2 && game.menu.state.detail[2] == 0);
        assert(solid == 1 && surface == 2 && layers == 0);
        /* 0413h and 0414h are slot 15's flag and update bytes. M03, M11 and
         * M15 each store both, so the copies stay the header's one byte. */
        assert(session.metadata[15].flags == session.resident.police_flags);
        assert(session.metadata[15].update == session.resident.launch_phase);
        assert(session.resident.police_flags & 2u);
    }
    if (argc > 2 && !strcmp(argv[2], "arcturus")) {
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
    printf("Population: %u craft, %u live objects\n", population(&session), occupied(&session));
    assert(population(&session) > 0);
    {
        /* FF 92 03 is M09 mode 3: a given name, a space and a family name.
         * The seed is the dword at 07E4h. */
        fe2_text_resources text = host.resources->text;
        uint8_t name[64];
        size_t length = 0;
        text.pointer_value[0] = UINT32_C(0x12345678);
        assert(!fe2_text_expand(&text, 0x9203, name, sizeof name, &length));
        printf("NPC name: %s\n", name);
        assert(strchr((char *)name, ' ') && length > 3);
    }
    if (argc > 2 && !strcmp(argv[2], "navigation")) {
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
    if (argc > 2 && !strcmp(argv[2], "lave")) {
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
    if (argc > 2 && !strcmp(argv[2], "fine")) {
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
    if (argc > 2 && !strcmp(argv[2], "scene")) {
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
    if (argc > 2 && !strcmp(argv[2], "far")) {
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
    if (argc > 2 && !strcmp(argv[2], "exit")) {
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
    if (argc > 2 && !strcmp(argv[2], "commander")) {
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
    if (argc > 2 && !strcmp(argv[2], "ui")) {
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
    if (argc > 2 && !strcmp(argv[2], "autopilot")) {
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
    if (argc > 2 && !strcmp(argv[2], "fire")) {
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
            fe2_object *source;
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
            }
        }
        printf("Weapon held for 100 frames: offence recorded, fine message queued, %u beams\n",
            game.beams);
        fe2_render_runtime_destroy(runtime); return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "police")) {
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
    if (argc > 2 && !strcmp(argv[2], "dust")) {
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
    if (argc > 2 && !strcmp(argv[2], "views")) {
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
    if (argc > 2 && !strcmp(argv[2], "hud")) {
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
    if (argc > 2 && !strcmp(argv[2], "crash")) {
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
    if (argc > 2 && !strcmp(argv[2], "jump")) {
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
    if (argc > 2 && !strcmp(argv[2], "shipyard")) {
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
    if (argc > 2 && !strcmp(argv[2], "loops")) {
        gameplay_loops();
        fe2_render_runtime_destroy(runtime);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "mission")) {
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
    if (argc > 2 && !strcmp(argv[2], "station")) {
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
    if (argc > 2 && !strcmp(argv[2], "shipinfo")) {
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
        fe2_render_runtime_destroy(runtime); return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "dest")) {
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
    if (argc > 2 && !strcmp(argv[2], "infophone")) {
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
    if (argc > 2 && !strcmp(argv[2], "screens")) {
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
    if (selection != 1) {
        assert(!session.error && game.bodies > 0);
        printf("start %04x: %u frames, %u bodies, %u ink, mode=%02x\n", selection, frames, game.bodies, ink, session.resident.flight_mode);
        fe2_render_runtime_destroy(runtime);
        return 0;
    }
    assert(game.scenes >= 200 && game.submitted >= 200);
    /* Original F7 command transitions from docked to take-off. */
    fe2_gameplay_key(&game, 0x41, 0, 0);
    assert(session.pointer.input_code == 0xf7);
    frame(); fe2_gameplay_key(&game, 0x41, 0, 1);
    assert(session.resident.flight_mode != FE2_FLIGHT_DOCKED);
    memcpy(before, ship->position_low, sizeof(before));
    for (i = 0; i < 200; ++i) frame();
    assert(memcmp(before, ship->position_low, sizeof(before)) != 0);
    /* Held native throttle and turn keys exercise the original controller. */
    throttle = session.resident.throttle;
    memcpy(before_basis, ship->orientation, sizeof(before_basis));
    fe2_gameplay_key(&game, 0x1c, 0, 0); /* Enter: increase set speed. */
    fe2_gameplay_key(&game, FE2_KEY_A, 0, 0);
    for (i = 0; i < 100; ++i) frame();
    fe2_gameplay_key(&game, 0x1c, 0, 1); fe2_gameplay_key(&game, FE2_KEY_A, 0, 1);
    assert(session.input.keys[0x1c] && session.input.keys[FE2_KEY_A]);
    assert(session.resident.throttle != throttle);
    assert(memcmp(before_basis, ship->orientation, sizeof(before_basis)) != 0);
    for (i = 0; i < FE2_SESSION_OVERLAYS; ++i) assert(!session.locks[i]);
    printf("gameplay: %u frames, %u bodies, %u scenes, %u submissions, %u ink, mode=%02x\n",
        frames, game.bodies, game.scenes, game.submitted, ink, session.resident.flight_mode);
    fe2_render_runtime_destroy(runtime);
    return 0;
}

