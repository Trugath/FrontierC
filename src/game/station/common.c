/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Parameters, references and records shared by the translation units. */
#include "station_internal.h"

uint32_t station_get(const station_context *c, unsigned n)
{
    const fe2_call_arg *arg = &c->a->arg[n];
    switch (arg->kind) {
    case FE2_ARG_BYTE: return arg->value & 0xffu;
    case FE2_ARG_WORD: return arg->value & 0xffffu;
    case FE2_ARG_DWORD: return arg->value;
    default: return 0;
    }
}

void station_set(station_context *c, unsigned n, uint32_t value) { fe2_args_dword(c->a, n, value); }

void station_set_word(station_context *c, unsigned n, uint16_t value)
{
    fe2_args_dword(c->a, n, (station_get(c, n) & 0xffff0000u) | value);
}

void station_set_high(station_context *c, unsigned n, uint16_t value)
{
    fe2_args_dword(c->a, n, (station_get(c, n) & 0xffffu) | (uint32_t)value << 16);
}

void station_set_byte(station_context *c, unsigned n, uint8_t value)
{
    fe2_args_dword(c->a, n, (station_get(c, n) & 0xffffff00u) | value);
}

void station_set_table(station_context *c, unsigned n, const void *table) { fe2_args_template(c->a, n, table); }

fe2_object *station_lookup_object(const station_context *c, fe2_ref ref) { return c->g->object(c->g->opaque, ref); }

/* The original adds 0xf6 to the offset word only. */
fe2_ref station_name_ref(fe2_ref object)
{
    return (object & 0xffff0000u) | (uint16_t)(object + STATION_NAME_OFFSET);
}

uint32_t station_ror(uint32_t value, unsigned bits)
{
    bits &= 31;
    return bits ? value >> bits | value << (32 - bits) : value;
}

uint32_t station_rol(uint32_t value, unsigned bits) { return station_ror(value, 32 - (bits & 31)); }

const fe2_station_model *station_lookup_model(const station_context *c, uint16_t model) { return c->g->model(c->g->opaque, model); }

/* A row named by the offset word of its reference (0x7e0 holds only that
 * word: the original indexes DS with it). */
fe2_station_row *station_row_at(const station_context *c, uint16_t ref_offset)
{
    uint16_t from = (uint16_t)(ref_offset - (uint16_t)c->s->refs.rows);
    if (from % STATION_ROW_BYTES != 0 || from / STATION_ROW_BYTES >= FE2_STATION_ROWS) return 0;
    return &c->s->rows[from / STATION_ROW_BYTES];
}

uint16_t station_row_offset(const station_context *c, unsigned index)
{
    return (uint16_t)((uint16_t)c->s->refs.rows + STATION_ROW_BYTES * index);
}

uint16_t station_market_word(const uint8_t *market, unsigned at)
{
    return (uint16_t)(market[at] | market[at + 1] << 8);
}

void station_set_market_word(uint8_t *market, unsigned at, uint16_t value)
{
    market[at] = (uint8_t)value;
    market[at + 1] = (uint8_t)(value >> 8);
}
