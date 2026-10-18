/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SCENE_PROBE_H
#define FE2_SCENE_PROBE_H

/*
 * The scene probe (image 0x13099..0x1614d): the nearest record a ray from a
 * source record meets. The ray starts at the source's position plus an
 * offset and runs along a direction for at most an extent (a 64-bit
 * distance). Every record of the current pool that is eligible (slot
 * status bit 2, update countdown zero) and related to the source (same
 * parent, the parent's parent, siblings' children, the source's children
 * and grandchildren) is tested in the source parent's frame, or in a
 * docked record's frame. A candidate is a sphere bounded by its extent
 * (+0x10a) or the model's interval geometry, depending on its flags. The
 * nearest hit keeps its distance, normal and point.
 *
 * The callers use resident vector 0x130; 0x14089, a probe
 * continued from the last hit along the reflected direction, has no
 * caller in the image. Requests and results travel in the argument block
 *  in the original; here they are typed. The probe's own game
 * state  is `fe2_scene_probe`.
 *
 * Positions are 64-bit (the original keeps their low and high dwords
 * apart); directions and normals are Q15 words.
 */
#include <stddef.h>
#include <stdint.h>

#include "math/math3.h"
#include "game/object.h"
#include "pool/pool.h"

/* Extent calculations retained for the closest hit. */
typedef struct fe2_probe_work {
    int32_t offset[3];                 /* the origin, at the hit's scale */
    int32_t reference;                 /* the candidate's extent at that scale */
    uint16_t shift;                    /* the scale's exponent */
    uint16_t spare;                    /* never written by the probe; copied with the rest */
} fe2_probe_work;

/* The probe state (the original's DS globals). */
typedef struct fe2_scene_probe {
    fe2_object_pool *pool;             /* the pool scanned */
    uint64_t limit;                    /* the requested extent */
    int16_t direction[3];              /* in the source's frame */
    int64_t origin[3];                 /* the source's position + offset */
    int16_t parent_direction[3];       /* in the source parent's frame */
    int64_t parent_origin[3];
    int16_t local_direction[3];        /* in the docked frame (the parent's basis) */
    int64_t local_origin[3];
    uint8_t status;                    /* bit 7: a hit */
    uint8_t hit_kind;                  /* 0xff: an extent hit; 0: geometry */
    uint8_t cleared;                   /* cleared by each probe */
    uint8_t hit_handle;
    uint8_t excluded;                  /* the slot skipped */
    uint8_t grandparent;               /* the source parent's parent */
    uint8_t candidate_frame;           /* 0xff: the candidate is tested in the docked frame */
    uint8_t hit_frame;                 /* the hit's */
    uint8_t hit_class[2];              /* (0x1e in the second: no normal) */
    fe2_object *parent;                /* the source's parent */
    fe2_object *hit_record;
    uint64_t best;                     /* the nearest hit's distance (the extent at first) */
    uint16_t mantissa, exponent;       /* the distance as mantissa << (exponent - 15) */
    uint16_t frame_mask;               /* bit 7: the docked origin is known */
    uint16_t flags;                    /* the request mode, bytes swapped */
    int16_t offset[3];                 /* the candidate-relative origin as words */
    uint16_t offset_shift;             /*.. and their exponent */
    int64_t saved_offset[3];           /* the candidate-relative origin (extent test) */
    fe2_probe_work work;
    fe2_probe_work hit_work;
    uint32_t hit_factor;
    int16_t model_normal[3];           /* a geometry hit's normal, in the candidate's frame */
    int16_t parent_normal[3];          /* the hit normal in the parent frame */
    int16_t normal[3];                 /*.. in the source's frame */
    int64_t parent_point[3];           /* the hit point in the parent frame */
    int64_t point[3];                  /*.. in the source's frame */
    int16_t crossing_normal[3];        /* the last crossing's normal */
    fe2_object *candidate;             /* the record whose geometry is walked */
} fe2_scene_probe;

/* Request mode bits (the low byte of `flags`; the high byte of the
 * caller's mode word). */
enum {
    FE2_PROBE_LENGTH = 0x01,           /* geometry candidates are spheres of the origin's length */
    FE2_PROBE_POINT = 0x02,            /* the hit point in the source's frame */
    FE2_PROBE_PARENT_POINT = 0x04,     /*... and in the parent frame */
    FE2_PROBE_NORMAL = 0x08,           /* the hit normal */
    FE2_PROBE_NORMAL_TO_SOURCE = 0x10, /* a parent-frame normal also in the source's frame */
    FE2_PROBE_NORMAL_TO_PARENT = 0x20, /* a source-frame normal also in the parent frame */
    FE2_PROBE_EXTENTS_ONLY = 0x40      /* every candidate by its extent */
};

typedef struct fe2_scene_probe_services {
    void *opaque;
    /* 0x50: a model's serialized descriptor. The probe reads its
     * vertex (+2), normal (+6) and contact program (+0x1a) offsets and the
     * scale exponent (+0xa). */
    const uint8_t *(*model)(void *opaque, uint16_t model);
    /* 0xd0: rebuild the record's alternate basis (+0x46). */
    void (*rebuild_secondary_basis)(void *opaque, fe2_object *record);
    /* The record's word at +0x72 + 2 * index (a contact program condition). */
    uint16_t (*condition_word)(void *opaque, const fe2_object *record, uint8_t index);
} fe2_scene_probe_services;

typedef struct fe2_scene_probe_request {
    fe2_object *source;
    int32_t offset[3];                 /* from the source's position */
    int16_t direction[3];
    uint8_t excluded;                  /* a slot not tested */
    uint32_t extent, extent_high;
    uint16_t mode;                     /* the high byte holds the FE2_PROBE_ bits */
} fe2_scene_probe_request;

typedef struct fe2_scene_probe_result {
    uint8_t status;                    /* bit 7 a hit (the returned sign flag) */
    uint64_t distance;                 /* the nearest hit, or the extent */
    fe2_object *record;                /* on a hit (else 0; the original leaves a record) */
    uint8_t handle;                    /* on a hit */
} fe2_scene_probe_result;

/* probe the scene. Returns whether it hit. */
int fe2_scene_probe_query(fe2_scene_probe *p, const fe2_scene_probe_services *game,
                          const fe2_scene_probe_request *request, fe2_scene_probe_result *result);
/* probe again from the last hit point along the direction
 * reflected by the last normal, for what remains of the extent
 * halved `reduction` times (as a signed count; 0x8000 halves 0x8000 times).
 * The offset is the low dwords of the hit point less the origin. Nothing
 * is probed when the best distance exceeds the extent. */
int fe2_scene_probe_reflect(fe2_scene_probe *p, const fe2_scene_probe_services *game, fe2_object *source,
                            uint16_t mode, uint16_t reduction, fe2_scene_probe_result *result);

/* Generated contact axis permutations: the source component of
 * each of a vector's three words, for axis codes 0..5. */
extern const uint8_t (*fe2_probe_axes)[3];
int fe2_probe_bind(const uint8_t *image, size_t count);

#endif
