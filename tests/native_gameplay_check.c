/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Real resources, system generation, player creation, cockpit and physics. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
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
#include "gameplay/harness.h"
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

fe2_gameplay game;
fe2_game_host host;
fe2_session session;
fe2_session_intro_overlay intro;
fe2_render_runtime_resources resources;
unsigned frames, ink, restarts;
fe2_render_runtime *runtime;
const fe2_object *ship;
uint16_t selection;
int check_argc;
char **check_argv;
/* Optional review artifacts, captured after the native overlay has drawn. */
void snapshot(const char *name)
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
int sentence_key(void *context)
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
unsigned occupied(const fe2_session *s)
{
    unsigned n = 0, slot;
    for (slot = 1; slot < FE2_POOL_SLOTS; ++slot)
        if (s->objects.status[slot] && !(s->objects.status[slot] & FE2_SLOT_REUSABLE)) ++n;
    return n;
}
/* Records M11 owns (role 11): the system's population. */
unsigned population(const fe2_session *s)
{
    unsigned n = 0, slot;
    for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
        fe2_object *o = s->objects.records[slot];
        if (s->objects.status[slot] && !(s->objects.status[slot] & FE2_SLOT_REUSABLE) && o->role == 11) ++n;
    }
    return n;
}
static int64_t axis_gap(uint32_t high_a, uint32_t low_a, uint32_t high_b, uint32_t low_b)
{
    int64_t d = (int64_t)((uint64_t)high_a << 32 | low_a) - (int64_t)((uint64_t)high_b << 32 | low_b);
    return d < 0 ? -d : d;
}

int64_t station_clearance(const fe2_object *ship, const fe2_object *station, int linked)
{
    int64_t gap = 0;
    unsigned k;
    for (k = 0; k < 3; ++k) {
        int64_t d = linked ? axis_gap(ship->position_high[k], ship->position_low[k],
                                      station->position_high[k], station->position_low[k])
                           : axis_gap(ship->position_high[k], ship->position_low[k], 0, 0);
        if (d > gap) gap = d;
    }
    return gap;
}

unsigned patrols(const fe2_session *s)
{
    unsigned n = 0, slot;
    for (slot = 1; slot < FE2_POOL_SLOTS; ++slot) {
        fe2_object *o = s->objects.records[slot];
        if (o && o->behaviour == 0x7c) ++n;
    }
    return n;
}
void frame(void)
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
/* Pixels of the 320x168 view that are not the cleared sky. A planet or
 * station fills thousands; the crosshair alone does not. */
unsigned view_ink(void)
{
    fe2_video *v = fe2_render_runtime_video(game.host->runtime);
    const uint8_t *page = v->pages[v->drawing];
    unsigned n = 0, x, y;
    for (y = 0; y < 168; ++y) for (x = 0; x < 320; ++x)
        if (page[y * 320u + x] != 0x7f) ++n;
    return n;
}
void commit_offence(uint16_t offence)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    fe2_args_object(&args, 9, game.objects.state.player_record);
    assert(session.calls.offence_event);
    session.calls.offence_event(session.calls.context[15], offence, &args);
}
unsigned flight_picture(const uint8_t *page)
{
    unsigned n = 0, x, y;
    for (y = 0; y < 168; ++y) for (x = 0; x < 320; ++x) {
        if (x < 220 && y < 140) continue;
        if (page[y * 320u + x] != 0x7f) ++n;
    }
    return n;
}
int row_has(const fe2_gameplay *g, uint16_t text)
{
    unsigned i;
    for (i = 0; i < g->station.state.row_count && i < 16; ++i)
        if (g->station.state.rows[i].text == text) return 1;
    return 0;
}
/* F4 opens the station screen; key 1..4 picks a row (scan codes 2..5). */
void station_open(void)
{
    fe2_input_restore(&session.pointer, 0xf4);
    frame();
}
/* A list region code (80h + row; bit 10h sells). */
void station_region(uint8_t code)
{
    unsigned i;
    fe2_input_restore(&session.pointer, code);
    for (i = 0; i < 10; ++i) frame();
}
void station_row(unsigned row)
{
    unsigned i;
    fe2_gameplay_key(&game, (uint8_t)(1 + row), 0, 0); frame();
    fe2_gameplay_key(&game, (uint8_t)(1 + row), 0, 1);
    for (i = 0; i < 10; ++i) frame();
}
/* Move onto a cockpit icon and click it. The hit is published at the end
 * of the press frame and the overlay reads it on the next one. */
void click_icon(unsigned x, unsigned y)
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
void plant_stale_request_head(void)
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
uint16_t mining_action(uint16_t action)
{
    fe2_call_args args;
    fe2_args_clear(&args);
    assert(session.calls.mining);
    return session.calls.mining(session.calls.context[17], action, &args);
}
/* One docked-to-flight pass: trade, take a mission, fly, deploy the mining
 * record bugs the assembly actually has. */
void gameplay_loops(void)
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
int basis_step(const int16_t *a, const int16_t *b)
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
void station_frame(void)
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
    uint32_t before[3], throttle;
    int16_t before_basis[9];
    unsigned i;
    check_argc = argc;
    check_argv = argv;
    selection = argc > 1 ? (uint16_t)strtoul(argv[1], NULL, 0) : 1;
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
    if (argc > 2 && !strcmp(argv[2], "intro"))
        fe2_check_intro();
    assert(!fe2_gameplay_start(&game, &host, NULL, selection));
    ship = game.objects.state.player_record;
    assert(ship && ship->role == 3);
    if (argc > 3 && !strcmp(argv[2], "original"))
        return fe2_check_original();
    if (argc > 2 && !strcmp(argv[2], "scanner"))
        return fe2_check_scanner();
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
    if (argc > 2 && !strcmp(argv[2], "arcturus"))
        return fe2_check_arcturus();
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
    if (argc > 2 && !strcmp(argv[2], "navigation"))
        return fe2_check_navigation();
    if (argc > 2 && !strcmp(argv[2], "lave"))
        return fe2_check_lave();
    if (argc > 2 && !strcmp(argv[2], "fine"))
        return fe2_check_fine();
    if (argc > 2 && !strcmp(argv[2], "scene"))
        return fe2_check_scene();
    if (argc > 2 && !strcmp(argv[2], "far"))
        return fe2_check_far();
    if (argc > 2 && !strcmp(argv[2], "exit"))
        return fe2_check_exit();
    if (argc > 2 && !strcmp(argv[2], "deliver"))
        return fe2_check_deliver();
    if (argc > 2 && !strcmp(argv[2], "commander"))
        return fe2_check_commander();
    if (argc > 2 && !strcmp(argv[2], "ui"))
        return fe2_check_ui();
    if (argc > 2 && !strcmp(argv[2], "autopilot"))
        return fe2_check_autopilot();
    if (argc > 2 && !strcmp(argv[2], "fire"))
        return fe2_check_fire();
    if (argc > 2 && !strcmp(argv[2], "spawn"))
        return fe2_check_spawn();
    if (argc > 2 && !strcmp(argv[2], "ports"))
        return fe2_check_ports();
    if (argc > 2 && !strcmp(argv[2], "police"))
        return fe2_check_police();
    if (argc > 2 && !strcmp(argv[2], "dust"))
        return fe2_check_dust();
    if (argc > 2 && !strcmp(argv[2], "views"))
        return fe2_check_views();
    if (argc > 2 && !strcmp(argv[2], "hud"))
        return fe2_check_hud();
    if (argc > 2 && !strcmp(argv[2], "crash"))
        return fe2_check_crash();
    if (argc > 2 && !strcmp(argv[2], "jump"))
        return fe2_check_jump();
    if (argc > 2 && !strcmp(argv[2], "shipyard"))
        return fe2_check_shipyard();
    if (argc > 2 && !strcmp(argv[2], "loops"))
        return fe2_check_loops();
    if (argc > 2 && !strcmp(argv[2], "mission"))
        return fe2_check_mission();
    if (argc > 2 && !strcmp(argv[2], "station"))
        return fe2_check_station();
    if (argc > 2 && !strcmp(argv[2], "shipinfo"))
        return fe2_check_shipinfo();
    if (argc > 2 && !strcmp(argv[2], "dest"))
        return fe2_check_dest();
    if (argc > 2 && !strcmp(argv[2], "infophone"))
        return fe2_check_infophone();
    if (argc > 2 && !strcmp(argv[2], "screens"))
        return fe2_check_screens();
    if (argc > 2 && !strcmp(argv[2], "view"))
        return fe2_check_view();
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

