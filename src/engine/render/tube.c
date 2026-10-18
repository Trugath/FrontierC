/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Opcodes 16/17: vertex-pair tubes (loaders 0x8129..0x8220, builder
 * 0x8221..0x8c8a) with the axis ellipse 0xbe67..0xc091, the shared 0xc168/0xc332
 * control points and partial cubic stepping 0xbdd5..0xbe66. */
#include "path_internal.h"
#include "backend.h"
#include <string.h>

typedef struct tube_end {
    fe2_screen_point p[6]; /* A, B, C, D, E, F */
    int16_t u[3];          /* radial vector (frame +0x14..0x18) */
    uint16_t radius;       /* frame +0x38 */
} tube_end;

static int16_t cross_high1(int16_t a, int16_t b, int16_t c, int16_t d)
{
    return fe2_path_high1((int32_t)((uint32_t)((int32_t)a * b) - (uint32_t)((int32_t)c * d)));
}

/* ellipse of the given radius around centre, perpendicular to axis
 * and to the centre's direction. The centre is first scaled down until each
 * component magnitude is below 0x3f00 (z, then x, then y). A zero cross length
 * or an overflowing scale quotient (resume 0xc332) uses the face-on vectors. */
static int axis_ellipse(const int32_t centre[3], const int16_t a[3], uint16_t radius, tube_end *out)
{
    static const unsigned order[3] = {2, 0, 1};
    unsigned shift = 0, i;
    int16_t c[3], n[3], w[3];
    uint16_t length;
    int16_t q;
    for (i = 0; i < 3; ++i) {
        int32_t v = centre[order[i]];
        uint32_t m = (v < 0 ? 0u - (uint32_t)v : (uint32_t)v) >> shift;
        while (m >= 0x3f00u) { ++shift; m >>= 1; }
    }
    for (i = 0; i < 3; ++i) {
        uint32_t v = (uint32_t)centre[i];
        if (shift) v = v >> shift | (v & UINT32_C(0x80000000) ? UINT32_MAX << (32 - shift) : 0);
        c[i] = fe2_path_sw(v);
    }
    n[0] = cross_high1(a[2], c[1], a[1], c[2]);
    n[1] = cross_high1(a[0], c[2], c[0], a[2]);
    n[2] = cross_high1(c[0], a[1], a[0], c[1]);
    length = fe2_math_length3(n[0], n[1], n[2]);
    if (!length ||
        !fe2_path_quotient((int32_t)((uint32_t)radius << 16 | length) >> 1, fe2_path_sw(length), &q)) {
        fe2_path_round_vectors(radius, out->u, w);
    } else {
        int16_t u[3];
        static const int16_t third = -21846;
        for (i = 0; i < 3; ++i) u[i] = fe2_path_high1((int32_t)n[i] * q);
        w[1] = cross_high1(u[0], a[2], a[0], u[2]);
        w[2] = cross_high1(a[0], u[1], u[0], a[1]);
        w[0] = cross_high1(u[2], a[1], u[1], a[2]);
        for (i = 0; i < 3; ++i) w[i] = fe2_path_sw((uint32_t)((int32_t)w[i] * third) >> 14);
        memcpy(out->u, u, sizeof(u));
    }
    out->radius = radius;
    return fe2_path_ellipse_points(centre, out->u, w, out->p);
}

/* continue a flattened cubic, emitting line-to points while the
 * remaining-step counter is not below limit; at least one point when the
 * counter is not negative. The stepping state is kept between calls. */
typedef struct partial_cubic {
    fe2_render_cubic_steps s;
    int counter;
    unsigned shift;
} partial_cubic;

static void partial_begin(partial_cubic *c, fe2_screen_point anchor, fe2_screen_point p1,
    fe2_screen_point p2, fe2_screen_point p3)
{
    fe2_screen_point p[3];
    unsigned level = 0;
    p[0] = p1; p[1] = p2; p[2] = p3;
    fe2_render_cubic_setup(anchor, p, &c->s);
    while ((4u << level) < c->s.segments) ++level;
    c->counter = (int)c->s.segments - 1;
    c->shift = 14u - level;
}
static fe2_screen_point partial_point(const partial_cubic *c)
{
    fe2_screen_point p;
    p.x = fe2_path_sw(c->s.x >> 16); p.y = fe2_path_sw(c->s.y >> 16);
    return p;
}
static fe2_render_program_status partial_emit(partial_cubic *c, int limit, fe2_path_builder *b)
{
    fe2_render_program_status status;
    if (c->counter < 0) return FE2_RENDER_PROGRAM_COMPLETE;
    do {
        unsigned i;
        c->s.x += c->s.d1[0]; c->s.y += c->s.d1[1];
        for (i = 0; i < 2; ++i) { c->s.d1[i] += c->s.d2[i]; c->s.d2[i] += c->s.d3[i]; }
        status = fe2_path_line_to(b, partial_point(c));
        if (status != FE2_RENDER_PROGRAM_COMPLETE) return status;
        --c->counter;
    } while (limit <= c->counter);
    return FE2_RENDER_PROGRAM_COMPLETE;
}

/* Material selector after the loaders' bank resolution: low twelve bits,
 * bank form again, bits 0/8 cleared, optional inherited material. */
static int selector_material(uint16_t word, uint32_t inherited, uint16_t *out)
{
    return fe2_render_line_material((uint16_t)((word & 0xffeu) << 4), 0, inherited, out);
}

#define STEP(expression) do { status = (expression); if (status != FE2_RENDER_PROGRAM_COMPLETE) goto fail; } while (0)

fe2_render_program_result fe2_path_tube(uint16_t token, fe2_render_bytes ops,
    fe2_render_program_state *state, fe2_render_program_drawing *d)
{
    fe2_render_program_result result = {FE2_RENDER_PROGRAM_TRUNCATED, 0, 0, 0};
    const fe2_render_execution_frame *f = d->execution->frames + d->execution->depth - 1;
    fe2_render_program_status status = FE2_RENDER_PROGRAM_COMPLETE;
    fe2_render_vertex va, vb;
    fe2_path_builder b;
    tube_end e[2];
    size_t words = (token & 31u) == 17 ? 5 : 3, i;
    uint16_t w[5], selector, material;
    uint32_t value;
    unsigned shift = f->transform.exponent & 31u;
    int16_t axis[3];
    int32_t middle[3];
    uint32_t key;
    int ia, ib, flat;
    if (ops.count / 2 < words) return result;
    for (i = 0; i < words; ++i) w[i] = fe2_path_word(ops, i);
    result.status = FE2_RENDER_PROGRAM_BAD_VALUE;
    /* Loader: the header selector's bank form is resolved immediately. */
    value = (token >> 4) & 0xffeu;
    if ((value & 0xf00u) == 0xe00u)
        value = (value & 0x10u) | ((value & 0xeu) << 15) | ((value & 0xe0u) << 14);
    if (fe2_path_bank(value, &selector)) return result;
    if (words == 5) { state->path.caps[0] = w[3]; state->path.caps[1] = w[4]; }
    result.cursor = words;
    ia = fe2_path_sb(w[0]); ib = fe2_path_sb(w[0] >> 8);
    if (fe2_path_resolve(d, ia)) return result;
    result.status = FE2_RENDER_PROGRAM_COMPLETE;
    if (fe2_path_depth(d, ia) <= 64) return result;
    result.status = FE2_RENDER_PROGRAM_BAD_VALUE;
    if (fe2_path_resolve(d, ib)) return result;
    result.status = FE2_RENDER_PROGRAM_COMPLETE;
    if (fe2_path_depth(d, ib) <= 64) return result;
    {
        /* Axis normal: bytes 0, 3, 2 in high bytes; odd selector negates x. */
        fe2_render_bytes normals = f->context.model.geometry;
        uint16_t offset = (uint16_t)((((unsigned)w[1] << 1) & 0xfcu) - 4u);
        int16_t x, y, z;
        result.status = FE2_RENDER_PROGRAM_BAD_VALUE;
        if (!normals.data || offset > normals.count || normals.count - offset < 4) return result;
        x = fe2_path_sw((unsigned)normals.data[offset] << 8);
        y = fe2_path_sw((unsigned)normals.data[offset + 3] << 8);
        z = fe2_path_sw((unsigned)normals.data[offset + 2] << 8);
        if (w[1] & 1u) x = fe2_path_sw(0u - (uint16_t)x);
        for (i = 0; i < 3; ++i)
            axis[i] = fe2_path_high1((int32_t)((uint32_t)((int32_t)f->transform.basis[i] * x) +
                (uint32_t)((int32_t)f->transform.basis[3 + i] * y) + (uint32_t)((int32_t)f->transform.basis[6 + i] * z)));
    }
    fe2_path_current(d, ia, &va); fe2_path_current(d, ib, &vb);
    result.status = FE2_RENDER_PROGRAM_COMPLETE;
    if (!axis_ellipse(va.camera, axis, shift >= 16 ? 0 : (uint16_t)((unsigned)(w[1] >> 8) << shift), e) ||
        !axis_ellipse(vb.camera, axis, shift >= 16 ? 0 : (uint16_t)((unsigned)(w[2] >> 8) << shift), e + 1))
        return result;
    for (i = 0; i < 3; ++i) {
        uint32_t sum = (uint32_t)va.camera[i] + (uint32_t)vb.camera[i];
        middle[i] = (int32_t)(sum >> 1 | (sum & UINT32_C(0x80000000)));
    }
    key = fe2_render_depth_key(middle, d->scale);
    result.status = FE2_RENDER_PROGRAM_BAD_VALUE;
    if (!d->execution->paths || d->queue->executing) return result;
    fe2_path_begin(&b, d->execution->paths);
    flat = (selector & 0x100u) != 0;
    if (!flat) {
        const int16_t *v = f->transform.direction;
        int16_t n[3];
        uint16_t length, divisor, lit, unlit;
        uint32_t dot;
        int16_t q;
        uint16_t angle;
        partial_cubic side[2];
        n[1] = cross_high1(v[0], axis[2], axis[0], v[2]);
        n[2] = cross_high1(axis[0], v[1], v[0], axis[1]);
        n[0] = cross_high1(v[2], axis[1], v[1], axis[2]);
        length = fe2_math_length3(n[0], n[1], n[2]);
        if (length < 0x1000u) flat = 1;
        else {
            unsigned shade = ((((unsigned)length >> 11) ^ 0xeu) & 0xfeu) / 2u;
            status = FE2_RENDER_PROGRAM_BAD_VALUE;
            if (shade >= 8 || selector_material(selector, d->inherited_material, &lit)) goto fail;
            unlit = (uint16_t)(lit + f->transform.shades[shade]);
            divisor = (uint16_t)((((uint32_t)length * e[0].radius) >> 15) + 3u);
            dot = (uint32_t)((int32_t)n[2] * e[0].u[2]) + (uint32_t)((int32_t)n[1] * e[0].u[1]) +
                  (uint32_t)((int32_t)n[0] * e[0].u[0]);
            if (!fe2_path_quotient((int32_t)dot, fe2_path_sw(divisor), &q)) q = fe2_path_sw(dot);
            angle = fe2_render_acos((uint16_t)q);
            dot = (uint32_t)((int32_t)e[0].u[0] * v[0]) + (uint32_t)((int32_t)e[0].u[1] * v[1]) +
                  (uint32_t)((int32_t)e[0].u[2] * v[2]);
            if (dot & UINT32_C(0x80000000)) {
                uint16_t t = lit;
                angle = (uint16_t)(0u - angle); lit = unlit; unlit = t;
            }
            /* The lit side: from the edge between the two A points along
             * part of each first cubic, then across the terminator. */
            STEP(fe2_path_edge(&b, e[0].p[0], e[1].p[0]));
            partial_begin(side + 1, e[1].p[0], e[1].p[1], e[1].p[4], e[1].p[3]);
            STEP(partial_emit(side + 1, angle >> side[1].shift, &b));
            fe2_path_move(&b, e[0].p[0]);
            partial_begin(side, e[0].p[0], e[0].p[1], e[0].p[4], e[0].p[3]);
            STEP(partial_emit(side, angle >> side[0].shift, &b));
            STEP(fe2_path_line_to(&b, partial_point(side + 1)));
            STEP(fe2_path_fill(&b, lit));
            STEP(fe2_path_edge(&b, partial_point(side + 1), partial_point(side)));
            STEP(partial_emit(side, 0, &b));
            STEP(fe2_path_line_to(&b, e[1].p[3]));
            fe2_path_move(&b, partial_point(side + 1));
            STEP(partial_emit(side + 1, 0, &b));
            material = unlit;
        }
    }
    if (flat) {
        STEP(fe2_path_curve(&b, e[0].p[0], e[0].p[1], e[0].p[4], e[0].p[3]));
        STEP(fe2_path_line_to(&b, e[1].p[3]));
        STEP(fe2_path_curve(&b, e[1].p[0], e[1].p[1], e[1].p[4], e[1].p[3]));
        STEP(fe2_path_edge(&b, e[0].p[0], e[1].p[0]));
        status = FE2_RENDER_PROGRAM_BAD_VALUE;
        if (selector_material(selector, d->inherited_material, &material)) goto fail;
    }
    /* End caps: selector bit 7 names a face; a visible face fills the
     * ellipse with the cap selector (opcode-17 operands, retained for 16). */
    for (i = 0; i < 2; ++i) {
        unsigned cap = w[1 + i] & 255u;
        uint16_t face, fill;
        if (!(cap & 0x80u)) continue;
        status = FE2_RENDER_PROGRAM_BAD_VALUE;
        if (fe2_path_classify(d, (uint16_t)(cap & 0x7fu), &face)) goto fail;
        if (face & 0x8000u) continue;
        if (fe2_render_line_material((uint16_t)((state->path.caps[i] & 0xfffu) << 4), face,
            d->inherited_material, &fill)) goto fail;
        STEP(fe2_path_fill(&b, material));
        STEP(fe2_path_curve(&b, e[i].p[0], e[i].p[1], e[i].p[4], e[i].p[3]));
        STEP(fe2_path_curve(&b, e[i].p[0], e[i].p[2], e[i].p[5], e[i].p[3]));
        material = fill;
    }
    STEP(fe2_path_commit(d, &b, key, material));
    result.status = FE2_RENDER_PROGRAM_COMPLETE;
    return result;
fail:
    fe2_path_discard(&b);
    result.status = status;
    return result;
}
