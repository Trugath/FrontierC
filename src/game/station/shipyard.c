/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Shipyard, the ship offer and its preview, ship services and repairs
 *. */
#include "station_internal.h"

static fe2_object *ship(station_context *c) { return station_lookup_object(c, c->s->ship); }

/* the shipyard menu. */
void station_open_yard_list(station_context *c)
{
    fe2_station_state *s = c->s;
    station_reset_rows(c);
    c->g->show_panel(c->g->opaque);
    c->g->set_widgets(c->g->opaque, fe2_station_widgets_station);
    s->heading = 0x98bc;
    station_set(c, P_SUBJECT, s->station);
    s->identity = station_rol(station_lookup_object(c, s->station)->target_seed, 0x15);
    s->portrait_timer = 0x8000;
    s->portrait = 0xc;
    station_append(c, 0x980d);
    station_append(c, 0x980e);
    station_append(c, 0x980f);
    station_append(c, 0x9810);
    station_draw_status_list(c, 0x98c5);
}

/* the ships for sale (market's category list) priced against the
 * trade-in value of the ship. */
void station_open_shipyard(station_context *c)
{
    fe2_station_state *s = c->s;
    fe2_station_market_reply reply;
    const fe2_station_model *m;
    uint8_t *market;
    uint16_t value;
    unsigned at;

    station_reset_rows(c);
    s->heading = 0x98bc;
    station_set(c, P_SUBJECT, s->station);
    s->identity = station_ror(station_lookup_object(c, s->station)->target_seed, 0x18);
    s->portrait_timer = 0x8000;
    s->portrait = 0xc;
    station_show_heading(c, 0x98d5);
    station_set(c, P_SUBJECT, s->station);
    reply = c->g->market_market(c->g->opaque, 0, station_lookup_object(c, s->station)->handle, c->a);
    station_set_word(c, P_RECORD, (uint16_t)reply.ref);
    s->market = market = reply.market;
    station_set_word(c, P_STYLE, (uint16_t)(station_market_word(market, 0x1d8) - 1));
    if ((int16_t)station_word(c, P_STYLE) < 0) {
        station_show_heading(c, 0x98d6);
    } else {
        station_set_word(c, P5, fe2_station_drives[ship(c)->as.flight.drive].trade_in_value);
        m = station_lookup_model(c, ship(c)->model);
        value = (uint16_t)(m->word[FE2_MODEL_PRICE] + station_word(c, P5));
        value = (uint16_t)(value - fe2_station_drives[(uint8_t)m->word[FE2_MODEL_DRIVE_OPTIONS]].trade_in_value);
        station_set(c, P5, (uint32_t)value * 0x3b6);
        at = 0x1a0;
        do {
            uint16_t model = station_market_word(market, at);
            station_set(c, P4, (uint32_t)station_market_word(market, at + 2) << 16 | model);
            at += 4;
            m = station_lookup_model(c, model);
            station_set(c, P1, (uint32_t)1000 * m->word[FE2_MODEL_PRICE]);
            station_set(c, P2, station_get(c, P1) - station_get(c, P5));
            station_set_word(c, P_SUBJECT, m->word[FE2_MODEL_NAME_TEXT]);
            station_set(c, P3, (uint32_t)(int32_t)(int16_t)m->word[FE2_MODEL_INTERNAL_CAPACITY]);
            station_append(c, 0x9811);
            station_set_word(c, P_STYLE, (uint16_t)(station_word(c, P_STYLE) - 1));
        } while ((int16_t)station_word(c, P_STYLE) >= 0);
    }
    station_set_word(c, P1, FE2_STATION_ACTION_SHIPYARD);
    station_show_result(c, 0x98df, FE2_STATION_ACTION_SHIPYARD);
}

/* the preview's scale lines. */
static void draw_scale(station_context *c)
{
    const fe2_station_services *g = c->g;
    uint16_t scale = c->s->preview.scale;
    c->s->line_colour = c->s->colours[0];
    g->draw_line(g->opaque, 4, 0x85, 4, 0x82);
    g->draw_line(g->opaque, (uint16_t)(4 + scale), 0x85, (uint16_t)(4 + scale), 0x82);
    g->draw_line(g->opaque, 4, 0x84, (uint16_t)(4 + scale), 0x84);
}

/* the preview of `model`: its drive, scale and camera, the view and
 * its light. */
void station_prepare_preview(station_context *c, uint16_t model)
{
    fe2_station_state *s = c->s;
    const fe2_station_model *m;
    int16_t size;

    s->preview.model = model;
    m = station_lookup_model(c, model);
    s->action = FE2_STATION_ACTION_SHIP;
    s->drive_index = m->word[FE2_MODEL_DRIVE_OPTIONS];
    size = (int16_t)m->word[FE2_MODEL_PREVIEW_SIZE];
    /* A zero divisor faults; the original resumes with the dividend. */
    s->preview.scale = size == 0 ? 0x1194 : (uint16_t)(0x1194u / (uint16_t)size);
    s->preview.position[0] = (uint32_t)((int32_t)(int16_t)0xfe8d * size);
    s->preview.position[1] = (uint32_t)((int32_t)7 * size);
    s->preview.position[2] = (uint32_t)((int32_t)1000 * size);
    c->g->open_view(c->g->opaque);
    c->g->set_view(c->g->opaque, 0xb0f, 0x17);
    s->redraw_flags &= 0xfe;
    s->light[0] = 0xb619;
    s->light[1] = 0xb619;
    s->light[2] = 0x49e7;
    s->shades[0] = 0x777;
    s->shades[1] = 0x777;
    s->shades[2] = 0x666;
    s->shades[3] = 0x555;
    s->shades[4] = 0x444;
    s->shades[5] = 0x333;
    s->shades[6] = 0x222;
    s->shades[7] = 0x111;
}

/* each pass of the ship offer: turn and draw the preview ship. */
void station_preview_frame(station_context *c)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    fe2_call_arg kept[2];
    fe2_object *o;
    fe2_ref work;
    unsigned i;

    s->preview.angle[0] = (uint16_t)(s->preview.angle[0] + (uint16_t)(s->real_step >> 3));
    s->preview.angle[1] = (uint16_t)(s->preview.angle[1] + (uint16_t)(s->real_step >> 4));
    {
        fe2_call_arg frame_kept[2];
        frame_kept[0] = c->a->arg[P_TEXT];
        frame_kept[1] = c->a->arg[P_LIST];
        g->draw_frame(g->opaque, 1, 0x76, 0x16, 0x88, 0);
        c->a->arg[P_TEXT] = frame_kept[0];
        c->a->arg[P_LIST] = frame_kept[1];
    }
    draw_scale(c);
    kept[0] = c->a->arg[P_LIST];
    kept[1] = c->a->arg[P_TEXT];
    s->redraw_flags &= 0xfe;
    work = g->work_record(g->opaque, FE2_STATION_WORK_PREVIEW);
    station_set(c, P_RECORD, work);
    station_set_table(c, P_SUBJECT, fe2_station_template);
    station_set_word(c, P1, 0);
    g->prepare_object(g->opaque, c->a);
    o = station_lookup_object(c, work);
    for (i = 0; i < 3; ++i) o->camera_offset[i] = s->preview.position[i];
    o->target_seed = station_ror(s->preview.seed, 16);
    o->as.flight.equipment = 0xffffffffu;
    for (i = 0; i < 4; ++i) {
        o->as.flight.installed[i] = 0x88;
        o->as.flight.installed[4 + i] = 0x82;
    }
    o->model = s->preview.model;
    for (i = 0; i < sizeof s->preview.name && (i == 0 || s->preview.name[i - 1] != 0); ++i)
        o->name[i] = s->preview.name[i];
    o->clock = (uint16_t)(s->time_of_day >> 10);
    s->basis_angle[0] = s->preview.angle[0];
    s->basis_angle[1] = s->preview.angle[1];
    g->orient_object(g->opaque, work);
    g->render_object(g->opaque, work, c->a);
    c->a->arg[P_TEXT] = kept[1];
    c->a->arg[P_LIST] = kept[0];
}

/* the ship offer of the row parameter 9 names: its registration,
 * statistics (drives adds its own) and the preview. */
void station_open_ship_offer(station_context *c)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    const fe2_station_row *row = station_row_at(c, station_word(c, P_RECORD));
    const fe2_station_model *m;
    fe2_station_name name;
    fe2_call_arg kept[5];
    uint16_t a, b, d, style;
    unsigned i;

    if (row == 0) return;                       /* not a row: not reached (domain) */
    station_set(c, P1, row->value[0]);
    station_set(c, P2, row->value[1]);
    station_set(c, P3, row->value[2]);
    station_set(c, P4, row->value[3]);
    station_set(c, P_NAME, row->subject);
    station_reset_rows(c);
    kept[0] = c->a->arg[P1];
    kept[1] = c->a->arg[P2];
    kept[2] = c->a->arg[P3];
    kept[3] = c->a->arg[P4];
    kept[4] = c->a->arg[P_NAME];
    station_set(c, P_SUBJECT, station_get(c, P_NAME));
    station_append(c, 0x9811);
    s->preview.seed = (uint32_t)0x4a0d * (uint16_t)(station_get(c, P4) >> 16);
    g->names_name(g->opaque, 0xf, s->preview.seed, FE2_STATION_WORK_SHIP_NAME, &name);
    for (i = 0; i < sizeof s->preview.name; ++i) {
        s->preview.name[i] = name.text[i];
        if (name.text[i] == 0) break;
    }
    station_set_word(c, P1, FE2_STATION_ACTION_SHIP);
    station_show_result(c, 0x98df, FE2_STATION_ACTION_SHIP);
    g->present(g->opaque);
    m = station_lookup_model(c, (uint16_t)kept[3].value);
    station_set(c, P1, (uint32_t)(int32_t)(int16_t)m->word[FE2_MODEL_HULL_MASS]);
    station_set(c, P2, (uint32_t)(int32_t)(int16_t)m->word[FE2_MODEL_INTERNAL_CAPACITY]);
    station_set(c, P3, (uint32_t)(m->word[FE2_MODEL_GUN_MOUNT_FLAGS] & 0xf));
    station_set(c, P_NAME, (uint32_t)(int32_t)(int16_t)m->word[FE2_MODEL_MISSILE_PYLONS]);
    station_set(c, P_TEXT, (uint32_t)(int32_t)(int16_t)m->word[FE2_MODEL_REQUIRED_CREW]);
    s->preview.crew = m->word[FE2_MODEL_REQUIRED_CREW];
    station_set_word(c, P_STYLE, 0);
    g->draw_text(g->opaque, 0x98fc, fe2_station_colours[2], 0x87, 0x1c, c->a);
    g->hyperspace_ship(g->opaque, 3, (uint16_t)kept[3].value, c->a);
    style = station_word(c, P_STYLE);
    a = station_word(c, P4);
    b = station_word(c, P5);
    d = station_word(c, P6);
    station_set_word(c, P_STYLE, 0);
    g->draw_text(g->opaque, 0x98fe, fe2_station_colours[2], 0x85, 0x78, c->a);
    station_set(c, P1, (uint32_t)(int32_t)(int16_t)a);
    station_set(c, P2, (uint32_t)(int32_t)(int16_t)b);
    station_set(c, P3, (uint32_t)(int32_t)(int16_t)d);
    station_set_word(c, P_STYLE, 0);
    g->draw_text(g->opaque, 0x98ff, fe2_station_colours[2], 0xd7, 0x8c, c->a);
    station_set_word(c, P_STYLE, style);
    station_set(c, P_NAME, kept[4].value);
    station_set(c, P1, (uint32_t)(m->word[FE2_MODEL_MAIN_THRUST] / 0x36));
    station_set(c, P2, (uint32_t)((uint16_t)-m->word[FE2_MODEL_RETRO_THRUST] / 0x36));
    station_set(c, P3, (uint32_t)(int32_t)(int16_t)station_word(c, P_STYLE));
    station_set_word(c, P_NAME, fe2_station_drives[(uint8_t)m->word[FE2_MODEL_DRIVE_OPTIONS]].name_text);
    station_set(c, P_TEXT, s->refs.preview_name);
    station_set_word(c, P_STYLE, 0);
    g->draw_text(g->opaque, 0x98fd, fe2_station_colours[2], 0x87, 0x4e, c->a);
    g->present(g->opaque);
    g->clear_picture(g->opaque);
    for (i = 0; i < 4; ++i) c->a->arg[P1 + i] = kept[i];
    c->a->arg[P_NAME] = kept[4];
    station_prepare_preview(c, station_word(c, P4));
}

/* refused: the payment (undone) is parameter 3. */
static void refuse(station_context *c, uint32_t price)
{
    station_set(c, P3, price);
    station_show_heading(c, 0x9939);
}

/* buy the offered ship (no passengers aboard): the old one goes
 * into the yard's list in its place. The hull is cleared of fittings
 * and given one tonne of fuel before it is rebuilt from the model.
 * A nonnegative drive word is fitted afterwards; a negative one was
 * already applied with the model. */
static void buy_ship(station_context *c)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    fe2_call_arg kept[5];
    uint32_t price;
    fe2_object *o;
    uint8_t *market;
    unsigned at, i;
    int16_t left;
    uint16_t quotient, hi, lo;

    if (s->cabins_used != 0) {
        station_show_heading(c, 0x98d8);
        return;
    }
    kept[0] = c->a->arg[P0];
    kept[1] = c->a->arg[P1];
    kept[2] = c->a->arg[P2];
    kept[3] = c->a->arg[P3];
    kept[4] = c->a->arg[P4];
    price = station_get(c, P2) * 10;
    if ((int32_t)price < 0 || s->balance >= price) {
        s->balance -= price;
    } else {
        refuse(c, price);
        return;
    }
    station_open_ship_offer(c);
    c->a->arg[P4] = kept[4];
    c->a->arg[P3] = kept[3];
    station_set_word(c, P2, (uint16_t)kept[2].value);
    c->a->arg[P1] = kept[1];
    c->a->arg[P0] = kept[0];
    s->refit = 0xff;
    station_set_word(c, P1, station_word(c, P4));
    lo = station_word(c, P4);
    hi = (uint16_t)(station_get(c, P4) >> 16);
    station_set(c, P4, (uint32_t)lo << 16 | hi);
    market = s->market;
    /* The count is read 0x1d8 past the list (the next record's text count). */
    left = (int16_t)(station_market_word(market, 0x1a0 + 0x1d8) - 1);
    at = 0x1a0;
    while (station_word(c, P4) != station_market_word(market, at + 2)) {
        at += 4;
        if (--left < 0) break;
    }
    o = ship(c);
    station_set_market_word(market, at, o->model);
    hi = (uint16_t)o->target_seed;
    lo = (uint16_t)(o->target_seed >> 16);
    /* A quotient over 16 bits faults; the original resumes with the dividend's low word. */
    quotient = hi >= 0x4a0d ? lo : (uint16_t)((((uint32_t)hi << 16) | lo) / 0x4a0d);
    station_set_market_word(market, at + 2, quotient);
    s->service_timer[0] = 0x1f4;
    s->service_timer[1] = 0x1f4;
    s->service_day[0] = s->day;
    s->service_day[1] = s->day;
    for (i = 0; i < 12; ++i) o->as.flight.installed[i] = 0;
    o->as.flight.equipment = 0;
    o->as.flight.equipment_damaged = 0;
    o->as.flight.shield_charge = 0;
    o->as.flight.shield_capacity = 0;
    s->passenger_limit = (uint16_t)(s->preview.crew - 1);
    o->fuel = 0x20000000u;
    o->target_seed = station_ror(s->preview.seed, 16);
    station_set(c, P_SUBJECT, s->ship);
    station_set(c, P_RECORD, s->ship);
    g->prepare_object(g->opaque, c->a);
    o = station_lookup_object(c, station_get(c, P_SUBJECT));
    for (i = 0; i < sizeof s->preview.name; ++i) {
        o->name[i] = s->preview.name[i];
        if (s->preview.name[i] == 0) break;
    }
    g->flight_restore_capacity(g->opaque);
    g->mining_service(g->opaque, 4, c->a);
    o = ship(c);
    for (i = 0; i < 6; ++i) s->saved_limits[i] = (uint16_t)o->as.flight.control_limits[i / 2][i % 2];
    if ((int16_t)s->drive_index >= 0) {
        o->as.flight.drive = (uint8_t)s->drive_index;
        s->capacity = (uint16_t)(s->capacity - fe2_station_drives[o->as.flight.drive].mass);
    }
    station_set(c, P_RECORD, s->ship);
    if (station_lookup_object(c, s->station)->flags & 0x40) {
        o = station_lookup_object(c, station_get(c, P_RECORD));
        station_set(c, P0, (uint32_t)(int32_t)(int16_t)-o->orientation[1]);
        station_set(c, P1, (uint32_t)(int32_t)(int16_t)-o->orientation[4]);
        station_set(c, P2, (uint32_t)(int32_t)(int16_t)-o->orientation[7]);
        g->move_object(g->opaque, c->a);
        station_set(c, P_LIST, s->objects);
        g->attach_object(g->opaque, c->a);
    }
    station_show_heading(c, 0x98d7);
    station_reset_rows(c);
}


void station_show_repairs(station_context *c)
{
    station_set_word(c, P1, FE2_STATION_ACTION_REPAIRS);
    station_show_result(c, 0x98dd, FE2_STATION_ACTION_REPAIRS);
}

/* the services: servicing (two parts), hull, the control limits
 * lost and the damaged equipment, each priced. */
void station_show_service_list(station_context *c)
{
    fe2_station_state *s = c->s;
    const fe2_station_model *m;
    fe2_object *o;
    uint32_t damage;
    int16_t left;
    uint16_t hull, k;

    station_reset_rows(c);
    s->heading = 0x98bc;
    s->identity = station_ror(station_lookup_object(c, s->station)->target_seed, 0x12);
    s->portrait_timer = 0x8000;
    s->portrait = 0xc;
    station_set(c, P_SUBJECT, s->ship);
    m = station_lookup_model(c, ship(c)->model);
    station_set(c, P_TEXT, m->ref);
    station_set(c, P3, (uint32_t)0x50 * m->word[FE2_MODEL_PRICE]);
    station_append(c, 0x9815);
    station_set(c, P3, (uint32_t)0x32 * m->word[FE2_MODEL_PRICE]);
    station_set(c, P2, s->service_day[1]);
    station_append(c, 0x9816);
    station_set(c, P2, s->service_day[0]);
    station_append(c, 0x9817);
    o = station_lookup_object(c, station_get(c, P_SUBJECT));
    hull = (uint16_t)((uint16_t)(m->word[FE2_MODEL_HULL_MASS] << 2) - o->hull);
    if (hull != 0) {
        station_set(c, P1, (uint32_t)hull * 0x19);
        station_set(c, P3, 0xa);
        station_append(c, 0x9813);
    }
    if (o->as.flight.drive != 0) {
        station_set(c, P3, 0x64);
        for (left = 5, k = 0; left >= 0; --left, ++k) {
            if (s->saved_limits[k] != 0) continue;
            station_set_word(c, P1, (uint16_t)(0x9997 + 5 - left));
            station_append(c, 0x9814);
        }
    }
    damage = station_ror(o->as.flight.equipment_damaged, 16);
    for (left = 0x1f; left >= 0; --left) {
        if (!(damage & ((uint32_t)1 << left))) continue;
        if (left >= STATION_REPAIR_COUNT) continue;   /* past the table (domain) */
        station_set_word(c, P1, (uint16_t)left);
        station_set_word(c, P2, fe2_station_repairs[left].name_text);
        station_set(c, P3, (uint32_t)(int32_t)(int16_t)fe2_station_repairs[left].cost);
        station_append(c, 0x9812);
    }
    station_show_repairs(c);
}

/* a paid service (ten times parameter 3's word). */
static int pay(station_context *c)
{
    uint32_t cost = (uint32_t)0xa * station_word(c, P3);
    if (c->s->balance < cost) {
        refuse(c, cost);
        return 0;
    }
    c->s->balance -= cost;
    return 1;
}

static void service(station_context *c, unsigned index)
{
    fe2_station_state *s = c->s;
    fe2_object *o;
    uint32_t bit;
    uint16_t at;
    int16_t limit;

    if (!pay(c)) return;
    o = ship(c);
    switch (index) {
    case 0x12:                                         /* repair equipment item parameter 1 */
        bit = station_word(c, P1) < 32 ? (uint32_t)1 << station_word(c, P1) : 0;
        bit = station_ror(bit, 16);
        o->as.flight.equipment |= bit;
        o->as.flight.equipment_damaged &= ~bit;
        station_show_heading(c, 0x98da);
        break;
    case 0x13: ++o->hull; break;
    case 0x14: {                                       /* restore control limit parameter 1 */
        const fe2_station_model *m = station_lookup_model(c, o->model);
        at = (uint16_t)((uint16_t)(station_word(c, P1) - 0x9997) * 2);
        if ((int16_t)at < 8) {
            limit = (int16_t)m->word[FE2_MODEL_RETRO_THRUST];
            if (!(at & 2)) limit = (int16_t)-limit;
        } else {
            limit = (int16_t)m->word[(at - 8) / 2];
        }
        o = ship(c);
        o->as.flight.control_limits[at / 4][(at / 2) % 2] = limit;
        s->saved_limits[at / 2] = (uint16_t)limit;
        break;
    }
    case 0x15:                                         /* full service */
        s->service_timer[0] = 0x1f4;
        s->service_timer[1] = 0x1f4;
        s->service_day[0] = s->day;
        s->service_day[1] = s->day;
        station_show_heading(c, 0x98db);
        break;
    case 0x16:
        s->service_timer[1] = 0x1f4;
        s->service_day[1] = s->day;
        station_show_heading(c, 0x98dc);
        break;
    default:
        s->service_timer[0] = 0x1f4;
        s->service_day[0] = s->day;
        station_show_heading(c, 0x98dc);
        break;
    }
    station_show_service_list(c);
}

void station_yard_handler(station_context *c, unsigned index)
{
    const fe2_station_services *g = c->g;
    switch (index) {
    case 0x0a: station_open_yard_list(c); break;
    case 0x0e:
        g->show_panel(g->opaque);
        g->set_widgets(g->opaque, fe2_station_widgets_board);
        station_show_heading(c, 0x98d9);
        station_show_service_list(c);
        break;
    case 0x0f:
        g->show_panel(g->opaque);
        g->set_widgets(g->opaque, fe2_station_widgets_list);
        station_open_shipyard(c);
        break;
    case 0x11:                                         /* a ship for sale */
        if (station_word(c, P_STYLE) & 0x10) station_open_ship_offer(c);
        else buy_ship(c);
        break;
    case 0x51:
        g->show_panel(g->opaque);
        g->set_widgets(g->opaque, fe2_station_widgets_station);
        station_open_yard_list(c);
        break;
    default: service(c, index); break;
    }
}
