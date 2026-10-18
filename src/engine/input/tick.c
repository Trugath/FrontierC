/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the background tick's input part. */
#include "input.h"

enum { DECAY = 300, STEP = 700 };

static int pressed(const fe2_input_state *in, unsigned key) { return in->keys[key] == 0; }

static void hold(uint16_t counter[2], uint8_t button)
{
    if (!counter[0]) return;
    ++counter[0];
    if (!button) counter[0] = counter[1] = 0;
}

/* Decay toward zero, then the positive key's step, then the negative's,
 * each from zero when the axis points the other way, saturating. */
static int16_t axis(const fe2_input_state *in, int16_t value, unsigned positive, unsigned negative)
{
    int32_t v = value;
    if (v > 0) { v -= DECAY; if (v < 0) v = 0; }
    else if (v < 0) { v += DECAY; if (v > 0) v = 0; }
    if (pressed(in, positive)) {
        if (v < 0) v = 0;
        v += STEP; if (v > INT16_MAX) v = INT16_MAX;
    }
    if (pressed(in, negative)) {
        if (v > 0) v = 0;
        v -= STEP; if (v < INT16_MIN) v = -INT16_MAX;
    }
    return (int16_t)v;
}

int fe2_input_tick(fe2_input_state *in, fe2_pointer_state *p)
{
    /* Either Ctrl: the AND of their bytes is zero (as bytes, not only
     * when one of them is zero). */
    if (!(in->keys[FE2_KEY_CTRL] & in->keys[FE2_KEY_RIGHT_CTRL]) && pressed(in, FE2_KEY_C)) return 1;
    hold(in->primary_hold, p->primary);
    hold(in->secondary_hold, p->secondary);
    if (in->countdown) --in->countdown;
    if (p->hold) ++p->hold;
    if (p->age[0] != 0xff) ++p->age[0];
    in->axes[0] = axis(in, in->axes[0], FE2_KEY_PERIOD, FE2_KEY_COMMA);
    in->axes[1] = axis(in, in->axes[1], FE2_KEY_A, FE2_KEY_Z);
    if (in->key_repeat) {
        if (!pressed(in, in->last_make)) in->key_repeat = 0;
        else ++in->key_repeat;
    }
    return 0;
}
