/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "backend.h"
#include <string.h>

/* MCGA dynamic palette cache. Slot records keep the original
 * state/DAC/RGB12 layout; the free list is the original's byte storage. */

static const uint16_t seed_rgb[16] = {
    0xfff, 0xeee, 0xddd, 0xccc, 0xbbb, 0xaaa, 0x999, 0x888,
    0x777, 0x666, 0x555, 0x444, 0x333, 0x222, 0x111, 0x000
};

void fe2_palette_age(fe2_palette_cache *c)
{
    unsigned slot, n = 0;
    for (slot = 0; slot < FE2_PALETTE_SLOTS; ++slot) {
        fe2_palette_slot *s = &c->slots[slot];
        if (s->state == FE2_PALETTE_ALIAS) {
            c->slot_of[s->rgb12 & 0xfffu] = 0xff;
            s->state = FE2_PALETTE_FREE;
        } else if (s->state != FE2_PALETTE_FREE) {
            if (s->state > FE2_PALETTE_REPLACEABLE) --s->state;
            continue;
        }
        c->free_list[n++] = (uint8_t)slot;
    }
    c->free_list[n] = 0xff;
    c->cursor = 0;
}

void fe2_palette_initialize(fe2_palette_cache *c)
{
    unsigned i;
    memset(c->slot_of, 0xff, sizeof(c->slot_of));
    for (i = 0; i < FE2_PALETTE_SLOTS; ++i) {
        c->slots[i].state = FE2_PALETTE_FREE;
        c->slots[i].dac = 0;
        c->slots[i].rgb12 = 0;
    }
    for (i = 0; i < 16; ++i) {
        /* Seeds write state/DAC only; the RGB12 word stays zero. */
        c->slot_of[seed_rgb[i]] = (uint8_t)i;
        c->slots[i].state = FE2_PALETTE_RESERVED;
        c->slots[i].dac = (uint8_t)(16 + i);
    }
    fe2_palette_age(c);
}

int fe2_palette_lookup(fe2_palette_cache *c, uint16_t rgb12)
{
    unsigned key = rgb12 & 0xfffu, slot = c->slot_of[key];
    if (slot != 0xff) {
        if (c->slots[slot].state >= FE2_PALETTE_REPLACEABLE) c->slots[slot].state = FE2_PALETTE_RECENT;
        return (int)slot;
    }
    if (c->cursor >= FE2_PALETTE_FREE_BYTES) return -1;
    slot = c->free_list[c->cursor];
    if (slot == 0xff) return c->slot_of[0];
    ++c->cursor;
    c->slot_of[key] = (uint8_t)slot;
    c->slots[slot].state = FE2_PALETTE_PENDING;
    c->slots[slot].rgb12 = (uint16_t)key;
    return (int)slot;
}

int fe2_palette_index(const fe2_palette_cache *c, unsigned slot)
{
    return slot < FE2_PALETTE_SLOTS ? c->slots[slot].dac : -1;
}

/* walk red, then green, then blue down one step at a time (all
 * cumulative) until a mapped, non-pending slot is found. */
static int approximate(const fe2_palette_cache *c, unsigned rgb)
{
    unsigned guard, slot;
    for (guard = 0; guard < 64; ++guard) {
        if (rgb & 0xf00u) {
            rgb -= 0x100u;
            slot = c->slot_of[rgb];
            if (slot != 0xff && c->slots[slot].state != FE2_PALETTE_PENDING) return (int)slot;
        }
        if (rgb & 0xf0u) {
            rgb -= 0x10u;
            slot = c->slot_of[rgb];
            if (slot != 0xff && c->slots[slot].state != FE2_PALETTE_PENDING) return (int)slot;
        }
        if (rgb & 0xfu) {
            rgb -= 1u;
            slot = c->slot_of[rgb];
            if (slot != 0xff && c->slots[slot].state != FE2_PALETTE_PENDING) return (int)slot;
        }
    }
    return -1; /* reached black without a usable slot: the original spins */
}

int fe2_palette_assign(fe2_palette_cache *c, fe2_palette_upload_fn upload, void *context)
{
    uint8_t usage[128][2], queue[FE2_PALETTE_SLOTS];
    unsigned slot, dac, n = 0, uploads = 0;
    memset(usage, 0, sizeof(usage));
    for (slot = 0; slot < FE2_PALETTE_SLOTS; ++slot) {
        const fe2_palette_slot *s = &c->slots[slot];
        /* DAC indices below 0x80 index below the original's workspace. */
        if (s->state < FE2_PALETTE_REPLACEABLE || s->dac < 0x80u) continue;
        usage[s->dac - 0x80u][0] = s->state == FE2_PALETTE_REPLACEABLE ? 1 : 2;
        usage[s->dac - 0x80u][1] = (uint8_t)slot;
    }
    for (dac = 0; dac < 128; ++dac)
        if (!usage[dac][0]) { c->free_list[n++] = (uint8_t)(dac + 0x80u); c->free_list[n++] = 0xff; }
    for (dac = 0; dac < 128; ++dac)
        if (usage[dac][0] == 1) { c->free_list[n++] = (uint8_t)(dac + 0x80u); c->free_list[n++] = usage[dac][1]; }
    c->free_list[n] = 0;
    c->cursor = 0;
    for (slot = 0; slot < FE2_PALETTE_SLOTS; ++slot) {
        fe2_palette_slot *s = &c->slots[slot];
        uint8_t index;
        if (s->state != FE2_PALETTE_PENDING) continue;
        index = c->free_list[c->cursor];
        if (index) {
            unsigned owner = c->free_list[c->cursor + 1u];
            c->cursor = (uint16_t)(c->cursor + 2u);
            if (owner != 0xff) {
                c->slots[owner].state = FE2_PALETTE_FREE;
                c->slot_of[c->slots[owner].rgb12 & 0xfffu] = 0xff;
            }
            s->state = FE2_PALETTE_RECENT;
            s->dac = index;
            queue[uploads++] = (uint8_t)slot;
        } else {
            int found = approximate(c, s->rgb12 & 0xfffu);
            fe2_palette_slot *f;
            if (found < 0) return -1;
            f = &c->slots[found];
            if (f->state >= FE2_PALETTE_REPLACEABLE) f->state = FE2_PALETTE_RECENT;
            s->dac = f->dac;
            s->state = FE2_PALETTE_ALIAS;
        }
    }
    /* One retrace-synchronized upload in slot order, after all assignments. */
    for (n = 0; n < uploads && upload; ++n) {
        const fe2_palette_slot *s = &c->slots[queue[n]];
        upload(context, s->dac, (uint8_t)(((s->rgb12 >> 8) & 0xfu) * 4u + 3u),
               (uint8_t)(((s->rgb12 >> 4) & 0xfu) * 4u + 3u), (uint8_t)((s->rgb12 & 0xfu) * 4u + 3u));
    }
    return (int)uploads;
}
