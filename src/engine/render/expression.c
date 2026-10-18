/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "expression.h"

static int get(fe2_render_words words, size_t index, uint16_t *out)
{
    if (!words.data || index >= words.count) return -1;
    *out = words.data[index];
    return 0;
}

static int signed_word(uint16_t value)
{
    return value < 32768u ? (int)value : (int)value - 65536;
}

int fe2_render_expression_apply(fe2_render_operator op, uint16_t left, uint16_t right,
                                 const fe2_render_expression_sources *sources, uint16_t *out)
{
    uint16_t value;
    unsigned count = right & 31u;
    if (!out) return -1;
    switch (op) {
    case FE2_RENDER_ADD: value = (uint16_t)((unsigned)left + right); break;
    case FE2_RENDER_SUBTRACT: value = (uint16_t)((unsigned)left - right); break;
    case FE2_RENDER_MULTIPLY:
    case FE2_RENDER_MULTIPLY_ALIAS: value = (uint16_t)((uint32_t)left * right); break;
    case FE2_RENDER_DIVIDE:
        if (!right) { *out = left; return 1; }
        value = (uint16_t)(left / right); break;
    case FE2_RENDER_SHIFT_RIGHT: value = count < 16 ? (uint16_t)(left >> count) : 0; break;
    case FE2_RENDER_SHIFT_LEFT: value = count < 16 ? (uint16_t)((uint32_t)left << count) : 0; break;
    case FE2_RENDER_MAXIMUM: value = left > right ? left : right; break;
    case FE2_RENDER_MINIMUM_MINUS_ONE: value = left < right ? left : (uint16_t)(right - 1u); break;
    case FE2_RENDER_SHIFT_SIGNED:
        if (!count) value = left;
        else if (count >= 16) value = left & 0x8000u ? UINT16_MAX : 0;
        else value = (uint16_t)((left >> count) | (left & 0x8000u ? (65535u << (16 - count)) : 0u));
        break;
    case FE2_RENDER_MODEL_WORD:
        if (!sources || get(sources->model, ((unsigned)left + right) & 32767u, &value)) return -1;
        break;
    case FE2_RENDER_KEEP_IF_NOT_LESS: value = left >= right ? left : 0; break;
    case FE2_RENDER_KEEP_IF_NOT_GREATER: value = left <= right ? left : 0; break;
    case FE2_RENDER_SCALE:
    case FE2_RENDER_SCALE_BIASED: {
        uint16_t phase = (uint16_t)((unsigned)right + (op == FE2_RENDER_SCALE_BIASED ? 0x4000u : 0u));
        uint16_t factor;
        uint32_t product;
        if (!sources || get(sources->scale, (phase & 32767u) >> 4, &factor)) return -1;
        if (phase & 0x8000u) factor = (uint16_t)(0u - factor);
        product = (uint32_t)(signed_word(left) * signed_word(factor));
        value = (uint16_t)((product * 2u) >> 16);
        break;
    }
    case FE2_RENDER_AND: value = left & right; break;
    default: return -1;
    }
    *out = value;
    return 0;
}

int fe2_render_expression_operand(uint8_t selector, const fe2_render_expression_sources *sources,
                                   uint16_t *out)
{
    if (!out) return -1;
    if (selector & 0x80u) {
        if (!sources) return -1;
        return get(selector & 0x40u ? sources->local : sources->model, selector & 63u, out);
    }
    *out = selector & 0x40u ? (uint16_t)((selector & 63u) << 10) : selector;
    return 0;
}

int fe2_render_expression_evaluate(uint16_t token, uint16_t operands,
                                    const fe2_render_expression_sources *sources, uint16_t slots[8])
{
    uint16_t left, right, result;
    int status;
    if (!slots || fe2_render_expression_operand((uint8_t)operands, sources, &left) ||
        fe2_render_expression_operand((uint8_t)(operands >> 8), sources, &right)) return -1;
    status = fe2_render_expression_apply((fe2_render_operator)((token >> 4) & 15u), left, right, sources, &result);
    if (status >= 0) slots[(token >> 8) & 7u] = result;
    return status;
}
