/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* model contact. The target model's
 * contact program is a list of records over the model's planes and
 * nested models, run on the record's position in the target's frame. */
#include "contact_internal.h"

enum {
    /* Descriptor words. */
    VERTICES = 0x02, NORMALS = 0x06, SCALE = 0x0a, PROGRAM = 0x1a,
    /* Program record tags. */
    CONDITIONAL = 0x20, ALTERNATE = 0x40, NESTED = 0x80,
    FINAL = 0x80,                      /* a result ending the program */
    EXTENDED = 0x80,                   /* record flags: walk the extension offsets */
    QUERY_PRECISION = 7, RADIUS_EXPONENT = 0x10, EXTENSION_EXPONENT = 0x17,
    COUNT_MASK = 0x3f, SHIFT_MASK = 0x1f, REFERENCE_MASK = 0xfffd
};

/* The 22-byte frame a program runs with. */
typedef struct query {
    int16_t at[3];                     /* the query point, in the model's units by 2^exponent */
    const uint8_t *normals, *vertices; /* descriptor + its +0x6 and +0x2 words */
    uint16_t scale, exponent;          /* the model's scale (+0xa); the query's exponent */
} query;

/* The evaluation: AL < 0 a list rejected the point, 0 a final nested
 * result, 1 the program ended; DL the reporting record's tag. */
typedef struct outcome {
    int value;
    uint8_t tag;
} outcome;

static int16_t sar16(int16_t v, unsigned n) { return (int16_t)(v >> (n > 15 ? 15 : n)); }
static int16_t shl16(int16_t v, unsigned n) { return (int16_t)(uint16_t)((uint32_t)(uint16_t)v << n); }
static int32_t sar32(int32_t v, unsigned n) { return v >> (n > 31 ? 31 : n); }

static void set_frame(query *q, const uint8_t *model)
{
    q->normals = model + fe2_contact_word(model + NORMALS);
    q->vertices = model + fe2_contact_word(model + VERTICES);
    q->scale = fe2_contact_word(model + SCALE);
}

/* A model point (signed bytes x, -, z, y; the odd reference mirrors x)
 * in query units: by the model scale less the query exponent, a word
 * shift with the 386's 5-bit count. */
static void point(const query *q, const uint8_t *vertex, int mirror, int16_t out[3])
{
    int16_t d = (int16_t)(uint16_t)(q->scale - q->exponent);
    unsigned i;
    out[0] = (int8_t)(mirror ? (uint8_t)(0u - vertex[0]) : vertex[0]);
    out[1] = (int8_t)vertex[3];
    out[2] = (int8_t)vertex[2];
    for (i = 0; i < 3; ++i)
        out[i] = d < 0 ? sar16(out[i], (unsigned)(uint16_t)-d & SHIFT_MASK) : shl16(out[i], (unsigned)d & SHIFT_MASK);
}

/* plane `reference` (normals nx, anchor, nz, ny as high bytes,
 * pairs from the reference's half, the odd one mirrored in x) against the
 * point: whether -2 (n. (anchor - point)), a wrapping dword, is at most
 * the radius. The anchor's x mirrors when its parity differs from the
 * plane's. */
static int within(const query *q, uint8_t reference, int32_t radius)
{
    uint16_t r = (uint16_t)(reference << 1), a;
    const uint8_t *normal = q->normals + (r & REFERENCE_MASK) - 4;
    int16_t n[3], p[3];
    uint32_t sum = 0;
    unsigned i;
    n[0] = (int16_t)(uint16_t)(normal[0] << 8);
    n[1] = (int16_t)(uint16_t)(normal[3] << 8);
    n[2] = (int16_t)(uint16_t)(normal[2] << 8);
    if (r & 2) n[0] = (int16_t)(uint16_t)(0u - (uint16_t)n[0]);
    a = (uint16_t)(normal[1] << 1);
    point(q, q->vertices + (a & REFERENCE_MASK), ((a & 2) != 0) != ((r & 2) != 0), p);
    for (i = 0; i < 3; ++i) sum += (uint32_t)((int32_t)(int16_t)(uint16_t)(p[i] - q->at[i]) * n[i]);
    return (int32_t)(0u - (sum << 1)) <= radius;
}

static outcome run(fe2_contact *c, const fe2_contact_services *game, const fe2_object *t, const query *q,
                   const uint8_t *p, int32_t radius);

/* a nested model at a vertex of this one (one-based pairs, the
 * odd reference mirrored in x), the query moved to it and permuted by the
 * transform's low bits (negating x, y, z for bits 0x20, 0x10, 8). */
static outcome nested(fe2_contact *c, const fe2_contact_services *game, const fe2_object *t, const query *q,
                      uint16_t index, uint8_t transform, uint8_t reference, int32_t radius)
{
    const uint8_t *model = game->model(game->opaque, index);
    uint16_t r = (uint16_t)(reference << 1);
    int16_t v[3];
    query child;
    unsigned i;
    set_frame(&child, model);
    child.exponent = q->exponent;
    point(q, q->vertices + (r & REFERENCE_MASK), (r & 2) != 0, v);
    for (i = 0; i < 3; ++i) v[i] = (int16_t)(uint16_t)(q->at[i] - v[i]);
    if (transform == 0) {
        for (i = 0; i < 3; ++i) child.at[i] = v[i];
    } else {
        static const uint8_t negate[3] = {0x20, 0x10, 0x08};
        const uint8_t *axes = fe2_contact_axes[(transform & 7) % 6];
        for (i = 0; i < 3; ++i)
            child.at[axes[i]] = (transform & negate[i]) ? (int16_t)(uint16_t)(0u - (uint16_t)v[i]) : v[i];
    }
    return run(c, game, t, &child, model + fe2_contact_word(model + PROGRAM), radius);
}

static const uint8_t *skip_list(const uint8_t *p)
{
    while (*p++ != 0) {
    }
    return p;
}

/*
 * 0x12dc7. Records: a tag (0 ends the program), a result, for tag 0x20 a
 * model variable index and a shift (bit 7 inverting) whose selected bit
 * must be clear, then a zero-terminated plane list or a nested model.
 * Plane lists (tag 0x40 clear) reject the point (the result's low seven
 * bits collected) when a plane is beyond; with tag 0x40 one plane within
 * passes the record and none rejects. Tag 0xc0 applies the result when all
 * planes are within; tag 0x80 runs a nested model (its tags collected in
 * the classification), whose rejection rejects and whose final result
 * applies. An applied result with bit 7 ends the program.
 */
static outcome run(fe2_contact *c, const fe2_contact_services *game, const fe2_object *t, const query *q,
                   const uint8_t *p, int32_t radius)
{
    outcome out;
    for (;;) {
        uint8_t tag = *p++, result, reference;
        if (tag == 0) {
            out.value = 1;
            out.tag = 0;
            return out;
        }
        result = *p++;
        if (tag & CONDITIONAL) {
            uint16_t field = game->model_variable(game->opaque, t, p[0]);
            uint8_t shift = p[1];
            unsigned count = shift & COUNT_MASK & SHIFT_MASK;
            unsigned bit = count > 15 ? 0u : (unsigned)(field >> count) & 1u;
            p += 2;
            if (bit != ((shift & 0x80) ? 1u : 0u)) {
                if (!(tag & ALTERNATE) && (tag & NESTED)) p += 4;
                else p = skip_list(p);
                continue;
            }
        }
        if (tag & NESTED) {
            if (!(tag & ALTERNATE)) {
                outcome inner = nested(c, game, t, q, (uint16_t)((p[0] << 8 | p[1]) << 1), p[2], p[3], radius);
                p += 4;
                c->classification |= inner.tag;
                if (inner.value < 0) goto reject;
                if (inner.value > 0) continue;
            } else {
                while ((reference = *p++) != 0)
                    if (!within(q, reference, radius)) break;
                if (reference != 0) {
                    p = skip_list(p);
                    continue;
                }
            }
            c->model_result |= result;
            if (result & FINAL) {
                out.value = 0;
                out.tag = tag;
                return out;
            }
        } else if (tag & ALTERNATE) {
            for (;;) {
                if ((reference = *p++) == 0) goto reject;
                if (within(q, reference, radius)) break;
            }
            p = skip_list(p);
        } else {
            while ((reference = *p++) != 0)
                if (!within(q, reference, radius)) goto reject;
        }
        continue;
    reject:
        c->model_result |= result & 0x7f;
        out.value = -1;
        out.tag = tag;
        return out;
    }
}

/* the point (32-bit components) reduced to words with the
 * exponent kept at least the model scale - 7, the radius moved to that
 * exponent (a dword, shifted by the exponent - 0x10), and the point into
 * the target's frame unless the record is in its basis frame (its parent). */
static outcome evaluate(fe2_contact *c, const fe2_contact_services *game, const fe2_object *o, const fe2_object *t,
                        const int32_t at[3], uint32_t *radius)
{
    const uint8_t *model = game->model(game->opaque, t->model);
    int16_t w[3], k, n;
    unsigned count, i;
    query q;
    set_frame(&q, model);
    count = fe2_contact_word_shift(at);
    for (i = 0; i < 3; ++i) w[i] = (int16_t)(uint16_t)(uint32_t)(at[i] >> count);
    q.exponent = (uint16_t)count;
    k = (int16_t)(uint16_t)(count + QUERY_PRECISION - q.scale);
    if (k < 0) {
        uint16_t up = (uint16_t)(0u - (uint16_t)k);
        q.exponent = (uint16_t)(q.exponent + up);
        for (i = 0; i < 3; ++i) w[i] = sar16(w[i], up & SHIFT_MASK);
    }
    n = (int16_t)(uint16_t)(q.exponent - RADIUS_EXPONENT);
    if (n > 0) *radius = n > 31 ? 0 : *radius >> n;
    else if (n < 0) *radius = (uint16_t)-n > 31 ? 0 : *radius << (uint16_t)-n;
    if (o->station_slot != 0 && o->parent_handle == t->handle) {
        for (i = 0; i < 3; ++i) q.at[i] = w[i];
    } else {
        for (i = 0; i < 3; ++i) {
            uint32_t sum = 0;
            unsigned j;
            for (j = 0; j < 3; ++j) sum += (uint32_t)((int32_t)w[j] * t->orientation[3 * j + i]);
            q.at[i] = (int16_t)(uint16_t)((sum << 1) >> 16);
        }
    }
    return run(c, game, t, &q, model + fe2_contact_word(model + PROGRAM), (int32_t)*radius);
}

/* The basis word the extension selector names: words k, k + 3, k + 6 (k
 * 3 reads the word after the basis). */
static int16_t basis_word(const fe2_object *o, unsigned i)
{
    return i < 9 ? o->orientation[i] : (int16_t)(uint16_t)(o->rebuild_budget | o->render_state << 8);
}

static void publish(fe2_contact *c, fe2_object *t, uint8_t tag)
{
    c->handle = t->handle;
    c->reported = t;
    c->result = c->model_result;
    c->classification = tag;
}

/*
 * 0x12888, 0x12ac6. The program runs on the original relative position
 * (its low dwords) with the record's +0x110 dword as the radius. A program
 * that ended, or a final result, reports the target; a rejection reports
 * it when results were collected. Results with bit 7 (or none) then take
 * the general response. Otherwise a record with extension offsets (+0x118
 * bit 7) walks them backward from its program: a length word (zero ends
 * the walk) and a selector word before it naming a basis column (bits 1-2)
 * and its sign (bit 0). The column times the length, shifted down by 0x17
 * less the record's model scale, moves the point; the selector shifted
 * down the same (and once more) is the radius.
 */
uint8_t fe2_contact_model(fe2_contact *c, const fe2_contact_services *game, fe2_object *o, fe2_object *t,
                          const int64_t relative[3])
{
    const uint8_t *model, *cursor;
    int32_t origin[3], at[3];
    uint32_t radius = (uint32_t)o->model_scale_b[0] | (uint32_t)o->model_scale_b[1] << 16;
    unsigned i;
    for (i = 0; i < 3; ++i) at[i] = origin[i] = (int32_t)(uint32_t)(uint64_t)relative[i];
    c->model_result = 0;
    model = game->model(game->opaque, o->model);
    c->model_scale = fe2_contact_word(model + SCALE);
    cursor = model + fe2_contact_word(model + PROGRAM);
    for (;;) {
        outcome out = evaluate(c, game, o, t, at, &radius);
        uint16_t length, selector;
        unsigned column, n;
        if (out.value < 0) {
            if (c->model_result != 0) publish(c, t, out.tag);
        } else {
            if (out.value > 0) c->model_result |= FINAL;
            publish(c, t, out.tag);
            if (c->model_result == 0 || (c->model_result & FINAL)) return fe2_contact_respond(c, game, o, t);
        }
        if (!(o->flags & EXTENDED)) return c->result;
        cursor -= 2;
        length = fe2_contact_word(cursor);
        if (length == 0) return c->result;
        cursor -= 2;
        selector = fe2_contact_word(cursor);
        column = (selector & 6u) >> 1;
        n = (uint16_t)(EXTENSION_EXPONENT - c->model_scale) & COUNT_MASK;
        for (i = 0; i < 3; ++i) {
            int16_t b = basis_word(o, column + 3 * i);
            if (selector & 1) b = (int16_t)(uint16_t)(0u - (uint16_t)b);
            at[i] = (int32_t)((uint32_t)origin[i] + (uint32_t)sar32((int32_t)b * (int16_t)length, n));
        }
        radius = n > 31 ? 0 : (((uint32_t)selector << 16) >> n) >> 1;
    }
}
