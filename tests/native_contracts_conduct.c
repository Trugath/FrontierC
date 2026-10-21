/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 *
 * Contracts for fines. Each check states the rule it is holding. Nothing here
 * records a defect as the expected result.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "station_internal.h"

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "contract failed: %s\n", #cond); abort(); } \
} while (0)

/* ---- tables the station sources name ------------------------------------ */

const uint8_t fe2_station_colours[7] = {1, 2, 3, 4, 5, 6, 7};
const uint16_t fe2_station_widgets_board[86];
const uint16_t fe2_station_widgets_station[31];
const fe2_station_mission_destination fe2_station_mission_destinations[] = {{0x42u, 3}};
const unsigned fe2_station_mission_destination_count = 1;

/* ---- station screen stand-ins: the checks record the rows and replies --- */

static fe2_station_state *state;
static fe2_call_args args;
static fe2_station_services services;
static station_context ctx;
static fe2_object station_object;
static uint8_t market[256];

static uint16_t appended[16];
static unsigned append_count;
static uint16_t headings[8];
static uint16_t heading_text[8];
static unsigned heading_count;
static unsigned offences_calls, offences_request, presents, polls;
static const uint16_t *widgets_set;
static uint16_t sample_result, sentence, drawn_text;
static int saw_notice, restart_mode;

static fe2_object *lookup_object(void *opaque, fe2_ref ref)
{
    (void)opaque; (void)ref;
    return &station_object;
}
static void set_widgets(void *opaque, const uint16_t *widgets)
{
    (void)opaque;
    widgets_set = widgets;
}
static void show_panel(void *opaque) { (void)opaque; }
static void clear_regions(void *opaque) { (void)opaque; }
static void clear_drawing(void *opaque) { (void)opaque; }
static void set_controls(void *opaque, uint16_t layout_ax, uint16_t layout_bx, uint8_t buttons)
{
    (void)opaque; (void)layout_ax; (void)layout_bx; (void)buttons;
}
static void present(void *opaque)
{
    (void)opaque;
    presents++;
    if (state->notice_wait) saw_notice = 1;
}
static void draw_notice(void *opaque, uint16_t text, uint8_t colour, uint16_t x, uint16_t y,
                        const fe2_call_args *call)
{
    (void)opaque; (void)colour; (void)x; (void)y; (void)call;
    drawn_text = text;
}
static int poll_key(void *opaque, uint16_t *key)
{
    (void)opaque;
    *key = 1;
    return ++polls > 1;
}
static uint16_t random_below(void *opaque, uint16_t bound)
{
    (void)opaque; (void)bound;
    return sentence;
}
static fe2_station_sample bounded_sample(void *opaque, uint16_t bound, uint32_t seed_a, uint32_t seed_b)
{
    fe2_station_sample sample;
    (void)opaque; (void)bound;
    sample.result = sample_result;
    sample.seed_a = seed_a;
    sample.seed_b = seed_b;
    return sample;
}
static void restart_session(void *opaque, uint16_t mode)
{
    (void)opaque;
    restart_mode = (int)mode;
}
static void stop_time(void *opaque) { (void)opaque; }
static void restore_time(void *opaque) { (void)opaque; }
static void offences(void *opaque, uint16_t request, const fe2_call_args *call)
{
    (void)opaque; (void)call;
    offences_calls++;
    offences_request = request;
}
static void status_contract(void *opaque, const fe2_call_args *call)
{
    (void)opaque; (void)call;
}

void station_reset_rows(station_context *c) { (void)c; }
void station_append(station_context *c, uint16_t text)
{
    (void)c;
    CHECK(append_count < 16);
    appended[append_count++] = text;
}
void station_show_heading(station_context *c, uint16_t text)
{
    CHECK(heading_count < 8);
    headings[heading_count] = text;
    heading_text[heading_count] = station_word(c, P_TEXT);
    heading_count++;
}
void station_show_result(station_context *c, uint16_t text, uint16_t action)
{
    (void)c; (void)text; (void)action;
}
void station_draw_status(station_context *c) { (void)c; }
void station_open_bulletin(station_context *c) { (void)c; }

static void begin(void)
{
    memset(state, 0, sizeof *state);
    memset(&args, 0, sizeof args);
    memset(&station_object, 0, sizeof station_object);
    memset(market, 0xff, sizeof market);
    append_count = heading_count = 0;
    offences_calls = offences_request = presents = polls = 0;
    widgets_set = NULL;
    saw_notice = 0;
    restart_mode = -1;
    drawn_text = 0;
    state->market = market;
    state->station = 1;
    state->day = 1000;
    station_object.target_seed = 0x12345678u;
    ctx.s = state;
    ctx.g = &services;
    ctx.a = &args;
    ctx.stop = 0;
}

static void prepare_station(void)
{
    static int ready;
    if (ready) return;
    state = calloc(1, sizeof *state);
    CHECK(state);
    memset(&services, 0, sizeof services);
    services.opaque = &ctx;
    services.object = lookup_object;
    services.set_widgets = set_widgets;
    services.show_panel = show_panel;
    services.clear_regions = clear_regions;
    services.clear_drawing = clear_drawing;
    services.set_controls = set_controls;
    services.present = present;
    services.draw_text_shadowed = draw_notice;
    services.poll_key = poll_key;
    services.random_below = random_below;
    services.bounded_sample = bounded_sample;
    services.restart_session = restart_session;
    services.stop_time = stop_time;
    services.commander_restore_time = restore_time;
    services.offences_police = offences;
    services.status_contract = status_contract;
    ready = 1;
}

static void expect_rows(uint16_t first, uint16_t count)
{
    unsigned i;
    CHECK(append_count == (unsigned)count + 1);
    for (i = 0; i < append_count; ++i) CHECK(appended[i] == (uint16_t)(first + i));
}

static void expect_reply(uint16_t text)
{
    CHECK(heading_count >= 1);
    CHECK(headings[heading_count - 1] == 0x9916);
    CHECK(heading_text[heading_count - 1] == text);
}

/* A reported dealing adds 5000. A fine that would pass the signed end of the
 * total is not recorded. The police office interviews either way. */
void fines_follow_the_charge_rules(void)
{
    prepare_station();
    sentence = 10;

    begin();
    state->deferred = 100;
    station_report_dealing(&ctx);
    CHECK(state->heading == 0x98c1);
    CHECK(offences_calls == 1 && offences_request == 5);
    CHECK(state->deferred == 5100);
    CHECK(widgets_set == fe2_station_widgets_board);
    CHECK(headings[0] == 0x9932);
    expect_rows(0x984a, 6);

    begin();
    state->deferred = 0x7ffff000u;
    station_report_dealing(&ctx);
    CHECK(state->deferred == 0x7ffff000u);
    expect_rows(0x984a, 6);

    begin();
    state->deferred = 100;
    station_charge_handler(&ctx, 0x10);
    CHECK(state->deferred == 100);
    CHECK(headings[0] == 0x9932);
    expect_rows(0x984a, 7);

    begin();
    station_charge_handler(&ctx, 0x10);
    CHECK(state->deferred == 0);
    CHECK(headings[0] == 0x993c);
    expect_rows(0x9851, 0);

    begin();
    state->deferred = 10;
    sample_result = 1;
    station_charge_handler(&ctx, 0x4e);
    CHECK(state->deferred == 20010);
    CHECK(offences_calls == 1 && offences_request == 6);
    expect_reply(0x9937);

    begin();
    state->deferred = 0x7ffffff0u;
    sample_result = 1;
    station_charge_handler(&ctx, 0x4f);
    CHECK(state->deferred == 0x7ffffff0u);

    begin();
    state->deferred = 40;
    state->balance = 1000;
    sample_result = 0;
    station_charge_handler(&ctx, 0x4e);
    CHECK(state->balance == 900 && state->deferred == 0);
    expect_reply(0x9938);

    begin();
    state->balance = 1000;
    state->deferred = 40;
    sample_result = 0;
    station_charge_handler(&ctx, 0x4f);
    CHECK(state->balance == 500 && state->deferred == 0);

    begin();
    state->balance = 3000;
    state->deferred = 40;
    sample_result = 0;
    station_charge_handler(&ctx, 0x50);
    CHECK(state->balance == 500 && state->deferred == 0);

    begin();
    state->balance = 50;
    state->deferred = 40;
    sample_result = 0;
    station_charge_handler(&ctx, 0x4e);
    CHECK(state->balance == 50 && state->deferred == 40);
    CHECK(headings[0] == 0x9939);

    begin();
    state->balance = 1000;
    state->deferred = 400;
    station_charge_handler(&ctx, 0x4a);
    CHECK(state->balance == 600 && state->deferred == 0);
    expect_reply(0x9933);

    begin();
    state->balance = 100;
    state->deferred = 5000;
    station_charge_handler(&ctx, 0x4a);
    CHECK(state->balance == 100 && state->deferred == 5000);
    CHECK(station_get(&ctx, P3) == 5000);
    expect_reply(0x9934);

    begin();
    state->balance = 1000;
    state->deferred = 100;
    station_charge_handler(&ctx, 0x4b);
    CHECK(state->balance == 1000 && state->deferred == 100);
    expect_reply(0x9934);

    begin();
    state->deferred = 10;
    station_charge_handler(&ctx, 0x4c);
    CHECK(state->deferred == 2510);
    expect_reply(0x9935);

    begin();
    state->deferred = 0x7ffffff0u;
    station_charge_handler(&ctx, 0x4c);
    CHECK(state->deferred == 0x7ffffff0u);

    begin();
    state->deferred = 80;
    station_charge_handler(&ctx, 0x4d);
    CHECK(state->deferred == 80);
    expect_reply(0x9936);

    begin();
    state->selected_text = 0x4455;
    state->game_flags = 0;
    station_process_deferred(&ctx);
    CHECK(drawn_text == 0x4455 && saw_notice && state->notice_wait == 0);
    CHECK(presents >= 2 && polls >= 2);
    CHECK((state->game_flags & 0x20) != 0);
    CHECK(state->day == 1000u + sentence + 365u);
    CHECK(restart_mode == 1 && ctx.stop == 1);
}
