/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "attachment.h"
#include <string.h>

static int word(fe2_render_bytes bank, size_t at, uint16_t *value)
{
    if (!bank.data || at >= bank.count || bank.count - at < 2) return -1;
    *value = (uint16_t)((unsigned)bank.data[at] + (unsigned)bank.data[at + 1] * 256u);
    return 0;
}
static int16_t signed_word(uint16_t value)
{
    return value < 32768 ? (int16_t)value : (int16_t)(-1 - (int)(65535u - value));
}
static uint32_t shift12(uint32_t n)
{
    return n >> 12 | (n & UINT32_C(0x80000000) ? UINT32_C(0xfff00000) : 0);
}
int fe2_render_place_attachment(fe2_object *source, const fe2_object *converted,
    fe2_render_bytes bank, size_t descriptor, const fe2_render_attachment_state *state, fe2_object *output)
{
    fe2_object placed;
    uint16_t extension, first, second;
    uint32_t delta[3];
    unsigned branch, offset, i;
    uint8_t flags;
    if (!source || !converted || !state || !output || descriptor > 65535u ||
        word(bank, descriptor + 0x1c, &extension)) return -1;
    flags = source->frame_state;
    branch = flags & 4 ? 0 : flags & 8 ? 1 : 2;
    offset = branch == 0 ? 0x20 : branch == 1 ? 0x24 : 0x28;
    if (word(bank, (uint16_t)(descriptor + extension + offset), &first) ||
        word(bank, (uint16_t)(descriptor + extension + offset + 2), &second)) return -1;
    if (branch == 2 && signed_word(state->angle[1]) >= 0) {
        /* Original reload uses the newly loaded second value as bank base. */
        size_t base = second;
        if (word(bank, (uint16_t)(base + extension + 0x2c), &first) ||
            word(bank, (uint16_t)(base + extension + 0x2e), &second)) return -1;
    }
    placed = *converted;
    if (branch == 1) {
        for (i = 2; i < 9; i += 3) placed.orientation[i] = signed_word((uint16_t)(0u - (uint16_t)placed.orientation[i]));
        first = (uint16_t)(0u - first);
    }
    for (i = 0; i < 3; ++i) {
        uint32_t sum = (uint32_t)((int32_t)placed.orientation[i * 3 + 2] * signed_word(first));
        sum += (uint32_t)((int32_t)placed.orientation[i * 3 + 1] * signed_word(second));
        delta[i] = shift12(sum);
    }
    if (branch == 2) {
        fe2_math_rotate_pair(placed.orientation + 2, placed.orientation, state->angle[0]);
        fe2_math_rotate_pair(placed.orientation + 1, placed.orientation + 2, state->angle[1]);
    }
    for (i = 0; i < 3; ++i) {
        uint32_t old = placed.position_low[i];
        placed.position_low[i] += delta[i];
        placed.position_high[i] += (delta[i] & UINT32_C(0x80000000) ? UINT32_MAX : 0u) + (placed.position_low[i] < old);
    }
    placed.size_mantissa = (uint16_t)((unsigned)state->size_high * 256u +
        placed.as.craft.installed[branch == 2 && signed_word(state->angle[1]) >= 0 ? 3 : branch]);
    placed.model = 0xbe;
    source->frame_state = (uint8_t)(flags & ~(branch == 0 ? 4u : branch == 1 ? 8u : 16u));
    *output = placed;
    if (output == source) output->frame_state = (uint8_t)(flags & ~(branch == 0 ? 4u : branch == 1 ? 8u : 16u));
    return 0;
}
int fe2_render_basis_attachment(const fe2_object *source, fe2_object *output)
{
    int16_t basis[9];
    if (!source || !output) return -1;
    memcpy(basis, source->station_slot ? source->alternate_orientation : source->orientation, sizeof(basis));
    /* The recovered temporary initializes only the fields consumed here. */
    memset(output, 0, sizeof(*output));
    memcpy(output->orientation, basis, sizeof(basis));
    output->camera_offset[2] = 0x200; output->model = 0x180;
    return 0;
}
int fe2_render_object_attachment(fe2_object *source, const fe2_render_attachment_state *state,
    const fe2_render_attachment_services *s)
{
    fe2_object converted, placed;
    fe2_render_bytes bank;
    size_t descriptor;
    if (!source || !s || !s->convert || !s->model || !s->render ||
        s->convert(s->context, source, &converted) ||
        s->model(s->context, converted.model, &bank, &descriptor) ||
        fe2_render_place_attachment(source, &converted, bank, descriptor, state, &placed)) return -1;
    return s->render(s->context, &placed);
}
