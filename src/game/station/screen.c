/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The station screen and its list (0x8db..0xd7f, 0x4544..0x4595,
 * 0x4948..0x499c, 0x4b9b..0x4bdd, 0x4c3f..0x4c64). */
#include "station_internal.h"

static uint16_t page_size(uint16_t action) { return fe2_station_displays[action / 2].page_size; }
static void draw_continue(station_context *c);


void station_reset_rows(station_context *c)
{
    c->s->rows[0].text = 0;
    c->s->rows[0].text_high = 0;
    c->s->row_count = 0;
    c->s->first_row = 0;
}


void station_start_selection(station_context *c)
{
    c->s->ui_flags |= STATION_UI_SELECTION;
    c->s->selection_timer = STATION_SELECTION_TIME;
    station_reset_rows(c);
}

/* append `text` with parameters 1..4, 5's low word and the
 * subject to the first free row of 45 (none: nothing). */
void station_append(station_context *c, uint16_t text)
{
    fe2_station_state *s = c->s;
    fe2_station_row *row;
    unsigned i;

    for (i = 0; i < FE2_STATION_ROWS; ++i)
        if (s->rows[i].text == 0 && s->rows[i].text_high == 0) break;
    if (i == FE2_STATION_ROWS) return;
    row = &s->rows[i];
    row->text = text;
    row->text_high = station_word(c, P5);
    row->value[0] = station_get(c, P1);
    row->value[1] = station_get(c, P2);
    row->value[2] = station_get(c, P3);
    row->value[3] = station_get(c, P4);
    row->subject = station_get(c, P_SUBJECT);
    if (i + 1 < FE2_STATION_ROWS) {
        s->rows[i + 1].text = 0;
        s->rows[i + 1].text_high = 0;
    } else {
        s->rows_end = 0;
    }
    ++s->row_count;
}

/* one row at height `y`: its marks (on the more-row: next page,
 * previous page) and its text. Parameters 1..4 and 10 are kept around the
 * marks, 11 around the row; 11 takes 4's high word, sign extended. */
static void draw_row(station_context *c, uint16_t text, int16_t kind, uint16_t y)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    fe2_call_arg kept_text = c->a->arg[P_TEXT];
    fe2_call_arg kept[5];
    unsigned n;

    kept[0] = c->a->arg[P1];
    kept[1] = c->a->arg[P2];
    kept[2] = c->a->arg[P3];
    kept[3] = c->a->arg[P4];
    kept[4] = c->a->arg[P_NAME];
    if (kind >= 0) {
        g->draw_icon(g->opaque, 0x65, 0x7c, (uint16_t)(y - 1));
    } else {
        if ((int16_t)(s->first_row + page_size(s->action)) < (int16_t)s->row_count)
            g->draw_icon(g->opaque, 0x67, 0x7c, (uint16_t)(y - 1));
        if (s->first_row != 0) g->draw_icon(g->opaque, 0x66, 0x88, (uint16_t)(y - 1));
    }
    if (kind >= 6 && kind < 0xe) g->draw_icon(g->opaque, 0x65, 0x88, (uint16_t)(y - 1));
    for (n = 0; n < 4; ++n) c->a->arg[P1 + n] = kept[n];
    c->a->arg[P_NAME] = kept[4];
    station_set(c, P_TEXT, (uint32_t)(int32_t)(int16_t)(station_get(c, P4) >> 16));
    station_set_word(c, P_STYLE, 0xb1);
    g->draw_text(g->opaque, text, fe2_station_colours[2], (kind < 0 ? fe2_station_pagination_text_x : fe2_station_displays[kind / 2].text_x), y, c->a);
    c->a->arg[P_TEXT] = kept_text;
}

/* the heading (with the station's name) and the rows from the
 * first shown; a zero text ends them early. The row limit is inclusive. */
void station_draw_list(station_context *c, uint16_t action)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    uint16_t y = 0xf;
    int16_t rows;
    unsigned i;

    s->action = action;
    s->list_colours[0] = fe2_station_colours[0];
    s->list_colours[1] = fe2_station_colours[2];
    s->list_colours[2] = fe2_station_colours[4];
    station_set(c, P_NAME, s->identity);
    station_set(c, P_TEXT, station_name_ref(s->station));
    station_set_word(c, P_STYLE, 0x79);
    g->draw_text(g->opaque, s->heading, fe2_station_colours[2], 4, 6, c->a);
    rows = (int16_t)fe2_station_displays[action / 2].row_limit;
    for (i = s->first_row; i < FE2_STATION_ROWS; ++i) {   /* rows past the list: not reached (domain) */
        const fe2_station_row *row = &s->rows[i];
        station_set(c, P1, row->value[0]);
        station_set(c, P2, row->value[1]);
        station_set(c, P3, row->value[2]);
        station_set(c, P_NAME, row->subject);
        station_set(c, P4, row->value[3]);
        if (row->text == 0) break;
        draw_row(c, row->text, (int16_t)action, y);
        y = (uint16_t)(y + fe2_station_displays[action / 2].row_spacing);
        if (--rows < 0) break;
    }
    if ((int16_t)(s->row_count - 1) > (int16_t)fe2_station_displays[s->action / 2].row_limit)
        draw_row(c, 0x98d2, -2, 0x9d);
}

/* balance, free hold and used cabins. */
void station_draw_balance(station_context *c)
{
    fe2_station_state *s = c->s;
    station_set(c, P1, s->balance);
    station_set(c, P2, (uint16_t)(s->capacity - s->cargo_used));
    station_set(c, P_NAME, s->cargo_used);
    station_set(c, P3, (uint16_t)(s->cabins - s->cabins_used));
    station_set(c, P_TEXT, s->cabins_used);
    station_set_word(c, P_STYLE, 0);
    c->g->draw_text(c->g->opaque, 0x98d1, fe2_station_colours[4], 1, 0x8a, c->a);
}

/* the picture frame (parameters 11 and 12 are kept). */
static void draw_picture_frame(station_context *c)
{
    fe2_call_arg kept[2];
    kept[0] = c->a->arg[P_TEXT];
    kept[1] = c->a->arg[P_LIST];
    c->g->draw_frame(c->g->opaque, 1, 0x76, 0x16, 0x88, 0);
    c->a->arg[P_TEXT] = kept[0];
    c->a->arg[P_LIST] = kept[1];
}

/* composites draws the portrait of the identity. */
void station_show_portrait(station_context *c)
{
    station_set_word(c, P1, c->s->portrait);
    c->g->composite_portrait(c->g->opaque, c->s->identity, c->a);
}

/* the portrait, or its frame and caption while it waits. */
void station_draw_status(station_context *c)
{
    if (c->s->portrait_timer == 0) {
        station_show_portrait(c);
        return;
    }
    draw_picture_frame(c);
    station_set_word(c, P_STYLE, 0);
    c->g->draw_text_shadowed(c->g->opaque, 0x98c2, c->s->colours[1], 0x1e, 0x4e, c->a);
}

/* the controls, the panel, the list for `action`, the portrait and
 * the balance. */
void station_draw_frame(station_context *c, uint16_t action)
{
    const fe2_station_services *g = c->g;
    g->set_controls(g->opaque, 0, 0x1317, 0x27);
    g->clear_panel(g->opaque);
    /* The docked refresh clears the view after the action screen was built.
     * Draw its column titles again, or Buy, View, Fit and the rest vanish. */
    if (c->s->screen_title != 0) {
        station_set_word(c, P_STYLE, 0);
        g->draw_text(g->opaque, c->s->screen_title, fe2_station_colours[4], 0x78, 2, c->a);
    }
    station_draw_list(c, action);
    station_draw_status(c);
    station_draw_balance(c);
    if (fe2_station_displays[c->s->action / 2].continue_text != 0) draw_continue(c);
    g->present(g->opaque);
}

void station_draw_default(station_context *c)
{
    c->s->screen_title = 0;
    station_draw_frame(c, 0);
}

/* a heading message and the bulletin list. */
void station_draw_board(station_context *c, uint16_t heading)
{
    c->s->screen_title = 0;
    station_show_heading(c, heading);
    station_draw_frame(c, FE2_STATION_ACTION_BULLETIN);
}

/* a heading message and the default list. */
void station_draw_status_list(station_context *c, uint16_t heading)
{
    station_show_heading(c, heading);
    station_draw_default(c);
}

/* the continue mark and its text. */
static void draw_continue(station_context *c)
{
    c->g->draw_icon(c->g->opaque, 0x65, 0xc8, 0x9d);
    station_set_word(c, P_STYLE, 0);
    c->g->draw_text(c->g->opaque, 0x98d3, fe2_station_colours[2], 0xd2, 0x9d, c->a);
}

/* an action's screen: its title `text` (parameter 1 kept), the
 * list, the balance, the continue mark when the action has a message. */
void station_show_result(station_context *c, uint16_t text, uint16_t action)
{
    const fe2_station_services *g = c->g;
    fe2_station_state *s = c->s;
    station_set_word(c, P1, action);
    s->screen_title = text;
    g->clear_panel(g->opaque);
    station_set_word(c, P_STYLE, 0);
    g->draw_text(g->opaque, text, fe2_station_colours[4], 0x78, 2, c->a);
    station_draw_list(c, action);
    station_draw_balance(c);
    if (fe2_station_displays[s->action / 2].continue_text != 0) draw_continue(c);
    g->set_controls(g->opaque, 0, 0x1317, 0x27);
    station_draw_status(c);
    g->present(g->opaque);
}

/* open the station screen: the ownership question when one is
 * pending, else the greeting rows. */
void station_open_screen(station_context *c)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    fe2_object *station;

    s->input_lock = 0;
    g->select_controls(g->opaque, 0xc);
    station_set_word(c, P2, 0x98c4);
    g->panels_panel(g->opaque, 4, 0xffff, c->a);
    s->screen_active = 0xff;
    station_reset_rows(c);
    g->set_widgets(g->opaque, fe2_station_widgets_station);
    s->refit = 0;
    s->heading = 0x98bb;
    station_set(c, P_SUBJECT, s->station);
    station = station_lookup_object(c, s->station);
    s->identity = station_ror(station->target_seed, 16);
    s->portrait = 0x1c;
    s->portrait_timer = 0x8000;
    if ((int32_t)s->shared_countdown < 0) {
        station_append(c, 0x9818);
        station_append(c, 0x980a);
        station_append(c, 0x980b);
        station_append(c, 0x980c);
        station_show_heading(c, 0x98c5);
    } else {
        station_set(c, P_NAME, station_ror(station->target_seed, 16));
        s->game_flags |= 1;
        s->portrait = 0x3a;
        g->clear_regions(g->opaque);
        station_set(c, P1, (uint32_t)(int32_t)s->question[0]);
        station_set(c, P2, (uint32_t)(int32_t)s->question[1]);
        station_set(c, P3, (uint32_t)(int32_t)s->question[2]);
        station_append(c, 0x986c);
    }
    station_draw_default(c);
}

/* redraw the screen of the current action. */
void station_dispatch_action(station_context *c)
{
    switch (c->s->action) {
    case FE2_STATION_ACTION_TRADE: station_open_trade(c); break;
    case FE2_STATION_ACTION_SHIPYARD: station_open_shipyard(c); break;
    case FE2_STATION_ACTION_BULLETIN: station_open_bulletin(c); break;
    case FE2_STATION_ACTION_MISSIONS: station_open_missions(c); break;
    case FE2_STATION_ACTION_FITTING: station_show_fitting(c); break;
    case FE2_STATION_ACTION_REPAIRS: station_show_repairs(c); break;
    case FE2_STATION_ACTION_CONVERSATION: station_show_responses(c); break;
    case FE2_STATION_ACTION_SHIP:
        station_set(c, P_RECORD, c->s->refs.rows);
        station_open_ship_offer(c);
        break;
    default: station_draw_default(c); break;
    }
}

/* offer a docked craft of the ship's parent (the owner of a
 * craft the ship may have taken): text 0x9801 or 0x9800 with it. */
void fe2_station_offer_owner(fe2_station_state *state, const fe2_station_services *game,
                             fe2_call_args *args, fe2_ref object)
{
    station_context c = {state, game, args, 0};
    fe2_object *record = station_lookup_object(&c, object);
    uint16_t kept;
    if (!(record->flags & 0x10)) return;
    if ((uint8_t)station_get(&c, P2) != record->parent_handle) return;
    if (record->level > 0x13) return;
    kept = station_word(&c, P2);
    station_set(&c, P_SUBJECT, object);
    station_append(&c, (record->flags & 0x20) ? 0x9801 : 0x9800);
    station_set_word(&c, P2, kept);
}

void fe2_station_offer_visitor(void *context, fe2_ref object)
{
    station_context *c = context;
    fe2_station_offer_owner(c->s, c->g, c->a, object);
}

/* 0x72: docked: the station screen; in flight the selection rows for the
 * ship, its target and its parent. */
void fe2_station_draw(fe2_station_state *state, const fe2_station_services *game, fe2_call_args *args)
{
    station_context c = {state, game, args, 0};
    fe2_object *record;
    fe2_ref ref;

    state->key_latch = 0;
    if (state->flight_state == FE2_STATION_MODE_BERTH_A || state->flight_state == FE2_STATION_MODE_BERTH_B) {
        station_open_screen(&c);
        return;
    }
    if (!(state->ui_flags & STATION_UI_SELECTION))
        game->set_widgets(game->opaque, fe2_station_widgets_flight);
    station_start_selection(&c);
    station_set(&c, P_LIST, state->objects);
    station_set(&c, P_RECORD, state->ship);
    station_append(&c, 0x9803);
    if (state->target != 0) {
        ref = game->lookup(game->opaque, state->target, args);
        station_set(&c, P_SUBJECT, ref);
        record = station_lookup_object(&c, ref);
        if (record->map_marker != 0xff &&
            ((record->flags & 0x10) || (record->model >= 0x14 && record->model <= 0x4e)))
            station_append(&c, 0x9802);
    }
    record = station_lookup_object(&c, station_get(&c, P_RECORD));
    ref = game->lookup(game->opaque, record->parent_handle, args);
    station_set(&c, P_SUBJECT, ref);
    if (station_lookup_object(&c, ref)->flags & 0x10) {
        station_append(&c, 0x9800);
    } else {
        station_set_byte(&c, P2, station_lookup_object(&c, station_get(&c, P_RECORD))->parent_handle);
        game->visit_objects(game->opaque, station_get(&c, P_LIST), fe2_station_offer_visitor, &c, args);
    }
    station_set(&c, P3, state->deferred);
    if (state->deferred != 0) station_append(&c, 0x9809);
    if (state->fined_handle != 0) station_append(&c, 0x982f);
}

/* The docked screen is built by activate. The session paints the 3D view
 * afterwards, so the open screen is drawn again in the draw phase, with
 * its own action (the list's region codes need it). */
void fe2_station_refresh(fe2_station_state *state, const fe2_station_services *game, fe2_call_args *args)
{
    station_context c = {state, game, args, 0};
    if (state->screen_active) station_draw_frame(&c, state->action);
}
