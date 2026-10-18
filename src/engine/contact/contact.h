/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_CONTACT_H
#define FE2_CONTACT_H

/*
 * Contacts between records (image 0x1129d..0x1147e, the scan, and
 * 0x120f5..0x13098, the classification): a broad phase on the records'
 * bounds, then either a sphere test with its responses or the target
 * model's contact program (plane lists and nested models), walked along
 * the current model's extension offsets.
 *
 * The overlays call the scan through resident vectors 0x1129d and 0x112a4
 * (with the motion response); it returns the first reported record and
 * the shared contact results. Records are in flight (`as.flight`).
 */
#include <stddef.h>
#include <stdint.h>

#include "game/object.h"
#include "motion/motion.h"
#include "pool/pool.h"

/* The game state the contact code reads and writes. */
typedef struct fe2_contact {
    fe2_object_pool *pool;             /* the current pool */
    uint32_t frame_step;               /* the frame's elapsed time */
    uint8_t result;                    /* the reported result (0x80: a motion response) */
    uint8_t sphere;                    /* sphere flags: 1 the target's bounds differ, 2 near
                                        * the sphere, 0x40 aligned, 0x80 responded */
    uint8_t model_result;              /* the results a model program collected */
    uint8_t handle;                    /* the first reported target's handle */
    uint8_t respond_now;               /* a linked contact asks for the response */
    uint8_t response;                  /* 0xff in the response scan */
    uint8_t classification;            /* the program tag reported */
    uint16_t model_scale;              /* the current model's scale */
    fe2_object *reported;              /* the reported record */
    uint32_t distance;                 /* the gap to the nearest sphere */
} fe2_contact;

typedef struct fe2_contact_services {
    void *opaque;
    /* The motion services (0xd0, for leaving a basis frame). */
    const fe2_motion_services *motion;
    /* 0x50: the active bank's model descriptor for a byte index. Offsets
     * in a descriptor are relative to it. */
    const uint8_t *(*model)(void *opaque, uint16_t index);
    /* The record's model variable word `index` (+0x72 + 2 * index; the
     * original reads past the record for large indexes). */
    uint16_t (*model_variable)(void *opaque, const fe2_object *record, uint8_t index);
    /* 0xcc: the record's position relative to the target, across frames. */
    void (*frame_difference)(void *opaque, fe2_object *record, fe2_object *target, int64_t difference[3]);
    /* 0x10: a contact event for `record` (the contact's target `target`),
     * with a strength and the other record's handle (0 for none). */
    void (*collision)(void *opaque, fe2_object *record, fe2_object *target, uint16_t strength, uint8_t other);
} fe2_contact_services;

/* What the scan returns (AL, DL and the argument block). */
typedef struct fe2_contact_report {
    uint8_t flags;                     /* AL: result | sphere flags */
    uint8_t handle;                    /* DL */
    fe2_object *record;                /* the reported record */
    uint32_t distance;
    uint16_t classification;           /* the tag's low five bits */
} fe2_contact_report;

/* (`respond`): classify the record against its parent
 * (moving it into or out of the parent's basis frame) and against each
 * contact candidate in the pool. */
fe2_contact_report fe2_contact_scan(fe2_contact *c, const fe2_contact_services *game, fe2_object *record,
                                    int respond);

/* classify `record` against `target`, at `relative` (the record's
 * position less the target's). Returns AL (0 when the bounds are apart). */
uint8_t fe2_contact_classify(fe2_contact *c, const fe2_contact_services *game, fe2_object *record,
                             fe2_object *target, const int64_t relative[3]);

/* The destination query axis for each permuted component, generated from
 * contact_axis_permutations.csv, including the two compatibility rows. */
extern const uint8_t (*fe2_contact_axes)[3];
int fe2_contact_bind(const uint8_t *image, size_t count);

#endif
