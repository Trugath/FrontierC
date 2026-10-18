/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_ORBITS_ORBITS_H
#define FE2_ORBITS_ORBITS_H

/*
 * Orbiting bodies and docking timers (orbits).
 *
 * - The orbit update (0x82) takes a body's new time sample. It advances the
 *   body's orbit accumulator by the time elapsed times its orbit radius
 *   (multiplier), and moves the body along its orbit, incrementally or by
 *   re-interpolating. It also re-orients the body when a shifted time
 *   bucket changes, and ramps a body scalar (+0x74) by the frame step.
 * - The countdown (0x86) runs a body's docking timer down. On expiry it
 *   tells the player whose claim lapsed and releases the claim through the
 *   market reference service. It also keeps the resident reference index on
 *   the lowest-ordered body.
 *
 * Math, object lookup and the market/station overlays come through
 * `fe2_orbit_services`.
 */
#include <stdint.h>

#include "game/call_args.h"
#include "game/object.h"

/* Body flags (+0x118). */
enum { FE2_ORBIT_FIXED = 0x20, FE2_ORBIT_SWAP_BASIS = 0x10 };

typedef struct fe2_orbit_state {
    uint32_t frame_step;
    uint16_t basis_angle[2];          /* shared with the math service */
    uint8_t reference_index;
    uint8_t player_handle;
    uint8_t flight_state;
} fe2_orbit_state;

typedef struct fe2_orbit_services {
    void *opaque;
    /* (new - old) * multiplier * 8, 64-bit: parameters 0/1 the old sample,
     * 2/3 the new, 4 the multiplier. Four words, low first. */
    void (*wide_product)(void *opaque, const fe2_call_args *args, uint16_t product[4]);
    /* Position from parameter 0 (lookup coordinate) and 1 (accumulator high
     * dword) at scale 6 and exponent 7. */
    void (*interpolate)(void *opaque, fe2_object *object, const fe2_call_args *args);
    /* Advance the position by `factor` steps of the object's vector. */
    void (*scale_vector)(void *opaque, fe2_object *object, uint16_t factor);
    /* Orientation from state->basis_angle (permutation B). */
    void (*basis_b)(void *opaque, fe2_object *object);
    fe2_object *(*lookup_object)(void *opaque, uint8_t handle);
    void (*show_message)(void *opaque, uint16_t text, const fe2_call_args *args);      /* station */
    /* market reference service `request` (BX) for `handle` (AL); parameter 9
     * is the object. */
    void (*reference_service)(void *opaque, uint16_t request, uint8_t handle,
                              const fe2_call_args *args);                              /* market */
} fe2_orbit_services;

/* 0x82: `object` (scratch 0x7dc) takes the new time `sample`. */
void fe2_orbit_update(fe2_orbit_state *state, const fe2_orbit_services *game, fe2_object *object,
                      uint64_t sample);
/* 0x86: run `object`'s docking timer down by `elapsed`. */
void fe2_orbit_count_down(fe2_orbit_state *state, const fe2_orbit_services *game,
                          fe2_object *object, uint16_t elapsed);

#endif
