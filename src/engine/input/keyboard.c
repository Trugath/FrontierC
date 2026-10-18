/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the keyboard decoder and the input code. */
#include "input.h"
#include <string.h>

enum { PREFIX_E0 = 1, PREFIX_TOGGLE = 2, RELEASED = 0x80, UNTRANSLATED = 0xff };

void fe2_input_init(fe2_input_state *in)
{
    memset(in, 0, sizeof(*in));
    memset(in->keys, RELEASED, sizeof(in->keys));
}

void fe2_input_scancode(fe2_input_state *in, fe2_pointer_state *p, uint8_t byte)
{
    unsigned key, index;
    uint8_t code;
    if (byte == FE2_KEY_E0) { in->prefix |= PREFIX_E0; return; }
    if (byte == FE2_KEY_TOGGLE) in->prefix ^= PREFIX_TOGGLE;
    key = byte & 0x7fu;
    if (in->prefix & PREFIX_E0) {
        in->prefix &= (uint8_t)~PREFIX_E0;
        key |= 0x80u;
    }
    in->keys[key] = byte & RELEASED;
    if (byte & RELEASED) return;
    if (key != in->last_make) { in->key_repeat = 1; in->last_make = (uint8_t)key; }
    /* Without a Shift the index gains bit 7 (the unshifted half). E0 keys
     * already have it and read from 0x8808 + scan code, so they always
     * translate as shifted. */
    index = key | (in->keys[FE2_KEY_LEFT_SHIFT] & in->keys[FE2_KEY_RIGHT_SHIFT]);
    code = fe2_input_translation[key & 0x80u ? index - 0x80u : index];
    if (code == UNTRANSLATED || in->suppress) return;
    p->input_code = code;
}

uint8_t fe2_input_poll(fe2_pointer_state *p, int *screenshot)
{
    uint8_t code;
    *screenshot = p->input_code == FE2_INPUT_SCREENSHOT;
    if (*screenshot) p->input_code = 0;
    code = p->input_code; p->input_code = 0;
    return code;
}

void fe2_input_restore(fe2_pointer_state *p, uint8_t code) { p->input_code = code; }
