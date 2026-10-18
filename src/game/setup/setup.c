/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the system check, 0x4af sound selection, 0x4dc CPU and FPU
 * detection, 0x57c..0x615 the requirements. */
#include "setup.h"

enum {
    FPU_PRESENT_CONTROL_MASK = 0x103f, FPU_PRESENT_CONTROL = 0x3f,
    FPU_EQUAL = 0x4000                 /* C3: the infinities compared equal */
};

static int fail(const fe2_setup_platform *p, const char *message)
{
    p->print(p->opaque, message);
    return FE2_SETUP_FAILED;
}

/* Any major version but 0 passes (the message asks for 2.00). */
static int check_dos(const fe2_setup_platform *p)
{
    return p->dos_major_version(p->opaque) != 0 ? FE2_SETUP_PASSED : fail(p, fe2_setup_dos_message);
}

/* The CPU from the FLAGS high bits and EFLAGS AC; an FPU initializes
 * with status 0 and its default control word. A 386's coprocessor that
 * compares the opposite infinities equal (projective) is a 287. */
static void detect_processor(fe2_setup_state *s, const fe2_setup_platform *p)
{
    uint8_t cpu = FE2_CPU_8086, fpu = 0;
    if (!p->flags_high_bits_stuck(p->opaque)) {
        cpu = FE2_CPU_286;
        if (p->flags_high_bits_settable(p->opaque)) {
            cpu = FE2_CPU_386;
            if (p->alignment_check_toggles(p->opaque)) cpu = FE2_CPU_486;
        }
    }
    if ((p->fpu_status(p->opaque) & 0xff) == 0 &&
        (p->fpu_control(p->opaque) & FPU_PRESENT_CONTROL_MASK) == FPU_PRESENT_CONTROL) {
        fpu = cpu;
        if (cpu == FE2_CPU_386 && (p->fpu_infinity_status(p->opaque) & FPU_EQUAL)) --fpu;
    }
    s->cpu_class = cpu;
    s->fpu_class = fpu;
}

static int check_cpu(fe2_setup_state *s, const fe2_setup_platform *p)
{
    detect_processor(s, p);
    return s->cpu_class >= FE2_CPU_386 ? FE2_SETUP_PASSED : fail(p, fe2_setup_cpu_message);
}

static int check_base_memory(const fe2_setup_state *s, const fe2_setup_platform *p)
{
    return s->free_base_paragraphs >= FE2_SETUP_BASE_PARAGRAPHS ? FE2_SETUP_PASSED
                                                                : fail(p, fe2_setup_base_memory_message);
}

/* The EMS driver's device must be a character device (information bit 7)
 * that is ready; it is closed before the EMS calls. */
static int ems_device_ready(const fe2_setup_platform *p, uint16_t handle)
{
    uint16_t information;
    uint8_t status;
    return p->device_information(p->opaque, handle, &information) == 0 && (information & 0x80) &&
           p->input_status(p->opaque, handle, &status) == 0 && status != 0;
}

static int check_ems(const fe2_setup_platform *p)
{
    uint16_t handle;
    if (p->open_device(p->opaque, fe2_setup_ems_device, &handle) != 0) return fail(p, fe2_setup_ems_missing_message);
    if (!ems_device_ready(p, handle)) {
        p->close_file(p->opaque, handle);
        return fail(p, fe2_setup_ems_missing_message);
    }
    if (p->close_file(p->opaque, handle) != 0) return fail(p, fe2_setup_ems_missing_message);
    if (p->ems_status(p->opaque) != 0) return fail(p, fe2_setup_ems_missing_message);
    if (p->ems_free_pages(p->opaque) < FE2_SETUP_EMS_PAGES) return fail(p, fe2_setup_ems_memory_message);
    if (p->ems_page_frame(p->opaque) != 0) return fail(p, fe2_setup_ems_missing_message);
    return FE2_SETUP_PASSED;
}

/* Keys 1..4; anything else is read again. 4 (no sound) keeps the
 * selection. */
static void select_sound(fe2_setup_state *s, const fe2_setup_platform *p)
{
    p->print(p->opaque, fe2_setup_sound_menu);
    for (;;) {
        switch (p->read_key(p->opaque)) {
        case '1': s->sound_selection = FE2_SOUND_ROLAND; return;
        case '2': s->sound_selection = FE2_SOUND_ADLIB; return;
        case '3': s->sound_selection = FE2_SOUND_SPEAKER; return;
        case '4': return;
        default: break;
        }
    }
}

int fe2_setup_check_system(fe2_setup_state *s, const fe2_setup_platform *p)
{
    if (check_dos(p) || check_cpu(s, p) || check_base_memory(s, p) || check_ems(p)) return FE2_SETUP_FAILED;
    select_sound(s, p);
    return FE2_SETUP_PASSED;
}
