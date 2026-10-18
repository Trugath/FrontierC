/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* firing installed equipment (beams and the mining laser). */
#include "objects.h"
#include "objects_internal.h"

enum { CODE_MINING_LASER = 0x9a, MODEL_ASTEROID = 0x7c, BEAM_KINDS = 8 };

/* the shot's cost and damage from the probe distance. Beam kinds
 * (code bits 3..6 below 8) add eight times their strength to the busy time
 * and set the cooldown (0x795c, 0xffff for kind 6); their damage is the
 * strength scaled by distance / cooldown. Other kinds scale by distance / 128
 * (saturating) and add the product shifted by their CS:0x69 count. Fuel is
 * consumed for sixteen times the damage. */
static uint16_t shot(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *o, uint32_t distance)
{
    unsigned kind = (s->equipment_code & 0x78u) >> 2;
    uint16_t strength = 0x7b + kind == 0x91 ? s->data_segment : fe2_objects_preamble_word(0x7b + kind);
    uint32_t value;
    if (kind < BEAM_KINDS) {
        uint16_t cooldown = kind == 6 ? 0xffff : 0x795c;
        v->sound(v->context, 0xa, o->level);
        o->busy_remaining = (uint16_t)(o->busy_remaining + (uint16_t)(strength << 3));
        o->cooldown = cooldown;
        value = (uint32_t)(int32_t)(int16_t)strength;
        if (distance >= cooldown) {
            /* A quotient beyond a word resumes with AX unchanged. */
            uint32_t q = distance / cooldown;
            uint16_t word = q > 0xffffu ? (uint16_t)distance : (uint16_t)q;
            value = (uint32_t)word * strength;
        }
    } else {
        uint32_t product, busy;
        uint16_t count = fe2_objects_preamble_word(0x69 + kind);
        v->sound(v->context, 0xb, o->level);
        product = (uint32_t)strength * (uint16_t)(distance >> 7);
        if (product > 0xffffu) product = 0xffff;
        busy = (product << 3) >> (count > 31 ? 31 : count);
        if (count > 31) busy = 0;
        value = product;
        {
            uint32_t sum = (uint32_t)o->busy_remaining + (uint16_t)busy;
            o->busy_remaining = sum > 0xffffu ? 0xffff : (uint16_t)sum;
        }
    }
    fe2_objects_consume(s, v, o, value << 4);
    return (uint16_t)(value & 0xffffu);
}

void fe2_objects_activate(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *o, int8_t index)
{
    uint8_t code = o->as.flight.installed[index];
    const uint8_t *ext;
    fe2_objects_probe probe;
    int16_t axis[3], weight[2];
    unsigned i;
    if (!code) return;
    s->equipment_code = code;
    if (o->cooldown || o->busy_remaining > 0xf000 || !o->fuel) return;
    if (o->handle == s->player && !(s->warnings & 4u)) {
        fe2_objects_message m = {3, 0, 0, 0, 0};
        s->warnings |= 4u;
        v->message(v->context, FE2_OBJECTS_PANEL_SLOT, &m);
    }
    ext = fe2_objects_extension(v, o);
    o->frame_state |= 2u;
    probe.owner = o->handle;
    for (i = 0; i < 3; ++i) axis[i] = o->orientation[2 + 3 * i];
    if (index == 1) {
        /* Rear mount: backwards, from the extension's rear weights. */
        o->frame_state |= 8u;
        for (i = 0; i < 3; ++i) axis[i] = (int16_t)(uint16_t)(0u - (uint16_t)axis[i]);
        weight[0] = (int16_t)fe2_objects_word(ext, 0x24); weight[1] = (int16_t)fe2_objects_word(ext, 0x26);
    } else if (index == 0 || index == 4) {
        o->frame_state |= 4u;
        weight[0] = (int16_t)fe2_objects_word(ext, 0x20); weight[1] = (int16_t)fe2_objects_word(ext, 0x22);
    } else {
        /* Other mounts: the aimed direction, owned by nobody. */
        o->frame_state |= 2u;
        probe.owner = 0;
        for (i = 0; i < 3; ++i) axis[i] = s->probe_parameters[1 + i];
        if (s->probe_parameters[0] < 0) {
            weight[0] = (int16_t)fe2_objects_word(ext, 0x28); weight[1] = (int16_t)fe2_objects_word(ext, 0x2a);
        } else {
            weight[0] = (int16_t)fe2_objects_word(ext, 0x2c); weight[1] = (int16_t)fe2_objects_word(ext, 0x2e);
        }
    }
    /* The mount point: third basis column by one weight, second by the
     * other, in 1/4096. */
    for (i = 0; i < 3; ++i) {
        int32_t at = (int32_t)o->orientation[2 + 3 * i] * weight[0];
        if (weight[1]) at = (int32_t)((uint32_t)at + (uint32_t)((int32_t)o->orientation[1 + 3 * i] * weight[1]));
        probe.offset[i] = at >> 12;
        probe.direction[i] = axis[i];
    }
    o->stored_model = 0xffff;
    probe.hit = 0; probe.target = 0; probe.reach = 0; probe.distance = 0; probe.range = UINT32_C(0x7fffff);
    for (i = 0; i < 6; ++i) probe.position[i] = 0;
    v->probe(v->context, o, &probe);
    if (!probe.hit) { (void)shot(s, v, o, probe.distance); return; }
    o->stored_model = (uint16_t)(probe.reach >> 7);
    {
        fe2_object *victim = probe.target;
        uint16_t damage;
        uint16_t attacker;
        if (victim && victim->model == MODEL_ASTEROID && s->equipment_code == CODE_MINING_LASER) {
            /* Mining: one to three fragments at the hit point. Each takes
             * its kind from the last record, and a failed
             * allocation leaves that record to be moved instead. */
            int16_t n = (int16_t)fe2_objects_random_below(s, 3);
            fe2_object *last = victim;
            for (; n >= 0; --n) {
                fe2_object *fragment = fe2_objects_fragment(s, v, o, fe2_objects_preamble[0x63 + (last->target_seed & 7u)]);
                if (fragment) last = fragment;
                for (i = 0; i < 3; ++i) {
                    last->position_low[i] = probe.position[i];
                    last->position_high[i] = probe.position[3 + i];
                }
            }
            victim = last;
            attacker = (uint16_t)(0xff00u | o->handle);
        } else attacker = (uint16_t)((probe.range & 0xff00u) | o->handle);
        damage = shot(s, v, o, probe.distance);
        if (victim) (void)fe2_objects_damage(s, v, victim, damage, attacker);
        fe2_objects_spawn_effect(s, v, o, probe.position, 1);
    }
}
