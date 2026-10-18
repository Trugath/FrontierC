/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SCENE_PROBE_INTERNAL_H
#define FE2_SCENE_PROBE_INTERNAL_H

/* Shared between the scene probe files. */
#include "scene_probe.h"

/* The candidate under test: its record, the probe origin relative to it
 * and the direction (the parent or the docked frame's). */
typedef struct fe2_probe_candidate {
    fe2_object *record;
    int64_t origin[3];
    const int16_t *direction;
} fe2_probe_candidate;

/* ---- frames.c ------------------------------------------------------------ */
int32_t fe2_probe_sar32(int32_t v, unsigned n);
uint32_t fe2_probe_shl32(uint32_t v, unsigned n);
/* The high word of twice the wrapped dot product of v with the basis's row
 * k (or column k). */
int16_t fe2_probe_row(const int16_t basis[9], unsigned k, const int16_t v[3]);
int16_t fe2_probe_column(const int16_t basis[9], unsigned k, const int16_t v[3]);
void fe2_probe_by_rows(const int16_t basis[9], const int16_t v[3], int16_t out[3]);
void fe2_probe_by_columns(const int16_t basis[9], const int16_t v[3], int16_t out[3]);
/* A 64-bit vector through the basis rows (columns): reduced to words by a
 * common shift, transformed, and shifted back. */
void fe2_probe_wide_by_rows(const int16_t basis[9], const int64_t in[3], int64_t out[3]);
void fe2_probe_wide_by_columns(const int16_t basis[9], const int64_t in[3], int64_t out[3]);
/* words reordered and negated by an axis code (0: unchanged). */
void fe2_probe_permute(int16_t v[3], uint8_t code);

/* ---- filter.c ------------------------------------------------------------ */
/* test one candidate. */
void fe2_probe_candidate_test(fe2_scene_probe *p, const fe2_scene_probe_services *game,
                              const fe2_probe_candidate *c);
/* keep a nearer hit (compared on the low dwords). */
void fe2_probe_keep_nearer(fe2_scene_probe *p, fe2_object *record, uint32_t distance, uint16_t mantissa,
                           uint16_t exponent, uint8_t class_a, uint8_t class_b, const int16_t normal[3]);

/* ---- geometry.c ---------------------------------------------------------- */
/* the candidate's model geometry. */
void fe2_probe_geometry(fe2_scene_probe *p, const fe2_scene_probe_services *game, const fe2_probe_candidate *c);

/* ---- extent.c ------------------------------------------------------------ */
/* the candidate as a sphere of its extent. */
void fe2_probe_extent(fe2_scene_probe *p, const fe2_probe_candidate *c);

/* ---- intervals.c --------------------------------------------------------- */
enum { FE2_PROBE_NODES = 200 };
/* The interval list of a geometry walk: boundaries sorted by position,
 * from node 0 (position 0) to node 1 (0xffff, the end). A state's low
 * byte is its kind (bit 1: open, bit 0: closed); the high byte the tag
 * of the record that painted it. */
typedef struct fe2_probe_node {
    uint16_t position, next, state;
    int16_t vector[3];
} fe2_probe_node;

typedef struct fe2_probe_intervals {
    fe2_probe_node node[FE2_PROBE_NODES];
    uint16_t count;
} fe2_probe_intervals;

void fe2_probe_intervals_init(fe2_probe_intervals *list);
/* paint [low, high] with kind 8, terminate at low
 * with kind 0xd, paint kind 7 (and close what lies outside). */
void fe2_probe_paint_open(fe2_probe_intervals *list, uint16_t low, uint16_t high, const int16_t v[3], uint8_t tag);
void fe2_probe_paint_end(fe2_probe_intervals *list, uint16_t low, uint16_t high, const int16_t v[3], uint8_t tag);
void fe2_probe_paint_closed(fe2_probe_intervals *list, uint16_t low, uint16_t high, const int16_t v[3],
                            uint8_t tag);

#endif
