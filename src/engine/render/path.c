/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Shared path services, vertex-pair lenses (opcode 9, 0x71ed..0x755a) and
 * four-vertex cubic strokes (opcode 22, 0x6f92..0x71ec). */
#include "path_internal.h"
#include "sphere.h"
#include <string.h>

/* ---- vertex records ----------------------------------------------------- */
static int available(const fe2_render_program_drawing *d, int index)
{
    const fe2_vertex_frame *f = d->vertex_frame;
    if (f && index < 0 && (size_t)-index <= d->prefix_count && f->vertices &&
        f->vertices[index].state == 0 && ((d->execution && index < -5) ||
        !f->inherited || !f->inherited[index])) return 0;
    return (f ? f->vertices != NULL : d->vertices != NULL) &&
           (index < 0 ? (size_t)-index <= d->prefix_count : (size_t)index < d->vertex_count);
}
void fe2_path_current(const fe2_render_program_drawing *d, int index, fe2_render_vertex *out)
{
    if (d->vertex_frame) {
        const fe2_vertex *v = d->vertex_frame->vertices + index;
        unsigned i;
        for (i = 0; i < 3; ++i) out->camera[i] = v->view[i];
        out->screen.x = v->screen[0]; out->screen.y = v->screen[1];
    } else *out = d->vertices[index];
}
int fe2_path_resolve(fe2_render_program_drawing *d, int index)
{
    const fe2_vertex_frame *f = d->vertex_frame;
    if (!available(d, index)) return -1;
    if (f && (uint8_t)f->vertices[index].state != f->stamp) fe2_math_vertex_refresh(f, index);
    return 0;
}
int32_t fe2_path_depth(const fe2_render_program_drawing *d, int index)
{
    fe2_render_vertex v;
    fe2_path_current(d, index, &v);
    return v.camera[2];
}
fe2_screen_point fe2_path_screen(const fe2_render_program_drawing *d, int index)
{
    fe2_render_vertex v;
    fe2_path_current(d, index, &v);
    return v.screen;
}
fe2_screen_point fe2_path_near(const fe2_render_program_drawing *d, int behind, int visible)
{
    fe2_render_vertex a, b;
    fe2_path_current(d, behind, &a); fe2_path_current(d, visible, &b);
    return fe2_render_project_near(a.camera, b.camera);
}

/* ---- materials, faces and arithmetic ------------------------------------- */
int fe2_path_bank(uint32_t value, uint16_t *out)
{
    if (value & UINT32_C(0xffff0000)) {
        uint16_t word;
        if (fe2_render_material_word(value >> 12, &word)) return -1;
        value = (uint16_t)((value & 0xfffu) + word);
    }
    *out = (uint16_t)value;
    return 0;
}
int fe2_path_classify(fe2_render_program_drawing *d, uint16_t index, uint16_t *face)
{
    if (!d->face_parameters || index >= d->face_parameter_count) return -1;
    if (d->face_parameters[index] == 0x8080u) {
        if (!d->face_frame) return -1;
        d->face_parameters[index] = fe2_math_classify_face(d->face_frame, (uint16_t)(index << 1));
    }
    *face = d->face_parameters[index];
    return 0;
}
/* Signed IDIV: 0 for a zero divisor or a quotient outside a word (the
 * divide-fault resumes handle those cases at each call). */
int fe2_path_quotient(int32_t dividend, int16_t divisor, int16_t *out)
{
    int64_t q;
    if (!divisor) return 0;
    q = (int64_t)dividend / divisor;
    if (q < -32768 || q > 32767) return 0;
    *out = (int16_t)q;
    return 1;
}

/* ---- path records -------------------------------------------------------- */
static void execute_path(void *target, const void *payload)
{
    fe2_render_execution_target *t = target;
    if (t && t->path) t->path(t->context, payload);
}
static void execute_curve(void *target, const void *payload)
{
    fe2_render_execution_target *t = target;
    if (t && t->curve) t->curve(t->context, payload);
}
void fe2_path_execute_round(void *target, const void *payload)
{
    fe2_render_execution_target *t = target;
    if (t && t->round) t->round(t->context, payload);
}
void fe2_path_begin(fe2_path_builder *b, fe2_render_path_output *out)
{
    memset(b, 0, sizeof(*b));
    b->out = out; b->start = out->element_count;
}
void fe2_path_discard(fe2_path_builder *b)
{
    b->out->element_count = b->start;
}
static fe2_render_program_status element(fe2_path_builder *b, fe2_render_path_kind kind,
    const fe2_screen_point *points, unsigned count, uint16_t material)
{
    fe2_render_path_element *e;
    if (b->out->element_count >= b->out->element_capacity) return FE2_RENDER_PROGRAM_OUTPUT_FULL;
    if (!b->out->elements) return FE2_RENDER_PROGRAM_BAD_VALUE;
    e = b->out->elements + b->out->element_count++;
    memset(e, 0, sizeof(*e));
    e->kind = kind; e->material = material;
    if (count) {
        memcpy(e->points, points, count * sizeof(*points));
        b->point = points[count - 1]; b->have_point = 1;
    } else b->have_point = 0; /* a following line-to would read the fill record */
    return FE2_RENDER_PROGRAM_COMPLETE;
}
fe2_render_program_status fe2_path_edge(fe2_path_builder *b, fe2_screen_point first, fe2_screen_point second)
{
    fe2_screen_point p[2];
    p[0] = first; p[1] = second;
    return element(b, FE2_RENDER_PATH_SEGMENT, p, 2, 0);
}
fe2_render_program_status fe2_path_line_to(fe2_path_builder *b, fe2_screen_point point)
{
    if (!b->have_point) return FE2_RENDER_PROGRAM_BAD_VALUE;
    return fe2_path_edge(b, b->point, point);
}
fe2_render_program_status fe2_path_curve(fe2_path_builder *b, fe2_screen_point p0, fe2_screen_point p1,
    fe2_screen_point p2, fe2_screen_point p3)
{
    fe2_screen_point p[4];
    p[0] = p0; p[1] = p1; p[2] = p2; p[3] = p3;
    return element(b, FE2_RENDER_PATH_CURVE, p, 4, 0);
}
void fe2_path_move(fe2_path_builder *b, fe2_screen_point point)
{
    b->point = point; b->have_point = 1;
}
fe2_render_program_status fe2_path_fill(fe2_path_builder *b, uint16_t material)
{
    return element(b, FE2_RENDER_PATH_FILL, NULL, 0, material);
}
fe2_render_program_status fe2_path_commit(fe2_render_program_drawing *d, fe2_path_builder *b,
    uint32_t key, uint16_t material)
{
    fe2_render_path_output *o = b->out;
    fe2_render_path_command *c;
    if (o->count >= o->capacity) return FE2_RENDER_PROGRAM_OUTPUT_FULL;
    if (!o->nodes || !o->paths) return FE2_RENDER_PROGRAM_BAD_VALUE;
    c = o->paths + o->count;
    c->key = key; c->material = material;
    c->elements = o->elements + b->start; c->count = o->element_count - b->start;
    if (fe2_render_execution_insert(d, o->nodes + o->count, key, execute_path, c)) return FE2_RENDER_PROGRAM_BAD_VALUE;
    ++o->count;
    return FE2_RENDER_PROGRAM_COMPLETE;
}

/* ---- 0xc168/0xc332: ellipse control points --------------------------------- */
static int project_offset(const int32_t base[3], const int16_t offset[3], int negate, fe2_screen_point *out)
{
    int32_t p[3];
    unsigned i;
    for (i = 0; i < 3; ++i) {
        int16_t v = negate ? fe2_path_sw(0u - (uint16_t)offset[i]) : offset[i];
        p[i] = (int32_t)((uint32_t)base[i] + (uint32_t)(int32_t)v);
    }
    return fe2_render_project(p[0], p[1], p[2], out);
}
int fe2_path_ellipse_points(const int32_t centre[3], const int16_t u[3], const int16_t w[3],
    fe2_screen_point points[6])
{
    int32_t p[3], q[3];
    unsigned i;
    for (i = 0; i < 3; ++i) {
        p[i] = (int32_t)((uint32_t)centre[i] + (uint32_t)(int32_t)u[i]);
        q[i] = (int32_t)((uint32_t)centre[i] + (uint32_t)(int32_t)fe2_path_sw(0u - (uint16_t)u[i]));
    }
    return fe2_render_project(p[0], p[1], p[2], points + 0) && project_offset(p, w, 0, points + 1) &&
        project_offset(p, w, 1, points + 2) && fe2_render_project(q[0], q[1], q[2], points + 3) &&
        project_offset(q, w, 0, points + 4) && project_offset(q, w, 1, points + 5);
}
void fe2_path_round_vectors(uint16_t radius, int16_t u[3], int16_t w[3])
{
    u[0] = fe2_path_sw(radius); u[1] = u[2] = 0;
    w[0] = w[2] = 0;
    w[1] = fe2_path_sw((0xaaaau * (uint32_t)radius) >> 15);
}

/* ---- opcode 22 ----------------------------------------------------------- */
static fe2_render_program_result four_vertex(uint16_t token, fe2_render_bytes ops,
    fe2_render_program_drawing *d)
{
    fe2_render_program_result result = {FE2_RENDER_PROGRAM_TRUNCATED, 0, 0, 0};
    fe2_render_path_output *o = d->execution->paths;
    fe2_render_curve_command command;
    fe2_render_vertex anchor;
    uint16_t first, second, face_index, face;
    int v55, v57, v59, v5b;
    if (ops.count / 2 < 3) return result;
    first = fe2_path_word(ops, 0); second = fe2_path_word(ops, 1); face_index = fe2_path_word(ops, 2);
    result.status = FE2_RENDER_PROGRAM_BAD_VALUE;
    if (face_index >= 0x8000u || fe2_path_classify(d, face_index, &face)) return result;
    result.cursor = 3;
    if (face & 0x8000u) { result.status = FE2_RENDER_PROGRAM_COMPLETE; return result; }
    if (fe2_render_line_material(token & 0xffefu, face, d->inherited_material, &command.material)) return result;
    v59 = fe2_path_sb(first); v55 = fe2_path_sb(first >> 8); v5b = fe2_path_sb(second); v57 = fe2_path_sb(second >> 8);
    if (fe2_path_resolve(d, v59) || fe2_path_resolve(d, v55) || fe2_path_resolve(d, v5b) || fe2_path_resolve(d, v57)) return result;
    result.status = FE2_RENDER_PROGRAM_COMPLETE;
    /* The first stream vertex  is not depth tested. */
    if (fe2_path_depth(d, v57) < 64 || fe2_path_depth(d, v5b) < 64 || fe2_path_depth(d, v55) < 64) return result;
    fe2_path_current(d, v55, &anchor);
    command.key = fe2_render_depth_key(anchor.camera, d->scale);
    command.points[0] = anchor.screen; command.points[1] = fe2_path_screen(d, v57);
    command.points[2] = fe2_path_screen(d, v59); command.points[3] = fe2_path_screen(d, v5b);
    result.status = FE2_RENDER_PROGRAM_BAD_VALUE;
    if (!o || d->queue->executing) return result;
    result.status = FE2_RENDER_PROGRAM_OUTPUT_FULL;
    if (o->curve_count >= o->curve_capacity) return result;
    result.status = FE2_RENDER_PROGRAM_BAD_VALUE;
    if (!o->curve_nodes || !o->curves) return result;
    o->curves[o->curve_count] = command;
    if (fe2_render_execution_insert(d, o->curve_nodes + o->curve_count, command.key, execute_curve,
        o->curves + o->curve_count)) return result;
    ++o->curve_count;
    result.status = FE2_RENDER_PROGRAM_COMPLETE;
    return result;
}

/* ---- opcode 9 ------------------------------------------------------------ */
static int16_t saturating_ratio(int16_t delta, int16_t divisor)
{
    int32_t dividend = (int32_t)(((uint32_t)(uint16_t)delta << 16) | 0x8000u) >> 1;
    int16_t q;
    if (fe2_path_quotient(dividend, divisor, &q)) return q;
    return (int16_t)((dividend >> 16) < 0 ? -0x7ffe : 0x7ffe);
}
static fe2_render_program_status lens_disc(fe2_render_program_drawing *d, int anchor,
    int16_t x, int16_t y, int16_t diameter, uint16_t resolved)
{
    fe2_render_execution *e = d->execution;
    fe2_render_round_command command;
    fe2_render_vertex v;
    uint32_t value = resolved & 0xefeu;
    int16_t half = (int16_t)((uint16_t)diameter >> 1);
    if ((uint16_t)x >= 320u && (x >= 0 ? fe2_path_sw((uint16_t)x - (uint16_t)half) >= 320 : fe2_path_sw((uint16_t)x + (uint16_t)half) < 0))
        return FE2_RENDER_PROGRAM_COMPLETE;
    if ((uint16_t)y >= 168u && (y >= 0 ? fe2_path_sw((uint16_t)y - (uint16_t)half) >= 168 : fe2_path_sw((uint16_t)y + (uint16_t)half) <= 0))
        return FE2_RENDER_PROGRAM_COMPLETE;
    if (value & 0x10u) value = (value & ~UINT32_C(0x10)) + d->inherited_material;
    memset(&command, 0, sizeof(command));
    if (fe2_path_bank(value, command.material)) return FE2_RENDER_PROGRAM_BAD_VALUE;
    command.material[1] = (uint16_t)(((command.material[0] & 0xeeeu) >> 1) + 0x222u);
    x = fe2_path_sw((uint16_t)x - 1u); y = fe2_path_sw((uint16_t)y - 1u);
    command.kind = diameter > 9 ? FE2_RENDER_DISC : FE2_RENDER_SMALL_DISC;
    if (command.kind == FE2_RENDER_SMALL_DISC && ((uint16_t)x >= 320u || (uint16_t)y >= 168u))
        return FE2_RENDER_PROGRAM_COMPLETE;
    command.center.x = x; command.center.y = y; command.size = diameter;
    fe2_path_current(d, anchor, &v);
    command.key = fe2_render_depth_key(v.camera, d->scale);
    if (e->round_count >= e->round_capacity) return FE2_RENDER_PROGRAM_OUTPUT_FULL;
    if (!e->rounds || !e->round_nodes) return FE2_RENDER_PROGRAM_BAD_VALUE;
    e->rounds[e->round_count] = command;
    if (fe2_render_execution_insert(d, e->round_nodes + e->round_count, command.key,
        fe2_path_execute_round, e->rounds + e->round_count)) return FE2_RENDER_PROGRAM_BAD_VALUE;
    ++e->round_count;
    return FE2_RENDER_PROGRAM_COMPLETE;
}

static fe2_render_program_result lens(uint16_t token, fe2_render_bytes ops, fe2_render_program_drawing *d)
{
    fe2_render_program_result result = {FE2_RENDER_PROGRAM_TRUNCATED, 0, 0, 0};
    const fe2_render_execution_frame *f = d->execution->frames + d->execution->depth - 1;
    fe2_render_vertex a, b;
    uint16_t pair, size, material, distance;
    uint32_t value, z;
    int16_t ratio, dx, dy, c;
    int ia, ib;
    fe2_path_builder path;
    fe2_render_program_status status;
    if (ops.count / 2 < 2) return result;
    pair = fe2_path_word(ops, 0); size = fe2_path_word(ops, 1);
    result.status = FE2_RENDER_PROGRAM_BAD_VALUE;
    if (fe2_render_line_material(token & 0xffe0u, 0, d->inherited_material, &material)) return result;
    ia = fe2_path_sb(pair); ib = fe2_path_sb(pair >> 8);
    if (fe2_path_resolve(d, ia)) return result;
    result.cursor = 2; result.status = FE2_RENDER_PROGRAM_COMPLETE;
    if (fe2_path_depth(d, ia) <= 0x200) return result;
    result.status = FE2_RENDER_PROGRAM_BAD_VALUE;
    if (fe2_path_resolve(d, ib)) return result;
    result.status = FE2_RENDER_PROGRAM_COMPLETE;
    if (fe2_path_depth(d, ib) <= 0x200) return result;
    fe2_path_current(d, ia, &a); fe2_path_current(d, ib, &b);
    c = fe2_path_sw((uint32_t)f->transform.exponent - 5u);
    if (c > 0) {
        unsigned n = (unsigned)c & 63u;
        value = !n || n >= 32 ? 0 : (uint32_t)size << n;
    } else {
        unsigned n = (unsigned)(uint16_t)-c & 31u;
        value = n >= 16 ? 0 : (uint16_t)(size >> n);
    }
    z = (uint32_t)a.camera[2];
    while (z & UINT32_C(0xffff8000)) {
        value = (uint32_t)((int32_t)value >> 1);
        z >>= 1;
    }
    if (!fe2_path_quotient((int32_t)(value << 8), (int16_t)z, &ratio)) ratio = fe2_path_sw(value << 8);
    if (!ratio) return result;
    if (ratio <= 8) {
        fe2_render_segment segment;
        int built = fe2_render_build_segment(&b, &a, token & 0xffe0u, 0, d->inherited_material, d->scale, &segment);
        if (built < 0) { result.status = FE2_RENDER_PROGRAM_BAD_VALUE; return result; }
        if (!built) return result;
        if (d->count >= d->capacity) { result.status = FE2_RENDER_PROGRAM_OUTPUT_FULL; return result; }
        result.status = FE2_RENDER_PROGRAM_BAD_VALUE;
        if (!d->nodes || !d->segments) return result;
        d->segments[d->count] = segment;
        if (fe2_render_execution_insert(d, d->nodes + d->count, segment.key, fe2_render_segment_execute,
            d->segments + d->count)) return result;
        ++d->count;
        result.status = FE2_RENDER_PROGRAM_COMPLETE;
        return result;
    }
    {
        int32_t sx = (int32_t)a.screen.x - b.screen.x, sy = (int32_t)a.screen.y - b.screen.y;
        if (sx < -32768 || sx > 32767 || sy < -32768 || sy > 32767) return result;
        dx = (int16_t)sx; dy = (int16_t)sy;
    }
    distance = fe2_math_square_root32((uint32_t)((int32_t)dx * dx) + (uint32_t)((int32_t)dy * dy));
    if (fe2_path_sw(distance) < (int16_t)((uint16_t)ratio >> 4)) {
        int16_t x = fe2_path_sw((uint32_t)(uint16_t)(b.screen.x >> 1) + (uint16_t)(a.screen.x >> 1) + 1u);
        int16_t y = fe2_path_sw((uint32_t)(uint16_t)(b.screen.y >> 1) + (uint16_t)(a.screen.y >> 1) + 1u);
        status = lens_disc(d, ia, x, y, (int16_t)((uint16_t)ratio >> 2), material);
        if (status != FE2_RENDER_PROGRAM_COMPLETE) { result.status = status; return result; }
        if (fe2_path_sw(distance) < (int16_t)((uint16_t)ratio >> 6)) return result;
    }
    {
        /* Unit direction from the distance; a zero or overflowing divide
         * saturates (resume 0x7472/0x749b). DIV by three faults when the
         * distance's CWD is negative and keeps the distance. */
        int16_t q1 = saturating_ratio(dx, fe2_path_sw(distance)), q2 = saturating_ratio(dy, fe2_path_sw(distance));
        uint16_t third = distance >= 0x8000u ? distance : (uint16_t)(distance / 3u);
        uint16_t scale = (uint16_t)((third + ((uint16_t)ratio >> 4)) << 1);
        int16_t h1 = fe2_path_high((int32_t)q1 * fe2_path_sw(scale)), h2 = fe2_path_high((int32_t)q2 * fe2_path_sw(scale));
        int16_t k1 = fe2_path_high((int32_t)q2 * ratio), k2 = fe2_path_sw(0u - (uint16_t)fe2_path_high((int32_t)q1 * ratio));
        fe2_screen_point p1, p2;
        uint32_t key;
        p1.x = fe2_path_sw((uint32_t)(uint16_t)a.screen.x + (uint16_t)h1 - (uint16_t)k1);
        p1.y = fe2_path_sw((uint32_t)(uint16_t)a.screen.y + (uint16_t)h2 - (uint16_t)k2);
        p2.x = fe2_path_sw((uint32_t)(uint16_t)a.screen.x + (uint16_t)h1 + (uint16_t)k1);
        p2.y = fe2_path_sw((uint32_t)(uint16_t)a.screen.y + (uint16_t)h2 + (uint16_t)k2);
        result.status = FE2_RENDER_PROGRAM_BAD_VALUE;
        if (!d->execution->paths || d->queue->executing) return result;
        fe2_path_begin(&path, d->execution->paths);
        key = fe2_render_depth_key(a.camera, d->scale);
        status = fe2_path_curve(&path, b.screen, p1, p2, b.screen);
        if (status == FE2_RENDER_PROGRAM_COMPLETE) status = fe2_path_commit(d, &path, key, material);
        if (status != FE2_RENDER_PROGRAM_COMPLETE) fe2_path_discard(&path);
        result.status = status;
    }
    return result;
}

fe2_render_program_result fe2_render_execute_geometry_command(uint16_t token,
    fe2_render_bytes operands, fe2_render_program_state *state,
    fe2_render_program_drawing *d)
{
    fe2_render_program_result bad = {FE2_RENDER_PROGRAM_BAD_VALUE, 0, 0, 0};
    fe2_render_execution *e;
    if (!d || !state || !(e = d->execution) || !e->depth || e->depth > e->frame_capacity || !e->frames ||
        !d->queue || (operands.count && !operands.data)) return bad;
    switch (token & 31u) {
    case 5: return fe2_path_value(token, operands, state, d);
    case 16: case 17: return fe2_path_tube(token, operands, state, d);
    case 31: return fe2_render_execute_sphere_command(token, operands, state, d);
    case 9: return lens(token, operands, d);
    case 22: return four_vertex(token, operands, d);
    default: bad.status = FE2_RENDER_PROGRAM_UNSUPPORTED; return bad;
    }
}
