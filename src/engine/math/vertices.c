/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the model vertex program. */
#include "math3.h"

enum {
    SCREEN_CENTRE_X = 0xa0, SCREEN_CENTRE_Y = 0x54, NEAREST_DEPTH = 0x40,
    WEIGHT_LIMIT = 0x3f00, RESCALE_UNIT = 0x7f00, MATRIX_SHIFT = 0x17
};

/* Op kinds (the signed high byte of the op's first word). */
enum {
    KIND_POINT = 2, KIND_SCREEN_MIDPOINT = 4, KIND_NEGATION = 6, KIND_NOISE = 8, KIND_MIRROR_POINT = 10,
    KIND_AVERAGE = 12, KIND_SCREEN_AVERAGE = 14, KIND_PARALLELOGRAM = 16, KIND_SUM = 18, KIND_LERP = 20
};

static void generate(const fe2_vertex_frame *f, int index, int requested, uint16_t mode);

static fe2_vertex *vertex(const fe2_vertex_frame *f, int index) { return f->vertices + index; }

/* Project the view position; too near gives 0x8002, 0x8002. Stamped. */
static void project(const fe2_vertex_frame *f, fe2_vertex *v)
{
    if (v->view[2] < NEAREST_DEPTH) {
        v->screen[0] = v->screen[1] = (int16_t)FE2_VERTEX_TOO_NEAR;
    } else {
        fe2_point p = fe2_math_project(v->view[0], v->view[1], v->view[2]);
        v->screen[0] = (int16_t)(p.x + SCREEN_CENTRE_X);
        v->screen[1] = (int16_t)(SCREEN_CENTRE_Y - p.y);
    }
    v->state = (uint16_t)((v->state & 0xff00u) | f->stamp);
}

static void add_offset(const fe2_vertex_frame *f, fe2_vertex *v)
{
    unsigned i;
    for (i = 0; i < 3; ++i) v->view[i] = (int32_t)((uint32_t)v->model[i] + (uint32_t)f->offset[i]);
}

/* Add the frame offset; project unless the vertex is model-only. */
static void finish(const fe2_vertex_frame *f, fe2_vertex *v)
{
    add_offset(f, v);
    if ((int16_t)v->state < 0) return;
    project(f, v);
}

/* A reference built (model) on demand. */
static fe2_vertex *built(const fe2_vertex_frame *f, int index)
{
    if (vertex(f, index)->state == 0) generate(f, index, index, FE2_VERTEX_MODEL_ONLY);
    return vertex(f, index);
}

void fe2_math_vertex_refresh(const fe2_vertex_frame *f, int index)
{
    fe2_vertex *v = vertex(f, index);
    if (v->state == 0) {
        generate(f, index, index, FE2_VERTEX_PROJECTED);
        return;
    }
    add_offset(f, v);
    project(f, v);
}

/* A reference projected in this frame on demand. */
static fe2_vertex *projected(const fe2_vertex_frame *f, int index)
{
    if ((uint8_t)vertex(f, index)->state != f->stamp) fe2_math_vertex_refresh(f, index);
    return vertex(f, index);
}

void fe2_math_vertex_build(const fe2_vertex_frame *f, int index)
{
    generate(f, index, index, FE2_VERTEX_MODEL_ONLY);
}

void fe2_math_vertex_place(const fe2_vertex_frame *f, int index)
{
    fe2_vertex *v = vertex(f, index);
    if (v->state == 0) {
        generate(f, index, index, FE2_VERTEX_MODEL_ONLY);
        return;
    }
    add_offset(f, v);
}

/* The packed point (bytes as the high bytes of signed words) through the
 * frame matrix, shifted down by 0x17 - scale; also each output's first-
 * column term (for the mirror). */
static void transform(const fe2_vertex_frame *f, int8_t x, int8_t y, int8_t z, int32_t out[3], int32_t column[3])
{
    int32_t p[3];
    unsigned shift = (unsigned)(uint16_t)(MATRIX_SHIFT - f->scale) & 31u, i;
    p[0] = (int32_t)x * 256;
    p[1] = (int32_t)y * 256;
    p[2] = (int32_t)z * 256;
    for (i = 0; i < 3; ++i) {
        uint32_t sum = (uint32_t)(f->matrix[i] * p[0]) + (uint32_t)(f->matrix[3 + i] * p[1]) +
                       (uint32_t)(f->matrix[6 + i] * p[2]);
        out[i] = (int32_t)sum >> shift;
        column[i] = (int32_t)(uint32_t)(f->matrix[i] * p[0]) >> shift;
    }
}

/* The noise generator: the next value is the old state rotated by 16
 * bits, and the state advances by its previous value (a Fibonacci-like
 * sequence). */
static int32_t noise(const fe2_vertex_frame *f, unsigned shift)
{
    uint32_t a = f->work->noise_a, value = a >> 16 | a << 16;
    f->work->noise_a = a + f->work->noise_b;
    f->work->noise_b = value;
    return (int32_t)value >> (shift & 31u);
}

/* Screen midpoint of two projected references, keeping the nearer depth. */
static void screen_midpoint(const fe2_vertex_frame *f, fe2_vertex *target, int first, int second)
{
    fe2_vertex *a = projected(f, first), *b = projected(f, second);
    target->screen[0] = (int16_t)(((int32_t)a->screen[0] + b->screen[0]) >> 1);
    target->screen[1] = (int16_t)(((int32_t)a->screen[1] + b->screen[1]) >> 1);
    target->view[2] = a->view[2] < b->view[2] ? a->view[2] : b->view[2];
}

/* The smallest shift that brings each difference below 0x3f00. */
static unsigned difference_shift(const int32_t d[3])
{
    unsigned shift = 0, i;
    static const unsigned order[3] = {2, 0, 1};
    for (i = 0; i < 3; ++i) {
        uint32_t m = d[order[i]] < 0 ? 0u - (uint32_t)d[order[i]] : (uint32_t)d[order[i]];
        while ((m >> shift) >= WEIGHT_LIMIT) ++shift;
    }
    return shift;
}

static uint16_t weight(const fe2_vertex_frame *f, uint8_t code)
{
    uint16_t w;
    if (code & 0x80) w = (code & 0x40) ? f->frame_words[code & 0x3f] : f->object_words[code & 0x3f];
    else w = (code & 0x40) ? (uint16_t)((code & 0x3f) << 10) : (uint16_t)(code & 0x3f);
    return (uint16_t)(w >> 1);
}

/* target = from + (to - from) * weight / 2^15, the difference first
 * reduced below 0x3f00. */
static void lerp(const fe2_vertex_frame *f, fe2_vertex *target, int from, int to, uint8_t code)
{
    fe2_vertex *a = projected(f, from), *b;
    int32_t d[3];
    int16_t w;
    unsigned shift, i;
    b = projected(f, to);
    w = (int16_t)weight(f, code);
    for (i = 0; i < 3; ++i) d[i] = (int32_t)((uint32_t)b->model[i] - (uint32_t)a->model[i]);
    shift = difference_shift(d);
    for (i = 0; i < 3; ++i) {
        int32_t p = (int32_t)(int16_t)(uint16_t)(d[i] >> shift) * w;
        if (shift > 15) p = (int32_t)((uint32_t)p << (shift - 15));
        else p >>= 15 - shift;
        target->model[i] = (int32_t)((uint32_t)a->model[i] + (uint32_t)p);
    }
}

/* Vertex k (k < 0) is the parent's: copy its projection and view, and the
 * model is the view less this frame's offset. */
static void inherit(const fe2_vertex_frame *f, int requested)
{
    const fe2_vertex *source = f->inherited[requested];
    fe2_vertex *v = vertex(f, requested);
    unsigned i;
    v->screen[0] = source->screen[0];
    v->screen[1] = source->screen[1];
    for (i = 0; i < 3; ++i) {
        v->view[i] = source->view[i];
        v->model[i] = (int32_t)((uint32_t)source->view[i] - (uint32_t)f->offset[i]);
    }
    v->spare = source->spare;
    v->state = source->state;
}

static int8_t sbyte(uint8_t b) { return (int8_t)b; }

/*
 * Build the pair holding `index` from its op; `requested` is the vertex
 * finished at the end (projected unless model-only).
 */
static void generate(const fe2_vertex_frame *f, int index, int requested, uint16_t mode)
{
    int even = index & ~1, i;
    const uint8_t *op;
    fe2_vertex *v, *pair;
    int8_t kind;
    int32_t out[3], column[3];
    if (index < 0) {
        inherit(f, requested);
        return;
    }
    op = f->program + 2 * even;
    v = vertex(f, even);
    pair = v + 1;
    kind = sbyte(op[1]);
    if (kind <= KIND_POINT || (kind <= KIND_MIRROR_POINT && kind > KIND_NOISE)) {
        v->state = pair->state = mode;
        transform(f, sbyte(op[0]), sbyte(op[3]), sbyte(op[2]), out, column);
        for (i = 0; i < 3; ++i) {
            v->model[i] = out[i];
            pair->model[i] = (int32_t)((uint32_t)out[i] - (uint32_t)column[i] *
                                       (kind <= KIND_POINT ? 2u : 1u));
        }
        finish(f, vertex(f, requested));
        return;
    }
    if (kind <= KIND_SCREEN_MIDPOINT) {
        /* The pair's references are read back from the shared word after
         * the first two are projected: an op built on the way (a midpoint
         * or a two-reference op) has replaced it with its own. */
        uint16_t pair_refs;
        v->state = (uint16_t)((v->state & 0xff00u) | f->stamp);
        pair->state = (uint16_t)((pair->state & 0xff00u) | f->stamp);
        f->work->references = (uint16_t)(op[2] | op[3] << 8);
        screen_midpoint(f, v, sbyte(op[3]), sbyte(op[2]));
        pair_refs = (uint16_t)(f->work->references ^ 0x101u);
        screen_midpoint(f, pair, sbyte((uint8_t)(pair_refs >> 8)), sbyte((uint8_t)pair_refs));
        return;
    }
    if (kind <= KIND_NEGATION) {
        int ref = (int16_t)(uint16_t)(op[2] | op[3] << 8);
        fe2_vertex *r;
        v->state = pair->state = mode;
        r = built(f, ref);
        for (i = 0; i < 3; ++i) v->model[i] = (int32_t)(0u - (uint32_t)r->model[i]);
        r = built(f, (int16_t)(uint16_t)(ref ^ 1));
        for (i = 0; i < 3; ++i) pair->model[i] = (int32_t)(0u - (uint32_t)r->model[i]);
        finish(f, vertex(f, requested));
        return;
    }
    if (kind <= KIND_NOISE) {
        int ref = (int16_t)(uint16_t)(op[2] | op[3] << 8);
        fe2_vertex *r;
        int32_t p[3];
        v->state = pair->state = mode;
        r = built(f, ref);
        for (i = 0; i < 3; ++i) {
            p[i] = noise(f, op[0]);
            v->model[i] = (int32_t)((uint32_t)r->model[i] + (uint32_t)p[i]);
        }
        r = built(f, (int16_t)(uint16_t)(ref ^ 1));
        pair->model[0] = (int32_t)((uint32_t)r->model[0] + (uint32_t)p[2]);
        pair->model[1] = (int32_t)((uint32_t)r->model[1] + (uint32_t)p[0]);
        pair->model[2] = (int32_t)((uint32_t)r->model[2] + (uint32_t)p[1]);
        finish(f, vertex(f, requested));
        return;
    }
    {
        /* Ops on references: for the odd vertex of the pair the references'
         * parity flips (and the third reference's for the parallelogram). */
        int swapped = requested != even;
        uint8_t low = op[0], first = op[2], second = op[3];
        fe2_vertex *target = vertex(f, requested), *a, *b;
        if (swapped) first ^= 1, second ^= 1;
        f->work->references = (uint16_t)(first | second << 8);
        target->state = mode;
        if (kind <= KIND_AVERAGE) {
            a = built(f, sbyte(second));
            b = built(f, sbyte(first));
            for (i = 0; i < 3; ++i)
                target->model[i] = (int32_t)((uint32_t)a->model[i] + (uint32_t)b->model[i]) >> 1;
        } else if (kind <= KIND_SCREEN_AVERAGE) {
            a = projected(f, sbyte(second));
            b = projected(f, sbyte(first));
            target->screen[0] = (int16_t)(((int32_t)a->screen[0] + b->screen[0]) >> 1);
            target->screen[1] = (int16_t)(((int32_t)a->screen[1] + b->screen[1]) >> 1);
            target->view[2] = b->view[2] < a->view[2] ? b->view[2] : a->view[2];
            target->view[0] = b->view[0];
            target->view[1] = b->view[1];
            target->state = (uint16_t)((target->state & 0xff00u) | f->stamp);
            return;
        } else if (kind <= KIND_PARALLELOGRAM) {
            fe2_vertex *c;
            if (swapped) low ^= 1;
            a = built(f, sbyte(first));
            b = built(f, sbyte(second));
            c = built(f, sbyte(low));
            for (i = 0; i < 3; ++i)
                target->model[i] = (int32_t)((uint32_t)b->model[i] - (uint32_t)a->model[i] + (uint32_t)c->model[i]);
        } else if (kind <= KIND_SUM) {
            a = built(f, sbyte(first));
            b = built(f, sbyte(second));
            for (i = 0; i < 3; ++i) target->model[i] = (int32_t)((uint32_t)a->model[i] + (uint32_t)b->model[i]);
        } else if (kind <= KIND_LERP) {
            lerp(f, target, sbyte(second), sbyte(first), low);
        } else {
            /* A normal: the packed point rescaled to the object's size,
             * the pair its negation (only the pair's state is set). */
            fe2_vertex *base = vertex(f, even), *mirror = base + 1;
            uint32_t multiplier;
            unsigned shift = (unsigned)(uint16_t)(MATRIX_SHIFT - f->scale) & 31u;
            mirror->state = mode;
            transform(f, sbyte(low), sbyte(op[3]), sbyte(op[2]), out, column);
            multiplier = ((uint32_t)f->object_size * RESCALE_UNIT) >> shift;
            fe2_math_rescale32(out, multiplier);
            for (i = 0; i < 3; ++i) {
                base->model[i] = out[i];
                mirror->model[i] = (int32_t)(0u - (uint32_t)out[i]);
            }
        }
        finish(f, target);
    }
}
