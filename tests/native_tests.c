/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native behavior tests: run with assertions enabled, independent of DOS tools. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mining/mining_internal.h"
#include "world/system_catalogue.h"
#include "catalogue_blob.h"
#include "input_blob.h"
#include "pointer_blob.h"
#include "mining_blob.h"
#include "math/math3.h"
#include "world/system_record.h"
#include "input/input.h"

extern int fe2_mining_bind(const uint8_t *image, size_t count);

static unsigned events, messages, releases;
static void event(void *p, uint16_t value) { (void)p; assert(value == 0x17); ++events; }
static void message(void *p, uint16_t text, uint16_t amount, const fe2_call_args *a)
{ (void)p; (void)text; (void)amount; (void)a; ++messages; }
static void release(void *p, fe2_object *o) { (void)p; (void)o; ++releases; }

static void mining(void)
{
    fe2_mining_state s;
    fe2_mining_services g;
    fe2_object o, body;
    unsigned i, day;
    assert(fe2_mining_bind(fe2_test_mining, sizeof fe2_test_mining) == 0);
    memset(&s, 0, sizeof s); memset(&g, 0, sizeof g);
    memset(&o, 0, sizeof o); memset(&body, 0, sizeof body);
    g.post_event = event; g.show_message = message; g.release_object = release;
    for (i = 0; i < 100; ++i) fe2_mining_action(&s, &g, FE2_MINING_ADD_SLOT);
    assert(s.count == FE2_MINING_RECORDS && s.free_count == FE2_MINING_RECORDS);
    for (i = 0; i < FE2_MINING_RECORDS; ++i) {
        uint8_t *r = fe2_mining_record(&s, i);
        fe2_mining_put32(r, UINT32_C(0x12345678));
        assert(fe2_mining_get32(r) == UINT32_C(0x12345678));
        r[FE2_MINING_OBJECT] = (uint8_t)(i + 1);
        r[FE2_MINING_HOPPER] = (uint8_t)i;
        r[FE2_MINING_RESERVE] = (uint8_t)(20 - i);
    }
    s.free_count = 0;
    assert(fe2_mining_find_free(&s) == -1);
    assert(fe2_mining_find_machine(&s, 4) == 3);
    assert(fe2_mining_find_machine(&s, 99) == -1);
    assert(!fe2_mining_action(&s, &g, FE2_MINING_REMOVE_SLOT));
    for (day = 0; day < 30; ++day) fe2_mining_event(&s, &g, 0x10);
    for (i = 0; i < s.count; ++i) {
        uint8_t *r = fe2_mining_record(&s, i);
        assert(r[FE2_MINING_HOPPER] == 10);
        assert(r[FE2_MINING_RESERVE] == 10);
    }
    fe2_mining_record(&s, 0)[FE2_MINING_RESERVE] = 0;
    fe2_mining_record(&s, 0)[FE2_MINING_HOPPER] = 0;
    fe2_mining_event(&s, &g, 0x10);
    assert(fe2_mining_record(&s, 0)[FE2_MINING_HOPPER] == 0);
    fe2_mining_event(&s, &g, 99);
    s.frame_step = 8; s.flight_state = 0x68; o.handle = 1; o.level = 5;
    o.cargo_type = 0xfffe;
    fe2_mining_progress(&s, &g, &o);
    assert(o.cargo_type == 0xffff && s.selected_machine == 1 && events == 1);
    fe2_mining_progress(&s, &g, &o); assert(events == 1);
    o.behaviour = 0xff; o.cargo_type = 2; s.cargo_capacity = 100;
    fe2_mining_progress(&s, &g, &o);
    assert(o.cargo_type == 0 && s.free_count == 0);
    fe2_mining_progress(&s, &g, &o);
    assert(s.free_count == 1 && s.cargo_capacity == 70 && o.model == 0xc2);
    assert(messages == 1 && releases == 1 && events == 2);
    assert(fe2_mining_action(&s, &g, FE2_MINING_REMOVE_SLOT));
    assert(s.count == 7 && s.free_count == 0);
    /* Preserve the recovered odd-offset compaction contract. */
    assert(fe2_mining_record(&s, 0)[0] == 0);
    assert(fe2_mining_record(&s, 0)[FE2_MINING_OBJECT] == 2);
    {
        fe2_mining_pass pass;
        pass.state = &s; pass.record = 0; pass.machine = &o;
        memcpy(fe2_mining_record(&s, 0) + FE2_MINING_NAME, "Mars", 5);
        memcpy(body.name, "Mar", 4); body.handle = 42; o.parent_handle = 0;
        fe2_mining_name_object(&pass, &body); assert(o.parent_handle == 0);
        memcpy(body.name, "Mars", 5);
        fe2_mining_name_object(&pass, &body); assert(o.parent_handle == 42);
    }
    fe2_mining_reset(&s); assert(s.count == 0 && s.free_count == 0);
    assert(!fe2_mining_action(&s, &g, FE2_MINING_REMOVE_SLOT));
    for (i = 0; i < 8; ++i) fe2_mining_action(&s, &g, FE2_MINING_ADD_SLOT);
    fe2_mining_action(&s, &g, 99); assert(s.count == 0 && s.free_count == 0);
}

static void catalogue(void)
{
    const fe2_system_catalogue *c = &fe2_default_catalogue;
    assert(fe2_catalogue_bind(fe2_test_catalogue, sizeof fe2_test_catalogue) == 0);
    fe2_catalogue_record rows[3];
    fe2_system_catalogue custom;
    unsigned i;
    assert(c->count == FE2_SYSTEM_KEYED);
    for (i = 0; i < c->count; ++i) {
        const fe2_catalogue_record *found = fe2_system_find(c, c->records[i].key);
        assert(found->key == c->records[i].key);
    }
    memset(rows, 0, sizeof rows); rows[0].key = 123; rows[1].key = 123;
    custom.fallbacks = c->fallbacks; custom.records = rows; custom.count = 3;
    assert(fe2_system_find(&custom, 123) == &rows[0]);
    rows[0].key = 0;
    assert(fe2_system_find(&custom, 123) != &rows[1]);
    /* Coordinates at the fallback centre and on both sides of each boundary. */
    for (i = 0; i <= 100; ++i) {
        uint32_t key = ((UINT32_C(0x1718) + i) << 6) | (UINT32_C(0x1524) << 19);
        unsigned expected = i >= 90 ? 0 : i >= 15 ? 1 : i >= 10 ? 2 :
                            i >= 7 ? 3 : i >= 5 ? 4 : i >= 3 ? 5 : 6;
        assert(fe2_system_fallback(c, key) == &c->fallbacks[expected]);
    }
}

static void math(void)
{
    int value;
    uint32_t n;
    for (value = -32768; value <= 32767; ++value) {
        fe2_scalar s = fe2_math_from_signed32(value);
        assert(fe2_math_to_signed32(s) == value);
    }
    for (n = 1; n < 10000; ++n) {
        uint32_t divisor = n % 37 + 1;
        fe2_division d = fe2_math_divide_unsigned(n, divisor);
        assert((uint64_t)d.quotient * divisor + d.remainder == n);
    }
    /* Preserve the original division quirk, rather than assuming C division. */
    { fe2_division d = fe2_math_divide_unsigned(2, 1); assert(d.quotient == 1 && d.remainder == 1); }
    assert(fe2_math_sin(0) == 0);
    assert(fe2_math_cos(0) > 32700);
}


static void input_and_regions(void)
{
    fe2_input_state keys;
    fe2_pointer_state pointer;
    fe2_pointer_regions regions, full;
    unsigned i;
    int screenshot;
    assert(fe2_input_bind(fe2_test_input, sizeof fe2_test_input) == 0);
    assert(fe2_pointer_bind(fe2_test_pointer, sizeof fe2_test_pointer) == 0);
    memset(&pointer, 0, sizeof pointer);
    fe2_input_init(&keys);
    for (i = 0; i < 256; ++i) assert(keys.keys[i] == 0x80);
    fe2_input_scancode(&keys, &pointer, FE2_KEY_A);
    assert(keys.keys[FE2_KEY_A] == 0 && keys.key_repeat == 1);
    fe2_input_scancode(&keys, &pointer, FE2_KEY_A | 0x80);
    assert(keys.keys[FE2_KEY_A] == 0x80);
    fe2_input_scancode(&keys, &pointer, FE2_KEY_E0);
    fe2_input_scancode(&keys, &pointer, FE2_KEY_CTRL);
    assert(keys.keys[FE2_KEY_RIGHT_CTRL] == 0 && keys.keys[FE2_KEY_CTRL] == 0x80);
    fe2_input_restore(&pointer, FE2_INPUT_SCREENSHOT);
    assert(fe2_input_poll(&pointer, &screenshot) == 0 && screenshot);
    fe2_input_restore(&pointer, 42);
    assert(fe2_input_poll(&pointer, &screenshot) == 42 && !screenshot);
    assert(fe2_input_poll(&pointer, &screenshot) == 0);
    keys.countdown = 1;
    assert(fe2_input_tick(&keys, &pointer) == 0 && keys.countdown == 0);
    fe2_pointer_regions_init(&regions);
    assert(fe2_pointer_regions_count(&regions) == 0);
    for (i = 0; i < FE2_POINTER_REGIONS; ++i)
        assert(fe2_pointer_regions_add(&regions, 10, 10, 20, 20, (uint16_t)i) == 0);
    assert(regions.region[0].left == 0 && regions.region[0].top == 0);
    full = regions;
    assert(fe2_pointer_regions_add(&regions, 1, 1, 1, 1, 999) == -1);
    assert(memcmp(&regions, &full, sizeof regions) == 0);
    fe2_pointer_regions_remove(&regions, 0);
    assert(fe2_pointer_regions_count(&regions) == FE2_POINTER_REGIONS - 1);
    assert(regions.region[0].id == 1);
    fe2_pointer_regions_reset(&regions);
    assert(fe2_pointer_regions_count(&regions) == FE2_POINTER_FUNCTION_KEY_REGIONS);
    fe2_pointer_regions_clear(&regions);
    assert(fe2_pointer_regions_count(&regions) == 0);
}

static void system_record_storage(void)
{
    fe2_system_record records[2];
    fe2_record_space space;
    unsigned i;
    memset(records, 0, sizeof records);
    space.records = records; space.count = 2;
    for (i = 0; i < FE2_RECORD_BYTES; ++i) {
        fe2_record_set_byte(&records[0], i, (uint8_t)i);
        assert(fe2_record_byte(&records[0], i) == i);
    }
    fe2_record_set_dword(&records[0], FE2_RECORD_SEED, UINT32_C(0x89abcdef));
    assert(fe2_record_dword(&records[0], FE2_RECORD_SEED) == UINT32_C(0x89abcdef));
    fe2_space_set_byte(&space, 63, 0xcd); fe2_space_set_byte(&space, 64, 0xab);
    assert(fe2_space_word(&space, 63) == 0xabcd);
    assert(fe2_space_copy_name(&space, 127, "AB") == 130);
    assert(fe2_space_byte(&space, 127) == 'A' && fe2_space_byte(&space, 0) == 'B');
    assert(fe2_space_copy_within(&space, 10, 127) == 13);
    assert(fe2_space_byte(&space, 10) == 'A' && fe2_space_byte(&space, 12) == 0);
}

int main(void)
{
    mining(); catalogue(); math(); input_and_regions(); system_record_storage();
    puts("Native behavior tests passed"); return 0;
}

