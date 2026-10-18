/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Opcode 31: shaded spheres (0x93d5..0xa030, rings and close views 0xa031..0xa509,
 * features 0xa50a..0xb113, palette helpers 0xb114..0xb206). */
#include "sphere.h"
#include "path_internal.h"
#include "math/math3.h"
#include <string.h>

enum {
    CENTRE_X = 0xa0, CENTRE_Y = 0x54,  /* screen mapping x + 0xa0, 0x54 - y */
    SAMPLE_LIMIT = 0x3f00,             /* camera words are reduced below this */
    HIDDEN = 0x11, FACING = 0x27, BEHIND = 9, VISIBLE = 0xff81
};

/* Byte-addressed thresholds, including the deepest boundary refinements. */
extern const uint8_t *fe2_render_sample_thresholds;

/* A curve sample : direction, classification and screen
 * position (or the direction's first two words when not projected). */
typedef struct sample {
    int16_t dir[3];
    uint16_t marker;
    int16_t x, y;
} sample;

/* The 0xd6-byte workspace (offsets in the comments) and the shared scratch
 * words the helpers pass to each other. */
typedef struct sphere {
    fe2_render_program_drawing *d;
    const fe2_render_execution_frame *f;
    fe2_render_program_state *state;
    fe2_render_sphere_output *out;
    fe2_render_bytes ops;
    size_t pos;
    fe2_render_program_status status;
    size_t start, resumes;
    uint16_t colors[16];      /*..0x1e */
    uint16_t flags;
    uint16_t c0;              /* : the radius reduction */
    uint16_t r;               /* : radius word */
    int16_t c[3];             /*..0x7e: centre words */
    fe2_scalar R, rho, T;     /*, +0x34, +0x40 */
    fe2_scalar zr;            /* : z^2 - R^2 (high mantissa word) */
    int16_t cosv, sinv;       /*, +0x4e */
    uint16_t k, ce;           /*, +0xce */
    uint16_t bias_base, bias; /*, +0xcc */
    int16_t radius;           /* : sample radius */
    uint16_t param_scale;
    int16_t offset[3];        /*..0x88 */
    int16_t terminator[5];    /*..0x92 */
    sample prev, closing;     /*, +0xa0 */
    int16_t depth;
    uint16_t jitter;
    int16_t rows[6];          /*..0xba: low row, its dir, count; high row, dir, count */
    uint16_t any;
    int16_t high_x;
    uint16_t closed;
    uint8_t single;
    uint16_t mask;
    sample cur;
    uint16_t seed[2];
    uint16_t attribute;
    uint32_t key;
    int opened;
} sphere;

static int16_t sw(uint32_t v) { return fe2_path_sw(v); }
/* H(a, b): the high word of 2ab. */
static int16_t H(int16_t a, int16_t b) { return fe2_path_high1((int32_t)a * b); }
static int16_t magnitude(int16_t v) { return (int16_t)(v < 0 ? (uint16_t)(0u - (uint16_t)v) : (uint16_t)v); }
static fe2_scalar S(uint16_t m, uint16_t e) { fe2_scalar s; s.mantissa = m; s.exponent = e; return s; }
static fe2_scalar high(fe2_scalar32 v) { return S((uint16_t)(v.mantissa >> 16), v.exponent); }
static int16_t to16(fe2_scalar v, int bias) { return fe2_math_to_signed16(S(v.mantissa, (uint16_t)(v.exponent + bias))); }
static fe2_scalar32 square(fe2_scalar v) { return fe2_math_normalize32(fe2_math_multiply(v, v)); }

static uint8_t byte_at(sphere *s)
{
    if (s->pos >= s->ops.count) { if (!s->status) s->status = FE2_RENDER_PROGRAM_TRUNCATED; return 0; }
    return s->ops.data[s->pos++];
}
static uint16_t word_at(sphere *s, size_t pos)
{
    if (pos + 2 > s->ops.count) { if (!s->status) s->status = FE2_RENDER_PROGRAM_TRUNCATED; return 0; }
    return (uint16_t)(s->ops.data[pos] | s->ops.data[pos + 1] << 8);
}
static uint16_t next_word(sphere *s) { uint16_t v = word_at(s, s->pos); s->pos += 2; return v; }

static void seed_step(uint16_t seed[2])
{
    uint32_t low = (uint32_t)seed[0] + seed[1];
    uint16_t high = (uint16_t)(seed[1] + seed[0] + (low >> 16));
    seed[0] = (uint16_t)low; seed[1] = high;
}

static void element(sphere *s, fe2_render_sphere_kind kind, const int16_t *xy, unsigned points, uint16_t value)
{
    fe2_render_sphere_output *o = s->out;
    fe2_render_sphere_element *e;
    unsigned i;
    if (s->status) return;
    if (o->element_count >= o->element_capacity) { s->status = FE2_RENDER_PROGRAM_OUTPUT_FULL; return; }
    e = o->elements + o->element_count++;
    memset(e, 0, sizeof(*e));
    e->kind = kind; e->value = value;
    for (i = 0; i < points; ++i) { e->points[i].x = xy[2 * i]; e->points[i].y = xy[2 * i + 1]; }
}

/* the control record; the coordinates are mapped to the screen in place. */
static void control(sphere *s, int16_t p[8])
{
    unsigned i;
    for (i = 0; i < 4; ++i) {
        p[2 * i] = sw((uint16_t)p[2 * i] + CENTRE_X);
        p[2 * i + 1] = sw(CENTRE_Y - (uint16_t)p[2 * i + 1]);
    }
    element(s, FE2_RENDER_SPHERE_CONTROL, p, 4, 0);
}

/* the queue key from the vertex with its depth raised to at least
 * 0x2625a00, then the first control record. */
static void begin(sphere *s, const fe2_render_vertex *v, int16_t p[8])
{
    int32_t camera[3];
    memcpy(camera, v->camera, sizeof(camera));
    if (camera[2] <= INT32_C(0x02625a00)) camera[2] = INT32_C(0x02625a00);
    s->key = fe2_render_depth_key(camera, s->d->scale);
    s->opened = 1;
    s->start = s->out->element_count;
    control(s, p);
}

/* the shade (BP-0x60 + offset) from the direction's angle to the
 * centre, and an atmosphere colour from the base word for the dark side. */
static void bias(sphere *s)
{
    const fe2_render_model_transform *t = &s->f->transform;
    uint32_t sum = (uint32_t)((int32_t)t->direction[0] * s->c[0]) + (uint32_t)((int32_t)t->direction[1] * s->c[1]) +
                   (uint32_t)((int32_t)t->direction[2] * s->c[2]);
    int16_t dot = fe2_path_high1((int32_t)sum), offset;
    offset = (int16_t)((int8_t)((uint16_t)dot >> 8));
    offset = sw((uint16_t)(0u - (uint16_t)(offset >> 2)) & 0xfffeu);
    if (offset <= -8) offset = -8;
    if (offset >= 6) offset = 6;
    s->bias = t->shades[4 + offset / 2];
    if (offset < 0 && (int16_t)s->ce >= 5) {
        uint16_t v = (uint16_t)(s->bias_base + s->bias);
        s->out->rgb[0] = (uint8_t)(((v >> 8) & 15u) * 4u + 3u);
        s->out->rgb[1] = (uint8_t)(((v & 0xf0u) >> 2) + 3u);
        s->out->rgb[2] = (uint8_t)((v & 15u) * 4u + 3u);
        s->out->rgb_set = 1;
    }
}

/* scale the sample direction to the sample radius (unsigned divide;
 * an overflowing quotient resumes as zero), offset it from the centre and
 * classify: 0xff81 faces away from the eye, 0x27 faces it but is behind the
 * centre plane, 9 is hidden behind the sphere, 0x11 is not projectable; the
 * extended viewport bounds add 0x40/0x42 (keeping 0x21). */
static void project(sphere *s)
{
    sample *c = &s->cur;
    uint16_t length = fe2_math_length3(c->dir[0], c->dir[1], c->dir[2]), b = length, q;
    unsigned n = 0, i;
    int16_t p[3], o[3], w[3];
    uint32_t dot = 0;
    if (!length) goto hidden;
    while (!(b & 0x8000u)) { ++n; b = (uint16_t)(b << 1); }
    if ((uint16_t)s->radius >= b) { q = 0; ++s->resumes; }
    else q = (uint16_t)(((uint32_t)(uint16_t)s->radius << 16) / b);
    for (i = 0; i < 3; ++i) {
        p[i] = sw((((uint32_t)((int32_t)c->dir[i] * (int16_t)q)) << n) >> 16);
        o[i] = sw((uint16_t)p[i] + (uint16_t)s->offset[i]);
        w[i] = sw((uint16_t)o[i] + (uint16_t)s->c[i]);
        dot += (uint32_t)((int32_t)w[i] * o[i]);
    }
    /* saves the scaled tuple before adding offsets/centre, then
     * restores it as the sample direction on every classification path.
     * Adaptive midpoint refinement must join these scaled directions. */
    memcpy(c->dir, p, sizeof(p));
    if (dot & UINT32_C(0x80000000)) c->marker = VISIBLE;
    else {
        c->marker = FACING;
        if (p[2] >= 0) { c->marker = BEHIND; goto unprojected; }
    }
    if (w[2] <= 0) goto hidden;
    {
        fe2_point pt = fe2_math_project((int32_t)((uint32_t)(uint16_t)w[0] << 16),
            (int32_t)((uint32_t)(uint16_t)w[1] << 16), (int32_t)((uint32_t)(uint16_t)w[2] << 16));
        c->x = sw((uint16_t)pt.x + CENTRE_X);
        c->y = sw(CENTRE_Y - (uint16_t)pt.y);
        if ((uint16_t)((uint16_t)c->x + 0x50u) > 0x1e0u) {
            c->marker = (uint16_t)((c->marker & 0x21u) | 0x40u);
            if ((uint16_t)((uint16_t)c->y + 0x50u) <= 0x148u) c->marker |= 4u;
        } else if ((uint16_t)((uint16_t)c->y + 0x50u) > 0x148u) {
            c->marker = (uint16_t)((c->marker & 0x21u) | 0x42u);
        }
    }
    return;
hidden:
    c->marker = HIDDEN;
unprojected:
    c->x = c->dir[0]; c->y = c->dir[1];
}

/* a row crossing at the sample. Rows above the viewport (y > 0xa7)
 * only track the high extremum; samples left of the screen or turning
 * across the centre emit a 0x2bb8 toggle for the row above. */
static void row(sphere *s)
{
    sample *c = &s->cur;
    if (c->y > 0xa7) {
        if (s->rows[3] < 0xa8) { s->rows[3] = 0xa8; goto high_row; }
        if (c->x < s->high_x) goto high_row;
        if (c->x != s->high_x) return;
        if (c->dir[1] < 0) goto high_row;
        return;
    }
    if ((uint16_t)c->x < 0x140u) {
        c->dir[0] = sw((uint16_t)c->dir[0] + (uint16_t)s->offset[0]);
        if (c->dir[0] >= 0) goto upper;
    } else if (!((uint16_t)((uint16_t)c->x - 0x140u) & 0x8000u)) goto upper;
    c->y = sw((uint16_t)c->y - 1u);
    if (c->y < 0) return;
    {
        int16_t xy[2];
        xy[0] = 0; xy[1] = c->y;
        element(s, FE2_RENDER_SPHERE_ROW, xy, 1, s->attribute);
    }
    if (c->y < s->rows[0]) return;
    if (c->y == s->rows[0] && c->dir[1] < 0) return;
    s->rows[0] = c->y; s->rows[1] = c->dir[1]; ++s->rows[2];
    return;
upper:
    if (c->y < s->rows[3]) return;
    if (c->y == s->rows[3] && c->dir[1] >= 0) return;
    s->rows[3] = c->y; s->rows[4] = c->dir[1]; ++s->rows[5];
    return;
high_row:
    s->rows[4] = c->dir[1]; ++s->rows[5]; s->high_x = c->x;
}

static void append(sphere *s);

/* refine at the (jittered) midpoint, then continue to the sample. */
static void refine(sphere *s, const sample *saved, const uint16_t seed[2])
{
    project(s);
    append(s);
    s->cur = *saved;
    append(s);
    s->seed[0] = seed[0]; s->seed[1] = seed[1];
    s->depth = sw((uint16_t)s->depth + 2u);
}

/* join the sample to the previous one. Two visible samples close
 * enough (CS:0x299d by depth) make an edge; otherwise the midpoint is
 * refined (with a jitter along the basis from the seed while the selector
 * allows), down to a minimum depth; crossings into or out of view make
 * row toggles. */
static void append(sphere *s)
{
    sample *c = &s->cur, *p = &s->prev;
    sample saved;
    uint16_t seed[2], ax;
    if (s->status) return;
    seed_step(s->seed);
    if (!(c->marker & 0x8000u)) goto classified;
    if (!(p->marker & 0x8000u)) goto split;
    {
        uint16_t distance = (uint16_t)((uint16_t)magnitude(sw((uint16_t)p->x - (uint16_t)c->x)) +
                                       (uint16_t)magnitude(sw((uint16_t)p->y - (uint16_t)c->y)));
        if (s->depth < -0x14 || s->depth > 0x14) { s->status = FE2_RENDER_PROGRAM_BAD_VALUE; return; }
        {
            unsigned offset = (unsigned)(s->depth + 0x14);
            /* Detail levels 1/3 address the table between word entries. */
            uint16_t threshold = (uint16_t)((unsigned)fe2_render_sample_thresholds[offset] |
                (unsigned)fe2_render_sample_thresholds[offset + 1] << 8);
            if ((int16_t)distance > sw(threshold)) goto split;
        }
    }
edge:
    {
        int16_t xy[4];
        xy[0] = p->x; xy[1] = p->y; xy[2] = c->x; xy[3] = c->y;
        *p = *c;
        c->x = xy[0]; c->y = xy[1];
        element(s, FE2_RENDER_SPHERE_EDGE, xy, 2, s->attribute);
        s->any = 0;
        return;
    }
split:
    s->depth = sw((uint16_t)s->depth - 2u);
    if (s->depth < 0) goto unsplit;
    seed[0] = s->seed[0]; seed[1] = s->seed[1]; saved = *c;
    {
        unsigned i;
        for (i = 0; i < 3; ++i) c->dir[i] = sw((uint16_t)c->dir[i] + (uint16_t)p->dir[i]);
    }
    if (s->jitter) {
        uint16_t count = (uint16_t)(s->jitter - (uint16_t)s->depth), bits, low = s->seed[0];
        unsigned axis, i;
        if ((int16_t)count >= 0x16) { *c = saved; goto unsplit; }
        count >>= 1;
        bits = s->seed[1];
        seed_step(s->seed);
        if (low & 0x8000u) ++count;
        for (axis = 0; axis < 3; ++axis) {
            bits = (uint16_t)(bits << 1);
            if (bits & 0x8000u) {
                int16_t v[3];
                bits = (uint16_t)(bits << 1);
                for (i = 0; i < 3; ++i) {
                    v[i] = s->f->transform.basis[3 * axis + i];
                    if (bits & 0x8000u) v[i] = sw(0u - (uint16_t)v[i]);
                    v[i] = (int16_t)(v[i] >> ((count & 31u) > 15u ? 15u : (count & 31u)));
                    c->dir[i] = sw((uint16_t)c->dir[i] + (uint16_t)v[i]);
                }
            }
        }
    }
    refine(s, &saved, seed);
    return;
unsplit:
    ax = (uint16_t)((p->marker | c->marker) & 0x70u);
    if (ax) {
        if (s->depth <= -0x14 || (ax & 0x20u)) goto settle;
        seed[0] = s->seed[0]; seed[1] = s->seed[1]; saved = *c;
        seed_step(s->seed);
        {
            unsigned i;
            for (i = 0; i < 3; ++i) c->dir[i] = sw((uint16_t)c->dir[i] + (uint16_t)p->dir[i]);
        }
        refine(s, &saved, seed);
        return;
    }
settle:
    s->depth = sw((uint16_t)s->depth + 2u);
    if (c->marker & 0x8000u) {
        if (p->marker & 0x8000u) goto edge;
        *p = *c;
        c->dir[1] = 1;
    } else {
        if (!(p->marker & 0x8000u)) { *p = *c; return; }
        p->marker = c->marker;
        c->dir[1] = -1; c->dir[0] = p->dir[0]; c->x = p->x; c->y = p->y;
    }
    row(s);
    return;
classified:
    if (p->marker & 0x8000u) goto split;
    ax = p->marker;
    if (ax == c->marker && !(ax & 0x40u)) { *p = *c; return; }
    ax |= c->marker;
    if (ax & 0x10u) goto split;
    if ((ax & 0x20u) && (s->depth < 0xa || (ax & 8u))) { *p = *c; return; }
    if (!(ax & 2u) && !(((uint16_t)p->x ^ (uint16_t)c->x) & 0x8000u)) { *p = *c; return; }
    if (ax & 4u) goto split;
    if (!(((uint16_t)p->y ^ (uint16_t)c->y) & 0x8000u)) { *p = *c; return; }
    goto split;
}

/* start a curve at the current direction. */
static void start_curve(sphere *s)
{
    s->rows[0] = s->rows[3] = -40;
    s->rows[1] = s->rows[2] = s->rows[4] = s->rows[5] = 0;
    project(s);
    s->prev = s->cur;
    s->closing = s->cur;
}

/* close the curve and decide whether its colour mask toggles (0xd4)
 * and whether the colours stay per slot (0xd2). */
static void finish_curve(sphere *s)
{
    s->cur = s->closing;
    append(s);
    if (s->rows[1] != 0) {
        if (s->rows[1] > 0) { s->single = 0; return; }
        if ((s->rows[2] & 1) || s->closed) goto toggle;
        s->single = 0;
        return;
    }
    if (s->rows[4] != 0) {
        if (s->rows[4] < 0) { s->single = 0; return; }
        if ((s->rows[5] & 1) || s->closed) goto toggle;
        s->single = 0;
        return;
    }
    if (!s->any) return;
    s->mask ^= s->attribute;
    return;
toggle:
    s->single = 0;
    s->mask ^= s->attribute;
}

static void point(sphere *s, const int16_t dir[3])
{
    s->cur.dir[0] = dir[0]; s->cur.dir[1] = dir[1]; s->cur.dir[2] = dir[2];
    project(s);
    append(s);
}

/* a circle of radius s->radius around the plane's normal (dir): four
 * quarter points through the perpendiculars. */
static void circle(sphere *s, int16_t marker_radius, uint16_t visibility)
{
    int16_t d[3], t[3], n[3];
    uint16_t bx;
    memcpy(d, s->cur.dir, sizeof(d));
    s->any = visibility;
    s->radius = marker_radius;
    t[0] = sw(0u - (uint16_t)d[1]);
    t[1] = d[0];
    n[0] = H(d[0], d[2]);
    n[1] = sw(0u - (uint16_t)H(t[0], d[2]));
    bx = (uint16_t)(H(d[0], d[0]) + H(d[1], d[1]));
    n[2] = sw(0u - bx);
    s->terminator[0] = t[0]; s->terminator[1] = t[1];
    s->terminator[2] = n[0]; s->terminator[3] = n[1]; s->terminator[4] = n[2];
    s->cur.dir[0] = t[0]; s->cur.dir[1] = t[1]; s->cur.dir[2] = 0;
    start_curve(s);
    {
        int16_t v[3];
        v[0] = sw(0u - (uint16_t)n[0]); v[1] = sw(0u - (uint16_t)n[1]); v[2] = sw(0u - (uint16_t)n[2]);
        point(s, v);
        v[0] = sw(0u - (uint16_t)t[0]); v[1] = sw(0u - (uint16_t)t[1]); v[2] = 0;
        point(s, v);
        point(s, n);
    }
    finish_curve(s);
}

static void offset_by(sphere *s, int16_t factor)
{
    unsigned i;
    for (i = 0; i < 3; ++i) s->offset[i] = H(s->cur.dir[i], factor);
}

/* a terminator ring of radius `radius` displaced along dir by u:
 * visible when the centre lies beyond it. */
static void terminator(sphere *s, int16_t radius, int16_t u)
{
    const sample *c = &s->cur;
    uint32_t sum = (uint32_t)((int32_t)s->c[0] * c->dir[0]) + (uint32_t)((int32_t)s->c[1] * c->dir[1]) +
                   (uint32_t)((int32_t)s->c[2] * c->dir[2]);
    uint16_t visible = fe2_path_high1((int32_t)sum) > u ? 0xffffu : 0;
    offset_by(s, sw(0u - (uint16_t)u));
    circle(s, radius, visible);
}

/* a feature ring (radius t, displaced by u along dir). Far spheres
 * (radius word below 0x1000) cannot see it; otherwise it is visible when
 * the displaced centre is within t (Manhattan, z less 0x200). */
static void feature_ring(sphere *s, int16_t t, int16_t u)
{
    uint16_t visible = 0;
    if (t > u) { terminator(s, t, u); return; }
    if ((int16_t)s->r >= 0x1000) {
        int16_t p[3];
        uint16_t sum;
        unsigned i;
        int16_t nu = sw(0u - (uint16_t)u);
        for (i = 0; i < 3; ++i) p[i] = H(s->cur.dir[i], nu);
        memcpy(s->offset, p, sizeof(p));
        sum = (uint16_t)((uint16_t)magnitude(sw((uint16_t)s->c[0] + (uint16_t)p[0])) +
                         (uint16_t)magnitude(sw((uint16_t)s->c[1] + (uint16_t)p[1])) +
                         (uint16_t)magnitude(sw((uint16_t)s->c[2] + (uint16_t)p[2] - 0x200u)));
        if (sum <= (uint16_t)t) visible = 0xffffu;
        circle(s, t, visible);
        return;
    }
    offset_by(s, sw(0u - (uint16_t)u));
    circle(s, t, visible);
}

/* a model direction from three signed bytes (x, y, z) through
 * the transposed basis. */
static void direction(sphere *s, int16_t x, int16_t y, int16_t z, int16_t out[3])
{
    const int16_t *b = s->f->transform.basis;
    unsigned i;
    for (i = 0; i < 3; ++i)
        out[i] = fe2_path_high1((int32_t)((uint32_t)((int32_t)x * b[i]) + (uint32_t)((int32_t)y * b[3 + i]) +
                                          (uint32_t)((int32_t)z * b[6 + i])));
}

/* a negative tag: a direction, a phase (1/1024 turn) giving the ring's
 * radius and displacement, optionally mirrored (tag bit 0x40). */
static void tagged_ring(sphere *s, int16_t tag)
{
    int16_t d[3], t, u;
    uint8_t b[3];
    uint16_t angle;
    unsigned i;
    s->jitter = 0;
    for (i = 0; i < 3; ++i) b[i] = byte_at(s);
    direction(s, sw(0u - ((unsigned)b[0] << 8)), sw(0u - ((unsigned)b[1] << 8)), sw(0u - ((unsigned)b[2] << 8)), d);
    angle = (uint16_t)(byte_at(s) << 6);
    if (s->status) return;
    t = H((int16_t)s->param_scale, fe2_math_sin(angle));
    if (t < 0x64) return;
    u = H((int16_t)s->param_scale, fe2_math_cos(angle));
    if (tag & 0x40) {
        for (i = 0; i < 3; ++i) s->cur.dir[i] = sw(0u - (uint16_t)d[i]);
        s->attribute = (uint16_t)(tag & 0x1c);
        feature_ring(s, t, u);
    }
    memcpy(s->cur.dir, d, sizeof(d));
    s->attribute = (uint16_t)(tag & 0x1c);
    feature_ring(s, t, u);
}

/* surface features. Positive tags (colour index) carry a jitter
 * selector, optional standalone points (each with a visibility parameter)
 * and a closed curve of points; negative tags are rings. Zero ends. */
static void features(sphere *s)
{
    uint8_t tag;
    s->single = 0xff;
    s->mask = 0;
    tag = byte_at(s);
    if (!tag || s->status) return;
    s->closed = 0;
    s->param_scale = (uint16_t)(((uint32_t)0x82c0u * s->r) >> 15);
    if (s->state->model.count < 4 || !s->state->model.data) { s->status = FE2_RENDER_PROGRAM_BAD_VALUE; return; }
    s->seed[0] = s->state->model.data[2]; s->seed[1] = s->state->model.data[3];
    do {
        int16_t t = (int16_t)(int8_t)tag;
        if (t < 0) tagged_ring(s, t);
        else {
            int8_t selector;
            uint8_t x;
            int active = 0;
            s->any = 0;
            s->attribute = (uint16_t)t;
            s->radius = (int16_t)s->r;
            memset(s->offset, 0, sizeof(s->offset));
            selector = (int8_t)byte_at(s);
            s->jitter = selector ? (uint16_t)((selector + (int16_t)s->c0 + 1) * 2) : 0;
            x = byte_at(s);
            for (;;) {
                uint16_t yz = next_word(s);
                int16_t d[3];
                if (s->status) return;
                direction(s, sw((unsigned)x << 8), sw((yz & 255u) << 8), sw(yz & 0xff00u), d);
                memcpy(s->cur.dir, d, sizeof(d));
                if (!active) {
                    if (s->pos < s->ops.count && s->ops.data[s->pos]) {
                        start_curve(s);
                        active = 1;
                    } else {
                        uint8_t parameter;
                        project(s);
                        ++s->pos;
                        parameter = byte_at(s);
                        if (s->status) return;
                        if ((int16_t)s->r >= 0x1000) {
                            uint16_t limit = (uint16_t)H(sw((unsigned)parameter << 8), (int16_t)s->param_scale);
                            uint16_t sum = (uint16_t)((uint16_t)magnitude(sw((uint16_t)s->c[0] + (uint16_t)s->cur.dir[0])) +
                                (uint16_t)magnitude(sw((uint16_t)s->c[1] + (uint16_t)s->cur.dir[1])) +
                                (uint16_t)magnitude(sw((uint16_t)s->c[2] + (uint16_t)s->cur.dir[2] - 0x80u)));
                            if (sum <= limit) s->any = 0xffffu;
                        }
                        x = byte_at(s);
                        continue;
                    }
                } else {
                    project(s);
                    append(s);
                }
                x = byte_at(s);
                if (s->status) return;
                if (!x) break;
            }
            finish_curve(s);
        }
        if (s->status) return;
        tag = byte_at(s);
    } while (tag && !s->status);
}

/* atmosphere terminators, then the closing row toggle and
 * the colour block. */
static void close_sphere(sphere *s)
{
    if (s->pos & 1u) ++s->pos;
    s->jitter = 0;
    s->closed = 0xffffu;
    if (s->flags & 0x8000u) {
        const int16_t *view = s->f->transform.direction;
        memcpy(s->cur.dir, view, sizeof(s->cur.dir));
        s->attribute = 0x20;
        terminator(s, (int16_t)s->r, 0);
        if (s->flags & 0x4000u) {
            memcpy(s->cur.dir, view, sizeof(s->cur.dir));
            if (s->flags & 0x2000u) {
                s->attribute = 0x20;
                terminator(s, H((int16_t)s->r, 0x7bef), (int16_t)(s->r >> 2));
                memcpy(s->cur.dir, view, sizeof(s->cur.dir));
                s->attribute = 0x30;
                terminator(s, H((int16_t)s->r, 0x7efe), (int16_t)(s->r >> 3));
            } else {
                s->attribute = 0x30;
                terminator(s, H((int16_t)s->r, 0x7bef), (int16_t)(s->r >> 2));
            }
        }
    }
    if (s->mask) {
        int16_t xy[2];
        xy[0] = 0; xy[1] = 0xa7;
        element(s, FE2_RENDER_SPHERE_ROW, xy, 1, s->mask);
    }
}

/* a close view: the outline's halves from the horizon
 * geometry, optional ring layers (one filled path each, scaled about the
 * outline's centre point), and two edges to that point. */
static void close_view(sphere *s, const fe2_render_vertex *v, int16_t top, int16_t bottom)
{
    int16_t p[8], p44, p46, p48, p4a, p76, cx, cy, t, sv16, b2;
    fe2_scalar q, sv;
    uint16_t k3;
    top = sw((uint16_t)top - 3u);
    p76 = top;
    k3 = (uint16_t)(3u * s->k);
    p46 = sw((uint16_t)top + k3);
    p4a = sw((uint16_t)top - s->k);
    q = fe2_math_divide(fe2_math_normalize_unsigned(S(k3, 0xa)), s->T);
    sv = fe2_math_square_root_scalar(high(fe2_math_multiply(q, s->R)));
    t = to16(high(fe2_math_multiply(high(fe2_math_multiply(fe2_math_divide(q, s->R), sv)), s->zr)), 3);
    sv16 = to16(sv, 7);
    p44 = sw((uint16_t)t - (uint16_t)sv16);
    p48 = sw((uint16_t)(sv16 / 3) + (uint16_t)t);
    b2 = sw((uint16_t)bottom << 1);
    cx = sw((uint16_t)H(b2, s->cosv) + CENTRE_X);
    cy = sw(CENTRE_Y - (uint16_t)H(s->sinv, b2));
    {
        int16_t a = H(p44, s->cosv), b = H(p46, s->cosv), c = H(p44, s->sinv), d = H(p46, s->sinv);
        p[0] = sw((uint16_t)b - (uint16_t)c); p[1] = sw((uint16_t)d + (uint16_t)a);
        p[6] = sw((uint16_t)c + (uint16_t)b); p[7] = sw((uint16_t)d - (uint16_t)a);
        a = H(p48, s->sinv); b = H(p4a, s->sinv); c = H(p48, s->cosv); d = H(p4a, s->cosv);
        p[2] = sw((uint16_t)d + (uint16_t)a); p[3] = sw((uint16_t)b - (uint16_t)c);
        p[4] = sw((uint16_t)d - (uint16_t)a); p[5] = sw((uint16_t)b + (uint16_t)c);
    }
    s->bias_base = next_word(s);
    if (s->status) return;
    if (s->bias_base) {
        if (p76 <= -0xb2) {
            s->pos += 2;
            do s->pos += 4; while (!s->status && word_at(s, s->pos - 2));
        } else {
            int16_t m[8];
            uint16_t scale = next_word(s);
            unsigned i;
            bias(s);
            for (i = 0; i < 4; ++i) {
                m[2 * i] = sw((uint16_t)p[2 * i] + CENTRE_X);
                m[2 * i + 1] = sw(CENTRE_Y - (uint16_t)p[2 * i + 1]);
            }
            while (scale && !s->status) {
                if ((int16_t)scale > s->out->layer_threshold) {
                    int16_t n[8];
                    fe2_path_builder b;
                    fe2_screen_point a[4], z[4];
                    uint16_t color;
                    for (i = 0; i < 8; ++i) {
                        int16_t centre = (i & 1u) ? cy : cx;
                        int16_t delta = sw((uint16_t)m[i] - (uint16_t)centre);
                        /* imul then two shl/rcl pairs: arithmetic product >> 14. */
                        int32_t product = (int32_t)delta * (int16_t)scale;
                        n[i] = (int16_t)((product >> 14) + centre);
                    }
                    color = (uint16_t)(next_word(s) + s->bias);
                    if (s->status) return;
                    for (i = 0; i < 4; ++i) {
                        a[i].x = m[2 * i]; a[i].y = m[2 * i + 1];
                        z[i].x = n[2 * i]; z[i].y = n[2 * i + 1];
                    }
                    if (!s->d->execution->paths) { s->status = FE2_RENDER_PROGRAM_BAD_VALUE; return; }
                    fe2_path_begin(&b, s->d->execution->paths);
                    if ((s->status = fe2_path_curve(&b, a[0], a[1], a[2], a[3])) != FE2_RENDER_PROGRAM_COMPLETE ||
                        (s->status = fe2_path_curve(&b, z[0], z[1], z[2], z[3])) != FE2_RENDER_PROGRAM_COMPLETE ||
                        (s->status = fe2_path_edge(&b, z[0], a[0])) != FE2_RENDER_PROGRAM_COMPLETE ||
                        (s->status = fe2_path_edge(&b, a[3], z[3])) != FE2_RENDER_PROGRAM_COMPLETE ||
                        (s->status = fe2_path_commit(s->d, &b, UINT32_MAX, color)) != FE2_RENDER_PROGRAM_COMPLETE) {
                        fe2_path_discard(&b);
                        return;
                    }
                    memcpy(m, n, sizeof(m));
                } else s->pos += 2;
                scale = next_word(s);
            }
        }
    }
    if (s->status) return;
    begin(s, v, p);
    {
        int16_t xy[4];
        xy[0] = p[0]; xy[1] = p[1]; xy[2] = cx; xy[3] = cy;
        element(s, FE2_RENDER_SPHERE_EDGE, xy, 2, 0);
        xy[0] = cx; xy[1] = cy; xy[2] = p[6]; xy[3] = p[7];
        element(s, FE2_RENDER_SPHERE_EDGE, xy, 2, 0);
    }
}

static void execute_sphere(void *target, const void *payload)
{
    const fe2_render_execution_target *t = target;
    if (t->sphere) t->sphere(t->context, payload);
}

/* the colour block: each slot's word, or one word repeated. */
static void colors(sphere *s, fe2_render_sphere_command *command, unsigned first, unsigned last)
{
    unsigned i;
    for (i = first; i < last; ++i)
        command->colors[i] = s->single ? s->colors[(s->mask >> 2) & 15u] : s->colors[i];
}

static void commit(sphere *s)
{
    fe2_render_sphere_output *o = s->out;
    fe2_render_sphere_command *command;
    if (s->status) return;
    if (o->count >= o->capacity) { s->status = FE2_RENDER_PROGRAM_OUTPUT_FULL; return; }
    if (!o->nodes || !o->commands) { s->status = FE2_RENDER_PROGRAM_BAD_VALUE; return; }
    command = o->commands + o->count;
    memset(command, 0, sizeof(*command));
    command->key = s->key;
    command->elements = o->elements + s->start;
    command->count = o->element_count - s->start;
    command->color_count = (s->flags & 0x8000u) ? 16 : 8;
    colors(s, command, 0, 8);
    if (s->flags & 0x8000u) colors(s, command, 8, 16);
    if (fe2_render_execution_insert(s->d, o->nodes + o->count, s->key, execute_sphere, command)) {
        s->status = FE2_RENDER_PROGRAM_BAD_VALUE;
        return;
    }
    ++o->count;
}

/* the header word, three colour words, an optional table of
 * seven alternative sets (flag 0x1000, selected by a constant, local or
 * model variable), the second colour set and shaded variants. */
static void read_colors(sphere *s)
{
    const uint16_t *shades = s->f->transform.shades;
    uint16_t v[4], w[4];
    unsigned i;
    s->flags = next_word(s);
    v[0] = s->flags;
    for (i = 1; i < 4; ++i) v[i] = next_word(s);
    if (s->flags & 0x1000u) {
        size_t base = s->pos;
        uint16_t index = word_at(s, base), bx = (uint16_t)(index << 1), set = 0;
        s->pos += 0x3a;
        if (!(bx & 0x100u)) set = (bx & 0x80u) ? (uint16_t)(((bx >> 7) | (bx << 9)) & 0xfc00u) : (uint16_t)((bx >> 1) & 0x3fu);
        else if (bx & 0x80u) set = s->state->local[(bx & 0x7eu) / 2u];
        else {
            if ((bx & 0x7eu) / 2u >= s->state->model.count || !s->state->model.data) {
                s->status = FE2_RENDER_PROGRAM_BAD_VALUE;
                return;
            }
            set = s->state->model.data[(bx & 0x7eu) / 2u];
        }
        set &= 7u;
        if (set) for (i = 0; i < 4; ++i) v[i] = word_at(s, base + 8u * set - 6u + 2u * i);
    }
    v[0] &= 0x0fffu;
    if (s->status) return;
    if (!(s->flags & 0x4000u)) {
        if (s->flags & 0x1000u) memcpy(w, v, sizeof(w));
        else for (i = 0; i < 4; ++i) w[i] = next_word(s);
        for (i = 0; i < 4; ++i) { s->colors[i] = v[i]; s->colors[4 + i] = w[i]; }
        if (s->flags & 0x8000u)
            for (i = 0; i < 8; ++i) s->colors[8 + i] = (uint16_t)(s->colors[i] + shades[4]);
        return;
    }
    for (i = 0; i < 4; ++i) {
        uint16_t lit = (uint16_t)(v[i] + shades[4]);
        s->colors[i] = v[i];
        s->colors[12 + i] = lit;
        s->colors[4 + i] = lit;
    }
    {
        unsigned count = (2u - s->d->execution->detail_shift[1]) & 255u & 31u;
        int16_t size = (int16_t)(count >= 16 ? 0 : (uint16_t)(s->r >> count));
        uint16_t add;
        if (size <= 0x30) {
            for (i = 0; i < 4; ++i) s->colors[8 + i] = s->colors[12 + i];
            return;
        }
        add = shades[6];
        if ((s->flags & 0x2000u) && size >= 0x60) {
            for (i = 0; i < 4; ++i) s->colors[4 + i] = (uint16_t)(v[i] + shades[5]);
            add = shades[7];
        }
        for (i = 0; i < 4; ++i) s->colors[8 + i] = (uint16_t)(v[i] + add);
    }
}

/* the shift bringing each camera component below 0x3f00. */
static unsigned reduction(const int32_t camera[3])
{
    static const unsigned order[3] = {2, 0, 1};
    unsigned shift = 0, i;
    for (i = 0; i < 3; ++i) {
        int32_t v = camera[order[i]];
        uint32_t m = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
        while ((m >> shift) >= SAMPLE_LIMIT) ++shift;
    }
    return shift;
}

fe2_render_program_result fe2_render_execute_sphere_command(uint16_t token,
    fe2_render_bytes operands, fe2_render_program_state *state,
    fe2_render_program_drawing *d)
{
    fe2_render_program_result result = {FE2_RENDER_PROGRAM_BAD_VALUE, 0, 0, 0};
    fe2_render_execution *e;
    fe2_render_vertex v;
    sphere s;
    uint16_t body = (uint16_t)((token >> 4) & 0xffeu), w0, w1;
    int index;
    unsigned shift, i;
    int16_t words[3];
    fe2_scalar32 x2, y2, z2, r2, xy2, zr, acc;
    fe2_scalar X, Y, Z;
    int16_t top, bottom;
    if (!d || !state || !(e = d->execution) || !e->depth || e->depth > e->frame_capacity || !e->frames ||
        !d->queue || !e->spheres || (operands.count && !operands.data) || (int16_t)e->detail_shift[1] > 4) return result;
    memset(&s, 0, sizeof(s));
    s.d = d; s.f = e->frames + e->depth - 1; s.state = state; s.out = e->spheres; s.ops = operands;
    if (operands.count < 4) { result.status = FE2_RENDER_PROGRAM_TRUNCATED; return result; }
    w0 = word_at(&s, 0); w1 = word_at(&s, 2);
    s.pos = 4;
    index = fe2_path_sb(w1);
    if (fe2_path_resolve(d, index)) return result;
    fe2_path_current(d, index, &v);
    s.depth = (int16_t)(e->detail_shift[1] + 0x10u);
    s.R = fe2_math_normalize_unsigned(S(w0, (uint16_t)(s.f->transform.exponent + 7u)));
    shift = reduction(v.camera);
    for (i = 0; i < 3; ++i) words[i] = sw((uint32_t)(v.camera[i] >> (shift > 31 ? 31 : shift)));
    {
        fe2_scalar reduced = S(s.R.mantissa, (uint16_t)(s.R.exponent - shift));
        int32_t r32 = fe2_math_to_signed32_unchecked(reduced);
        if (!r32) goto release;
        s.c0 = (uint16_t)(0x1fu - reduced.exponent);
        s.r = (uint16_t)r32;
    }
    if ((int16_t)s.r > 0x4000) {
        s.r = (uint16_t)(s.r >> 1);
        for (i = 0; i < 3; ++i) words[i] = (int16_t)(words[i] >> 1);
        ++s.c0;
    }
    memcpy(s.c, words, sizeof(words));
    if ((int16_t)(0u - s.r) >= s.c[2]) goto release;
    s.k = 0x2a;
    X = fe2_math_from_signed32(v.camera[0]);
    Y = fe2_math_from_signed32(v.camera[1]);
    Z = fe2_math_from_signed32(v.camera[2]);
    x2 = square(X); y2 = square(Y); z2 = square(Z); r2 = square(s.R);
    xy2 = fe2_math_add32(x2, y2);
    zr = fe2_math_subtract32(z2, r2);
    acc = fe2_math_add32(xy2, zr);
    /* far enough for the horizon? Otherwise shrink R to just below
     * the distance (D^2 less 1/4096) and recompute. */
    {
        int recompute = 1;
        if ((int32_t)acc.mantissa > 0) {
            uint16_t gap = (uint16_t)(r2.exponent - acc.exponent);
            s.ce = gap;
            if ((int16_t)gap < 7) recompute = 0;
            else { s.k = 0xa; if ((int16_t)gap < 8) recompute = 0; }
        }
        if (recompute) {
            fe2_scalar32 sum;
            s.ce = 0x10; s.k = 5;
            sum = fe2_math_add32(r2, acc);
            sum.mantissa -= sum.mantissa >> 12;
            r2 = sum;
            s.R = fe2_math_square_root_scalar32(r2);
            s.r = s.c0 ? (uint16_t)(s.c0 >= 32 ? 0 : (((uint32_t)s.R.mantissa << 16) >> s.c0)) : 0;
            zr = fe2_math_subtract32(z2, r2);
            acc = fe2_math_add32(xy2, zr);
        }
    }
    s.zr = high(zr);
    s.T = fe2_math_square_root_scalar32(acc);
    s.rho = fe2_math_square_root_scalar32(xy2);
    if (!s.rho.mantissa) { s.cosv = 0x7fff; s.sinv = 0; }
    else {
        s.cosv = to16(fe2_math_divide(X, s.rho), 0xf);
        s.sinv = to16(fe2_math_divide(Y, s.rho), 0xf);
    }
    read_colors(&s);
    if (s.status) goto fail;
    {
        fe2_scalar a = high(fe2_math_multiply(s.rho, Z)), b = high(fe2_math_multiply(s.R, s.T));
        top = to16(fe2_math_divide(fe2_math_subtract(a, b), s.zr), 8);
        if (top >= 0xb2) {
            s.bias_base = word_at(&s, s.pos);
            if (s.status) goto fail;
            if (s.bias_base) bias(&s);
            goto release;
        }
        bottom = to16(fe2_math_divide(fe2_math_add(a, b), s.zr), 8);
    }
    if ((int16_t)s.zr.mantissa <= 0) { close_view(&s, &v, top, 0x600); goto features; }
    {
        int16_t total = sw((uint16_t)bottom + (uint16_t)top);
        if (total >= 0x600) { close_view(&s, &v, top, 0x600); goto features; }
        if (total >= 0x199) { close_view(&s, &v, top, bottom); goto features; }
    }
    {
        /* the outline from the two horizon rows. */
        int16_t p[8], a, b, minor;
        fe2_scalar root = fe2_math_square_root_scalar(s.zr);
        minor = to16(high(fe2_math_multiply(fe2_math_divide(s.R, root), S(0x5555, 1))), 8);
        p[0] = H(top, s.cosv); p[1] = H(top, s.sinv);
        p[6] = H(bottom, s.cosv); p[7] = H(bottom, s.sinv);
        a = H(minor, s.cosv); b = H(minor, s.sinv);
        p[2] = sw((uint16_t)p[0] - (uint16_t)b); p[3] = sw((uint16_t)p[1] + (uint16_t)a);
        p[4] = sw((uint16_t)p[6] - (uint16_t)b); p[5] = sw((uint16_t)p[7] + (uint16_t)a);
        {
            int16_t q[8], second[8];
            memcpy(q, p, sizeof(q));
            begin(&s, &v, q);
            second[0] = p[6]; second[1] = p[7];
            second[2] = sw(((uint16_t)p[6] << 1) - (uint16_t)p[4]); second[3] = sw(((uint16_t)p[7] << 1) - (uint16_t)p[5]);
            second[4] = sw(((uint16_t)p[0] << 1) - (uint16_t)p[2]); second[5] = sw(((uint16_t)p[1] << 1) - (uint16_t)p[3]);
            second[6] = p[0]; second[7] = p[1];
            control(&s, second);
        }
        if (word_at(&s, s.pos)) {
            s.pos += 4;
            do s.pos += 4; while (!s.status && word_at(&s, s.pos - 2));
        } else s.pos += 2;
    }
features:
    if (s.status) goto fail;
    features(&s);
    if (s.status) goto fail;
    close_sphere(&s);
    commit(&s);
    if (s.status) goto fail;
    result.status = FE2_RENDER_PROGRAM_COMPLETE;
    result.cursor = s.pos / 2;
    result.divide_resumes = s.resumes;
    return result;
release:
    result.status = FE2_RENDER_PROGRAM_COMPLETE;
    result.cursor = (4u + body) / 2u;
    result.divide_resumes = s.resumes;
    return result;
fail:
    if (s.opened) s.out->element_count = s.start;
    result.status = s.status ? s.status : FE2_RENDER_PROGRAM_BAD_VALUE;
    result.divide_resumes = s.resumes;
    return result;
}
