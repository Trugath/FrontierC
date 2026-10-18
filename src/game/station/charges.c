/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Fines and deferred charges : the court screen when fines
 * are left unpaid, the police interview and its outcomes. */
#include "station_internal.h"

/* unpaid fines when the ship would leave: the notice, a key, a
 * random sentence added to the day, and the session restarts. */
void station_process_deferred(station_context *c)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    fe2_object *station;
    uint16_t key;

    g->commander_restore_time(g->opaque);
    g->clear_drawing(g->opaque);
    s->game_flags |= 0x20;
    g->stop_time(g->opaque);
    g->set_controls(g->opaque, 0, 0x1317, 0x27);
    station = station_lookup_object(c, s->station);
    s->identity = station_ror(station->target_seed, 0x16);
    s->portrait = 0x3a;
    station_set(c, P_NAME, station_name_ref(s->station));
    station_set_word(c, P_STYLE, 0xa0);
    g->draw_text_shadowed(g->opaque, s->selected_text, fe2_station_colours[2], 0x8c, 0x3c, c->a);
    station_draw_status(c);
    /*, then 0x25c and 0x48 until a key. notice_wait makes the
     * host show the page and return; without it the first present never
     * comes back and the launch request is stuck. */
    s->notice_wait = 1;
    g->present(g->opaque);
    do g->present(g->opaque);
    while (!g->poll_key(g->opaque, &key));
    s->notice_wait = 0;
    s->day += (uint32_t)(int32_t)(int16_t)(uint16_t)(g->random_below(g->opaque, 0x2ac6) + 0x16d);
    g->restart_session(g->opaque, 1);
    c->stop = 1;
}

/* the police interview: fine `fine` is added (unless the total
 * would turn negative), heading `heading`, rows `first`.. + `count`. */
static void interview(station_context *c, uint16_t first, uint16_t count, uint16_t heading, uint16_t fine)
{
    fe2_station_state *s = c->s;
    const fe2_station_services *g = c->g;
    uint32_t total;

    station_set_word(c, P_STYLE, count);
    station_set_word(c, P1, heading);
    station_set_word(c, P3, fine);
    g->clear_regions(g->opaque);
    g->set_widgets(g->opaque, fe2_station_widgets_board);
    s->identity = station_ror(station_lookup_object(c, s->station)->target_seed, 0x14);
    s->portrait_timer = 0x4000;
    s->portrait = 0x3a;
    station_set(c, P_NAME, station_name_ref(s->station));
    total = (uint32_t)(int32_t)(int16_t)fine + s->deferred;
    station_set(c, P3, total);
    if ((int32_t)total >= 0) s->deferred = total;
    station_show_heading(c, heading);
    station_reset_rows(c);
    station_set_word(c, P_STYLE, count);
    do {
        station_append(c, first++);
        station_set_word(c, P_STYLE, (uint16_t)(station_word(c, P_STYLE) - 1));
    } while ((int16_t)station_word(c, P_STYLE) >= 0);
    station_show_responses(c);
}

/* dealing reported: offences records the offence, a fine of 5000. */
void station_report_dealing(station_context *c)
{
    c->s->heading = 0x98c1;
    station_set(c, P_RECORD, c->s->station);
    c->g->offences_police(c->g->opaque, 5, c->a);
    interview(c, 0x984a, 6, 0x9932, 0x1388);
}

/* a bribe or plea judged by a bounded sample of the seed in
 * parameter 10: refused adds 20000 to the fines (offences hears of it); else
 * paid from the balance. */
static void plead(station_context *c, uint16_t bound, uint32_t amount)
{
    fe2_station_state *s = c->s;
    fe2_station_sample sample;
    uint32_t seed = station_get(c, P_NAME), total;

    station_set(c, P3, amount);
    sample = c->g->bounded_sample(c->g->opaque, bound, seed, (seed & 0xffff) << 16 | seed >> 16);
    station_set(c, P1, sample.seed_a);
    station_set(c, P2, sample.seed_b);
    if (sample.result != 0) {
        total = s->deferred + 0x4e20;
        station_set(c, P3, total);
        if ((int32_t)total < 0) return;
        s->deferred = total;
        station_set_word(c, P_TEXT, 0x9937);
        station_reply(c);
        station_set(c, P_RECORD, s->station);
        c->g->offences_police(c->g->opaque, 6, c->a);
        return;
    }
    amount = station_get(c, P3);
    if (s->balance < amount) {
        station_show_heading(c, 0x9939);
        return;
    }
    s->balance -= amount;
    s->deferred = 0;
    station_set_word(c, P_TEXT, 0x9938);
    station_reply(c);
    station_end_reply(c);
}

/* pay the fines (or say they cannot be paid). */
static void settle(station_context *c, int pay)
{
    fe2_station_state *s = c->s;
    if (pay && s->deferred <= s->balance) {
        s->balance -= s->deferred;
        s->deferred = 0;
        station_set_word(c, P_TEXT, 0x9933);
    } else {
        station_set_word(c, P_TEXT, 0x9934);
        station_set(c, P3, s->deferred);
    }
    station_reply(c);
    station_end_reply(c);
}

void station_charge_handler(station_context *c, unsigned index)
{
    fe2_station_state *s = c->s;
    uint32_t total;
    switch (index) {
    case 0x10:                                         /* the police office */
        if (s->deferred != 0) interview(c, 0x984a, 7, 0x9932, 0);
        else interview(c, 0x9851, 0, 0x993c, 0);
        break;
    case 0x4a: settle(c, 1); break;
    case 0x4b: settle(c, 0); break;
    case 0x4c:                                         /* deny: 2500 more */
        total = s->deferred + 0x9c4;
        if ((int32_t)total < 0) return;
        s->deferred = total;
        station_set_word(c, P_TEXT, 0x9935);
        station_reply(c);
        break;
    case 0x4d:
        station_set_word(c, P_TEXT, 0x9936);
        station_reply(c);
        break;
    case 0x4e: plead(c, 0x14, 0x64); break;
    case 0x4f: plead(c, 6, 0x1f4); break;
    default: plead(c, 2, 0x9c4); break;
    }
}
