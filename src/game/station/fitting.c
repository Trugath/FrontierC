/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Equipment fitting : the list of items the station's tech
 * level offers, buying (fitting) and selling (removing) them, and the
 * weapon mounts. */
#include "station_internal.h"

static const fe2_station_item *item(unsigned index) { return &fe2_station_items[index]; }

/* The selected equipment item index is kept in parameter 6. */
static unsigned chosen(station_context *c) { return station_word(c, P6); }
static uint8_t chosen_code(station_context *c) { return item(chosen(c))->code; }
static fe2_object *subject(station_context *c) { return station_lookup_object(c, station_get(c, P_SUBJECT)); }


void station_show_fitting(station_context *c)
{
    station_set_word(c, P1, FE2_STATION_ACTION_FITTING);
    station_show_result(c, 0x98de, FE2_STATION_ACTION_FITTING);
}

/* the items at or below the system's tech level, priced by it. */
static void open_items(station_context *c)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    uint16_t level = 0xc;
    unsigned k;

    s->identity = station_rol(station_lookup_object(c, s->station)->target_seed, 0x17);
    s->portrait = 0xc;
    s->portrait_timer = 0x8000;
    g->show_panel(g->opaque);
    g->set_widgets(g->opaque, fe2_station_widgets_list);
    station_show_heading(c, 0x98d4);
    station_reset_rows(c);
    if (s->system_seed >= STATION_FEE_TIER_LO) --level;
    if (s->system_seed >= STATION_FEE_TIER_MID) --level;
    if (s->system_seed >= STATION_FEE_TIER_HI) --level;
    s->tech_level = level;
    for (k = 0; k < STATION_ITEMS; ++k) {
        if ((uint8_t)s->system_seed < item(k)->tech_level) continue;
        station_set_word(c, P_SUBJECT, (uint16_t)(0x9873 + k));
        station_set(c, P1, (uint32_t)s->tech_level * item(k)->price);
        station_set(c, P2, (uint32_t)s->tech_level * item(k)->refund);
        station_set(c, P3, (uint32_t)(int32_t)(int16_t)item(k)->mass);
        station_append(c, 0x986e);
    }
    station_show_fitting(c);
}

/* the outcome, and the list again. */
static void outcome(station_context *c, uint16_t text)
{
    station_show_heading(c, text);
    station_show_fitting(c);
}

/* paid and stowed. */
static void bought(station_context *c)
{
    c->s->capacity = (uint16_t)(c->s->capacity - station_word(c, P4));
    c->s->balance -= station_get(c, P2);
    outcome(c, 0x98e9);
}

/* refunded and unloaded. */
static void sold(station_context *c)
{
    c->s->capacity = (uint16_t)(c->s->capacity + station_word(c, P4));
    c->s->balance += station_get(c, P3);
    outcome(c, 0x98ea);
}

/* a mount refused (parameter 1 names it, 0x8600 + mount). */
static void mount_refused(station_context *c, uint16_t text)
{
    station_set_word(c, P1, (uint16_t)(station_word(c, P_STYLE) + 0x8600));
    outcome(c, text);
}

/* fit to or clear weapon mount `parameter 7`. */
static void use_mount(station_context *c, int remove)
{
    uint8_t *mount = &subject(c)->as.flight.installed[station_word(c, P_STYLE)];
    if (!remove) {
        if (*mount != 0) {
            mount_refused(c, 0x98e6);
            return;
        }
        *mount = chosen_code(c);
        bought(c);
        return;
    }
    if (chosen_code(c) != *mount) {
        outcome(c, 0x98e3);
        return;
    }
    *mount = 0;
    sold(c);
}

/* The parked parameters (at the far pointer 0x7fc): the mode (0 fit,
 * 0xffff remove) and parameters 1..6, 8..11. */
static void park(station_context *c, uint16_t mode)
{
    uint32_t *p = c->s->parked;
    unsigned n;
    p[0] = (p[0] & 0xffff0000u) | mode;
    for (n = 1; n <= 6; ++n) p[n] = station_get(c, n);
    for (n = 0; n < 4; ++n) p[7 + n] = station_get(c, P_SUBJECT + n);
}

static uint16_t unpark(station_context *c)
{
    const uint32_t *p = c->s->parked;
    unsigned n;
    for (n = 1; n <= 6; ++n) station_set(c, n, p[n]);
    for (n = 0; n < 4; ++n) station_set(c, P_SUBJECT + n, p[7 + n]);
    return (uint16_t)p[0];
}

/* an item for the weapon mounts: one mount is used at once; with
 * more the choice of mount is offered (0x9870..0x9872 by the mount count). */
static void choose_mount(station_context *c, uint16_t mode)
{
    fe2_ref ship = station_get(c, P_SUBJECT);
    fe2_object *o = station_lookup_object(c, ship);
    uint8_t mounts = o->as.flight.capacity_class;

    station_set(c, P_RECORD, (ship & 0xffff0000u) | (uint16_t)(ship + 0xa8));
    if (mounts == 1) {
        station_set_word(c, P_STYLE, 0);
        use_mount(c, mode != 0);
        return;
    }
    if (mounts == 0) {
        mount_refused(c, 0x98e8);
        return;
    }
    park(c, mode);
    station_reset_rows(c);
    station_append(c, 0x986f);
    station_append(c, 0x9870);
    unpark(c);
    o = subject(c);
    if (o->as.flight.capacity_class != 2) {
        station_append(c, 0x9871);
        if (o->as.flight.capacity_class != 3) station_append(c, 0x9872);
    }
    station_draw_status_list(c, 0x98eb);
    c->g->show_panel(c->g->opaque);
    c->g->set_widgets(c->g->opaque, fe2_station_widgets_station);
}

/* an equipment bit (the code with its halves swapped). */
static void buy_equipment(station_context *c)
{
    fe2_object *o = subject(c);
    uint32_t bit = (uint32_t)1 << ((chosen_code(c) ^ 0x10) & 31);
    if (o->as.flight.equipment & bit) {
        outcome(c, 0x98e0);
        return;
    }
    if (bit & (1u << 0x14)) {
        if (!(station_lookup_model(c, o->model)->word[FE2_MODEL_GUN_MOUNT_FLAGS] & 0x100)) {
            outcome(c, 0x98ec);
            return;
        }
    }
    if ((bit & (1u << 0x1d)) && !(o->as.flight.equipment & (1u << 0x14))) {
        outcome(c, 0x98ed);
        return;
    }
    o->as.flight.equipment |= bit;
    bought(c);
}

/* the first free slot of the installed items (+0xac), last first. */
static void buy_installed(station_context *c)
{
    fe2_object *o = subject(c);
    int16_t slot = (int16_t)(station_lookup_model(c, o->model)->word[FE2_MODEL_MISSILE_PYLONS] - 1);
    o = subject(c);
    for (; slot >= 0; --slot) {
        if (o->as.flight.installed[4 + slot] == 0) {
            o->as.flight.installed[4 + slot] = chosen_code(c);
            bought(c);
            return;
        }
    }
    outcome(c, 0x98e1);
}

/* a passenger cabin through mining (fewer than ten police?). */
static void buy_cabin_service(station_context *c)
{
    uint16_t count = c->s->police_count;
    station_set_word(c, P_STYLE, count);
    if (count >= 0xa) {
        outcome(c, 0x99bf);
        return;
    }
    c->g->mining_service(c->g->opaque, 2, c->a);
    bought(c);
}

static void buy(station_context *c, unsigned route)
{
    fe2_object *o;
    switch (route) {
    case 0: buy_equipment(c); break;
    case 1: ++c->s->cabins; bought(c); break;
    case 2: choose_mount(c, 0); break;
    case 3:
        o = subject(c);
        if (o->as.flight.drive != 0) {
            outcome(c, 0x98e2);
            return;
        }
        o->as.flight.drive = chosen_code(c);
        c->s->motion_flags &= 0xfe;
        bought(c);
        break;
    case 4: subject(c)->as.flight.shield_capacity = (uint16_t)(subject(c)->as.flight.shield_capacity + 0x40); bought(c); break;
    case 5: buy_installed(c); break;
    default: buy_cabin_service(c); break;
    }
}

static void sell(station_context *c, unsigned route)
{
    fe2_object *o = subject(c);
    uint32_t bit;
    int slot;
    switch (route) {
    case 0:
        station_set_byte(c, P2, chosen_code(c));
        bit = (uint32_t)1 << ((chosen_code(c) ^ 0x10) & 31);
        if (!(o->as.flight.equipment & bit)) {
            outcome(c, 0x98e3);
            return;
        }
        o->as.flight.equipment ^= bit;
        sold(c);
        break;
    case 1:
        station_set_word(c, P_STYLE, c->s->cabins);
        if (c->s->cabins == 0) {
            outcome(c, 0x98e3);
            return;
        }
        if (c->s->cabins == c->s->cabins_used) {
            outcome(c, 0x98e5);
            return;
        }
        --c->s->cabins;
        sold(c);
        break;
    case 2: choose_mount(c, 0xffff); break;
    case 3:
        if (chosen_code(c) != o->as.flight.drive) {
            outcome(c, 0x98e3);
            return;
        }
        if ((int16_t)station_lookup_model(c, o->model)->word[FE2_MODEL_DRIVE_OPTIONS] < 0) {
            outcome(c, 0x98e4);
            return;
        }
        o = subject(c);
        o->as.flight.drive = 0;
        o->fuel = 0;
        sold(c);
        break;
    case 4:
        if (o->as.flight.shield_capacity < 0x40) {
            outcome(c, 0x98e3);
            return;
        }
        o->as.flight.shield_capacity = (uint16_t)(o->as.flight.shield_capacity - 0x40);
        sold(c);
        break;
    case 5:
        for (slot = 7; slot >= 0; --slot) {
            if (chosen_code(c) == o->as.flight.installed[4 + slot]) {
                o->as.flight.installed[4 + slot] = 0;
                sold(c);
                return;
            }
        }
        outcome(c, 0x98e3);
        break;
    default:
        if (c->g->mining_service(c->g->opaque, 3, c->a) != 0) sold(c);
        else outcome(c, 0x98e3);
        break;
    }
}

/* an item row: buy it (if affordable and it fits) or, with
 * parameter 7's bit 4 (a right click), sell it. Prices are tenfold. */
static void choose_item(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t price = station_get(c, P1);
    uint16_t selector;
    unsigned k;

    station_set(c, P3, station_get(c, P2));
    station_set(c, P2, price);
    selector = station_word(c, P_NAME);
    station_set_word(c, P1, selector);
    s->refit = 0xff;
    k = (uint16_t)(selector - 0x9873);
    if (k >= STATION_ITEMS) return;
    station_set(c, P2, station_get(c, P2) * 10);
    station_set(c, P3, station_get(c, P3) * 10);
    station_set_word(c, P4, item(k)->mass);
    if (station_word(c, P_STYLE) & 0x10) {
        station_set(c, P_SUBJECT, s->ship);
        station_set_word(c, P6, (uint16_t)k);
        sell(c, item(k)->handler);
        return;
    }
    if (station_get(c, P2) > s->balance) {
        outcome(c, 0x99cd);
        return;
    }
    if ((int16_t)(s->capacity - s->cargo_used) < (int16_t)station_word(c, P4)) {
        outcome(c, 0x98e7);
        return;
    }
    station_set(c, P_SUBJECT, s->ship);
    station_set_word(c, P6, (uint16_t)k);
    buy(c, item(k)->handler);
}

/* weapon mount 0..3 chosen: the list is
 * rebuilt, the parked item is fitted or removed there. */
static void choose_mount_row(station_context *c, uint16_t mount)
{
    uint16_t mode;
    station_set_word(c, P_STYLE, mount);
    open_items(c);
    mode = unpark(c);
    station_set_word(c, P_STYLE, mount);
    use_mount(c, mode != 0);
}

void station_fitting_handler(station_context *c, unsigned index)
{
    switch (index) {
    case 0x0d: open_items(c); break;
    case 0x6e: choose_item(c); break;
    default: choose_mount_row(c, (uint16_t)(index - 0x6f)); break;
    }
}
