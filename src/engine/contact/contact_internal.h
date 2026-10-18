/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_CONTACT_INTERNAL_H
#define FE2_CONTACT_INTERNAL_H

/* Shared between the contact files. */
#include "contact.h"

/* A little-endian word of model data. */
static inline uint16_t fe2_contact_word(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static inline uint32_t fe2_contact_magnitude(uint32_t v) { return (v & UINT32_C(0x80000000)) ? 0u - v : v; }

/* The shift bringing three magnitudes below 0x3f00, measured in z, x, y
 * order (each already shifted by the count so far). */
unsigned fe2_contact_word_shift(const int32_t v[3]);

/* The upper dword of v^2 without the low square (hi^2 + (2 lo hi)/65536,
 * wrapping), and an approximate root of a sum of them (the resident
 * rescale's divisor refinement, 2 * s halved plus 0x40000000, averaged
 * with a restoring trial quotient until within 32768). */
uint32_t fe2_contact_square(uint32_t v);
uint32_t fe2_contact_root(uint32_t s);

/* the general motion response. Returns AL. */
uint8_t fe2_contact_respond(fe2_contact *c, const fe2_contact_services *game, fe2_object *record, fe2_object *target);

/* the target model's contact program, walked along the record's
 * extension offsets. Returns AL. */
uint8_t fe2_contact_model(fe2_contact *c, const fe2_contact_services *game, fe2_object *record, fe2_object *target,
                          const int64_t relative[3]);

#endif
