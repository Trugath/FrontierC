/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* capacity recovery and fuel; 0x213e..0x2210 timed effects;
 * 0x2211..0x2421 deployment; 0x248f..0x253f the name editor; 0x2540..0x25cd
 * servicing failures. */
#include "objects.h"
#include "objects_internal.h"
#include <string.h>

enum {
    MSG_FUEL_EMPTY = 0x98f0, MSG_INCOMING_MISSILE = 0x98ad, MSG_DRIVE_FAILURE = 0x98b5,
    MSG_EQUIPMENT_FAILURE = 0x98b6, REFILL = 0x20000000, SERVICING_PERIOD = 0x1a5e0
};

static int16_t high1(int32_t product) { return (int16_t)(uint16_t)((uint32_t)product >> 15); }
static uint16_t magnitude(int16_t v) { return v < 0 ? (uint16_t)(0u - (uint16_t)v) : (uint16_t)v; }

static void message(const fe2_objects_services *v, uint16_t selector, uint16_t value, unsigned uses)
{
    fe2_objects_message m;
    m.selector = selector; m.parameter = 0xffff; m.value = value; m.amount = 0;
    m.uses = FE2_OBJECTS_USES_PARAMETER | uses;
    v->message(v->context, FE2_OBJECTS_MESSAGE_SLOT, &m);
}

/* zero the control limits (the craft cannot manoeuvre). */
static void clear_controls(fe2_object *o) { memset(o->as.flight.control_limits, 0, sizeof(o->as.flight.control_limits)); }

void fe2_objects_consume(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *o, uint32_t amount)
{
    uint32_t before = o->fuel;
    o->fuel = before - amount;
    if (before > amount) return;
    o->fuel = 0;
    if (s->player == o->handle) {
        if (!(o->as.flight.equipment & 2u)) goto exhausted;
        if (o->as.flight.drive >= 0xa) {
            if (!s->cargo[FE2_CARGO_MILITARY_FUEL]) goto exhausted;
            --s->cargo[FE2_CARGO_MILITARY_FUEL];
            ++s->cargo[FE2_CARGO_SPENT_FUEL];
        } else {
            if (!s->cargo[FE2_CARGO_STANDARD_FUEL]) goto exhausted;
            --s->cargo[FE2_CARGO_STANDARD_FUEL];
            --s->cargo_space;
        }
    } else {
        if (!o->drive_fuel) { clear_controls(o); return; }
        --o->drive_fuel;
    }
    o->fuel += REFILL;
    return;
exhausted:
    /* Warn once; the controls are lost either way. */
    if (!(s->warnings & 1u)) {
        s->warnings |= 1u;
        message(v, MSG_FUEL_EMPTY, 0, 0);
    }
    clear_controls(o);
}

uint32_t fe2_objects_update(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *o, uint16_t quantum)
{
    uint32_t flags = o->as.flight.equipment, rotated = flags >> 16 | flags << 16;
    uint16_t sum;
    /* Shields recover toward their limit; half as fast without bit 28. */
    if (o->as.flight.shield_capacity != o->as.flight.shield_charge) {
        uint16_t gain = (uint16_t)((uint16_t)((uint32_t)quantum * o->as.flight.shield_capacity) >> 5);
        uint16_t value;
        if (!(rotated & (1u << 12))) gain >>= 1;
        value = (uint16_t)(o->as.flight.shield_charge + gain);
        if (value > o->as.flight.shield_capacity) value = o->as.flight.shield_capacity;
        o->as.flight.shield_charge = value;
    }
    /* With bit 31 the hull recovers toward four times extension word 6. */
    if (rotated & (1u << 15)) {
        uint16_t limit = (uint16_t)(fe2_objects_word(fe2_objects_extension(v, o), 6) << 2);
        if (limit != o->hull) {
            uint16_t value = (uint16_t)(o->hull + quantum);
            if (value > limit) value = limit;
            o->hull = value;
        }
    }
    if (o->trail) o->trail = (uint8_t)(o->trail < (uint8_t)quantum ? 0 : o->trail - (uint8_t)quantum);
    if (o->effect) {
        if (o->effect <= (uint8_t)quantum) { o->effect = 0; o->frame_state &= 0x9fu; }
        else o->effect = (uint8_t)(o->effect - (uint8_t)quantum);
    }
    if (o->busy_remaining) {
        uint16_t step = (uint16_t)(quantum << 10);
        if (rotated & (1u << 16)) step = (uint16_t)(step << 2);
        o->busy_remaining = o->busy_remaining < step ? 0 : (uint16_t)(o->busy_remaining - step);
    }
    /* fuel for the controls' magnitude. The product's bytes
     * are moved as the original's registers: bits 8..23 below, the top
     * byte twice above, then halved. */
    sum = (uint16_t)(magnitude(o->controls[0]) + magnitude(o->controls[1]) + magnitude(o->controls[2]));
    {
        uint32_t product = (uint32_t)sum * fe2_objects_word(fe2_objects_extension(v, o), 6);
        uint8_t top = (uint8_t)(product >> 24);
        uint32_t shifted = ((uint32_t)top << 24 | (uint32_t)top << 16 | (product >> 8 & 0xffffu)) >> 1;
        uint32_t amount = (uint32_t)(uint16_t)shifted * quantum;
        fe2_objects_consume(s, v, o, amount);
        return amount;
    }
}

void fe2_objects_spawn_effect(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *source,
                              const uint32_t position[6], uint16_t flags)
{
    fe2_objects_request request;
    fe2_object *effect;
    uint32_t mix;
    unsigned i;
    if (!s->time_step) return;
    /* Without flag 1 only half the effects appear. */
    if (!(flags & 1u) && (fe2_objects_random(s, NULL) & UINT32_C(0x80000000))) return;
    request.flags = 0x29; request.model = 0xc2; request.source = source;
    effect = v->allocate(v->context, &request, 0);
    if (!effect) return;
    for (i = 0; i < 3; ++i) { effect->position_low[i] = position[i]; effect->position_high[i] = position[3 + i]; }
    effect->role = 0xb;
    effect->behaviour = 0x5c;
    effect->size_mantissa = 0xffff;
    {
        uint32_t a = fe2_objects_random(s, &mix);
        s->effect_seed[0] = (uint16_t)a; s->effect_seed[1] = (uint16_t)mix;
        effect->size_mantissa = (uint16_t)(a | 0x8000u);
    }
    v->basis(v->context, effect, s->effect_seed);
    if (flags & 2u) effect->model_parameter = (uint16_t)(effect->model_parameter + 2u);
    if (flags & 4u) effect->model_parameter = (uint16_t)(effect->model_parameter - 1u);
}

/* Deployed objects' names (data at 0x1ea9:0x36 and :0x3e). */
static const char missile_name[] = "Missile", mine_name[] = "Mine";

fe2_object *fe2_objects_deploy(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *source,
                               uint16_t slot, uint8_t target)
{
    uint8_t *equipment = &source->as.flight.installed[3 + slot];
    uint8_t code = *equipment;
    fe2_objects_request request;
    fe2_object *o;
    const char *name;
    int16_t column[3], offset[3];
    int32_t delta[3];
    uint16_t speed;
    unsigned i;
    if (!code) return 0;
    request.flags = 0x4b; request.model = (uint16_t)((code & 7u) * 2u + 6u); request.source = source;
    o = v->allocate(v->context, &request, 0);
    if (!o) return 0;
    *equipment = 0;
    if (s->player == source->handle) {
        if (!(s->warnings & 4u)) {
            s->warnings |= 4u;
            fe2_objects_message m = {3, 0, 0, 0, 0};
            v->message(v->context, FE2_OBJECTS_PANEL_SLOT, &m);
        }
        ++s->free_capacity;
    } else ++source->capacity;
    o->role = 0xb;
    o->behaviour = 0x44;
    o->map_marker = 0xfd;
    if (code & 0x40u) {
        name = mine_name;
        o->behaviour = o->model == 6 ? 0x50 : 0x48;
    } else {
        o->controls[2] = 0x442;
        name = missile_name;
    }
    memcpy(o->name, name, strlen(name) + 1);
    o->behaviour_parent = target;
    o->source_offset[2] = 8;
    if (o->behaviour == 0x44) {
        if (target == s->player) {
            message(v, MSG_INCOMING_MISSILE, 0, 0);
            v->sound_plain(v->context, 0x1e);
        } else {
            /* A craft targeted by a missile turns to evade it. */
            fe2_object *victim = v->lookup(v->context, target);
            if (victim && victim->role == 0xb && victim->behaviour < 0x28) {
                victim->behaviour = 0x28;
                victim->behaviour_parent = o->handle;
            }
        }
    }
    o->behaviour_target = source->handle;
    o->cargo_type = 0;
    v->sound(v->context, 0x20, source->level);
    /* Launch velocity: back along the second basis column by the model's
     * first scale, offset sideways by a quarter (half when faster than
     * 0x4650) of the first column, to the slot's side. */
    speed = source->model_scale_b[0];
    for (i = 0; i < 3; ++i) {
        column[i] = high1((int32_t)source->orientation[1 + 3 * i] * (int16_t)(0u - speed));
        offset[i] = (int16_t)(source->orientation[3 * i] >> 2);
        if (speed > 0x4650) offset[i] = (int16_t)(uint16_t)((uint16_t)offset[i] << 1);
        column[i] = (int16_t)(uint16_t)(slot & 1u ? (uint16_t)column[i] + (uint16_t)offset[i]
                                                 : (uint16_t)column[i] - (uint16_t)offset[i]);
        delta[i] = column[i];
    }
    v->move(v->context, o, delta);
    return o;
}

uint16_t fe2_objects_remove_equipment(fe2_objects_state *s, fe2_object *o)
{
    uint32_t fitted = o->as.flight.equipment, rotated = fitted >> 16 | fitted << 16;
    int tries;
    if (!rotated) return 0;
    for (tries = 0x12; tries >= 0; --tries) {
        uint16_t k = fe2_objects_random_below(s, 0x13);
        if (rotated >> k & 1u) {
            unsigned bit = (unsigned)(k ^ 0x10u);
            o->as.flight.equipment_damaged |= UINT32_C(1) << bit;
            o->as.flight.equipment &= ~(UINT32_C(1) << bit);
            return fe2_objects_preamble_word(0x1e + 2u * k);
        }
    }
    return 0;
}

void fe2_objects_service(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *o, uint32_t amount)
{
    uint32_t before = s->servicing;
    s->servicing = before - amount;
    if (before >= amount) return;
    s->servicing += SERVICING_PERIOD;
    --s->equipment_threshold;
    --s->drive_threshold;
    if ((int16_t)(uint8_t)fe2_objects_random(s, NULL) > s->equipment_threshold) {
        uint16_t text = fe2_objects_remove_equipment(s, o);
        if (text) { message(v, MSG_EQUIPMENT_FAILURE, text, FE2_OBJECTS_USES_VALUE); return; }
    }
    if ((int16_t)(uint8_t)fe2_objects_random(s, NULL) > s->drive_threshold && o->as.flight.drive) {
        uint16_t mass = fe2_objects_preamble_word(2u * o->as.flight.drive);
        o->as.flight.drive = 0;
        o->fuel = 0;
        fe2_objects_add_mass(s, mass);
        message(v, MSG_DRIVE_FAILURE, 0, 0);
    }
}

static int accepted(int key, uint16_t mode)
{
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z') || key == '_')
        return 1;
    return !(mode & 0x8000u) && (key == ' ' || key == '(' || key == ')');
}

void fe2_objects_edit_text(const fe2_objects_services *v, char *text, uint16_t capacity, uint16_t argument,
                           uint16_t mode)
{
    size_t end = strlen(text);
    int16_t left = (int16_t)(capacity - end);
    v->draw_text(v->context, text, (uint8_t)argument, argument);
    for (;;) {
        int key = v->key(v->context);
        if (key < 0) continue;
        key &= 0xff;
        if (key == 0xd) return;
        if (key == 8) {
            if (!end) continue;
            ++left;
            /* Redraw in the mode's low byte (erasing), then without the character. */
            v->draw_text(v->context, text, (uint8_t)mode, argument);
            text[--end] = 0;
            v->draw_text(v->context, text, (uint8_t)argument, argument);
            continue;
        }
        if (!accepted(key, mode) || left <= 0) continue;
        text[end++] = (char)key;
        text[end] = 0;
        v->draw_text(v->context, text, (uint8_t)argument, argument);
        --left;
    }
}
