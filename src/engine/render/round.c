/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "execution.h"
#include <string.h>

static int16_t w(uint32_t value)
{
    uint16_t bits = (uint16_t)value;
    return (int16_t)(bits < 32768u ? (int)bits : (int)bits - 65536);
}
static int32_t d(uint32_t value)
{
    return value < UINT32_C(0x80000000) ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}
static uint32_t sar(uint32_t value, unsigned shift)
{
    if (!shift) return value;
    if (shift >= 32) return value & UINT32_C(0x80000000) ? UINT32_MAX : 0;
    return value >> shift | (value & UINT32_C(0x80000000) ? UINT32_MAX << (32 - shift) : 0);
}
static uint32_t magnitude(int32_t value) { return value < 0 ? 0u - (uint32_t)value : (uint32_t)value; }
static int16_t product(int16_t a, int16_t b, unsigned shift)
{
    return w(((uint32_t)((int32_t)a * b) << shift) >> 16);
}
static int16_t quotient(int32_t numerator, int16_t divisor)
{
    int64_t answer;
    if (!divisor) return w((uint32_t)numerator);
    answer = (int64_t)numerator / divisor;
    return answer < -32768 || answer > 32767 ? w((uint32_t)numerator) : (int16_t)answer;
}
static int material(uint16_t token, uint32_t inherited, uint16_t *out)
{
    /* Round producers never add the face base and clear selector bit zero. */
    return fe2_render_line_material(token & 0xffe0u, 0, inherited, out);
}
static fe2_screen_point point(int32_t x, int32_t y)
{
    fe2_screen_point result = {w((uint32_t)x), w((uint32_t)y)};
    return result;
}

static int build_round(const fe2_render_vertex *v, uint32_t radius, uint16_t token,
    uint16_t control, const fe2_render_model_transform *t, fe2_render_round_command *out, int distant)
{
    fe2_render_round_command result;
    uint32_t depth, original_radius = radius;
    int16_t x, y, diameter, half;
    uint16_t color;
    int shaded = !((token >> 4) & 0x100u);
    if (!v || !t || !out) return -1;
    if (v->camera[2] < 64) return 0;
    memset(&result, 0, sizeof(result));
    depth = (uint32_t)v->camera[2];
    if (distant || !( ((depth >> 3) - radius) & UINT32_C(0x80000000))) {
        uint32_t numerator;
        while (depth > 32767u) { radius = sar(radius, 1); depth >>= 1; }
        numerator = radius << 8;
        diameter = quotient(d(numerator), (int16_t)depth);
        x = v->screen.x; y = v->screen.y;
    } else {
        uint32_t r = radius >> 1, components[3], bits, normalization = 0;
        uint32_t xx, yy, zz, rr, denominator, ratio, factor, sum;
        int16_t hx, hy, hz;
        if ((uint32_t)v->camera[2] <= r) return 0;
        bits = r;
        for (unsigned i = 0; i < 3; ++i) { components[i] = (uint32_t)v->camera[i]; bits |= magnitude(v->camera[i]); }
        if (bits) {
            bits <<= 1;
            if (!bits) return -1;
            while (!(bits & UINT32_C(0x80000000))) { bits <<= 1; ++normalization; }
        }
        for (unsigned i = 0; i < 3; ++i) components[i] <<= normalization;
        r <<= normalization;
        hx = w(components[0] >> 16); hy = w(components[1] >> 16); hz = w(components[2] >> 16);
        xx = (uint32_t)((int32_t)hx * hx) * 2u;
        yy = (uint32_t)((int32_t)hy * hy) * 2u;
        zz = (uint32_t)((int32_t)hz * hz) * 2u;
        rr = (uint32_t)((int32_t)w(r >> 16) * w(r >> 16)) * 2u;
        denominator = zz - rr;
        if (!(denominator >> 16)) return 0;
        ratio = (components[2] >> 8) / (denominator >> 16);
        if (ratio > 65535u) return 0; /* original divide resume rejects */
        x = w((uint16_t)product(hx, w(ratio), 1) + 160u);
        y = w(84u - (uint16_t)product(hy, w(ratio), 1));
        factor = sar(r, 5) / (denominator >> 16);
        if (factor > 65535u) return 0;
        sum = (denominator + xx + yy) >> 1;
        diameter = product((int16_t)(fe2_math_square_root32(sum) >> 1), w(factor), 0);
    }
    half = (int16_t)((uint16_t)diameter >> 1);
    if ((uint16_t)x >= 320u) {
        if (x >= 0 ? w((uint16_t)x - (uint16_t)half) >= 320 : w((uint16_t)x + (uint16_t)half) < 0) return 0;
    }
    if ((uint16_t)y >= 168u) {
        if (y >= 0 ? w((uint16_t)y - (uint16_t)half) >= 168 : w((uint16_t)y + (uint16_t)half) <= 0) return 0;
    }
    if (material(token, t->material, &color)) return -1;
    result.key = fe2_render_depth_key(v->camera, w(t->scale));
    result.size = diameter;
    if (shaded && diameter >= 9 && t->direction[2] >= -32000 && t->direction[2] <= 32000) {
        int16_t a = t->direction[0], b = t->direction[1], c = t->direction[2];
        uint32_t square = (uint32_t)((int32_t)a * a) + (uint32_t)((int32_t)b * b);
        int16_t divisor = w(fe2_math_square_root32(square) + 8u);
        int16_t raw_a = product(quotient((int32_t)a * 32768, divisor), half, 1);
        int16_t raw_b = product(quotient((int32_t)w(0u - (uint16_t)b) * 32768, divisor), half, 1);
        int16_t ux = raw_b, uy = w(0u - (uint16_t)raw_a);
        int16_t vx = product(raw_a, -21846, 2), vy = product(raw_b, -21846, 2);
        int16_t wx = product(w(0u - (uint16_t)c), vx, 1), wy = product(w(0u - (uint16_t)c), vy, 1);
        fe2_screen_point start = point((int32_t)x + ux, (int32_t)y + uy);
        fe2_screen_point end = point((int32_t)x - ux, (int32_t)y - uy);
        result.kind = FE2_RENDER_SHADED_CURVE;
        result.center = point(x, y);
        result.material[0] = color; result.material[1] = (uint16_t)(color + t->shades[3]);
        result.curves[0][0] = start; result.curves[0][1] = point((int32_t)start.x - vx, (int32_t)start.y - vy);
        result.curves[0][2] = point((int32_t)end.x - vx, (int32_t)end.y - vy); result.curves[0][3] = end;
        result.curves[1][0] = end; result.curves[1][1] = point((int32_t)end.x + wx, (int32_t)end.y + wy);
        result.curves[1][2] = point((int32_t)start.x + wx, (int32_t)start.y + wy); result.curves[1][3] = start;
        result.curves[2][0] = start; result.curves[2][1] = point((int32_t)start.x + vx, (int32_t)start.y + vy);
        result.curves[2][2] = point((int32_t)end.x + vx, (int32_t)end.y + vy); result.curves[2][3] = end;
        memcpy(result.curves[3], result.curves[1], sizeof(result.curves[3]));
    } else {
        int16_t size = diameter;
        result.kind = FE2_RENDER_DISC;
        if (shaded && (diameter < 9 ? t->direction[2] >= -28672 : t->direction[2] > 32000)) color = (uint16_t)(color + t->shades[0]);
        x = w((uint16_t)x - 1u); y = w((uint16_t)y - 1u);
        if (!size && (control & 0x80u)) {
            uint32_t denominator = (uint32_t)v->camera[2] >> 8, numerator = original_radius;
            int16_t sample;
            while (denominator > 32767u) { numerator = sar(numerator, 1); denominator >>= 1; }
            sample = quotient(d(numerator << 8), (int16_t)denominator);
            if (sample) {
                unsigned index = (uint16_t)sample >> 3;
                if (index >= 33) return -1;
                size = (int16_t)(fe2_render_curve_coefficients[index] < 128u ? fe2_render_curve_coefficients[index] :
                    (int)fe2_render_curve_coefficients[index] - 256);
                result.kind = FE2_RENDER_SMALL_DISC;
            }
        } else if (size && size <= 9) result.kind = FE2_RENDER_SMALL_DISC;
        if (result.kind == FE2_RENDER_SMALL_DISC) {
            if ((uint16_t)x >= 320 || (uint16_t)y >= 168) return 0;
            if (control & 0x80u) { result.kind = FE2_RENDER_MARKER; x = w((uint16_t)x - 1u); y = w((uint16_t)y - 1u); }
        }
        result.size = size; result.center = point(x, y);
        result.material[0] = color; result.material[1] = (uint16_t)(((color & 0xeeeu) >> 1) + 0x222u);
    }
    *out = result;
    return 1;
}

int fe2_render_build_round(const fe2_render_vertex *v, uint32_t radius, uint16_t token,
    uint16_t control, const fe2_render_model_transform *t, fe2_render_round_command *out)
{ return build_round(v, radius, token, control, t, out, 0); }

/* enters the shared producer at 0x4bd0, bypassing the analytic sphere
 * projection. Keep its wrapped diameter shift and ordinary round shading. */
int fe2_render_build_distant(const fe2_render_vertex *v, uint16_t radius, uint16_t token,
    const fe2_render_model_transform *t, fe2_render_round_command *out)
{
    unsigned shift = (t->exponent + 1u) & 63u;
    uint32_t diameter = shift >= 32 ? 0 : (uint32_t)radius << shift;
    return build_round(v, diameter, (uint16_t)((token & 0xfffu) << 4), token >> 8, t, out, 1);
}
