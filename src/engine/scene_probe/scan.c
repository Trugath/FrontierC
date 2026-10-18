/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the entry, 0x14089..0x14935 the reflected probe and the record
 * scan, 0x14936..0x14f14 the result: normals and hit points. */
#include "motion/motion.h"
#include "scene_probe_internal.h"

enum {
    STATUS_HIT = 0x80,
    FRAME_MASK_DOCKED = 0xffff, FRAME_MASK_PARENT = 0xff00, FRAME_MASK_KNOWN = 0x80,
    PROBE_ELIGIBLE = 0x04, NO_HANDLE = 0xff, CLASS_NONE = 0x1e, UNIT_SHIFT = 15
};

static int64_t position(const fe2_object *o, unsigned i)
{
    return (int64_t)((uint64_t)o->position_high[i] << 32 | o->position_low[i]);
}

/* 64-bit wrapping arithmetic. */
static int64_t add64(int64_t a, int64_t b) { return (int64_t)((uint64_t)a + (uint64_t)b); }
static int64_t sub64(int64_t a, int64_t b) { return (int64_t)((uint64_t)a - (uint64_t)b); }

static void copy3(int16_t to[3], const int16_t from[3])
{
    unsigned i;
    for (i = 0; i < 3; ++i) to[i] = from[i];
}

static void copy_wide(int64_t to[3], const int64_t from[3])
{
    unsigned i;
    for (i = 0; i < 3; ++i) to[i] = from[i];
}

/* A record's position in its parent's frame: through its basis rows when
 * docked. */
static void placed(const fe2_object *o, int64_t out[3])
{
    int64_t at[3];
    unsigned i;
    for (i = 0; i < 3; ++i) at[i] = position(o, i);
    if (o->station_slot) fe2_probe_wide_by_rows(o->orientation, at, out);
    else copy_wide(out, at);
}

/* The parent-frame origin less (or plus) a record's position. */
static void parent_origin_less(const fe2_scene_probe *p, const fe2_object *o, int sign, int64_t out[3])
{
    unsigned i;
    for (i = 0; i < 3; ++i) out[i] = sign < 0 ? sub64(p->parent_origin[i], position(o, i)) : add64(p->parent_origin[i], position(o, i));
}

/*
 * The candidate's relation to the source S (parent P, its parent G)
 * selects the probe origin relative to it:
 *   - S's siblings: the parent-frame origin (or the docked one, for a
 *     docked candidate) less the candidate's position;
 *   - P's siblings (P itself: the origin as it is), with the docked
 *     candidates placed by their rows;
 *   - children of S's siblings, the children of S (from zero: S's offset
 *     is not added), G itself, and S's grandchildren.
 * Others are not tested.
 */
static int relation(fe2_scene_probe *p, fe2_object *s, uint8_t slot, fe2_object *o, fe2_probe_candidate *c,
                    uint8_t *frame)
{
    int64_t at[3];
    unsigned i;
    if (o->parent_handle == s->parent_handle) {
        if (o->station_slot) {
            if (!(p->frame_mask & FRAME_MASK_KNOWN)) {
                p->frame_mask |= FRAME_MASK_KNOWN;
                fe2_probe_by_columns(p->parent->orientation, p->parent_direction, p->local_direction);
                fe2_probe_wide_by_columns(p->parent->orientation, p->parent_origin, p->local_origin);
            }
            copy_wide(c->origin, p->local_origin);
            c->direction = p->local_direction;
            *frame = 0xff;
        } else {
            copy_wide(c->origin, p->parent_origin);
        }
        for (i = 0; i < 3; ++i) c->origin[i] = sub64(c->origin[i], position(o, i));
        return 1;
    }
    if (o->parent_handle == p->grandparent) {
        if (slot == s->parent_handle) {
            if (s->station_slot) {
                copy_wide(c->origin, p->local_origin);
                c->direction = p->local_direction;
                *frame = 0xff;
            } else {
                copy_wide(c->origin, p->parent_origin);
            }
            return 1;
        }
        parent_origin_less(p, p->parent, 1, c->origin);
        placed(o, at);
        for (i = 0; i < 3; ++i) c->origin[i] = sub64(c->origin[i], at[i]);
        return 1;
    }
    {
        const fe2_object *q = fe2_pool_record(p->pool, o->parent_handle);
        if (q->parent_handle == s->parent_handle) {
            if (o->parent_handle == s->handle) {
                for (i = 0; i < 3; ++i) c->origin[i] = 0;
            } else {
                parent_origin_less(p, q, -1, c->origin);
            }
        } else if (slot == p->grandparent) {
            parent_origin_less(p, p->parent, 1, c->origin);
            return 1;
        } else if (q->parent_handle == s->handle) {
            for (i = 0; i < 3; ++i) c->origin[i] = sub64(0, position(q, i));
        } else {
            return 0;
        }
        placed(o, at);
        for (i = 0; i < 3; ++i) c->origin[i] = sub64(c->origin[i], at[i]);
        return 1;
    }
}

/* The frame is set only for related records. */
static int relate(fe2_scene_probe *p, fe2_object *s, uint8_t slot, fe2_object *o, fe2_probe_candidate *c)
{
    uint8_t frame = 0;
    c->record = o;
    c->direction = p->parent_direction;
    if (!relation(p, s, slot, o, c, &frame)) return 0;
    p->candidate_frame = frame;
    return 1;
}

/* a normal from a direction and the extent hit's arithmetic: the
 * direction scaled by the hit factor plus half the scaled origin,
 * normalized. */
static void extent_normal(const fe2_scene_probe *p, const int16_t q[3], int16_t n[3])
{
    uint32_t factor = p->hit_factor;
    unsigned i;
    for (i = 0; i < 3; ++i) {
        uint32_t a = q[i] < 0 ? (uint16_t)(0u - (uint16_t)q[i]) : (uint16_t)q[i];
        uint32_t t = a * (factor >> 16) + ((a * (factor & 0xffffu)) >> 16);
        t <<= 1;
        if (q[i] < 0) t = 0u - t;
        n[i] = (int16_t)(uint16_t)(((uint32_t)fe2_probe_sar32(p->hit_work.offset[i], 1) + t) >> 16);
    }
    fe2_motion_normalize(n);
}

static void source_normal(fe2_scene_probe *p, const int16_t n[3])
{
    copy3(p->normal, n);
    if (p->flags & FE2_PROBE_NORMAL_TO_PARENT) fe2_probe_by_rows(p->parent->orientation, n, p->parent_normal);
}

static void parent_normal(fe2_scene_probe *p, const fe2_object *s, const int16_t n[3])
{
    copy3(p->parent_normal, n);
    if (!(p->flags & FE2_PROBE_NORMAL_TO_SOURCE)) return;
    if (s->station_slot) fe2_probe_by_columns(p->parent->orientation, n, p->normal);
    else copy3(p->normal, n);
}

static int finish(fe2_scene_probe *p, fe2_object *s, fe2_scene_probe_result *result)
{
    unsigned i;
    result->record = 0;
    result->handle = 0;
    if (p->status & STATUS_HIT) {
        fe2_object *h = p->hit_record;
        unsigned shift = (uint16_t)(UNIT_SHIFT - p->exponent) & 63u;
        if ((p->flags & FE2_PROBE_NORMAL) && p->hit_class[1] != CLASS_NONE) {
            int16_t n[3];
            if (p->hit_kind) {
                if (p->hit_frame) {
                    extent_normal(p, p->direction, n);
                    source_normal(p, n);
                } else {
                    extent_normal(p, p->parent_direction, n);
                    parent_normal(p, s, n);
                }
            } else if (!h->station_slot) {
                fe2_probe_by_rows(h->orientation, p->model_normal, n);
                parent_normal(p, s, n);
            } else if (!p->hit_frame) {
                fe2_probe_by_rows(h->alternate_orientation, p->model_normal, n);
                parent_normal(p, s, n);
            } else {
                fe2_probe_by_rows(h->orientation, p->model_normal, n);
                source_normal(p, n);
            }
        }
        if (p->flags & FE2_PROBE_POINT)
            for (i = 0; i < 3; ++i)
                p->point[i] = add64(p->origin[i], fe2_probe_sar32((int32_t)p->direction[i] * (int16_t)p->mantissa, shift));
        if (p->flags & FE2_PROBE_PARENT_POINT)
            for (i = 0; i < 3; ++i) {
                /* The high dword takes the sign of the summed low dword. */
                uint32_t d = (uint32_t)fe2_probe_sar32((int32_t)p->parent_direction[i] * (int16_t)p->mantissa, shift);
                uint32_t low = (uint32_t)p->parent_origin[i] + d;
                uint32_t high = (uint32_t)((uint64_t)p->parent_origin[i] >> 32) + (low & 0x80000000u ? 0xffffffffu : 0) +
                                (low < d);
                p->parent_point[i] = (int64_t)((uint64_t)high << 32 | low);
            }
        result->record = h;
        result->handle = h->handle;
        p->hit_handle = h->handle;
    }
    result->status = p->status;
    result->distance = p->best;
    return (p->status & STATUS_HIT) != 0;
}

static void start(fe2_scene_probe *p)
{
    p->status = 0;
    p->hit_kind = 0;
    p->cleared = 0;
    p->hit_handle = 0;
}

static int query(fe2_scene_probe *p, const fe2_scene_probe_services *game, const fe2_scene_probe_request *r,
                 fe2_scene_probe_result *result)
{
    fe2_object *s = r->source;
    int slot;
    unsigned i;
    p->flags = (uint16_t)(r->mode >> 8 | r->mode << 8);
    copy3(p->direction, r->direction);
    p->excluded = r->excluded;
    p->limit = p->best = (uint64_t)r->extent_high << 32 | r->extent;
    p->parent = fe2_pool_record(p->pool, s->parent_handle);
    p->grandparent = p->parent->parent_handle;
    for (i = 0; i < 3; ++i) p->origin[i] = add64(position(s, i), r->offset[i]);
    if (s->station_slot) {
        copy3(p->local_direction, p->direction);
        copy_wide(p->local_origin, p->origin);
        p->frame_mask = FRAME_MASK_DOCKED;
        fe2_probe_by_rows(p->parent->orientation, p->local_direction, p->parent_direction);
        fe2_probe_wide_by_rows(p->parent->orientation, p->local_origin, p->parent_origin);
    } else {
        copy3(p->parent_direction, p->direction);
        copy_wide(p->parent_origin, p->origin);
        p->frame_mask = FRAME_MASK_PARENT;
    }
    for (slot = FE2_POOL_LAST_SLOT; slot >= 0; --slot) {
        fe2_probe_candidate c;
        fe2_object *o;
        if (!(p->pool->status[slot] & PROBE_ELIGIBLE) || (uint8_t)slot == p->excluded) continue;
        o = fe2_pool_record(p->pool, (uint8_t)slot);
        if (o->update_countdown != 0) continue;
        if (relate(p, s, (uint8_t)slot, o, &c)) fe2_probe_candidate_test(p, game, &c);
    }
    return finish(p, s, result);
}

int fe2_scene_probe_query(fe2_scene_probe *p, const fe2_scene_probe_services *game,
                          const fe2_scene_probe_request *request, fe2_scene_probe_result *result)
{
    start(p);
    return query(p, game, request, result);
}

int fe2_scene_probe_reflect(fe2_scene_probe *p, const fe2_scene_probe_services *game, fe2_object *source,
                            uint16_t mode, uint16_t reduction, fe2_scene_probe_result *result)
{
    fe2_scene_probe_request r;
    uint64_t rest;
    uint32_t dot = 0;
    int16_t c;
    unsigned i;
    start(p);
    rest = p->limit - p->best;
    if ((int64_t)rest < 0) {
        result->record = 0;
        result->handle = 0;
        result->status = p->status;
        result->distance = p->best;
        return 0;
    }
    for (--reduction; (int16_t)reduction >= 0; --reduction) rest >>= 1;
    for (i = 0; i < 3; ++i) dot += (uint32_t)((int32_t)p->direction[i] * p->normal[i]);
    c = (int16_t)(uint16_t)((dot << 1) >> 16);
    for (i = 0; i < 3; ++i) {
        uint32_t t = (uint32_t)((int32_t)p->normal[i] * c) << 2;
        r.direction[i] = (int16_t)(uint16_t)((uint16_t)p->direction[i] - (uint16_t)(t >> 16));
        r.offset[i] = (int32_t)((uint32_t)p->point[i] - (uint32_t)p->origin[i]);
    }
    r.source = source;
    r.excluded = NO_HANDLE;
    r.extent = (uint32_t)rest;
    r.extent_high = (uint32_t)(rest >> 32);
    r.mode = mode;
    return query(p, game, &r, result);
}
