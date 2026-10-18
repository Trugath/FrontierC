/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "points.h"
#include "commands.h"

static uint16_t word(const uint8_t *p)
{
    return (uint16_t)((unsigned)p[0] | (unsigned)p[1] << 8);
}

static int signed_byte(unsigned value)
{
    return value < 128u ? (int)value : (int)value - 256;
}

int fe2_render_point_material(uint16_t token, uint32_t inherited, uint16_t *out)
{
    uint32_t value = (uint32_t)(token >> 4) & 0xfffu;
    if (!out) return -1;
    if ((value & 0xf00u) == 0xe00u)
        value = (value & 0x10u) | ((value & 0xeu) << 15) | ((value & 0xe0u) << 14);
    value &= ~UINT32_C(0xf101); /* and ax, 0xefe: high dword bits survive */
    if (value & 0x10u) value = (value & ~UINT32_C(0x10)) + inherited;
    if (value & UINT32_C(0xffff0000)) {
        uint32_t offset = value >> 12;
        if (offset < 14u) return -1;
        offset -= 14u;
        if (offset >= fe2_render_material_bytes || fe2_render_material_bytes - offset < 2) return -1;
        value = (uint16_t)((value & 0xfffu) + ((unsigned)fe2_render_material_data[offset] |
            (unsigned)fe2_render_material_data[offset + 1] << 8));
    }
    *out = (uint16_t)value;
    return 0;
}

int fe2_render_point_size(uint16_t value, uint16_t exponent, int32_t depth, uint16_t *size)
{
    /* sub cx,7 / jle: the signed test sees the true (unwrapped) difference. */
    uint16_t raw = (uint16_t)(exponent - 7u);
    int delta = (exponent < 32768u ? (int)exponent : (int)exponent - 65536) - 7;
    uint32_t numerator, denominator, dividend;
    int64_t quotient;
    if (value & 0x8000u) { *size = value & 0x7fffu; return 1; }
    if (delta <= 0) {
        unsigned count = (unsigned)(256u - (raw & 255u)) & 31u; /* neg cl */
        numerator = count >= 16 ? 0 : (uint32_t)(value >> count);
    } else {
        unsigned count = raw & 63u;
        numerator = count >= 32 ? 0 : (uint32_t)value << count;
    }
    if (depth <= 0) return 0;
    denominator = (uint32_t)depth;
    while (denominator >= 0x8000u) {
        numerator = (numerator >> 1) | (numerator & UINT32_C(0x80000000)); /* sar */
        denominator >>= 1;
    }
    dividend = numerator << 8;
    quotient = (dividend & UINT32_C(0x80000000) ? -(int64_t)(UINT32_MAX - dividend) - 1 : (int64_t)dividend) /
        (int64_t)denominator;
    if (quotient < -32768 || quotient > 32767) {
        *size = (uint16_t)((numerator & 255u) << 8);
        return 2;
    }
    *size = (uint16_t)quotient;
    return 1;
}

static void execute_points(void *target, const void *payload)
{
    fe2_render_execution_target *out = target;
    if (out && out->points) out->points(out->context, payload);
}

/* Returns 0 on success, -1 for an unavailable vertex, -2 for full storage. */
static int append(fe2_render_program_drawing *d, fe2_render_point_output *o, unsigned selector, size_t *count)
{
    fe2_vertex *v = fe2_render_execution_refresh(d, signed_byte(selector & 255u));
    uint16_t x, y;
    if (!v) return -1;
    if (v->view[2] < 64) return 0;
    x = (uint16_t)(v->screen[0] - 1); y = (uint16_t)(v->screen[1] - 1);
    if (y >= 167u || x >= 319u) return 0;
    if (o->point_count + *count >= o->point_capacity) return -2;
    o->points[o->point_count + *count].x = (int16_t)(x < 32768u ? (int)x : (int)x - 65536);
    o->points[o->point_count + *count].y = (int16_t)(y < 32768u ? (int)y : (int)y - 65536);
    ++*count;
    return 0;
}

fe2_render_program_result fe2_render_submit_points(uint16_t token, fe2_render_bytes operands,
    fe2_render_program_state *state, fe2_render_program_drawing *d)
{
    fe2_render_program_result result = {FE2_RENDER_PROGRAM_BAD_VALUE, 0, 0, 0};
    fe2_render_execution *e;
    fe2_render_execution_frame *f;
    fe2_render_point_output *o;
    fe2_render_point_command command;
    size_t words = operands.count / 2, count = 0;
    uint16_t operand, value, w;
    int status;
    if (!d || !state || !(e = d->execution) || !e->depth || !(o = e->points) || !d->queue ||
        d->queue->executing || (operands.count && !operands.data)) return result;
    f = e->frames + e->depth - 1;
    if (fe2_render_point_material(token, d->inherited_material, &command.material)) return result;
    if (!words) { result.status = FE2_RENDER_PROGRAM_TRUNCATED; return result; }
    operand = word(operands.data);
    if (!(operand & 0x80u)) value = (uint16_t)((operand >> 7) | (operand << 9));
    else {
        unsigned offset = (uint16_t)(operand << 1);
        if (offset & 0x80u) value = state->local[(offset & 0x7fu) / 2];
        else {
            offset &= 0xfe7fu;
            if (!state->model.data || offset / 2 >= state->model.count) return result;
            value = state->model.data[offset / 2];
        }
    }
    status = fe2_render_point_size(value, f->transform.exponent, f->vertices.offset[2], &command.size);
    result.cursor = 1;
    if (!status) {
        do {
            if (result.cursor >= words) { result.status = FE2_RENDER_PROGRAM_TRUNCATED; return result; }
            w = word(operands.data + result.cursor++ * 2);
        } while ((w & 255u) != 0x7fu);
        result.status = FE2_RENDER_PROGRAM_COMPLETE;
        return result;
    }
    result.divide_resumes = status == 2;
    if (!o->nodes || !o->commands || (!o->points && o->point_capacity)) return result;
    if (result.cursor >= words) { result.status = FE2_RENDER_PROGRAM_TRUNCATED; return result; }
    w = word(operands.data + result.cursor++ * 2);
    for (;;) {
        int r = append(d, o, w >> 8, &count);
        if (!r && (w & 255u) != 0x7fu) r = append(d, o, w, &count);
        if (r) { result.status = r == -2 ? FE2_RENDER_PROGRAM_OUTPUT_FULL : FE2_RENDER_PROGRAM_BAD_VALUE; return result; }
        if ((w & 255u) == 0x7fu) break;
        if (result.cursor >= words) { result.status = FE2_RENDER_PROGRAM_TRUNCATED; return result; }
        w = word(operands.data + result.cursor++ * 2);
        if ((w >> 8) == 0x7fu) break;
    }
    if (o->count >= o->capacity) { result.status = FE2_RENDER_PROGRAM_OUTPUT_FULL; return result; }
    command.key = fe2_render_depth_key(f->vertices.offset, d->scale);
    command.points = count ? o->points + o->point_count : NULL;
    command.count = count;
    o->commands[o->count] = command;
    if (fe2_render_execution_insert(d, o->nodes + o->count, command.key, execute_points, o->commands + o->count))
        return result;
    ++o->count;
    o->point_count += count;
    result.status = FE2_RENDER_PROGRAM_COMPLETE;
    return result;
}
