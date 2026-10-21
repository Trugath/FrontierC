/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 *
 * Contracts for fines and delivery offers. Each check states the rule it is
 * holding. Nothing here records a defect as the expected result.
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
static unsigned offences_calls, offences_request, status_contracts, bulletins, presents, polls;
static unsigned sample_calls;
static const uint16_t *widgets_set;
static uint16_t sample_result, sentence, drawn_text;
static int saw_notice, restart_mode, panels;

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
static void show_panel(void *opaque) { (void)opaque; panels++; }
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
    sample_calls++;
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
    status_contracts++;
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
void station_open_bulletin(station_context *c) { (void)c; bulletins++; }

static void begin(void)
{
    memset(state, 0, sizeof *state);
    memset(&args, 0, sizeof args);
    memset(&station_object, 0, sizeof station_object);
    memset(market, 0xff, sizeof market);
    append_count = heading_count = 0;
    offences_calls = offences_request = status_contracts = bulletins = presents = polls = 0;
    sample_calls = panels = 0;
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

static uint16_t board_word(void) { return (uint16_t)(market[0x7e] | market[0x7f] << 8); }

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

static void offer(uint32_t system, uint32_t flags, uint32_t fee, uint32_t detail)
{
    begin();
    state->saved[0] = system;
    state->saved[1] = flags;
    state->saved[2] = fee;
    state->saved[3] = detail;
    state->saved[5] = 0x5000;
    state->saved[6] = 0x1111;
}

/* A full list is refused. Cabins follow the group size unless the detail
 * says none are needed. A half advance is ten times half the fee. The board
 * entry is cleared and the deadline is 28 days on. */
void delivery_follows_the_offer(void)
{
    uint16_t text;
    prepare_station();

    offer(1, 0, 0, 0);
    state->contract_count = FE2_STATION_CONTRACTS;
    state->cabins_used = 2;
    station_talk_handler(&ctx, 0x32);
    CHECK(headings[0] == 0x9912);
    CHECK(state->cabins_used == 2 && status_contracts == 0 && board_word() == 0xffff);

    offer(1, 0, 0, 0x0c);
    state->cabins = 3;
    state->cabins_used = 1;
    station_talk_handler(&ctx, 0x32);
    CHECK(headings[0] == 0x9911);
    CHECK(state->cabins_used == 1 && status_contracts == 0);

    offer(1, 0x80000000u, 100, 0x04);
    state->cabins = 4;
    state->cabins_used = 1;
    state->balance = 20;
    station_talk_handler(&ctx, 0x32);
    CHECK(state->cabins_used == 3);
    CHECK(state->balance == 520);
    expect_reply(0x9914);
    CHECK(board_word() == 0);
    CHECK(station_get(&ctx, P2) == 1028);
    CHECK(status_contracts == 1);
    CHECK(appended[append_count - 1] == 0x983e);

    offer(1, 0, 0, 0x2000);
    state->cabins = 0;
    station_talk_handler(&ctx, 0x32);
    CHECK(state->cabins_used == 0 && status_contracts == 1 && board_word() == 0);

    offer(0x42, 0, 0, 0x2000);
    station_talk_handler(&ctx, 0x38);
    expect_reply(0x9919);
    CHECK((state->saved[1] & 0x10000000u) != 0);
    state->mission_flags = 1;
    station_talk_handler(&ctx, 0x32);
    CHECK(state->mission_flags == (1u | (1u << 3)));

    offer(7, 0x10000000u, 0, 0x2000);
    state->mission_flags = 1;
    station_talk_handler(&ctx, 0x38);
    expect_reply(0x9918);
    CHECK(state->saved[1] == 0x10000000u);
    station_talk_handler(&ctx, 0x32);
    CHECK(state->mission_flags == 1);

    offer(1, 0, 0, 0x0c);
    station_talk_handler(&ctx, 0x33);
    CHECK(station_get(&ctx, P1) == 4);
    expect_reply(0x9909);

    offer(1, 0, 0, 0x0600);
    station_talk_handler(&ctx, 0x34);
    text = (uint16_t)(((6u >> 1) & 0x1fu) + 0x9967u);
    CHECK(station_word(&ctx, P_TEXT) == text);
    offer(1, 0, 0, 0x0600);
    station_talk_handler(&ctx, 0x35);
    text = (uint16_t)(((6u >> 1) & 0xfu) + 0x9987u);
    CHECK(station_word(&ctx, P_TEXT) == text);

    offer(1, 0, 0, 0);
    station_talk_handler(&ctx, 0x37);
    CHECK(headings[0] == 0x9906);

    offer(1, 0, 1000, 0);
    state->rating = 4;
    station_talk_handler(&ctx, 0x39);
    CHECK(state->saved[2] == 1000);
    expect_reply(0x990a);

    offer(1, 0, 1000, 0);
    state->rating = 5;
    station_talk_handler(&ctx, 0x39);
    CHECK(state->saved[2] > 1000 && state->saved[2] < 2000);
    {
        uint32_t raised = state->saved[2];
        expect_reply(0x9910);
        station_talk_handler(&ctx, 0x39);
        CHECK(state->saved[2] == raised);
    }
    expect_reply(0x990f);

    offer(1, 0, 100, 0);
    state->rating = 0;
    sample_result = 1;
    station_talk_handler(&ctx, 0x3b);
    expect_reply(0x990e);
    CHECK((state->saved[1] & 0x40000000u) == 0);

    offer(1, 0, 100, 0);
    state->rating = 0;
    sample_result = 0;
    station_talk_handler(&ctx, 0x3b);
    expect_reply(0x990d);
    station_talk_handler(&ctx, 0x3b);
    expect_reply(0x990f);

    offer(1, 0, 0, 0x1200);
    sample_calls = 0;
    station_talk_handler(&ctx, 0x3b);
    CHECK(sample_calls == 0);
    expect_reply(0x990b);

    offer(1, 0, 0, 0);
    state->rating = 2;
    sample_calls = 0;
    station_talk_handler(&ctx, 0x3a);
    CHECK(sample_calls == 0);
    expect_reply(0x990c);

    offer(1, 0, 0, 0);
    state->rating = 0;
    sample_result = 0;
    sample_calls = 0;
    station_talk_handler(&ctx, 0x3a);
    CHECK(sample_calls == 1);
    expect_reply(0x990c);

    offer(1, 0, 0, 0);
    station_talk_handler(&ctx, 0x01);
    CHECK(panels == 1 && bulletins == 1);
    CHECK(widgets_set == fe2_station_widgets_station);
}

