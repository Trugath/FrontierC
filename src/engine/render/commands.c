/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "commands.h"

static uint32_t add_word(uint32_t value, uint16_t addend)
{
    return (value & UINT32_C(0xffff0000)) | (uint16_t)((uint16_t)value + (unsigned)addend);
}

int fe2_render_material_word(uint32_t index, uint16_t *out)
{
    if (!out || index < 14u) return -1;
    index -= 14u;
    if (index < fe2_render_material_bytes && fe2_render_material_bytes - index >= 2u) {
        *out = (uint16_t)((unsigned)fe2_render_material_data[index] |
                          (unsigned)fe2_render_material_data[index + 1] << 8);
        return 0;
    }
    /* Past the 592-byte table. Index 0x3f0 addresses image 0x29b6. */
    if (index == 994u) {
        *out = 0xd88bu;
        return 0;
    }
    return -1;
}

int fe2_render_line_material(uint16_t token, uint16_t base, uint32_t inherited, uint16_t *out)
{
    uint32_t value = token >> 4;
    unsigned add_inherited, no_base;
    if (!out) return -1;
    if ((value & 0xf00u) == 0xe00u)
        value = (value & 0x10u) | ((value & 0xeu) << 15) | ((value & 0xe0u) << 14);
    add_inherited = value & 0x10u;
    value &= ~UINT32_C(0x10);
    no_base = value & 0x100u;
    value &= ~UINT32_C(0x100);
    if (!no_base) value = add_word(value, base);
    if (add_inherited) value += inherited;
    if (value & UINT32_C(0xffff0000)) {
        uint16_t word;
        if (fe2_render_material_word(value >> 12, &word)) return -1;
        value = (uint16_t)((value & 0xfffu) + word);
    }
    *out = (uint16_t)value;
    return 0;
}

static int inside(fe2_screen_point p)
{
    return (uint16_t)p.x < 320u && (uint16_t)p.y < 168u;
}

static unsigned region(int16_t value, unsigned bound)
{
    if ((uint16_t)value == 0x8002u) return 16;
    if ((uint16_t)value < bound) return 4;
    return value < 0 ? 2 : 8;
}

static int rejected(fe2_screen_point a, fe2_screen_point b)
{
    unsigned x = region(a.x, 320) | region(b.x, 320);
    unsigned y = region(a.y, 168) | region(b.y, 168);
    return fe2_render_pair_rejection[x / 2 - 1] || fe2_render_pair_rejection[y / 2 - 1];
}

static int clip(fe2_screen_point *a, fe2_screen_point b,
                  const fe2_render_vertex *source_a, const fe2_render_vertex *source_b)
{
    if (source_a->camera[2] < 64) {
        if (source_b->camera[2] < 64) return 0;
        *a = fe2_render_project_near(source_a->camera, source_b->camera);
    }
    *a = fe2_render_clip_line(*a, b);
    return inside(*a);
}

int fe2_render_build_segment(const fe2_render_vertex *low, const fe2_render_vertex *high,
                               uint16_t token, uint16_t base, uint32_t inherited,
                               int16_t scale, fe2_render_segment *out)
{
    fe2_render_segment result;
    fe2_screen_point a, b, t;
    const fe2_render_vertex *sa = high, *sb = low, *st;
    if (!low || !high || !out || fe2_render_line_material(token, base, inherited, &result.color)) return -1;
    a = high->screen; b = low->screen;
    if (!inside(a)) {
        if (inside(b)) {
            if (!clip(&a, b, sa, sb)) return 0;
            goto ready;
        }
        if (rejected(a, b)) return 0;
        if (low->camera[2] < 64) {
            t = a; a = b; b = t;
            st = sa; sa = sb; sb = st;
        }
        if (!clip(&a, b, sa, sb)) return 0;
    } else if (inside(b)) goto ready;
    /* Clip the second endpoint, restoring the current command ordering. */
    if (!clip(&b, a, sb, sa)) return 0;
ready:
    result.first = a;
    result.second = b;
    result.key = fe2_render_depth_key(sa->camera, scale);
    *out = result;
    return 1;
}

void fe2_render_segment_execute(void *context, const void *payload)
{
    fe2_render_line_target *target = context;
    const fe2_render_segment *segment = payload;
    if (target && target->draw && segment)
        target->draw(target->context, segment->first, segment->second, segment->color);
}

int fe2_render_build_surface(const fe2_render_vertex *v, const fe2_screen_point *cached,
                               unsigned count, uint16_t token, uint16_t face,
                               uint32_t inherited, int16_t scale, fe2_render_surface_command *out)
{
    fe2_render_surface_command result = {0};
    fe2_render_vertex quad[4];
    unsigned i, all_inside = 1, any_inside = 0, x = 0, y = 0;
    if (!v || !cached || !out || (count != 3 && count != 4)) return -1;
    if (face & 0x8000u) return 0;
    if (fe2_render_line_material(token, face, inherited, &result.material)) return -1;
    for (i = 0; i < count; ++i) {
        int visible = inside(cached[i]);
        any_inside |= (unsigned)visible;
        all_inside &= (unsigned)visible;
        x |= region(cached[i].x, 320); y |= region(cached[i].y, 168);
    }
    result.key = fe2_render_depth_key(v[0].camera, scale);
    if (all_inside) {
        result.kind = count == 3 ? FE2_RENDER_SOLID_TRIANGLE : FE2_RENDER_SOLID_QUAD;
        result.polygon.count = count;
        for (i = 0; i < count; ++i) result.polygon.points[i] = cached[i];
    } else {
        if (!any_inside && (fe2_render_pair_rejection[x / 2 - 1] || fe2_render_pair_rejection[y / 2 - 1])) return 0;
        for (i = 0; i < count; ++i) quad[i] = v[i];
        if (count == 3) quad[3] = quad[2];
        if (fe2_render_clip_quad(quad, 0, &result.polygon) <= 0) return 0;
        result.kind = FE2_RENDER_CLIPPED_POLYGON;
    }
    *out = result;
    return 1;
}

void fe2_render_surface_execute(void *context, const void *payload)
{
    fe2_render_drawing_target *target = context;
    if (target && target->surface && payload) target->surface(target->surface_context, payload);
}
