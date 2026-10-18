/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Opcode 5: value-token paths (0x67e7..0x6df9, skip table 0x2902) and the
 * transformed-vertex ellipse token (0x6dfa..0x6f90 with 0xc092..0xc331). */
#include "path_internal.h"
#include <string.h>

/* two cubics (A,B,E,D) and (A,C,F,D) around a camera-space centre in
 * the plane perpendicular to axis. A quotient overflow (divide resume 0xc32f)
 * or a rejected projection discards the ellipse. */
static int transformed_ellipse(const int32_t centre[3], const int16_t axis[3], uint16_t radius,
    fe2_screen_point points[6])
{
    int16_t u[3], w[3];
    uint32_t square = (uint32_t)((int32_t)axis[0] * axis[0]) + (uint32_t)((int32_t)axis[1] * axis[1]);
    if (square < 0x9c40u) fe2_path_round_vectors(radius, u, w);
    else {
        int16_t s = fe2_path_sw(fe2_math_square_root32(square)), q0, q1;
        uint16_t scale = (uint16_t)(radius << 1), third;
        if (!fe2_path_quotient((int32_t)((uint32_t)(uint16_t)axis[0] << 16) >> 2, s, &q0)) return 0;
        u[1] = fe2_path_high1((int32_t)q0 * fe2_path_sw(scale));
        if (!fe2_path_quotient((int32_t)((uint32_t)(uint16_t)axis[1] << 16) >> 2, s, &q1)) return 0;
        u[0] = fe2_path_sw(0u - (uint16_t)fe2_path_high1((int32_t)q1 * fe2_path_sw(scale)));
        u[2] = 0;
        third = (uint16_t)((0x5554u * (uint32_t)scale) >> 15);
        if (!fe2_path_quotient((int32_t)axis[0] * axis[2], s, &q0)) return 0;
        w[0] = fe2_path_high1((int32_t)q0 * fe2_path_sw(third));
        if (!fe2_path_quotient((int32_t)axis[1] * axis[2], s, &q1)) return 0;
        w[1] = fe2_path_high1((int32_t)q1 * fe2_path_sw(third));
        w[2] = fe2_path_high1((int32_t)fe2_path_sw(0u - (uint16_t)s) * fe2_path_sw(third));
    }
    return fe2_path_ellipse_points(centre, u, w, points);
}

typedef struct path_run {
    fe2_render_program_drawing *d;
    fe2_render_program_state *s;
    fe2_render_bytes ops;
    size_t words, pos;
    fe2_path_builder b;
    int abort_mode, nested;
    unsigned axis;
    fe2_render_program_status status;
} path_run;

static int next(path_run *r, uint16_t *value)
{
    if (r->pos >= r->words) { r->status = FE2_RENDER_PROGRAM_TRUNCATED; return 0; }
    *value = fe2_path_word(r->ops, r->pos++);
    return 1;
}
static int ref(path_run *r, int index)
{
    if (fe2_path_resolve(r->d, index)) { r->status = FE2_RENDER_PROGRAM_BAD_VALUE; return 0; }
    return 1;
}
static int emit(path_run *r, fe2_render_program_status status)
{
    if (status != FE2_RENDER_PROGRAM_COMPLETE) { r->status = status; return 0; }
    return 1;
}
/* Abort mode rejects a near-plane crossing; nested closing calls
 * would resume inside the caller with a consumed terminator (domain limit). */
static int abort_path(path_run *r)
{
    if (r->nested) { r->status = FE2_RENDER_PROGRAM_BAD_VALUE; return 0; }
    r->status = FE2_RENDER_PROGRAM_COMPLETE;
    return 2;
}
static int last_vertex(path_run *r)
{
    if (!r->s->path.last_set) { r->status = FE2_RENDER_PROGRAM_BAD_VALUE; return 0; }
    return 1;
}
/* re-enter the visible side from the last (behind) vertex. */
static int reenter(path_run *r, int v)
{
    fe2_render_path_state *s = &r->s->path;
    fe2_screen_point p;
    int previous;
    if (!last_vertex(r)) return 0;
    previous = s->last;
    s->last = v; s->clipped = 0;
    p = fe2_path_near(r->d, previous, v);
    if (!s->open) {
        s->open = 0xffffu; s->entry = p;
        return emit(r, fe2_path_edge(&r->b, p, fe2_path_screen(r->d, v)));
    }
    return emit(r, fe2_path_line_to(&r->b, p)) && emit(r, fe2_path_line_to(&r->b, fe2_path_screen(r->d, v)));
}
/* continue the open path to v. Returns 0 error, 1 ok, 2 abort. */
static int point_to(path_run *r, int v)
{
    fe2_render_path_state *s = &r->s->path;
    if (!s->clipped) {
        if (fe2_path_depth(r->d, v) < 64) {
            fe2_screen_point p;
            if (r->abort_mode) return abort_path(r);
            if (!last_vertex(r)) return 0;
            p = fe2_path_near(r->d, v, s->last);
            s->last = v; s->last_set = 1; s->clipped = 0xffffu;
            return emit(r, fe2_path_line_to(&r->b, p));
        }
        s->last = v; s->last_set = 1;
        return emit(r, fe2_path_line_to(&r->b, fe2_path_screen(r->d, v)));
    }
    if (fe2_path_depth(r->d, v) < 64) { s->last = v; s->last_set = 1; return 1; }
    return reenter(r, v);
}
/* an explicit edge between a and b, clipped at depth 64. */
static int pair(path_run *r, int a, int b)
{
    fe2_render_path_state *s = &r->s->path;
    s->last = b; s->last_set = 1; s->open = 0xffffu;
    if (fe2_path_depth(r->d, a) < 64) {
        fe2_screen_point p;
        if (r->abort_mode) return abort_path(r);
        if (fe2_path_depth(r->d, b) < 64) { s->open = 0; s->clipped = 0xffffu; return 1; }
        p = fe2_path_near(r->d, a, b);
        s->entry = p; s->clipped = 0;
        return emit(r, fe2_path_edge(&r->b, p, fe2_path_screen(r->d, b)));
    }
    if (fe2_path_depth(r->d, b) < 64) {
        if (r->abort_mode) return abort_path(r);
        s->clipped = 0xffffu;
        return emit(r, fe2_path_edge(&r->b, fe2_path_screen(r->d, a), fe2_path_near(r->d, b, a)));
    }
    s->clipped = 0;
    return emit(r, fe2_path_edge(&r->b, fe2_path_screen(r->d, a), fe2_path_screen(r->d, b)));
}
/* three more references after 0x1f57; a cubic when all are visible. */
static int quad(path_run *r, int v57, unsigned low)
{
    fe2_render_path_state *s = &r->s->path;
    uint16_t w;
    int v59 = fe2_path_sb(low), v5b, v55;
    if (!ref(r, v59) || !next(r, &w)) return 0;
    v5b = fe2_path_sb(w >> 8);
    if (!ref(r, v5b)) return 0;
    v55 = fe2_path_sb(w);
    if (!ref(r, v55)) return 0;
    if (fe2_path_depth(r->d, v55) < 64 || fe2_path_depth(r->d, v57) < 64 || fe2_path_depth(r->d, v59) < 64 || fe2_path_depth(r->d, v5b) < 64)
        return pair(r, v57, v55);
    s->last = v55; s->last_set = 1; s->clipped = 0; s->open = 0xffffu;
    return emit(r, fe2_path_curve(&r->b, fe2_path_screen(r->d, v57), fe2_path_screen(r->d, v59), fe2_path_screen(r->d, v5b), fe2_path_screen(r->d, v55)));
}
/* ellipse around a vertex, perpendicular to a transformed normal. */
static int transformed(path_run *r, uint16_t token)
{
    fe2_render_path_state *s = &r->s->path;
    const fe2_render_execution_frame *f = r->d->execution->frames + r->d->execution->depth - 1;
    fe2_render_vertex v;
    fe2_screen_point p[6];
    uint16_t w, radius;
    unsigned shift = f->transform.exponent & 31u, selector, i;
    int index = fe2_path_sb(token);
    if (!ref(r, index)) return 0;
    fe2_path_current(r->d, index, &v);
    if (v.camera[2] <= 64) { r->pos += 1; s->open = 0; return 1; }
    if (!next(r, &w)) return 0;
    radius = shift >= 16 ? 0 : (uint16_t)((unsigned)(w >> 8) << shift);
    selector = w & 255u;
    if (selector != r->axis) {
        fe2_render_bytes normals = f->context.model.geometry;
        uint16_t offset = (uint16_t)((((unsigned)fe2_path_sb(selector) * 2u) & 0xfffdu) - 4u);
        uint8_t n[4];
        int16_t x, y, z;
        r->axis = selector;
        if (!normals.data || offset > normals.count || normals.count - offset < 4) {
            r->status = FE2_RENDER_PROGRAM_BAD_VALUE; return 0;
        }
        memcpy(n, normals.data + offset, 4);
        /* Fractional low bytes are copies, as in the recovered loads. */
        x = fe2_path_sw((unsigned)n[0] << 8 | n[0]);
        y = fe2_path_sw((unsigned)n[3] << 8 | n[2]);
        z = fe2_path_sw((unsigned)n[2] << 8 | n[2]);
        if (selector & 1u) x = fe2_path_sw(0u - (uint16_t)x);
        for (i = 0; i < 3; ++i) {
            uint32_t sum = (uint32_t)((int32_t)f->transform.basis[6 + i] * z) +
                           (uint32_t)((int32_t)f->transform.basis[3 + i] * y) +
                           (uint32_t)((int32_t)f->transform.basis[i] * x);
            s->products[i] = fe2_path_high1((int32_t)sum);
        }
    }
    if (transformed_ellipse(v.camera, s->products, radius, p)) {
        if (!emit(r, fe2_path_curve(&r->b, p[0], p[1], p[4], p[3])) || !emit(r, fe2_path_curve(&r->b, p[0], p[2], p[5], p[3]))) return 0;
    }
    s->open = 0;
    return 1;
}

fe2_render_program_result fe2_path_value(uint16_t token, fe2_render_bytes ops,
    fe2_render_program_state *state, fe2_render_program_drawing *d)
{
    fe2_render_program_result result = {FE2_RENDER_PROGRAM_TRUNCATED, 0, 0, 0};
    path_run r;
    fe2_render_vertex anchor;
    uint16_t operand, face, material, value;
    uint32_t key;
    int index;
    memset(&r, 0, sizeof(r));
    r.d = d; r.s = state; r.ops = ops; r.words = ops.count / 2;
    if (!next(&r, &operand)) return result;
    result.status = FE2_RENDER_PROGRAM_BAD_VALUE;
    if (fe2_path_classify(d, operand & 0x7fu, &face)) return result;
    if (face & 0x8000u) {
        unsigned skip = operand >> 8;
        if (skip & 1u) return result; /* odd byte skips misalign the word stream */
        result.status = FE2_RENDER_PROGRAM_TRUNCATED;
        if (skip / 2 > r.words - 1) return result;
        result.status = FE2_RENDER_PROGRAM_COMPLETE; result.cursor = 1 + skip / 2;
        return result;
    }
    if (fe2_render_line_material(token, face, d->inherited_material, &material)) return result;
    r.abort_mode = (operand & 0x80u) != 0;
    if (r.pos >= r.words) { result.status = FE2_RENDER_PROGRAM_TRUNCATED; return result; }
    index = fe2_path_sb(fe2_path_word(ops, r.pos));
    if (fe2_path_resolve(d, index)) return result;
    fe2_path_current(d, index, &anchor);
    key = fe2_render_depth_key(anchor.camera, d->scale);
    if (!d->execution->paths || d->queue->executing) return result;
    fe2_path_begin(&r.b, d->execution->paths);
    for (;;) {
        uint16_t t;
        int status = 1;
        fe2_render_path_state *s = &state->path;
        if (!next(&r, &t)) break;
        switch ((t >> 9) & 7u) {
        case 0: case 5:
            if (s->open) {
                if (!s->first_set) { r.status = FE2_RENDER_PROGRAM_BAD_VALUE; status = 0; break; }
                r.nested = 1;
                status = point_to(&r, s->first);
                r.nested = 0;
                if (status == 1 && s->clipped) status = emit(&r, fe2_path_line_to(&r.b, s->entry));
            }
            if (status == 1 && !((t >> 9) & 7u)) {
                r.status = fe2_path_commit(d, &r.b, key, material);
                if (r.status == FE2_RENDER_PROGRAM_COMPLETE) {
                    result.status = r.status; result.cursor = r.pos;
                    return result;
                }
                status = 0;
            }
            break;
        case 1:
            if (!next(&r, &value) || !ref(&r, fe2_path_sb(value >> 8))) { status = 0; break; }
            s->first = fe2_path_sb(value >> 8); s->first_set = 1;
            status = quad(&r, s->first, value);
            break;
        case 2:
            index = fe2_path_sb(t);
            if (!ref(&r, index) || !next(&r, &value) || !ref(&r, fe2_path_sb(value))) { status = 0; break; }
            s->first = index; s->first_set = 1;
            status = pair(&r, index, fe2_path_sb(value));
            break;
        case 3:
            if (!ref(&r, fe2_path_sb(t))) { status = 0; break; }
            status = point_to(&r, fe2_path_sb(t));
            break;
        case 4:
            if (!last_vertex(&r)) { status = 0; break; }
            if (fe2_path_depth(d, s->last) >= 64) { status = quad(&r, s->last, t); break; }
            if (!next(&r, &value) || !ref(&r, fe2_path_sb(value))) { status = 0; break; }
            if (fe2_path_depth(d, fe2_path_sb(value)) >= 64) status = reenter(&r, fe2_path_sb(value));
            else s->last = fe2_path_sb(value);
            break;
        case 6:
            status = transformed(&r, t);
            break;
        default:
            r.status = FE2_RENDER_PROGRAM_BAD_VALUE; status = 0;
        }
        if (status == 2) {
            /* Skip the remaining tokens through the terminator (0x2902 table). */
            static const unsigned skip[7] = {0, 2, 1, 0, 1, 0, 1};
            fe2_path_discard(&r.b);
            for (;;) {
                unsigned kind;
                if (!next(&r, &t)) { result.status = r.status; return result; }
                kind = (t >> 9) & 7u;
                if (kind == 7) { result.status = FE2_RENDER_PROGRAM_BAD_VALUE; return result; }
                if (!kind) break;
                r.pos += skip[kind];
            }
            result.status = FE2_RENDER_PROGRAM_COMPLETE; result.cursor = r.pos;
            return result;
        }
        if (!status) break;
    }
    fe2_path_discard(&r.b);
    result.status = r.status;
    return result;
}

/* ---- opcode 22 ----------------------------------------------------------- */
