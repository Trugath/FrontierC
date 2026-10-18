/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The message line: show, queue and redisplay. */
#include "station_internal.h"

/* the marks beside a message: parameter 3's low bit picks the
 * first, the style's bits 1..2 the second. */
static void draw_marks(station_context *c)
{
    const fe2_station_services *g = c->g;
    uint16_t style;

    station_set_word(c, P_STYLE, (uint16_t)(station_word(c, P_STYLE) & 0xfffe));
    g->draw_sprite(g->opaque, (station_get(c, P3) & 1) ? 0x69 : 0x68, 0x59, 0xa9, c->a);
    style = (uint16_t)(station_word(c, P_STYLE) & 6);
    station_set_word(c, P_STYLE, style);
    if (style == 2) g->draw_sprite(g->opaque, 0x6a, 0x58, 0xa9, c->a);
    else if (style == 6) g->draw_sprite(g->opaque, 0x6b, 0x58, 0xa9, c->a);
}

/* draw the current message (a negative style: plain, in the third
 * colour; else with its marks, and unless the style's bit 0 is set as
 * text 0x9900 naming the text and the subject). */
void station_show_current(station_context *c)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    uint16_t text;

    if (s->flight_state == FE2_STATION_MODE_HYPERSPACE) return;
    s->message_flags &= 0xfe;
    station_set_word(c, P2, 0);
    g->panels_panel(g->opaque, 1, 0xc, c->a);
    station_set(c, P1, s->message.value[0]);
    station_set(c, P2, s->message.value[1]);
    station_set(c, P3, s->message.value[2]);
    station_set(c, P_SUBJECT, s->message.subject);
    s->message_colours[0] = s->colours[1];
    s->message_colours[1] = s->colours[2];
    s->message_colours[2] = s->colours[3];
    station_set_word(c, P_STYLE, (uint16_t)s->message.style);
    if ((int16_t)s->message.style < 0) {
        station_set(c, P1, s->message.value[0]);
        station_set(c, P2, s->message.value[1]);
        station_set(c, P3, s->message.value[2]);
        station_set(c, P_NAME, s->message.subject);
        station_set(c, P_TEXT, s->message.kind);
        station_set_word(c, P_STYLE, 0x92);
        g->draw_text_both(g->opaque, s->message.text, s->colours[3], 0x59, 0xaa, c->a);
        return;
    }
    station_set(c, P_SUBJECT, station_get(c, P3));
    draw_marks(c);
    station_set(c, P1, s->message.value[0]);
    station_set(c, P2, s->message.value[1]);
    station_set(c, P3, s->message.value[2]);
    station_set(c, P_NAME, s->message.subject);
    station_set(c, P_TEXT, s->message.kind);
    station_set_word(c, P_STYLE, (uint16_t)s->message.style);
    text = s->message.text;
    if ((station_word(c, P_STYLE) & 1) == 0) {
        uint32_t subject = station_get(c, P_NAME);
        station_set_word(c, P1, text);
        station_set(c, P_NAME, station_get(c, P_TEXT));
        station_set(c, P_TEXT, station_name_ref(subject));
        text = 0x9900;
    }
    station_set_word(c, P_STYLE, 0x82);
    g->draw_text_both(g->opaque, text, s->colours[2], 0x74, 0xaa, c->a);
}

/* make `text` (parameters 1..3, style, subject and 10) the current
 * message and draw it. */
void station_set_message(station_context *c, uint16_t text)
{
    fe2_station_state *s = c->s;
    s->message_timer = STATION_MESSAGE_TIME;
    s->message.text = text;
    s->message.value[0] = station_get(c, P1);
    s->message.value[1] = station_get(c, P2);
    s->message.value[2] = station_get(c, P3);
    s->message.style = station_get(c, P_STYLE);
    s->message.subject = station_get(c, P_SUBJECT);
    s->message.kind = station_get(c, P_NAME);
    station_show_current(c);
}

/* tell commander, sound (style not negative), then show the message, or
 * queue it while another is up (four places). */
void station_format_message(station_context *c, uint16_t text)
{
    if (c->s->flight_state != FE2_STATION_MODE_LAUNCH) c->g->commander_restore_time(c->g->opaque);
    station_post_message(c, text);
}

/* the same without telling commander. */
void station_post_message(station_context *c, uint16_t text)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    fe2_station_message *slot;
    unsigned had;

    if (s->flight_state == FE2_STATION_MODE_HYPERSPACE) return;
    if ((int16_t)station_word(c, P_STYLE) >= 0) g->play_effect(g->opaque, 0x16);
    had = s->ui_flags & STATION_UI_MESSAGE;
    s->ui_flags |= STATION_UI_MESSAGE;
    if (!had) {
        station_set_message(c, text);
        return;
    }
    if (s->queue_cursor >= STATION_QUEUE_BYTES) return;
    slot = &s->queue[s->queue_cursor / STATION_MESSAGE_BYTES];
    slot->text = text;
    slot->value[0] = station_get(c, P1);
    slot->value[1] = station_get(c, P2);
    slot->value[2] = station_get(c, P3);
    slot->style = station_get(c, P_STYLE);
    slot->subject = station_get(c, P_SUBJECT);
    slot->kind = station_get(c, P_NAME);
    s->queue_cursor = (uint16_t)(s->queue_cursor + STATION_MESSAGE_BYTES);
}

/* parameters 8..12 are kept around the message. */
void station_show_message(station_context *c, uint16_t text)
{
    fe2_call_arg kept[5];
    unsigned n;
    for (n = 0; n < 5; ++n) kept[n] = c->a->arg[P_SUBJECT + n];
    station_format_message(c, text);
    for (n = 0; n < 5; ++n) c->a->arg[P_SUBJECT + n] = kept[n];
}

void fe2_station_show_message(fe2_station_state *state, const fe2_station_services *game,
                              fe2_call_args *args, uint16_t text)
{
    station_context c = {state, game, args, 0};
    station_show_message(&c, text);
}

/* a heading message, shown at once without sound: parameter 11
 * becomes the name (10) and the old name the subject. */
void station_show_heading(station_context *c, uint16_t text)
{
    uint32_t name = station_get(c, P_NAME);
    station_set(c, P_NAME, station_get(c, P_TEXT));
    station_set(c, P_SUBJECT, name);
    station_set_word(c, P_STYLE, 0xffff);
    station_set_message(c, text);
}

/* show the first queued message and move the others up; the last
 * place takes the bytes after the queue. */
static void next_queued(station_context *c)
{
    fe2_station_state *s = c->s;
    fe2_station_message *q = s->queue;
    fe2_station_message tail;
    unsigned i;

    station_set(c, P1, q[0].value[0]);
    station_set(c, P2, q[0].value[1]);
    station_set(c, P3, q[0].value[2]);
    station_set(c, P_STYLE, q[0].style);
    station_set(c, P_SUBJECT, q[0].subject);
    station_set(c, P_NAME, q[0].kind);
    station_set_message(c, q[0].text);
    tail.text = s->event_word;
    tail.pad = s->queue_tail_padding;
    tail.value[0] = s->queue_tail_value;
    tail.value[1] = (uint32_t)s->preview.angle[1] << 16 | s->preview.angle[0];
    tail.value[2] = s->preview.position[0];
    tail.style = s->preview.position[1];
    tail.subject = s->preview.position[2];
    tail.kind = (uint32_t)(uint16_t)s->preview.seed << 16 | s->contract_count;
    for (i = 0; i + 1 < FE2_STATION_QUEUE; ++i) q[i] = q[i + 1];
    q[FE2_STATION_QUEUE - 1] = tail;
    s->queue_cursor = (uint16_t)(s->queue_cursor - STATION_MESSAGE_BYTES);
}

/* the message line's time (twice as fast with messages waiting);
 * when it runs out, the next queued message or the end of the line. */
void station_pump_messages(station_context *c)
{
    fe2_station_state *s = c->s;
    uint32_t step;

    if (!(s->ui_flags & STATION_UI_MESSAGE)) return;
    if (s->check_value <= 0x55553) {
        s->selected_text = 0x99d3 - 8;
        ++s->deferred;
    }
    step = s->real_step;
    if (s->queue_cursor != 0) step <<= 1;
    s->message_timer -= step;
    if ((int32_t)s->message_timer >= 0) return;
    if (s->queue_cursor != 0) {
        next_queued(c);
        return;
    }
    s->message.text = 0;
    s->ui_flags &= (uint8_t)~STATION_UI_MESSAGE;
    station_resume_action(c);
}

/* 0x8e. */
uint16_t fe2_station_resume(fe2_station_state *state, const fe2_station_services *game,
                            fe2_call_args *args, int16_t request)
{
    station_context c = {state, game, args, 0};
    if (request < 0) {
        state->message_flags |= 1;
        return (uint16_t)request;
    }
    if (request > 0) {
        if (!(state->ui_flags & STATION_UI_MESSAGE)) return 0;
        station_show_current(&c);
        return 1;
    }
    state->screen_active = 0;
    game->show_panel(game->opaque);
    station_resume_action(&c);
    if (c.stop) return 0;
    if (state->refit != 0) game->post_event(game->opaque, 0x12);
    game->restore_view(game->opaque);
    return 0;
}
