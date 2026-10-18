/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Keys, selection and the handler dispatch. */
#include "station_internal.h"


void station_reset_selection_keys(station_context *c)
{
    uint16_t key;
    c->s->ui_flags &= (uint8_t)~STATION_UI_SELECTION;
    for (key = 0x31; key <= 0x35; ++key) c->g->unregister_key(c->g->opaque, key);
}

/* the handler of row text `text` (0x9800 + index, 115 handlers;
 * texts past them index beyond the table: not reached). */
void station_run_handler(station_context *c, uint16_t text)
{
    unsigned index = (uint16_t)(text - STATION_TEXT_BASE);
    if (index >= sizeof(fe2_station_reply_routes) / sizeof(fe2_station_reply_routes[0])) return;
    switch (fe2_station_reply_routes[index]) {
    case STATION_REPLY_SERVICE: station_service_handler(c, index); break;
    case STATION_REPLY_FITTING: station_fitting_handler(c, index); break;
    case STATION_REPLY_YARD: station_yard_handler(c, index); break;
    case STATION_REPLY_TRADE: station_trade_handler(c, index); break;
    case STATION_REPLY_CHARGE: station_charge_handler(c, index); break;
    case STATION_REPLY_TALK: station_talk_handler(c, index); break;
    case STATION_REPLY_MISSION: station_mission_handler(c, index); break;
    case STATION_REPLY_BOARD: station_board_handler(c, index); break;
    }
}

/* run row `index`: commander hears of it (unless the time step is 0x3e2),
 * a click, its values become parameters 1..4 and 10, parameter 9's offset
 * word names it. */
void station_select_row(station_context *c, uint16_t index)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    const fe2_station_row *row = &s->rows[index];

    if (s->time_step != 0x3e2) g->commander_restore_time(g->opaque);
    station_set_word(c, P_RECORD, station_row_offset(c, index));
    g->play_effect(g->opaque, 0x14);
    station_set(c, P1, row->value[0]);
    station_set(c, P2, row->value[1]);
    station_set(c, P3, row->value[2]);
    station_set(c, P4, row->value[3]);
    station_set(c, P_NAME, row->subject);
    station_run_handler(c, row->text);
}

/* after the message line: panels restores its panel, or (the
 * deferred-charge notice selected) the fines are dealt with. */
void station_resume_action(station_context *c)
{
    fe2_station_state *s = c->s;
    if (s->flight_state == FE2_STATION_MODE_HYPERSPACE) return;
    if (s->message_flags & 1) return;
    if ((uint16_t)(s->selected_text - 5) == 0x99c6) {
        s->misjump_flag = 0;
        station_request_launch(c);
        return;
    }
    c->g->panels_panel(c->g->opaque, 0, 0xc, c->a);
}

/* page back or on; the action's screen is redrawn. */
static void turn_page(station_context *c, int16_t step)
{
    fe2_station_state *s = c->s;
    int16_t first = (int16_t)(s->first_row + step);
    uint16_t action = s->action;

    if (first < 0) return;
    if (first > (int16_t)(s->row_count - fe2_station_displays[action / 2].page_size)) return;
    s->first_row = (uint16_t)first;
    if (action == FE2_STATION_ACTION_MISSIONS) station_show_missions(c);
    else if (action == FE2_STATION_ACTION_REPAIRS) station_show_repairs(c);
    else if (action == FE2_STATION_ACTION_TRADE) station_show_trade(c);
    else if ((int16_t)action > FE2_STATION_ACTION_TRADE) station_show_fitting(c);
    else if (action == FE2_STATION_ACTION_BULLETIN) {
        s->event_word = s->first_row;
        station_draw_frame(c, FE2_STATION_ACTION_BULLETIN);
    } else {
        station_draw_default(c);
    }
}

/* a row of the page (index from the first shown). */
static void choose(station_context *c, uint16_t index)
{
    index = (uint16_t)(index + c->s->first_row);
    if ((int16_t)index < (int16_t)c->s->row_count) station_select_row(c, index);
}

/* the answer to the ownership question: right quadruples the check
 * value (32-bit wrap, then at most 0x15554dc0), wrong divides it by 16. */
static void answer_question(station_context *c, uint8_t key, uint8_t answer)
{
    fe2_station_state *s = c->s;
    uint32_t value = s->check_value;
    s->shared_countdown = 0xffffffffu;
    s->game_flags &= 0xfe;
    s->expected_letter = 0;
    s->input_lock = 0xff;
    if (key == answer) {
        value <<= 2;
        if (value >= 0x15554dc0u) value = 0x15554dc0u;
    } else {
        value >>= 4;
    }
    s->check_value = value;
    s->check_elapsed = 0;
}

/* 0x7a. */
void fe2_station_input(fe2_station_state *state, const fe2_station_services *game, fe2_call_args *args)
{
    station_context c = {state, game, args, 0};
    uint16_t key = 0;
    int have = 1;

    if (state->input_lock != 0) {
        key = 0xf4;
        state->key_latch = 1;
    } else {
        uint8_t letter;
        have = game->poll_key(game->opaque, &key);
        if (have) {
            state->key_count = state->key_word;
            letter = state->expected_letter;
            if (letter != 0) {
                uint8_t k = (uint8_t)key;
                if (k >= 0x61 && k <= 0x7a) k = (uint8_t)(k - 0x20);
                key = (uint16_t)((key & 0xff00) | k);
                if (k >= 0x41 && k <= 0x5a) {
                    answer_question(&c, k, letter);
                    key = (uint16_t)((key & 0xff00) | 0xf4);
                    state->key_latch = 1;
                }
            }
        }
    }
    if (have) {
        uint8_t k = (uint8_t)key;
        if (k >= 0x31 && k <= 0x34) {
            choose(&c, (uint16_t)(k - 0x31));
        } else if (k == 6) {
            turn_page(&c, 1);
            return;
        } else if (k == 3) {
            uint16_t text = fe2_station_displays[state->action / 2].continue_text;
            if (text != 0) station_run_handler(&c, text);
            return;
        } else if (k == 5) {
            turn_page(&c, -1);
            return;
        } else if (state->action != 0 && k >= 0x80 && k <= 0x9f) {
            station_set_word(&c, P_STYLE, key);
            choose(&c, (uint16_t)(key & 0xf));
        } else {
            game->forward_key(game->opaque, key);
        }
        if (c.stop) return;
    }
    if (state->action == FE2_STATION_ACTION_SHIP) {
        station_preview_frame(&c);
        return;
    }
    if (state->portrait_timer != 0) {
        uint32_t left = (uint32_t)state->portrait_timer - state->frame_step;
        if ((int32_t)left >= 0) {
            state->portrait_timer = (uint16_t)left;
            return;
        }
        state->portrait_timer = 0;
        game->clear_picture(game->opaque);
        station_show_portrait(&c);
        game->present(game->opaque);
    }
}

/* the selection menu: four rows from the first shown (and a more
 * row), parameter 7 counting them. */
static void draw_menu(station_context *c)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    unsigned i = s->first_row;
    uint16_t y = 0x14;

    station_set_word(c, P_STYLE, 3);
    for (;;) {
        const fe2_station_row *row;
        if (i >= FE2_STATION_ROWS) return;      /* past the list: not reached (domain) */
        row = &s->rows[i++];
        uint16_t kept;
        station_set(c, P1, row->value[0]);
        station_set(c, P2, row->value[1]);
        station_set(c, P3, row->value[2]);
        station_set(c, P_NAME, station_name_ref(row->subject));
        if (row->text == 0 && row->text_high == 0) return;
        kept = station_word(c, P_STYLE);
        station_set_word(c, P_STYLE, 0);
        g->draw_text(g->opaque, row->text, 0xf, 0x1e, y, c->a);
        station_set_word(c, P_STYLE, kept);
        y = (uint16_t)(y + 0x1e);
        station_set_word(c, P_STYLE, (uint16_t)(station_word(c, P_STYLE) - 1));
        if ((int16_t)station_word(c, P_STYLE) < 0) break;
    }
    if (i < FE2_STATION_ROWS ? (s->rows[i].text | s->rows[i].text_high) != 0 : s->rows_end != 0) {
        station_set_word(c, P_STYLE, 0);
        g->draw_text(g->opaque, 0x98ba, 0xf, 0x1e, y, c->a);
    }
}

/* 0x7e. */
void fe2_station_select(fe2_station_state *state, const fe2_station_services *game, fe2_call_args *args)
{
    station_context c = {state, game, args, 0};

    if (state->ui_flags & STATION_UI_SELECTION) {
        if (state->view_mode >= 0) {
            station_reset_selection_keys(&c);
        } else {
            int draw = 1;
            uint16_t key;
            if (game->poll_key(game->opaque, &key)) {
                uint8_t k = (uint8_t)key;
                if (k == 0x35) {
                    uint16_t first = (uint16_t)(state->first_row + 4);
                    if ((int16_t)first >= (int16_t)state->row_count) first = 0;
                    state->first_row = first;
                    state->selection_timer = STATION_SELECTION_TIME;
                    return;
                }
                if (k >= 0x31 && k < 0x35) {
                    uint16_t index = (uint16_t)(k - 0x31 + state->first_row);
                    if ((int16_t)index < (int16_t)state->row_count) {
                        station_select_row(&c, index);
                        if (c.stop) return;
                        draw = 0;
                    }
                } else {
                    game->forward_key(game->opaque, key);
                }
            }
            if (draw) {
                draw_menu(&c);
                state->selection_timer -= state->real_step;
                if ((int32_t)state->selection_timer < 0) station_reset_selection_keys(&c);
            }
        }
    }
    station_pump_messages(&c);
}
