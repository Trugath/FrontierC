/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_FRONTIER_INPUT_H
#define FE2_FRONTIER_INPUT_H

/*
 * Keyboard and timer input: the keyboard decoder (image 0xccff..0xcdea) and
 * the input part of the background tick, run every fourth
 * timer tick: Ctrl+C, the button hold counts, the countdown, the pointer's
 * hold count and press age, the two keyboard flight axes and the key
 * repeat. The tick's time accumulation  is the
 * session's (`fe2_session_ticks`).
 *
 * The input code  the keyboard and the pointer publish is the
 * pointer state's `input_code`.
 */
#include <stddef.h>
#include <stdint.h>

#include "pointer/pointer.h"

typedef struct fe2_input_state {
    uint16_t primary_hold[2];    /* (counts while nonzero), 0x9d6: cleared on release */
    uint16_t secondary_hold[2];
    uint16_t countdown;          /* down to zero */
    int16_t axes[2];             /* ('.' and ','), 0x5bfd ('A' and 'Z') */
    uint16_t key_repeat;         /* counts while the last key stays down */
    uint8_t last_make;           /* the last key pressed */
    uint8_t keys[256];           /* by scan code (E0 codes +0x80); zero while pressed, else 0x80 */
    uint8_t prefix;              /* the decoder's flags: bit 0 an E0 prefix pending, bit 1 toggled by 0x45 */
    uint8_t suppress;            /* nonzero keeps keys from the input code */
} fe2_input_state;

/* The key translation : shifted codes by scan code, then unshifted
 * (0xff for none), generated from the resources. */
extern const uint8_t *fe2_input_translation;
int fe2_input_bind(const uint8_t *image, size_t count);

/* The initial image: every key released, no prefix. */
void fe2_input_init(fe2_input_state *input);
/* one byte from the keyboard (set 1 scan codes, E0 prefixes). Key
 * states follow makes and breaks; a new make restarts the key repeat; a
 * translated make is published to the input code unless suppressed. */
void fe2_input_scancode(fe2_input_state *input, fe2_pointer_state *pointer, uint8_t byte);
/* take the input code (0 for none); 0xfd is the screenshot action,
 * reported in *screenshot and consumed. 0xcde7: put a code back. */
uint8_t fe2_input_poll(fe2_pointer_state *pointer, int *screenshot);
void fe2_input_restore(fe2_pointer_state *pointer, uint8_t code);

enum {
    FE2_KEY_CTRL = 0x1d, FE2_KEY_RIGHT_CTRL = 0x9d, FE2_KEY_C = 0x2e,
    FE2_KEY_LEFT_SHIFT = 0x2a, FE2_KEY_RIGHT_SHIFT = 0x36, FE2_KEY_E0 = 0xe0, FE2_KEY_TOGGLE = 0x45,
    FE2_INPUT_SCREENSHOT = 0xfd,
    FE2_KEY_A = 0x1e, FE2_KEY_Z = 0x2c, FE2_KEY_COMMA = 0x33, FE2_KEY_PERIOD = 0x34
};

/* one subtick. Returns 1 when Ctrl+C asks to exit to DOS (nothing
 * else is changed), 0 otherwise. */
int fe2_input_tick(fe2_input_state *input, fe2_pointer_state *pointer);

#endif
