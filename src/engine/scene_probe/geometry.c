/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/*
 * 0x15228..0x15ae7 a candidate's model geometry: its contact program walked
 * into an interval list along the ray.
 *
 * A model descriptor gives the program (+0x1a), packed normals (+6), packed
 * vertices (+2) and a scale exponent (+0xa), as offsets from the
 * descriptor. Program records are a type byte (its tag), a second byte
 * (unused here) and:
 *   - type bit 0x20: a condition, a field index and a bit number (bit 7:
 *     the value wanted) of the candidate's word at +0x72 + 2 * index; a
 *     failed condition skips the record;
 *   - types without bit 0x80: a zero-terminated list of crossings; the
 *     range of ray parameters inside them paints the list (kind 7, or with
 *     bit 0x40 kind 8 from two below the start);
 *   - types 0xc0..0xff: the same, ending the list at the start (kind 0xd);
 *   - other types from 0x80: a child model (big-endian word, doubled), an
 *     axis code and a vertex of this model as its origin; the child's list
 *     is walked and painted into this one.
 * The program ends at a zero type byte. The hit is the first closed node;
 * its position is the distance at the walk's scale.
 */
#include <string.h>

#include "game/model.h"
#include "scene_probe_internal.h"

enum {
    MODEL_VERTICES = 0x02, MODEL_NORMALS = 0x06, MODEL_SCALE = 0x0a, MODEL_PROGRAM = 0x1a,
    TYPE_CONDITION = 0x20, TYPE_PAINTED = 0x40, TYPE_CHILD = 0x80,
    CONDITION_WANTED = 0x80, CONDITION_SHIFT = 0x1f,
    SCALE_BIAS = 7,
    PARAMETER_MAX = 0x7fff, PARAMETER_MIN = -0x8000,
    KIND_CLOSED = 0x07, KIND_PAINTED = 0x08, KIND_END = 0x0d, KIND_LAST = 0x15,
    CLOSED_BIT = 0x01, CLASS_NONE = 0x1e,
    AXIS_NEGATE_X = 0x20, AXIS_NEGATE_Y = 0x10, AXIS_NEGATE_Z = 0x08, AXIS_ROW = 7
};

/* The walk's workspace (the original's 0x97e bytes of stack). */
typedef struct workspace {
    int16_t origin[3], direction[3];   /* +0, +6 at the walk's scale, in the model's frame */
    const uint8_t *normals, *vertices; /*, +0x10 */
    uint16_t scale;                    /* the model's scale exponent */
    uint16_t shift;                    /* the walk's exponent */
    uint8_t tag;                       /* the current record's type */
    uint8_t axes;                      /* a child's axis code */
    fe2_probe_intervals list;
} workspace;

typedef struct walk {
    fe2_scene_probe *p;
    const fe2_scene_probe_services *game;
    fe2_object *record;
} walk;

/* x86 16-bit shifts: the count masked to 5 bits. */
static int16_t sar16(int16_t v, unsigned n)
{
    n &= 31;
    if (n >= 16) return v < 0 ? -1 : 0;
    return (int16_t)(uint16_t)(v < 0 ? ~((uint16_t)~(uint16_t)v >> n) : (uint16_t)v >> n);
}

static int16_t shl16(int16_t v, unsigned n)
{
    n &= 31;
    return n >= 16 ? 0 : (int16_t)(uint16_t)((uint16_t)v << n);
}

static int16_t neg16(int16_t v) { return (int16_t)(uint16_t)(0u - (uint16_t)v); }

static void open_model(const fe2_scene_probe_services *game, uint16_t model, workspace *ws, const uint8_t **program)
{
    const uint8_t *d = game->model(game->opaque, model);
    ws->vertices = d + fe2_model_word(d, MODEL_VERTICES);
    ws->normals = d + fe2_model_word(d, MODEL_NORMALS);
    ws->scale = fe2_model_word(d, MODEL_SCALE);
    *program = d + fe2_model_word(d, MODEL_PROGRAM);
    fe2_probe_intervals_init(&ws->list);
}

/* A packed vertex (x, ?, z, y; x negated for the odd one of a pair) at
 * the scale of the walk. */
static void vertex(const workspace *ws, uint8_t index, int negate_odd, int16_t v[3])
{
    const uint8_t *at = ws->vertices + 4 * (index >> 1);
    uint8_t x = at[0];
    uint16_t shift = (uint16_t)(ws->scale - ws->shift);
    unsigned i;
    if ((index & 1) == (unsigned)negate_odd) x = (uint8_t)(0u - x);
    v[0] = (int8_t)x;
    v[1] = (int8_t)at[3];
    v[2] = (int8_t)at[2];
    for (i = 0; i < 3; ++i) v[i] = (int16_t)shift < 0 ? sar16(v[i], (uint16_t)(0u - shift)) : shl16(v[i], shift);
}

/*
 * 0x159c4: a crossing's ray parameter: (vertex - origin). normal over the
 * high word of 2 * direction. normal. Normals pair up (4 bytes per pair:
 * x, vertex, z, y in high bytes; the odd index mirrors x), from two
 * entries before the table. A zero or overflowing quotient gives 0x7fff or
 * 0x8000 by the signs (the divide-fault resume).
 */
static int16_t crossing(const workspace *ws, uint8_t index, int16_t normal[3], int16_t *denominator)
{
    const uint8_t *at = ws->normals + 2 * index - ((index & 1) ? 6 : 4);
    int16_t v[3];
    int32_t numerator;
    int16_t den;
    unsigned i;
    normal[0] = (int16_t)(uint16_t)(at[0] << 8);
    if (index & 1) normal[0] = neg16(normal[0]);
    normal[1] = (int16_t)(uint16_t)(at[3] << 8);
    normal[2] = (int16_t)(uint16_t)(at[2] << 8);
    vertex(ws, at[1], (index & 1) ? 0 : 1, v);
    numerator = 0;
    for (i = 0; i < 3; ++i)
        numerator = (int32_t)((uint32_t)numerator +
                              (uint32_t)((int32_t)(int16_t)(uint16_t)((uint16_t)v[i] - (uint16_t)ws->origin[i]) * normal[i]));
    {
        uint32_t s = 0;
        for (i = 0; i < 3; ++i) s += (uint32_t)((int32_t)ws->direction[i] * normal[i]);
        den = (int16_t)(uint16_t)((s << 1) >> 16);
    }
    *denominator = den;
    if (den != 0) {
        int64_t q = (int64_t)numerator / den;
        if (q >= PARAMETER_MIN && q <= PARAMETER_MAX) return (int16_t)q;
    }
    return ((uint16_t)((uint32_t)numerator >> 16) ^ (uint16_t)den) & 0x8000u ? (int16_t)PARAMETER_MIN
                                                                              : (int16_t)PARAMETER_MAX;
}

/* the parameter range [low, high] inside a list of
 * crossings (entering where the denominator is negative, for the first
 * kind, else positive). A crossing outside the range, or a negative upper
 * end, empties it (high 0x8000) and skips the rest of the list. */
static void crossings(walk *w, const workspace *ws, const uint8_t **cursor, int second, int16_t *low, int16_t *high)
{
    const uint8_t *at = *cursor;
    *high = PARAMETER_MAX;
    *low = PARAMETER_MIN;
    for (;;) {
        int16_t normal[3], den, t;
        uint8_t index = *at++;
        if (index == 0) break;
        t = crossing(ws, index, normal, &den);
        if ((den < 0) != second) {
            if (t > *high) goto empty;
            if (t < *low) continue;
            *low = t;
            if (!second) memcpy(w->p->crossing_normal, normal, sizeof normal);
        } else {
            if (t < *low) goto empty;
            if (t > *high) continue;
            *high = t;
            if (second) memcpy(w->p->crossing_normal, normal, sizeof normal);
            if (t < 0) goto empty;
        }
    }
    *cursor = at;
    return;
empty:
    *high = PARAMETER_MIN;
    while (*at++ != 0) {
    }
    *cursor = at;
}

static void walk_program(walk *w, workspace *ws, const uint8_t *cursor);

/* a child model, walked in its own workspace and painted into
 * this list. */
static void child(walk *w, workspace *ws, const uint8_t **cursor)
{
    workspace sub;
    const uint8_t *at = *cursor, *program;
    uint16_t model = (uint16_t)((at[0] << 8 | at[1]) << 1);
    uint8_t axes = at[2], index = at[3];
    int16_t v[3], o[3], d[3];
    uint16_t n;
    unsigned i;
    *cursor = at + 4;
    open_model(w->game, model, &sub, &program);
    sub.shift = ws->shift;
    sub.axes = axes;
    vertex(ws, index, 1, v);
    for (i = 0; i < 3; ++i) {
        o[i] = (int16_t)(uint16_t)((uint16_t)ws->origin[i] - (uint16_t)v[i]);
        d[i] = ws->direction[i];
    }
    if (axes == 0) {
        memcpy(sub.origin, o, sizeof o);
        memcpy(sub.direction, d, sizeof d);
    } else {
        if (axes & AXIS_NEGATE_X) { o[0] = neg16(o[0]); d[0] = neg16(d[0]); }
        if (axes & AXIS_NEGATE_Y) { o[1] = neg16(o[1]); d[1] = neg16(d[1]); }
        if (axes & AXIS_NEGATE_Z) { o[2] = neg16(o[2]); d[2] = neg16(d[2]); }
        for (i = 0; i < 3; ++i) {
            sub.origin[fe2_probe_axes[axes & AXIS_ROW][i]] = o[i];
            sub.direction[fe2_probe_axes[axes & AXIS_ROW][i]] = d[i];
        }
    }
    walk_program(w, &sub, program);

    /* The child's painted ranges, turned back by its axis code. */
    n = 0;
    for (;;) {
        const fe2_probe_node *node = &sub.list.node[n];
        uint16_t next = node->next, high = sub.list.node[next].position;
        uint8_t kind = (uint8_t)node->state, tag = (uint8_t)(node->state >> 8);
        if (kind == KIND_CLOSED || kind == KIND_END) {
            memcpy(v, node->vector, sizeof v);
            fe2_probe_permute(v, sub.axes);
            if (node->position != high) fe2_probe_paint_end(&ws->list, node->position, high, v, tag);
            break;
        }
        if (kind == KIND_PAINTED) {
            memcpy(v, sub.list.node[next].vector, sizeof v);
            fe2_probe_permute(v, sub.axes);
            if (node->position != high) fe2_probe_paint_open(&ws->list, node->position, high, v, tag);
        } else if (kind == KIND_LAST) {
            break;
        }
        n = next;
    }
}


static void walk_program(walk *w, workspace *ws, const uint8_t *cursor)
{
    for (;;) {
        uint8_t type = *cursor;
        int16_t low, high;
        uint16_t start;
        if (type == 0) return;
        cursor += 2;
        ws->tag = type;
        if (type & TYPE_CONDITION) {
            uint16_t word = w->game->condition_word(w->game->opaque, w->record, cursor[0]);
            uint8_t bit = cursor[1];
            unsigned n = bit & CONDITION_SHIFT;
            cursor += 2;
            if ((unsigned)(n >= 16 ? 0 : (word >> n) & 1u) != (unsigned)(bit >> 7)) {
                if ((type & TYPE_PAINTED) || !(type & TYPE_CHILD)) {
                    while (*cursor++ != 0) {
                    }
                } else {
                    cursor += 4;
                }
                continue;
            }
        }
        if ((type & (TYPE_CHILD | TYPE_PAINTED)) == TYPE_CHILD) {
            child(w, ws, &cursor);
            continue;
        }
        crossings(w, ws, &cursor, (type & (TYPE_CHILD | TYPE_PAINTED)) == TYPE_PAINTED, &low, &high);
        if (type & TYPE_CHILD) {
            /* 0xc0..0xff: the list ends at the start. */
            if (high < 0) continue;
            start = low < 0 ? 0 : (uint16_t)low;
            if (start != (uint16_t)high) fe2_probe_paint_end(&ws->list, start, (uint16_t)high, w->p->crossing_normal, type);
        } else if (type & TYPE_PAINTED) {
            if (high < 0) continue;
            start = low >= 2 ? (uint16_t)(low - 2) : 0;
            if (start != (uint16_t)high) fe2_probe_paint_open(&ws->list, start, (uint16_t)high, w->p->crossing_normal, type);
        } else {
            /* A negative end stops this walk. */
            if (high < 0) return;
            start = low < 0 ? 1 : (uint16_t)low;
            if (start != (uint16_t)high)
                fe2_probe_paint_closed(&ws->list, start, (uint16_t)high, w->p->crossing_normal, type);
        }
    }
}

void fe2_probe_geometry(fe2_scene_probe *p, const fe2_scene_probe_services *game, const fe2_probe_candidate *c)
{
    fe2_object *o = c->record;
    workspace ws;
    walk w;
    const uint8_t *program;
    const int16_t *basis = o->orientation;
    const fe2_probe_node *hit;
    int16_t v[3];
    uint16_t raise, n, previous;
    uint8_t class_b = CLASS_NONE;
    uint32_t distance;
    unsigned i;
    open_model(game, o->model, &ws, &program);
    for (i = 0; i < 3; ++i) v[i] = p->offset[i];
    ws.shift = p->offset_shift;
    /* The walk's exponent is at least the model's scale - 7. */
    if ((int16_t)(uint16_t)(p->offset_shift + SCALE_BIAS) < (int16_t)ws.scale) {
        raise = (uint16_t)(ws.scale - (uint16_t)(p->offset_shift + SCALE_BIAS));
        ws.shift = (uint16_t)(ws.shift + raise);
        for (i = 0; i < 3; ++i) v[i] = sar16(v[i], raise);
    }
    if (p->candidate_frame != 0 && o == p->parent) {
        /* Docked in the candidate: already in its frame. */
        memcpy(ws.origin, v, sizeof v);
        memcpy(ws.direction, c->direction, sizeof ws.direction);
    } else {
        if (p->candidate_frame == 0 && o->station_slot != 0) {
            game->rebuild_secondary_basis(game->opaque, o);
            basis = o->alternate_orientation;
        }
        fe2_probe_by_columns(basis, v, ws.origin);
        fe2_probe_by_columns(basis, c->direction, ws.direction);
    }
    p->candidate = o;
    w.p = p;
    w.game = game;
    w.record = o;
    walk_program(&w, &ws, program);

    /* The first closed node, and the tag of the node before it. */
    n = 0;
    previous = 0;
    while (!(ws.list.node[n].state & CLOSED_BIT)) {
        previous = n;
        n = ws.list.node[n].next;
    }
    if (n != 0) class_b = (uint8_t)(ws.list.node[previous].state >> 8);
    hit = &ws.list.node[n];
    distance = (uint32_t)(int32_t)(int16_t)hit->position;
    if ((int16_t)hit->position < 0) {
        distance = 0xffffffffu;
    } else {
        for (i = 0; i < (ws.shift & 63u); ++i) {
            uint32_t d = distance;
            distance = d << 1;
            if (((d >> 31) ^ (d >> 30)) & 1u) {
                distance = 0xffffffffu;
                break;
            }
        }
    }
    fe2_probe_keep_nearer(p, o, distance, hit->position, ws.shift, (uint8_t)(hit->state >> 8), class_b, hit->vector);
}
