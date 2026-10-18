/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "library_calls.h"
#include <string.h>

enum { CATALOGUE_MODULE = 6, NAMES_MODULE = 9, PORTRAITS_MODULE = 13, INTERACTION_MODULE = 18 };
enum { KEY_PARAMETER = 1, OVERRIDE_PARAMETER = 1 };

/* ---- catalogue: the catalogue query (0x8a) ------------------------------------ */
static uint16_t catalogue(void *context, uint16_t request, fe2_call_args *args, fe2_overlay_trade *trade)
{
    fe2_session_catalogue *b = context;
    const fe2_catalogue_record *record = fe2_system_find(b->catalogue, args->arg[KEY_PARAMETER].value);

    if ((uint8_t)request == 0) {
        fe2_system_profile p = fe2_system_profile_of(record);
        fe2_args_word(args, 0, p.resource_richness); fe2_args_word(args, 1, p.map_text);
        fe2_args_word(args, 2, p.view_text); fe2_args_word(args, 3, p.category_0_rate);
        fe2_args_word(args, 4, p.market_profile); fe2_args_word(args, 5, p.category_1_rate);
        fe2_args_word(args, 6, p.market_counter_seed); fe2_args_word(args, 7, p.government);
        return p.resource_richness;
    }
    if ((uint8_t)request == 1) {
        fe2_catalogue_trade t = fe2_system_trade_of(record);
        fe2_args_word(args, 2, t.government);
        if (trade) { trade->metadata = t.economic_factor_index; trade->government = t.government; trade->codes = t.trade_codes; }
        return t.population_band;
    } else {
        uint16_t first = 0, second = 0;
        fe2_system_summary s;
        if (b->host.resident_pair) b->host.resident_pair(b->host.opaque, &first, &second);
        s = fe2_system_summary_of(record, first, second);
        fe2_args_word(args, 0, s.low_strength); fe2_args_word(args, 1, s.high_strength);
        fe2_args_word(args, 2, s.larger); fe2_args_word(args, 3, s.allegiance);
        fe2_args_word(args, 6, s.second); fe2_args_word(args, 7, s.first);
        return 0;
    }
}

int fe2_session_bind_catalogue(fe2_session *s, fe2_session_catalogue *b)
{
    if (!s || !b || s->busy) return -1;
    b->session = s;
    if (!b->catalogue) b->catalogue = &fe2_default_catalogue;
    s->calls.catalogue = catalogue;
    s->calls.context[CATALOGUE_MODULE] = b;
    return 0;
}

/* ---- names: names  ------------------------------------------------ */
static void name(void *context, uint16_t type, uint32_t seed, char *out, size_t capacity)
{
    fe2_session_names *b = context;
    fe2_name_result result;
    if (!out || !capacity) { b->session->error = 1; return; }
    if (fe2_name_generate(type, (uint16_t)seed, (uint16_t)(seed >> 16), (uint8_t *)out, capacity,
                          &result) != FE2_NAME_OK) {
        out[0] = 0; b->session->error = 1;
    }
}

int fe2_session_bind_names(fe2_session *s, fe2_session_names *b)
{
    if (!s || !b || s->busy) return -1;
    b->session = s;
    s->calls.name = name;
    s->calls.context[NAMES_MODULE] = b;
    return 0;
}

/* ---- composites: the portrait (0x8a) ------------------------------------------- */
static void portrait(void *context, uint32_t identity, const fe2_call_args *args)
{
    fe2_session_portraits *b = context;
    fe2_composite_result result;
    if (!b->host.rectangle || !b->host.palette || !b->host.sprite) return;
    b->composite.video = b->host;
    if (fe2_composite_draw(&b->composite, (uint16_t)identity, (uint16_t)(identity >> 16),
                           (uint16_t)args->arg[OVERRIDE_PARAMETER].value, &result))
        b->session->error = 1;
}

int fe2_session_bind_portraits(fe2_session *s, fe2_session_portraits *b)
{
    if (!s || !b || s->busy) return -1;
    b->session = s;
    memset(&b->composite, 0, sizeof(b->composite));
    s->calls.portrait = portrait;
    s->calls.context[PORTRAITS_MODULE] = b;
    return 0;
}

/* ---- interaction: interaction text (0x92) --------------------------------------- */
/* No resident globals: going out and coming back carry nothing. */
static void interaction_out(fe2_session_interaction_overlay *b) { (void)b; }
static void interaction_back(fe2_session_interaction_overlay *b) { (void)b; }
#include "interaction_forward.h"

static void interaction_text(void *context, uint16_t selector, fe2_text_out *out)
{
    fe2_session_interaction_overlay *b = context;
    ++b->depth; fe2_interaction_display(&b->services, selector, out); --b->depth;
}

int fe2_session_bind_interaction(fe2_session *s, fe2_session_interaction_overlay *b)
{
    if (!s || !b || s->busy) return -1;
    b->session = s; b->depth = 0;
    b->services.opaque = b;
    interaction_forward_all(&b->services);
    s->calls.interaction_text = interaction_text;
    s->calls.context[INTERACTION_MODULE] = b;
    return 0;
}
