/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_OBJECT_DATA_H
#define FE2_OBJECT_DATA_H
/* Decode an immutable object resource into native storage. Resource offsets
 * stop at this boundary; simulation uses fe2_object fields throughout. */
#include "object.h"
#include <string.h>
static inline uint16_t fe2_object_data_word(const uint8_t *p)
{ return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static inline uint32_t fe2_object_data_dword(const uint8_t *p)
{ return (uint32_t)fe2_object_data_word(p) | (uint32_t)fe2_object_data_word(p + 2) << 16; }
static inline void fe2_object_read_template(fe2_object *o, const uint8_t *p, int body)
{
    unsigned i;
    memset(o, 0, sizeof(*o));
#define W(field, at) o->field = fe2_object_data_word(p + (at))
#define D(field, at) o->field = fe2_object_data_dword(p + (at))
    for (i = 0; i < 9; ++i) {
        o->orientation[i] = (int16_t)fe2_object_data_word(p + i * 2);
        o->alternate_orientation[i] = (int16_t)fe2_object_data_word(p + 0x46 + i * 2);
    }
    o->rebuild_budget = p[0x12]; o->render_state = p[0x13];
    for (i = 0; i < 3; ++i) {
        D(camera_offset[i], 0x14 + 4 * i); D(camera_target[i], 0x20 + 4 * i);
        D(position_low[i], 0x2c + 4 * i); D(position_high[i], 0x38 + 4 * i);
        o->velocity[i] = (int32_t)fe2_object_data_dword(p + 0x62 + 4 * i);
        o->spin[i] = (int16_t)fe2_object_data_word(p + 0x86 + 2 * i);
        o->controls[i] = (int16_t)fe2_object_data_word(p + 0x8c + 2 * i);
        W(vector_delta[i], 0x8a + 2 * i); W(velocity_fraction[i], 0xc0 + 2 * i);
        o->feedback[i] = (int32_t)fe2_object_data_dword(p + 0xc6 + 4 * i);
        o->source_offset[i] = (int16_t)fe2_object_data_word(p + 0xd6 + 2 * i);
        o->steering[i] = (int16_t)fe2_object_data_word(p + 0xdc + 2 * i);
        o->rotation_rates[i] = (int16_t)fe2_object_data_word(p + 0xe4 + 2 * i);
        W(model_scale_a[i], 0x10a + 2 * i); W(model_scale_b[i], 0x110 + 2 * i);
    }
    o->parent_handle = p[0x44]; o->station_slot = p[0x45];
    W(model_parameter, 0x58); W(model, 0x5a); o->basis_generation = p[0x5c];
    o->handle = p[0x5d]; o->role = p[0x5e]; o->level = p[0x5f];
    o->update_period = p[0x60]; o->update_countdown = p[0x61];
    W(screen_x, 0x6e); W(screen_y, 0x70); W(clock, 0x72); W(cargo_type, 0x74);
    D(target_seed, 0x76); W(size_mantissa, 0x7a); W(size_exponent, 0x7c);
    D(orbit[0], 0x7e); D(orbit[1], 0x82); D(scalar, 0x86); D(camera_roll, 0x8c);
    if (body) {
        for (i = 0; i < 4; ++i) W(as.body.orbit_product[i], 0x90 + 2 * i);
        D(as.body.orbit_radius, 0x98); W(as.body.orbit_inclination, 0x9c);
        o->as.body.time_shift = p[0x9e]; o->as.body.docking_state = p[0x9f];
        o->as.body.docking_claim = p[0xa0]; o->as.body.generation = p[0xa1];
        W(as.body.basis_seed, 0xa2); W(as.body.shape, 0xa4);
        memcpy(o->as.body.docked, p + 0xa6, 8); W(as.body.docking_timer, 0xae);
        W(as.body.scale_exponent, 0xb0); W(as.body.radius_class, 0xb2);
        W(as.body.progress, 0xb4); W(as.body.temperature, 0xb6);
    } else {
        for (i = 0; i < 6; ++i)
            o->as.flight.control_limits[i / 2][i % 2] = (int16_t)fe2_object_data_word(p + 0x92 + i * 2);
        D(as.flight.equipment, 0x9e); D(as.flight.equipment_damaged, 0xa2);
        o->as.flight.drive = p[0xa6]; o->as.flight.capacity_class = p[0xa7];
        memcpy(o->as.flight.installed, p + 0xa8, 12);
        W(as.flight.shield_charge, 0xb4); W(as.flight.shield_capacity, 0xb6);
    }
    W(hull, 0xb8); W(stored_model, 0xba); W(cooldown, 0xbc); W(busy_remaining, 0xbe);
    o->behaviour_parent = p[0xd2]; o->behaviour = p[0xd3];
    o->behaviour_target = p[0xd4]; o->control_class = p[0xd5];
    W(steering_shift, 0xe2); W(capacity, 0xea); o->map_marker = p[0xec]; o->drive_fuel = p[0xed];
    D(reward, 0xee); D(fuel, 0xf2); memcpy(o->name, p + 0xf6, sizeof(o->name));
    W(transition_countdown, 0x116); W(flags, 0x118);
    o->trail = p[0x11a]; o->effect = p[0x11b]; o->probe_countdown = p[0x11c]; o->frame_state = p[0x11d];
#undef W
#undef D
}

/* The whole record (0x11e bytes, the original layout) and back. The original
 * keeps one copy of bytes the native record names several ways: 0x86..0x91
 * (scalar and vector deltas; a turning craft's spin and a flying craft's
 * controls; a camera frame's roll), 0x90..0xb7 (the craft, body and flight
 * views) and 0xc6..0xd1 (feedback limits, a hyperspace cloud's origin, an
 * event craft's arrival). Reading gives every name of such bytes their
 * value; writing stores the common name and then any other one that is
 * set (nonzero) over it. */
enum { FE2_OBJECT_RECORD_BYTES = 0x11e };
/* The three views are the 0x28 bytes at 0x90, without padding. */
typedef char fe2_object_view_bytes[sizeof(((fe2_object *)0)->as) == 0x28 ? 1 : -1];
static inline void fe2_object_put_word(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void fe2_object_put_dword(uint8_t *p, uint32_t v)
{ fe2_object_put_word(p, (uint16_t)v); fe2_object_put_word(p + 2, (uint16_t)(v >> 16)); }
static inline void fe2_object_read_record(fe2_object *o, const uint8_t *p)
{
    unsigned i;
    fe2_object_read_template(o, p, 1);
    memcpy(&o->as, p + 0x90, sizeof(o->as));
    for (i = 0; i < 3; ++i) o->hyperspace[i] = fe2_object_data_dword(p + 0xc6 + 4 * i);
    o->event_param = fe2_object_data_word(p + 0xca); o->resume_behaviour = p[0xcc];
    o->event_pad = p[0xcd]; o->event_delay = fe2_object_data_dword(p + 0xce);
}
static inline void fe2_object_write_record(const fe2_object *o, uint8_t *p)
{
    unsigned i;
    int set;
    memset(p, 0, FE2_OBJECT_RECORD_BYTES);
    for (i = 0; i < 9; ++i) {
        fe2_object_put_word(p + i * 2, (uint16_t)o->orientation[i]);
        fe2_object_put_word(p + 0x46 + i * 2, (uint16_t)o->alternate_orientation[i]);
    }
    p[0x12] = o->rebuild_budget; p[0x13] = o->render_state;
    for (i = 0; i < 3; ++i) {
        fe2_object_put_dword(p + 0x14 + 4 * i, o->camera_offset[i]);
        fe2_object_put_dword(p + 0x20 + 4 * i, o->camera_target[i]);
        fe2_object_put_dword(p + 0x2c + 4 * i, o->position_low[i]);
        fe2_object_put_dword(p + 0x38 + 4 * i, o->position_high[i]);
        fe2_object_put_dword(p + 0x62 + 4 * i, (uint32_t)o->velocity[i]);
        fe2_object_put_word(p + 0xc0 + 2 * i, o->velocity_fraction[i]);
        fe2_object_put_word(p + 0xd6 + 2 * i, (uint16_t)o->source_offset[i]);
        fe2_object_put_word(p + 0xdc + 2 * i, (uint16_t)o->steering[i]);
        fe2_object_put_word(p + 0xe4 + 2 * i, (uint16_t)o->rotation_rates[i]);
        fe2_object_put_word(p + 0x10a + 2 * i, o->model_scale_a[i]);
        fe2_object_put_word(p + 0x110 + 2 * i, o->model_scale_b[i]);
    }
    p[0x44] = o->parent_handle; p[0x45] = o->station_slot;
    fe2_object_put_word(p + 0x58, o->model_parameter); fe2_object_put_word(p + 0x5a, o->model);
    p[0x5c] = o->basis_generation; p[0x5d] = o->handle; p[0x5e] = o->role; p[0x5f] = o->level;
    p[0x60] = o->update_period; p[0x61] = o->update_countdown;
    fe2_object_put_word(p + 0x6e, o->screen_x); fe2_object_put_word(p + 0x70, o->screen_y);
    fe2_object_put_word(p + 0x72, o->clock); fe2_object_put_word(p + 0x74, o->cargo_type);
    fe2_object_put_dword(p + 0x76, o->target_seed);
    fe2_object_put_word(p + 0x7a, o->size_mantissa); fe2_object_put_word(p + 0x7c, o->size_exponent);
    fe2_object_put_dword(p + 0x7e, o->orbit[0]); fe2_object_put_dword(p + 0x82, o->orbit[1]);
    /* 0x86..0x91: scalar, vector deltas and the view's first word; then spin,
     * controls and the camera roll where set. */
    fe2_object_put_dword(p + 0x86, o->scalar);
    for (i = 0; i < 3; ++i) fe2_object_put_word(p + 0x8a + 2 * i, o->vector_delta[i]);
    memcpy(p + 0x90, &o->as, sizeof(o->as));
    for (set = 0, i = 0; i < 3; ++i) set |= o->spin[i] != 0;
    if (set) for (i = 0; i < 3; ++i) fe2_object_put_word(p + 0x86 + 2 * i, (uint16_t)o->spin[i]);
    for (set = 0, i = 0; i < 3; ++i) set |= o->controls[i] != 0;
    if (set) for (i = 0; i < 3; ++i) fe2_object_put_word(p + 0x8c + 2 * i, (uint16_t)o->controls[i]);
    if (o->camera_roll) fe2_object_put_dword(p + 0x8c, o->camera_roll);
    fe2_object_put_word(p + 0xb8, o->hull); fe2_object_put_word(p + 0xba, o->stored_model);
    fe2_object_put_word(p + 0xbc, o->cooldown); fe2_object_put_word(p + 0xbe, o->busy_remaining);
    /* 0xc6..0xd1: feedback; then a hyperspace origin or an event's arrival. */
    for (i = 0; i < 3; ++i) fe2_object_put_dword(p + 0xc6 + 4 * i, (uint32_t)o->feedback[i]);
    for (set = 0, i = 0; i < 3; ++i) set |= o->hyperspace[i] != 0;
    if (set) for (i = 0; i < 3; ++i) fe2_object_put_dword(p + 0xc6 + 4 * i, o->hyperspace[i]);
    if (o->event_param || o->resume_behaviour || o->event_pad || o->event_delay) {
        fe2_object_put_word(p + 0xca, o->event_param); p[0xcc] = o->resume_behaviour;
        p[0xcd] = o->event_pad; fe2_object_put_dword(p + 0xce, o->event_delay);
    }
    p[0xd2] = o->behaviour_parent; p[0xd3] = o->behaviour; p[0xd4] = o->behaviour_target;
    p[0xd5] = o->control_class; fe2_object_put_word(p + 0xe2, o->steering_shift);
    fe2_object_put_word(p + 0xea, o->capacity); p[0xec] = o->map_marker; p[0xed] = o->drive_fuel;
    fe2_object_put_dword(p + 0xee, o->reward); fe2_object_put_dword(p + 0xf2, o->fuel);
    memcpy(p + 0xf6, o->name, sizeof(o->name));
    fe2_object_put_word(p + 0x116, o->transition_countdown); fe2_object_put_word(p + 0x118, o->flags);
    p[0x11a] = o->trail; p[0x11b] = o->effect; p[0x11c] = o->probe_countdown; p[0x11d] = o->frame_state;
}
#endif
