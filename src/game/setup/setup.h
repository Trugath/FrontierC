/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SETUP_H
#define FE2_SETUP_H

/*
 * The system check and sound selection (setup), run before the game
 * starts. The checks run in order, and the first to fail prints its message:
 * - a DOS version;
 * - an 80386 or later (the CPU and FPU are classified);
 * - 580K of free base memory;
 * - an EMS driver with 768K free and a page frame.
 * Then the player chooses the sound support.
 *
 * DOS, EMS and the processor observations are reached through
 * `fe2_setup_platform`.
 */
#include <stdint.h>

enum { FE2_SETUP_PASSED = 0, FE2_SETUP_FAILED = 1 };

/* Processor classes; an FPU's class is its CPU's (a 287 with a 386 is 2),
 * or 0 for none. */
enum { FE2_CPU_8086 = 1, FE2_CPU_286 = 2, FE2_CPU_386 = 3, FE2_CPU_486 = 4 };

/* Sound selections. */
enum { FE2_SOUND_ROLAND = 1, FE2_SOUND_ADLIB = 2, FE2_SOUND_SPEAKER = 4 };

enum {
    FE2_SETUP_BASE_PARAGRAPHS = 0x9100,   /* 580K */
    FE2_SETUP_EMS_PAGES = 48              /* 768K */
};

typedef struct fe2_setup_state {
    uint16_t free_base_paragraphs;
    uint8_t sound_selection;              /*; kept when the player chooses no sound */
    uint8_t cpu_class;
    uint8_t fpu_class;
} fe2_setup_state;

/* Calls that can fail return nonzero for a failure (DOS carry). */
typedef struct fe2_setup_platform {
    void *opaque;
    /* DOS (INT 0x21). */
    uint8_t (*dos_major_version)(void *opaque);                                       /* 0x30 */
    int (*open_device)(void *opaque, const char *name, uint16_t *handle);             /*, read */
    int (*device_information)(void *opaque, uint16_t handle, uint16_t *information);
    int (*input_status)(void *opaque, uint16_t handle, uint8_t *status);
    int (*close_file)(void *opaque, uint16_t handle);                                 /* 0x3e */
    void (*print)(void *opaque, const char *text);                                    /* 0x9 */
    uint8_t (*read_key)(void *opaque);                                                /* 0x7, no echo */
    /* EMS (INT 0x67). */
    uint8_t (*ems_status)(void *opaque);                                              /* 0x40: AH */
    uint16_t (*ems_free_pages)(void *opaque);                                         /* 0x42: BX */
    uint8_t (*ems_page_frame)(void *opaque);                                          /* 0x41: AH */
    /* The processor. */
    int (*flags_high_bits_stuck)(void *opaque);    /* FLAGS bits 12..15 stay set when cleared (8086) */
    int (*flags_high_bits_settable)(void *opaque); /* any of them can be set (386) */
    int (*alignment_check_toggles)(void *opaque);  /* EFLAGS AC can be changed (486) */
    uint16_t (*fpu_status)(void *opaque);          /* after FNINIT */
    uint16_t (*fpu_control)(void *opaque);         /* after FNINIT */
    uint16_t (*fpu_infinity_status)(void *opaque); /* after comparing +inf with -inf */
} fe2_setup_platform;

/* FE2_SETUP_PASSED or FE2_SETUP_FAILED. */
int fe2_setup_check_system(fe2_setup_state *state, const fe2_setup_platform *platform);

/* Native tables (tools/optional_tables_c.py). */
extern const char fe2_setup_dos_message[];
extern const char fe2_setup_cpu_message[];
extern const char fe2_setup_base_memory_message[];
extern const char fe2_setup_ems_missing_message[];
extern const char fe2_setup_ems_memory_message[];
extern const char fe2_setup_sound_menu[];
extern const char fe2_setup_ems_device[];       /* "EMMXXXX0" */

#endif
