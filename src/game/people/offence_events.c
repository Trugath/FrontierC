/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* events; 0xd1b: offence text. */
#include "offence_internal.h"

enum {
    EVENT_RESET = 1, EVENT_CUSTOMS = 5, EVENT_JETTISON = 0x11,
    LETTER_BASE = 0x41, TEXT_SELECTOR_MASK = 0x3fe, ILLEGAL = 0x80
};

static uint16_t illegal_cargo(const fe2_offence_state *s)
{
    uint16_t total = 0;
    int i;
    for (i = FE2_OFFENCE_COMMODITIES - 1; i >= 0; --i)
        if (s->trade_codes[i] & ILLEGAL) total = (uint16_t)(total + s->cargo[i]);
    return total;
}

/* The manual check: a random question of 300 (the 301st row is never
 * drawn). */
static void ask_ownership(fe2_offence_state *s, const fe2_offence_services *game)
{
    const fe2_ownership_challenge *c = &fe2_offences_challenges[game->random_below(game->opaque, 300)];
    s->challenge[0] = c->word;
    s->challenge[1] = c->line;
    s->challenge[2] = c->page;
    s->expected_letter = (uint8_t)(c->answer + LETTER_BASE);
}

/* Customs: unless a random 0..7 reaches the law level, carrying illegal
 * cargo is smuggling. The manual check follows either way. */
static void customs(fe2_offence_state *s, const fe2_offence_services *game)
{
    if ((int16_t)(game->random(game->opaque) & 7u) < (int16_t)s->law_level && illegal_cargo(s) != 0)
        fe2_offence_commit(s, game, FE2_OFFENCE_SMUGGLING, s->smuggler);
    ask_ownership(s, game);
}

void fe2_offence_event(fe2_offence_state *s, const fe2_offence_services *game, uint8_t event,
                       fe2_object *subject)
{
    switch (event) {
    case EVENT_JETTISON:
        fe2_offence_commit(s, game, FE2_OFFENCE_DUMPING, subject);
        break;
    case EVENT_RESET:
        s->launch_phase = FE2_LAUNCH_READY;
        s->police_flags &= (uint8_t)~ALERTED;
        break;
    case EVENT_CUSTOMS:
        customs(s, game);
        break;
    default:
        break;
    }
}

void fe2_offence_text(const fe2_offence_services *game, uint16_t selector, fe2_text_out *out)
{
    game->format_table_text(game->opaque, selector & TEXT_SELECTOR_MASK,
                            (const fe2_text_table *)fe2_offences_text_directory, out);
}
