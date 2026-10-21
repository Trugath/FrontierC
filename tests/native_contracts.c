/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 *
 * Contracts for code the gameplay suite never reaches. Each check states the
 * rule it is holding: a page shows the right text, a price is the stored
 * price, equipment does what its bit says. Nothing here records a defect as
 * the expected result.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/call_args.h"
#include "math3.h"
#include "render/texture.h"

void equipment_bits_drive_the_effect(void);
#include "people_internal.h"
#include "interaction_text.h"
#include "market_internal.h"
#include "status_internal.h"
#include "systems_internal.h"

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "contract failed: %s\n", #cond); abort(); } \
} while (0)

/* ---- render material table (filled by the texture checks) --------------- */

static uint8_t material_bytes[32];
const uint8_t *fe2_render_material_data = material_bytes;
uint32_t fe2_render_material_bytes;

/* ---- system tables the station code names but these checks do not walk -- */

static const uint32_t profile_u32[1];
static const uint16_t profile_u16[1];
const uint32_t *fe2_systems_profile_thresholds = profile_u32;
const uint32_t *fe2_systems_profile_or_object_references = profile_u32;
const uint16_t *fe2_systems_profile_kind = profile_u16;
const uint16_t *fe2_systems_profile_descriptor = profile_u16;
const uint16_t *fe2_systems_profile_temperature = profile_u16;
const uint16_t *fe2_systems_profile_subtree_position = profile_u16;
const uint16_t *fe2_systems_profile_curve = profile_u16;

static uint16_t generated_descriptors[32];
static uint16_t station_orbit[32];
const uint16_t *fe2_systems_generated_descriptors = generated_descriptors;
const uint16_t *fe2_systems_station_orbit_parameters = station_orbit;

fe2_system_record fe2_systems_sol_daedalus;
fe2_system_record fe2_systems_sol_galileo;
fe2_system_record fe2_systems_sol_abraham_lincoln;
fe2_system_record fe2_systems_sol_apollonius_city;

fe2_profile_rule fe2_systems_profile_rule(unsigned profile)
{
    (void)profile;
    return 0;
}

/* ---- market collaborators the daily update calls ------------------------ */

static const uint8_t market_text_bytes[1];
const uint8_t *fe2_market_text_directory = market_text_bytes;
const uint8_t fe2_status_page_sprite_bytes[17];

static unsigned request_generations, price_refreshes, text_generations, category_appends;
static int text_count_while_generating;
static uint16_t appended_kinds[4];

void fe2_market_generate_request(fe2_market_state *s, const fe2_market_services *game)
{
    (void)s;
    (void)game;
    request_generations++;
}

void fe2_market_refresh_prices(fe2_market_state *s, const fe2_market_services *game, uint32_t market)
{
    (void)s;
    (void)game;
    (void)market;
    price_refreshes++;
}

void fe2_market_generate_text(fe2_market_state *s, const fe2_market_services *game, fe2_market_writer *w)
{
    (void)game;
    text_generations++;
    text_count_while_generating = (int)fe2_market_word(s, w->market + FE2_MARKET_TEXT_COUNT);
}

void fe2_market_append_category(fe2_market_state *s, const fe2_market_services *game, fe2_market_writer *w)
{
    (void)game;
    (void)w;
    CHECK(category_appends < 4);
    appended_kinds[category_appends++] = (uint16_t)s->draft.kind;
}

static const uint16_t *rolls;
static unsigned rolls_left;

static uint16_t scripted_roll(void *opaque, uint16_t bound)
{
    uint16_t value;
    (void)opaque;
    CHECK(rolls_left > 0);
    CHECK(bound > 0);
    value = *rolls++;
    rolls_left--;
    CHECK(value < bound);
    return value;
}

static void use_rolls(fe2_market_services *game, const uint16_t *script, unsigned count)
{
    memset(game, 0, sizeof *game);
    game->random_below = scripted_roll;
    rolls = script;
    rolls_left = count;
    request_generations = price_refreshes = text_generations = category_appends = 0;
}

static fe2_market_state *new_market(void)
{
    fe2_market_state *s = calloc(1, sizeof *s);
    CHECK(s != 0);
    return s;
}

/* ---- render contexts ----------------------------------------------------- */

static void render_contexts_initialize_what_they_own(void)
{
    fe2_small_context *small = malloc(sizeof *small);
    fe2_large_context *large = malloc(sizeof *large);
    CHECK(small != 0 && large != 0);
    memset(small, 0x3c, sizeof *small);
    memset(large, 0x3c, sizeof *large);
    large->last = 0x1234;

    fe2_math_init_small_context(small);
    CHECK(small->header[0] == 0 && small->header[sizeof small->header - 1] == 0);
    CHECK(small->body[0] == 0x3c && small->body[sizeof small->body - 1] == 0x3c);
    CHECK(small->limit_low == 0xffff && small->limit_high == 0xa8 && small->limit_end == 0xffff);

    fe2_math_init_large_context(large);
    CHECK(large->first == 0xa8 && large->second == 0xffff && large->last == 0x1234);
    CHECK(large->header[0] == 0x3c && large->header[sizeof large->header - 1] == 0x3c);
    CHECK(large->blocks[0][0].count == 0 && large->blocks[20][7].count == 0);
    CHECK(large->blocks[20][7].items[0] == 0x3c && large->blocks[20][7].items[0x7d] == 0x3c);
    CHECK(large->tails[0][0] == 0 && large->tails[20][15] == 0);
    free(small);
    free(large);
}

/* ---- textured surfaces --------------------------------------------------- */

static void put_word(unsigned offset, uint16_t value)
{
    material_bytes[offset] = (uint8_t)value;
    material_bytes[offset + 1] = (uint8_t)(value >> 8);
}

/* Saturating RGB12, one nibble per channel, ignoring bits 12..15. */
static uint16_t expected_shade(uint16_t color, uint16_t add)
{
    unsigned r = (color & 0xfu) + (add & 0xfu);
    unsigned g = (color & 0xf0u) + (add & 0xf0u);
    unsigned b = (color & 0xf00u) + (add & 0xf00u);
    if (r > 0xfu) r = 0xfu;
    if (g > 0xf0u) g = 0xf0u;
    if (b > 0xf00u) b = 0xf00u;
    return (uint16_t)(r | g | b);
}

static unsigned texture_calls;
static const fe2_render_texture_command *texture_payload;

static void record_texture(void *context, const fe2_render_texture_command *command)
{
    (void)context;
    texture_calls++;
    texture_payload = command;
}

static void textured_surface_follows_the_material_rules(void)
{
    fe2_screen_point on_screen[4];
    fe2_screen_point tiny[3];
    fe2_render_texture_command command;
    int32_t camera[3] = {1000, -2000, 5000};
    const uint16_t face = 0x0111;
    on_screen[0].x = 10; on_screen[0].y = 20;
    on_screen[1].x = 200; on_screen[1].y = 30;
    on_screen[2].x = 40; on_screen[2].y = 140;
    on_screen[3] = on_screen[0];
    tiny[0].x = 0; tiny[0].y = 0;
    tiny[1].x = 1; tiny[1].y = 0;
    tiny[2].x = 0; tiny[2].y = 1;

    memset(&command, 0xab, sizeof command);
    CHECK(fe2_render_build_texture(0, 3, 0, 0, 0, camera, 0, &command) == -1);
    CHECK(fe2_render_build_texture(on_screen, 3, 0, 0, 0, 0, 0, &command) == -1);
    CHECK(fe2_render_build_texture(on_screen, 3, 0, 0, 0, camera, 0, 0) == -1);
    CHECK(fe2_render_build_texture(on_screen, 2, 0, 0, 0, camera, 0, &command) == -1);
    CHECK(command.key == 0xabababab);

    /* Outside the accepted window the surface stays on the solid path. */
    on_screen[0].x = -2001;
    CHECK(fe2_render_build_texture(on_screen, 3, 0, 0, 0, camera, 0, &command) == FE2_RENDER_TEXTURE_SOLID);
    on_screen[0].x = 10;
    on_screen[1].y = 0x879;
    CHECK(fe2_render_build_texture(on_screen, 3, 0, 0, 0, camera, 0, &command) == FE2_RENDER_TEXTURE_SOLID);
    on_screen[1].y = 30;
    /* No material entry: the surface stays solid even though it is large. */
    CHECK(fe2_render_build_texture(on_screen, 3, 0, 0, 0, camera, 0, &command) == FE2_RENDER_TEXTURE_SOLID);
    /* A span under 10 (triangle) or 14 (quad) is solid once a material is selected. */
    CHECK(fe2_render_build_texture(tiny, 3, 0xe020, face, 0, camera, 0, &command) == FE2_RENDER_TEXTURE_SOLID);
    {
        fe2_screen_point quad[4] = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
        fe2_screen_point wide[3] = {{0, 0}, {6, 0}, {6, 6}};
        CHECK(fe2_render_build_texture(quad, 4, 0xe020, face, 0, camera, 0, &command) ==
              FE2_RENDER_TEXTURE_SOLID);
        fe2_render_material_bytes = 0;
        CHECK(fe2_render_build_texture(wide, 3, 0xe020, face, 0, camera, 0, &command) == -1);
        CHECK(command.key == 0xabababab);
    }

    /* Entirely left of the viewport: a real material is still not drawn. */
    {
        fe2_screen_point left[3] = {{-1, 10}, {-1, 80}, {-1, 150}};
        CHECK(fe2_render_build_texture(left, 3, 0xe020, 0, 0, camera, 0, &command) ==
              FE2_RENDER_TEXTURE_REJECTED);
        CHECK(command.key == 0xabababab);
    }

    /* Token 0xe020 selects material entry 1. Face 0x0111 shades the colours. */
    put_word(0, 0x00ab);
    put_word(2, 0x0123);
    put_word(4, 0x0f00);
    put_word(6, 0xf000);
    put_word(8, 0xffff);
    fe2_render_material_bytes = 16;
    CHECK(fe2_render_build_texture(on_screen, 3, 0xe020, face, 0, camera, 3, &command) ==
          FE2_RENDER_TEXTURE_EMITTED);
    CHECK(command.texture == 0x00ab);
    CHECK(command.color_count == 3);
    CHECK(command.colors[0] == expected_shade(0x0123, face));
    CHECK(command.colors[1] == expected_shade(0x0f00, face));
    CHECK(command.colors[2] == expected_shade(0xf000, face));
    CHECK(command.colors[0] == 0x0234 && command.colors[1] == 0x0f11 && command.colors[2] == 0x0111);
    CHECK(command.count == 3);
    CHECK(command.points[0].x == 10 && command.points[2].y == 140);
    CHECK(command.key == fe2_render_depth_key(camera, 3));

    /* The boundary of the window is still eligible. */
    on_screen[0].x = -2000;
    CHECK(fe2_render_build_texture(on_screen, 3, 0xe020, face, 0, camera, 3, &command) ==
          FE2_RENDER_TEXTURE_EMITTED);

    /* Bit 0x10 of the material value adds the inherited dword. */
    CHECK(fe2_render_build_texture(on_screen, 3, 0x100, face, 0, camera, 3, &command) ==
          FE2_RENDER_TEXTURE_SOLID);
    CHECK(fe2_render_build_texture(on_screen, 3, 0x100, face, 0x10000, camera, 3, &command) ==
          FE2_RENDER_TEXTURE_EMITTED);
    CHECK(command.texture == 0x00ab);

    /* An entry that falls outside the table reports the miss and keeps output. */
    memset(&command, 0xab, sizeof command);
    CHECK(fe2_render_build_texture(on_screen, 3, 0x100, face, 0x10000000u, camera, 3, &command) == -1);
    CHECK(command.key == 0xabababab);

    texture_calls = 0;
    texture_payload = 0;
    {
        fe2_render_drawing_target target = {0};
        fe2_render_texture_command payload = {0};
        payload.count = 3;
        fe2_render_texture_execute(0, &payload);
        fe2_render_texture_execute(&target, &payload);
        fe2_render_texture_execute(&target, 0);
        target.texture = record_texture;
        fe2_render_texture_execute(&target, &payload);
        CHECK(texture_calls == 1 && texture_payload == &payload);
    }
}

/* ---- docking approach ---------------------------------------------------- */

static fe2_object *dock_parent;
static unsigned rate_calls, difference_calls, transform_calls;
static uint32_t seen_mode;
static uint64_t seen_change;

static fe2_object *lookup_parent(void *opaque, uint8_t handle, const fe2_call_args *args)
{
    (void)opaque;
    (void)args;
    CHECK(handle == 4);
    return dock_parent;
}

static void rates_toward(void *opaque, fe2_object *object, fe2_object *reference, const fe2_call_args *args)
{
    (void)opaque;
    (void)object;
    CHECK(reference == dock_parent);
    seen_mode = args->arg[P_MODE].value;
    rate_calls++;
}

static void difference(void *opaque, fe2_object *object, const fe2_call_args *args, uint32_t low[3])
{
    (void)opaque;
    (void)object;
    (void)args;
    low[0] = 128;
    low[1] = 127;
    low[2] = (uint32_t)-128;
    difference_calls++;
}

static void transform_vector(void *opaque, fe2_object *object, const fe2_call_args *args)
{
    (void)opaque;
    (void)object;
    seen_change = (uint64_t)args->arg[P2].value | ((uint64_t)args->arg[P3].value << 32);
    transform_calls++;
}

void people_motion_follow_docking_parent(people_context *c, fe2_object *object, uint64_t change);

static void docking_takes_a_fraction_of_the_separation(void)
{
    fe2_people_services services;
    fe2_call_args args;
    people_context context;
    fe2_object craft, parent;
    memset(&services, 0, sizeof services);
    memset(&args, 0, sizeof args);
    memset(&craft, 0, sizeof craft);
    memset(&parent, 0, sizeof parent);
    services.lookup = lookup_parent;
    services.rates_toward = rates_toward;
    services.difference = difference;
    services.transform_vector = transform_vector;
    context.g = &services;
    context.args = &args;
    dock_parent = &parent;
    craft.behaviour_parent = 4;
    craft.velocity[0] = craft.velocity[1] = craft.velocity[2] = 9;
    parent.flags = FLAG_LOUD;
    people_motion_follow_docking_parent(&context, &craft, 99);
    CHECK(craft.velocity[0] == 9 && rate_calls == 0 && transform_calls == 0);

    parent.flags = 0;
    people_motion_follow_docking_parent(&context, &craft, UINT64_C(0x100000002));
    CHECK(rate_calls == 1 && seen_mode == 0xffff);
    CHECK(difference_calls == 1 && transform_calls == 1);
    CHECK(seen_change == UINT64_C(0x100000002));
    CHECK(craft.velocity[0] == 1 && craft.velocity[1] == 0 && craft.velocity[2] == -1);
}

/* ---- interaction text ---------------------------------------------------- */

static uint16_t shown_selector;
static const fe2_text_table *shown_table;

static void show_interaction(void *opaque, uint16_t selector, const fe2_text_table *table, fe2_text_out *out)
{
    (void)opaque;
    (void)out;
    shown_selector = selector;
    shown_table = table;
}

static void interaction_text_is_addressed_by_its_index(void)
{
    static uint8_t directory[1024];
    fe2_interaction_services game;
    const uint8_t *entry;
    uint16_t length = 0x1234;
    unsigned at = FE2_INTERACTION_ENTRIES * 2;

    memset(directory, 0, sizeof directory);
    memcpy(directory + at, "alpha", 6);
    directory[0] = (uint8_t)at;
    directory[1] = (uint8_t)(at >> 8);
    at += 6;
    memcpy(directory + at, "beta", 5);
    directory[2] = (uint8_t)at;
    directory[3] = (uint8_t)(at >> 8);
    at += 5;
    memcpy(directory + at, "omega", 6);
    directory[461 * 2] = (uint8_t)at;
    directory[461 * 2 + 1] = (uint8_t)(at >> 8);
    fe2_interaction_text_text_directory = directory;

    entry = fe2_interaction_entry(0, &length);
    CHECK(entry != 0 && length == 6 && memcmp(entry, "alpha", 6) == 0);
    entry = fe2_interaction_entry(1, &length);
    CHECK(length == 5 && memcmp(entry, "beta", 5) == 0);
    entry = fe2_interaction_entry(461, &length);
    CHECK(length == 6 && memcmp(entry, "omega", 6) == 0);
    length = 0x1234;
    CHECK(fe2_interaction_entry(462, &length) == 0 && length == 0x1234);
    CHECK(fe2_interaction_entry(0, 0) != 0);

    memset(&game, 0, sizeof game);
    game.format_table_text = show_interaction;
    fe2_interaction_display(&game, 0xffff, 0);
    CHECK(shown_selector == 0x3fe && shown_table == (const fe2_text_table *)directory);
    fe2_interaction_display(&game, 0x401, 0);
    CHECK(shown_selector == 0);
}

/* ---- commodity list ------------------------------------------------------ */

static uint16_t formatted_names[8];
static unsigned name_count;
static uint32_t formatted_prices[8];
static unsigned price_count;
static uint16_t number_format;

static void format_commodity(void *opaque, uint16_t selector, const fe2_text_table *table, fe2_text_out *out)
{
    (void)opaque;
    CHECK(table == (const fe2_text_table *)fe2_market_text_directory);
    CHECK(name_count < 8);
    formatted_names[name_count++] = selector;
    out->buffer[out->at++] = (uint8_t)selector;
}

static void format_price(void *opaque, uint16_t format, const fe2_call_args *args, fe2_text_out *out)
{
    (void)opaque;
    number_format = format;
    CHECK(price_count < 8);
    formatted_prices[price_count++] = args->arg[1].value;
    out->buffer[out->at++] = 'p';
    out->buffer[out->at++] = ' ';
}

static void price_list_skips_illegal_goods_and_restores_scratch(void)
{
    fe2_market_state *s = new_market();
    fe2_market_services game;
    uint8_t buffer[64];
    fe2_text_out out;
    unsigned i;

    memset(&game, 0, sizeof game);
    game.format_text = format_commodity;
    game.format_number = format_price;
    for (i = 0; i < FE2_COMMODITIES; ++i) s->trade_codes[i] = 0x80;
    s->trade_codes[1] = 0;
    s->trade_codes[2] = 0;
    fe2_market_set_word(s, 6, 25);
    fe2_market_set_word(s, 10, (uint16_t)-3);
    s->draft.counter = 0xbeef0003;
    s->draft.system = 0x33334444;
    s->draft.due = 0x11112222;
    s->draft.reward = 0x55556666;
    s->draft.seed = 0x77778888;
    memset(buffer, 0, sizeof buffer);
    out.buffer = buffer;
    out.at = 0;
    name_count = price_count = 0;
    fe2_market_format(s, &game, 0xfc3f, 0, &out);
    CHECK(name_count == 2 && formatted_names[0] == 2 && formatted_names[1] == 4);
    CHECK(price_count == 2 && formatted_prices[0] == 25);
    CHECK(formatted_prices[1] == (uint32_t)(int32_t)(int16_t)-3);
    CHECK(number_format == 0x3004);
    CHECK(buffer[0] == 0x1f && buffer[1] == 0x0f && buffer[2] == 0x01);
    CHECK(buffer[out.at - 1] == 0);
    CHECK(s->draft.counter == 0xbeef0000u + FE2_COMMODITIES);
    CHECK(s->draft.system == 0x33334444 && s->draft.due == 0x11112222);
    CHECK(s->draft.reward == 0x55556666 && s->draft.seed == 0x77778888);

    for (i = 0; i < FE2_COMMODITIES; ++i) s->trade_codes[i] = 0x80;
    s->draft.counter = 0xabcd0001;
    memset(buffer, 0, sizeof buffer);
    out.at = 0;
    name_count = price_count = 0;
    fe2_market_format(s, &game, 0x3e, 0, &out);
    CHECK(name_count == 0 && price_count == 0);
    CHECK(buffer[0] == 0x1f && buffer[1] == 0x0f && buffer[2] == 0x01 && buffer[3] == 0);
    CHECK(s->draft.counter == 0xabcd0000u + FE2_COMMODITIES);

    s->draft.counter = 0x11110002;
    name_count = 0;
    fe2_market_format(s, &game, 0xfcab, 0, &out);
    CHECK(name_count == 1 && formatted_names[0] == 0xaa && price_count == 0);
    CHECK(s->draft.counter == 0x11110002);
    free(s);
}

/* ---- daily market update ------------------------------------------------- */

enum {
    TEXT_ILLEGAL = 0x9828, TEXT_IMPERIAL = 0x9826, TEXT_FEDERAL = 0x9827,
    FLAG_LISTED = 0x01, FLAG_WARNED = 0x02, FLAG_IMPERIAL = 0x04, FLAG_FEDERAL = 0x08
};

static void fill_request(fe2_market_state *s, unsigned index, uint32_t head, uint32_t system,
                         uint32_t due, uint32_t reward, uint32_t detail, uint32_t seed)
{
    fe2_market_set_timed(s, index, FE2_TIMED_HEAD, head);
    fe2_market_set_timed(s, index, FE2_TIMED_SYSTEM, system);
    fe2_market_set_timed(s, index, FE2_TIMED_DUE, due);
    fe2_market_set_timed(s, index, FE2_TIMED_REWARD, reward);
    fe2_market_set_timed(s, index, FE2_TIMED_DETAIL, detail);
    fe2_market_set_timed(s, index, FE2_TIMED_SEED, seed);
}

static int same_request(const fe2_market_state *s, unsigned index, uint32_t head, uint32_t system,
                        uint32_t due, uint32_t reward, uint32_t detail, uint32_t seed)
{
    return fe2_market_timed(s, index, FE2_TIMED_HEAD) == head &&
           fe2_market_timed(s, index, FE2_TIMED_SYSTEM) == system &&
           fe2_market_timed(s, index, FE2_TIMED_DUE) == due &&
           fe2_market_timed(s, index, FE2_TIMED_REWARD) == reward &&
           fe2_market_timed(s, index, FE2_TIMED_DETAIL) == detail &&
           fe2_market_timed(s, index, FE2_TIMED_SEED) == seed;
}

static void expired_requests_move_the_later_ones_down(void)
{
    fe2_market_state *s = new_market();
    fe2_market_services game;
    static const uint16_t tries[3] = {0, 0, 0};
    static const uint16_t quiet[3] = {1, 1, 1};

    s->day = 100;
    fill_request(s, 0, 0x11, 0xaaa, 104, 1, 2, 3);
    fill_request(s, 1, 0x22, 0xbbb, 999, 4, 5, 6);
    fill_request(s, 2, 0, 0xccc, 0, 7, 8, 9);
    s->draft.counter = 0xabc00000;
    use_rolls(&game, tries, 3);
    fe2_market_update_markets(s, &game);
    CHECK(rolls_left == 0);
    CHECK(same_request(s, 0, 0x22, 0xbbb, 999, 4, 5, 6));
    CHECK(same_request(s, 1, 0, 0xccc, 0, 7, 8, 9));
    CHECK(request_generations == 3 && price_refreshes == 0);
    CHECK(s->draft.counter == 0xabc0ffff);
    free(s);

    s = new_market();
    s->day = 100;
    fill_request(s, 0, 1, 10, 105, 10, 11, 12);
    fill_request(s, 1, 2, 20, 104, 13, 14, 15);
    fill_request(s, 2, 3, 30, 200, 16, 17, 18);
    fill_request(s, 3, 0, 40, 0, 19, 20, 21);
    use_rolls(&game, quiet, 3);
    fe2_market_update_markets(s, &game);
    CHECK(same_request(s, 0, 1, 10, 105, 10, 11, 12));
    CHECK(same_request(s, 1, 3, 30, 200, 16, 17, 18));
    CHECK(same_request(s, 2, 0, 40, 0, 19, 20, 21));
    CHECK(request_generations == 0 && price_refreshes == 0);
    free(s);
}

static void replaced_bulletin_clears_its_flag_and_categories_close_up(void)
{
    static const uint16_t with_text[9] = {1, 1, 1, 0, 0, 0, 0, 0, 1};
    static const uint16_t no_text[8] = {1, 1, 1, 1, 0, 0, 0, 1};
    fe2_market_state *s = new_market();
    fe2_market_services game;
    uint8_t flags;

    s->day = 100;
    s->market_count = 1;
    s->population_band = 1;
    fe2_market_set_byte(s, FE2_MARKET_FLAGS, (uint8_t)(FLAG_LISTED | FLAG_WARNED | FLAG_IMPERIAL | FLAG_FEDERAL));
    fe2_market_set_word(s, FE2_MARKET_TEXT_COUNT, 4);
    fe2_market_set_word(s, FE2_MARKET_TEXTS_AT, TEXT_ILLEGAL);
    fe2_market_set_word(s, FE2_MARKET_TEXTS_AT + 2, 0);
    fe2_market_set_word(s, FE2_MARKET_CATEGORY_COUNT, 2);
    fe2_market_set_dword(s, FE2_MARKET_CATEGORIES_AT, 0x11111111);
    fe2_market_set_dword(s, FE2_MARKET_CATEGORIES_AT + 4, 0x22222222);
    fe2_market_set_dword(s, FE2_MARKET_CATEGORIES_AT + 8, 0x33333333);
    use_rolls(&game, with_text, 9);
    fe2_market_update_markets(s, &game);
    CHECK(rolls_left == 0 && price_refreshes == 1 && text_generations == 1);
    CHECK(text_count_while_generating == 3);
    CHECK(fe2_market_word(s, FE2_MARKET_TEXT_COUNT) == 4);
    CHECK(fe2_market_word(s, FE2_MARKET_TEXTS_AT) == 0);
    flags = fe2_market_byte(s, FE2_MARKET_FLAGS);
    CHECK((flags & FLAG_LISTED) == 0);
    CHECK((flags & FLAG_WARNED) && (flags & FLAG_IMPERIAL) && (flags & FLAG_FEDERAL));
    CHECK(category_appends == 0);
    CHECK(fe2_market_word(s, FE2_MARKET_CATEGORY_COUNT) == 1);
    CHECK(fe2_market_dword(s, FE2_MARKET_CATEGORIES_AT) == 0x22222222);
    CHECK(fe2_market_dword(s, FE2_MARKET_CATEGORIES_AT + 4) == 0x33333333);
    free(s);

    s = new_market();
    s->population_band = 1;
    s->market_count = 1;
    fe2_market_set_byte(s, FE2_MARKET_FLAGS, FLAG_WARNED);
    fe2_market_set_word(s, FE2_MARKET_TEXT_COUNT, 4);
    fe2_market_set_word(s, FE2_MARKET_TEXTS_AT, TEXT_ILLEGAL);
    fe2_market_set_word(s, FE2_MARKET_TEXTS_AT + 2, 0x8000);
    use_rolls(&game, with_text, 9);
    fe2_market_update_markets(s, &game);
    CHECK((fe2_market_byte(s, FE2_MARKET_FLAGS) & FLAG_WARNED) == 0);
    free(s);

    s = new_market();
    s->population_band = 1;
    s->market_count = 1;
    fe2_market_set_byte(s, FE2_MARKET_FLAGS, (uint8_t)(FLAG_IMPERIAL | FLAG_FEDERAL));
    fe2_market_set_word(s, FE2_MARKET_TEXT_COUNT, 4);
    fe2_market_set_word(s, FE2_MARKET_TEXTS_AT, TEXT_IMPERIAL);
    use_rolls(&game, with_text, 9);
    fe2_market_update_markets(s, &game);
    CHECK(fe2_market_byte(s, FE2_MARKET_FLAGS) == FLAG_FEDERAL);
    free(s);

    s = new_market();
    s->population_band = 1;
    s->market_count = 1;
    fe2_market_set_byte(s, FE2_MARKET_FLAGS, (uint8_t)(FLAG_IMPERIAL | FLAG_FEDERAL));
    fe2_market_set_word(s, FE2_MARKET_TEXT_COUNT, 4);
    fe2_market_set_word(s, FE2_MARKET_TEXTS_AT, TEXT_FEDERAL);
    use_rolls(&game, with_text, 9);
    fe2_market_update_markets(s, &game);
    CHECK(fe2_market_byte(s, FE2_MARKET_FLAGS) == FLAG_IMPERIAL);
    free(s);

    s = new_market();
    s->population_band = 1;
    s->market_count = 1;
    fe2_market_set_byte(s, FE2_MARKET_FLAGS, FLAG_LISTED);
    fe2_market_set_word(s, FE2_MARKET_TEXT_COUNT, 4);
    fe2_market_set_word(s, FE2_MARKET_TEXTS_AT, 0x1111);
    use_rolls(&game, with_text, 9);
    fe2_market_update_markets(s, &game);
    CHECK(text_generations == 1 && fe2_market_word(s, FE2_MARKET_TEXTS_AT) == 0);
    CHECK(fe2_market_byte(s, FE2_MARKET_FLAGS) == FLAG_LISTED);
    free(s);

    s = new_market();
    s->population_band = 1;
    s->market_count = 1;
    fe2_market_set_byte(s, FE2_MARKET_FLAGS, FLAG_LISTED);
    fe2_market_set_word(s, FE2_MARKET_TEXT_COUNT, 4);
    fe2_market_set_word(s, FE2_MARKET_TEXTS_AT, TEXT_ILLEGAL);
    use_rolls(&game, no_text, 8);
    fe2_market_update_markets(s, &game);
    CHECK(text_generations == 0 && price_refreshes == 1);
    CHECK(fe2_market_byte(s, FE2_MARKET_FLAGS) == FLAG_LISTED);
    CHECK(fe2_market_word(s, FE2_MARKET_TEXTS_AT) == TEXT_ILLEGAL);
    free(s);

    /* A roll below the rate asks for that kind of category. The record itself
     * is produced by the category writer, which this check does not stand in for. */
    {
        static const uint16_t add_categories[8] = {1, 1, 1, 1, 0, 0, 0, 0};
        s = new_market();
        s->population_band = 1;
        s->market_count = 1;
        s->category_0_rate = 1;
        s->category_1_rate = 1;
        use_rolls(&game, add_categories, 8);
        fe2_market_update_markets(s, &game);
        CHECK(rolls_left == 0 && category_appends == 3);
        CHECK(appended_kinds[0] == 0 && appended_kinds[1] == 1 && appended_kinds[2] == 9);
        free(s);
    }
}

/* ---- commander pages ----------------------------------------------------- */

enum { MAX_DRAWS = 48 };

typedef struct draw {
    uint16_t text, x, y;
    uint32_t p1, p2, p3, p10, p11;
    const void *name;
} draw;

static draw draws[MAX_DRAWS];
static unsigned draw_count, icon_count, frame_count, clear_count;
static uint16_t ship_models[4];
static unsigned ship_model_count;
static uint16_t name_modes[4];
static uint32_t name_seeds[4];
static unsigned page_name_count;
static const fe2_ship_type ship_info = {{0, 0}, 0, 0, 0, 0x4242};

static void reset_page(void)
{
    draw_count = icon_count = frame_count = clear_count = 0;
    ship_model_count = page_name_count = 0;
}

static void clear_panel(void *opaque) { (void)opaque; clear_count++; }

static void draw_text(void *opaque, uint16_t text, uint8_t colour, uint16_t x, uint16_t y,
                      const fe2_call_args *args)
{
    draw *d;
    (void)opaque;
    (void)colour;
    CHECK(draw_count < MAX_DRAWS);
    d = &draws[draw_count++];
    d->text = text;
    d->x = x;
    d->y = y;
    d->p1 = args->arg[PARAM_1].value;
    d->p2 = args->arg[PARAM_2].value;
    d->p3 = args->arg[PARAM_3].value;
    d->p10 = args->arg[PARAM_10].value;
    d->p11 = args->arg[PARAM_11].value;
    d->name = args->arg[PARAM_10].ref;
}

static void draw_icon(void *opaque, uint16_t icon, uint16_t x, uint16_t y)
{
    (void)opaque;
    (void)icon;
    (void)x;
    (void)y;
    icon_count++;
}

static void draw_frame(void *opaque, const fe2_rect *frame, uint16_t style)
{
    (void)opaque;
    (void)frame;
    (void)style;
    frame_count++;
}

static void page_generate_name(void *opaque, uint16_t mode, uint32_t seed, char out[FE2_TEXT_BYTES])
{
    (void)opaque;
    CHECK(page_name_count < 4);
    name_modes[page_name_count] = mode;
    name_seeds[page_name_count] = seed;
    page_name_count++;
    memcpy(out, "Viper", 6);
}

static const fe2_ship_type *page_ship(void *opaque, uint16_t model)
{
    (void)opaque;
    CHECK(ship_model_count < 4);
    ship_models[ship_model_count++] = model;
    return &ship_info;
}

static void bind_page(fe2_game_services *game)
{
    memset(game, 0, sizeof *game);
    game->clear_panel = clear_panel;
    game->draw_text = draw_text;
    game->draw_icon = draw_icon;
    game->draw_frame = draw_frame;
    game->generate_name = page_generate_name;
    game->ship_type = page_ship;
}

static const draw *nth_text(uint16_t text, unsigned nth)
{
    unsigned i, seen = 0;
    for (i = 0; i < draw_count; ++i)
        if (draws[i].text == text && seen++ == nth) return &draws[i];
    return 0;
}

static unsigned texts(uint16_t text)
{
    unsigned i, n = 0;
    for (i = 0; i < draw_count; ++i)
        if (draws[i].text == text) n++;
    return n;
}

static fe2_commander_status *new_status(void)
{
    fe2_commander_status *status = calloc(1, sizeof *status);
    CHECK(status != 0);
    status->dim_colour = 0x55;
    status->cash = 90;
    return status;
}

static void mining_page_lists_deployed_and_stowed_machines(void)
{
    fe2_commander_status *status = new_status();
    fe2_game_services game;
    const draw *deployed, *stowed;
    unsigned i;
    bind_page(&game);
    reset_page();
    status->mining_count = 2;
    memcpy(status->mining[0].name, "Pit", 4);
    status->mining[0].system = 0x100;
    status->mining[0].prospect = 2;
    memcpy(status->mining[1].name, "Bay", 4);
    status->mining[1].prospect = 0;
    fe2_status_draw_mining_page(status, &game);
    CHECK(status->heading_colour == 0x55);
    CHECK(texts(0x9c08) == 1 && texts(0x9c38) == 1 && frame_count == 1 && clear_count >= 1);
    CHECK(icon_count == 2);
    deployed = nth_text(0xa200, 0);
    stowed = nth_text(0xa201, 0);
    CHECK(deployed != 0 && stowed != 0 && stowed->y > deployed->y);
    CHECK(deployed->p1 == 0x100 && deployed->p2 == 0xa202 + 2 && deployed->name == status->mining[0].name);
    CHECK(stowed->p1 == 0 && stowed->p2 == 0xa202 && stowed->name == status->mining[1].name);

    reset_page();
    status->mining_count = 0;
    fe2_status_draw_mining_page(status, &game);
    CHECK(texts(0xa200) == 0 && texts(0xa201) == 0 && icon_count == 0 && texts(0x9c38) == 1);

    reset_page();
    status->mining_count = 9;
    for (i = 0; i < FE2_MAX_MINING_UNITS; ++i) status->mining[i].system = 1;
    fe2_status_draw_mining_page(status, &game);
    CHECK(texts(0xa200) == FE2_MAX_MINING_UNITS && icon_count == FE2_MAX_MINING_UNITS);
    free(status);
}

static void passenger_page_uses_the_contract_kind(void)
{
    fe2_commander_status *status = new_status();
    fe2_game_services game;
    const draw *line;
    unsigned i;
    bind_page(&game);
    reset_page();
    status->contract_count = 4;
    status->contracts[0].client = 5;
    status->contracts[0].fee = 10;
    status->contracts[0].destination = 70;
    status->contracts[0].group = 0x0c;
    status->contracts[1].client = 6;
    status->contracts[1].kind = FE2_CONTRACT_PREPAID;
    status->contracts[1].fee = 11;
    status->contracts[2].kind = FE2_CONTRACT_IMPERIAL;
    status->contracts[2].fee = 12;
    status->contracts[2].deadline = 80;
    status->contracts[3].fee = 13;
    status->contracts[3].deadline = 81;
    fe2_status_draw_passenger_page(status, &game);
    CHECK(texts(0x9c04) == 1 && texts(0x9c38) == 1 && frame_count == 1);
    line = nth_text(0x9c12, 0);
    CHECK(line != 0 && line->p3 == 10 && line->p1 == 70 && line->p10 == 5 && line->p2 == 4);
    line = nth_text(0x9c13, 0);
    CHECK(line != 0 && line->p3 == 11);
    line = nth_text(0x9c15, 0);
    CHECK(line != 0 && line->p3 == 12 && line->p2 == 80);
    line = nth_text(0x9c14, 0);
    CHECK(line != 0 && line->p3 == 13 && line->p2 == 81);
    CHECK(nth_text(0x9c14, 0)->y > nth_text(0x9c12, 0)->y);

    reset_page();
    memset(status->contracts, 0, sizeof status->contracts);
    status->contract_count = 14;
    for (i = 0; i < 14; ++i) {
        status->contracts[i].client = 1;
        status->contracts[i].fee = (uint16_t)(10 + i);
    }
    status->contracts[13].fee = 999;
    fe2_status_draw_passenger_page(status, &game);
    CHECK(texts(0x9c12) == 13);
    for (i = 0; i < draw_count; ++i) CHECK(draws[i].p3 != 999);
    CHECK(nth_text(0x9c12, 12)->p3 == 22);
    free(status);
}

static void crew_page_lists_wages_only_while_someone_is_aboard(void)
{
    fe2_commander_status *status = new_status();
    fe2_game_services game;
    const draw *payday, *member;
    bind_page(&game);
    reset_page();
    status->crew_count = 2;
    status->crew_needed = 2;
    status->unpaid_wages = 500;
    status->day = 0x00020003;
    status->crew[0].name_seed = 0x111;
    status->crew[0].salary = 40;
    status->crew[1].name_seed = 0x222;
    status->crew[1].salary = 50;
    fe2_status_draw_crew_page(status, &game);
    CHECK(texts(0x9c06) == 1 && texts(0x9c16) == 2 && texts(0x9c17) == 1 && texts(0x9c38) == 1);
    CHECK(icon_count == 2 && frame_count == 1);
    member = nth_text(0x9c16, 0);
    CHECK(member->p10 == 0x111 && member->p3 == 40);
    CHECK(nth_text(0x9c16, 1)->y > member->y && nth_text(0x9c16, 1)->p3 == 50);
    CHECK(nth_text(0x9c17, 0)->p1 == 500);
    payday = nth_text(0x9c18, 0);
    CHECK(payday != 0 && payday->p1 == 0x00020007 && payday->p2 == 2);

    reset_page();
    status->unpaid_wages = 0;
    fe2_status_draw_crew_page(status, &game);
    CHECK(texts(0x9c17) == 0 && texts(0x9c16) == 2 && texts(0x9c18) == 1);

    reset_page();
    status->crew_count = 0;
    status->unpaid_wages = 500;
    status->day = 7;
    fe2_status_draw_crew_page(status, &game);
    CHECK(texts(0x9c16) == 0 && texts(0x9c17) == 0 && icon_count == 0);
    CHECK(nth_text(0x9c18, 0)->p1 == 7);
    free(status);
}

static void mission_page_describes_kills_and_recon(void)
{
    fe2_commander_status *status = new_status();
    fe2_game_services game;
    const draw *title;
    bind_page(&game);
    reset_page();
    status->military_count = 3;
    status->military[0].type = 0;
    status->military[0].target_model = 0x10;
    status->military[0].quality = 0x01;
    status->military[0].target_seed = 0x111;
    status->military[0].home_system = 5;
    status->military[1].type = 2;
    status->military[1].outcome = 1;
    status->military[1].target_model = 0x20;
    status->military[1].quality = 0x02;
    status->military[1].target_seed = 0x222;
    status->military[2].type = 0x14;
    status->military[2].target_system = 9;
    status->military[2].deadline = 30;
    status->military[2].reward = 40;
    fe2_status_draw_mission_page(status, &game);
    CHECK(texts(0x9c07) == 1 && texts(0x9c38) == 0 && frame_count == 0);
    title = nth_text(0x9c1b, 0);
    CHECK(title != 0 && title->p3 == 0x999d);
    title = nth_text(0x9c1d, 0);
    CHECK(title != 0 && title->p3 == 0x99a5 && title->y > nth_text(0x9c1b, 0)->y);
    CHECK(texts(0x9c1e) == 2 && texts(0x9c1f) == 2 && texts(0x9c20) == 1);
    CHECK(ship_model_count == 2 && ship_models[0] == 0x0110 && ship_models[1] == 0x0220);
    CHECK(nth_text(0x9c1f, 0)->p11 == 0x4242);
    CHECK(page_name_count == 2 && name_modes[0] == 0x0f && name_modes[1] == 0x0f);
    CHECK(name_seeds[0] == 0x111 && name_seeds[1] == 0x222);
    title = nth_text(0x9c22, 0);
    CHECK(title != 0 && title->p2 == 0x9950 && title->y > nth_text(0x9c1d, 0)->y);
    CHECK(texts(0x9c25) == 1);
    CHECK(nth_text(0x9c25, 0)->p1 == 9 && nth_text(0x9c25, 0)->p2 == 30 && nth_text(0x9c25, 0)->p3 == 40);

    reset_page();
    status->military_count = 1;
    status->military[0].type = 7;
    status->military[0].outcome = 0;
    fe2_status_draw_mission_page(status, &game);
    CHECK(draw_count == 1 && draws[0].text == 0x9c07);
    free(status);
}

/* ---- stations ------------------------------------------------------------ */

static unsigned generated_names;
static uint16_t generated_name_type;
static fe2_scalar products[8][2];
static unsigned product_count;
static fe2_scalar divided_by;
static fe2_scalar rooted;

static void generate_station_name(void *opaque, uint16_t type, uint32_t seed, char out[FE2_NAME_TEXT_BYTES])
{
    (void)opaque;
    (void)seed;
    generated_names++;
    generated_name_type = type;
    memcpy(out, "Haven", 6);
}

static fe2_scalar multiply_scalar(void *opaque, fe2_scalar a, fe2_scalar b)
{
    (void)opaque;
    CHECK(product_count < 8);
    products[product_count][0] = a;
    products[product_count][1] = b;
    product_count++;
    return b;
}

static fe2_scalar divide_scalar(void *opaque, fe2_scalar dividend, fe2_scalar divisor)
{
    fe2_scalar result = {3, 4};
    (void)opaque;
    (void)dividend;
    divided_by = divisor;
    return result;
}

static fe2_scalar root_scalar(void *opaque, fe2_scalar value)
{
    fe2_scalar result = {5, 6};
    (void)opaque;
    rooted = value;
    return result;
}

static void bind_generation(fe2_system_generation *g, fe2_system_services *game, fe2_system_record *records,
                            unsigned count)
{
    memset(g, 0, sizeof *g);
    memset(game, 0, sizeof *game);
    game->multiply = multiply_scalar;
    game->divide = divide_scalar;
    game->square_root = root_scalar;
    game->generate_name = generate_station_name;
    g->game = game;
    g->space.records = records;
    g->space.count = count;
    generated_names = 0;
    product_count = 0;
}

static uint16_t descriptor_index(uint16_t danger, uint16_t base, uint16_t seed_high)
{
    return (uint16_t)((((danger >> 4) & 0x1eu) + base + (seed_high & 6u)) / 2u);
}

static void generated_stations_copy_the_city_and_stop_at_the_cap(void)
{
    fe2_system_record records[8];
    fe2_system_generation generation;
    fe2_system_services game;
    unsigned record = 0, i;

    for (i = 0; i < 32; ++i) {
        generated_descriptors[i] = (uint16_t)(0x4000 + i);
        station_orbit[i] = (uint16_t)(0x3000 + i);
    }
    memset(&fe2_systems_sol_apollonius_city, 0, sizeof fe2_systems_sol_apollonius_city);
    fe2_record_set_word(&fe2_systems_sol_apollonius_city, FE2_RECORD_KIND, 0x0abc);

    bind_generation(&generation, &game, records, 8);
    generation.station_count = FE2_MAX_STATIONS;
    generation.random_a = 0x42;
    record = 3;
    memset(records, 0x11, sizeof records);
    fe2_systems_append_stations(&generation, &record, 1, 0);
    CHECK(record == 3 && generation.random_a == 0x42 && generation.station_count == FE2_MAX_STATIONS);
    CHECK(generated_names == 0 && records[0].bytes[0] == 0x11);

    memset(records, 0, sizeof records);
    bind_generation(&generation, &game, records, 8);
    generation.published_count = 9;
    generation.random_a = 0x100;
    generation.random_b = 1;
    record = 0;
    fe2_systems_append_stations(&generation, &record, 0, 0);
    CHECK(generation.station_count == 1 && record == 1);
    CHECK(generation.position_u & 0x8000u);
    CHECK(fe2_record_dword(&records[1], FE2_RECORD_SIZE_MANTISSA) == 0x00390000);
    CHECK(fe2_record_word(&records[1], FE2_RECORD_PARENT) == 9);
    CHECK(fe2_record_word(&records[1], FE2_RECORD_KIND) == 0x0abc);
    CHECK(fe2_record_dword(&records[1], FE2_RECORD_SEED) == generation.delta);
    CHECK(fe2_record_word(&records[1], FE2_RECORD_ORBIT_INCLINATION) == station_orbit[2]);
    CHECK(fe2_record_word(&records[1], FE2_RECORD_ORBIT_LONGITUDE) == station_orbit[3]);
    CHECK(fe2_record_word(&records[1], FE2_RECORD_DESCRIPTOR) ==
          generated_descriptors[descriptor_index(0, 0, fe2_record_word(&records[1], FE2_RECORD_SEED_HIGH))]);
    CHECK(generated_names == 1 && generated_name_type == 6);
    CHECK(strcmp(fe2_record_name(&records[1]), "Haven") == 0);
    CHECK(fe2_record_byte(&records[1], FE2_RECORD_GENERATION) & FE2_RECORD_GENERATED_NAME);
    CHECK(records[0].bytes[0] == 0 && records[2].bytes[0] == 0);

    memset(records, 0, sizeof records);
    bind_generation(&generation, &game, records, 8);
    generation.published_count = 9;
    generation.random_b = 0xffff;
    record = 0;
    fe2_systems_append_stations(&generation, &record, 4, 0);
    CHECK(generation.station_count >= 1 && generation.station_count <= 4);
    CHECK(generation.position_u & 0x8000u);
    CHECK(generation.station_count < FE2_MAX_STATIONS);
    for (i = 1; i <= generation.station_count; ++i) {
        CHECK(fe2_record_dword(&records[i], FE2_RECORD_SIZE_MANTISSA) == 0x00390000);
        CHECK(fe2_record_word(&records[i], FE2_RECORD_PARENT) == 9);
        CHECK(strcmp(fe2_record_name(&records[i]), "Haven") == 0);
    }
    CHECK(generated_names == generation.station_count);
    CHECK(records[generation.station_count + 1].bytes[0] == 0);
}

static void sol_stations_are_copies_of_the_named_templates(void)
{
    fe2_system_record records[8];
    fe2_system_generation generation;
    fe2_system_services game;
    unsigned record;
    fe2_system_record *chosen[3];
    unsigned i;

    chosen[0] = &fe2_systems_sol_daedalus;
    chosen[1] = &fe2_systems_sol_galileo;
    chosen[2] = &fe2_systems_sol_abraham_lincoln;
    for (i = 0; i < 3; ++i) {
        memset(chosen[i], 0, sizeof *chosen[i]);
        fe2_record_set_word(chosen[i], FE2_RECORD_KIND, (uint16_t)(0x1000 + i));
    }
    memset(records, 0, sizeof records);
    fe2_record_set_word(&records[6], FE2_RECORD_SIZE_MANTISSA, 0x1357);
    fe2_record_set_word(&records[6], FE2_RECORD_SIZE_EXPONENT, 0x2468);

    bind_generation(&generation, &game, records, 8);
    generation.station_count = FE2_MAX_STATIONS;
    generation.sol_station_count = 4;
    generation.random_a = 0x89abcdef;
    record = 3;
    fe2_systems_append_sol_station(&generation, &record, 0);
    CHECK(record == 3 && generation.station_count == FE2_MAX_STATIONS && generation.sol_station_count == 4);
    CHECK(generated_names == 0 && product_count == 0);

    bind_generation(&generation, &game, records, 8);
    generation.published_count = 5;
    generation.record_count = 2;
    generation.random_a = 0x89abcdef;
    record = 3;
    fe2_systems_append_sol_station(&generation, &record, 0);
    CHECK(record == 4 && generation.station_count == 1 && generation.sol_station_count == 1);
    CHECK(fe2_record_word(&records[4], FE2_RECORD_KIND) == 0x1000);
    CHECK(fe2_record_dword(&records[4], FE2_RECORD_SIZE_MANTISSA) == 0x00390000);
    CHECK(fe2_record_dword(&records[4], FE2_RECORD_SEED) == 0x89abcdef);
    CHECK(fe2_record_word(&records[4], FE2_RECORD_PARENT) == 5);
    CHECK(fe2_record_word(&records[4], FE2_RECORD_SCALAR_MANTISSA) == 0x7fff);
    CHECK(fe2_record_word(&records[4], FE2_RECORD_SCALAR_EXPONENT) == 0x22);
    CHECK(divided_by.mantissa == 0x1357 && divided_by.exponent == 0x2468);
    CHECK(rooted.mantissa == 3 && rooted.exponent == 4);
    CHECK(product_count >= 1);
    CHECK(products[0][0].mantissa == 0x7fff && products[0][0].exponent == 0x22);
    CHECK(products[0][1].mantissa == 0x7fff && products[0][1].exponent == 0x22);
    CHECK(products[product_count - 1][1].mantissa == 0x5edb && products[product_count - 1][1].exponent == 0);
    CHECK(fe2_record_word(&records[4], FE2_RECORD_DERIVED_MANTISSA) == 0x5edb);
    CHECK(fe2_record_word(&records[4], FE2_RECORD_DERIVED_EXPONENT) == 0);
    CHECK(generated_names == 1 && generated_name_type == 7);
    CHECK(strcmp(fe2_record_name(&records[4]), "Haven") == 0);

    memset(&records[4], 0, sizeof records[4]);
    bind_generation(&generation, &game, records, 8);
    generation.record_count = 2;
    generation.published_count = 5;
    record = 3;
    fe2_systems_append_sol_station(&generation, &record, 1);
    CHECK(fe2_record_word(&records[4], FE2_RECORD_KIND) == 0x1001);

    memset(&records[4], 0, sizeof records[4]);
    bind_generation(&generation, &game, records, 8);
    generation.record_count = 2;
    generation.published_count = 5;
    record = 3;
    fe2_systems_append_sol_station(&generation, &record, 2);
    CHECK(fe2_record_word(&records[4], FE2_RECORD_KIND) == 0x1002);
}

int main(void)
{
    render_contexts_initialize_what_they_own();
    textured_surface_follows_the_material_rules();
    equipment_bits_drive_the_effect();
    docking_takes_a_fraction_of_the_separation();
    interaction_text_is_addressed_by_its_index();
    price_list_skips_illegal_goods_and_restores_scratch();
    expired_requests_move_the_later_ones_down();
    replaced_bulletin_clears_its_flag_and_categories_close_up();
    mining_page_lists_deployed_and_stowed_machines();
    passenger_page_uses_the_contract_kind();
    crew_page_lists_wages_only_while_someone_is_aboard();
    mission_page_describes_kills_and_recon();
    generated_stations_copy_the_city_and_stop_at_the_cap();
    sol_stations_are_copies_of_the_named_templates();
    puts("Behavior contracts passed");
    return 0;
}
