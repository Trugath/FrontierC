/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_GAME_CALL_ARGS_H
#define FE2_GAME_CALL_ARGS_H

/*
 * Numbered call parameters.
 *
 * Text, messages, object and math services use thirteen typed parameter slots.
 * Callers fill only the slots they supply, in call order. Byte and word writes
 * update the low part of a slot's integer value, retaining the other bits.
 */
#include "object.h"

enum { FE2_CALL_ARGS = 13 };

typedef enum fe2_arg_kind {
    FE2_ARG_UNSET,
    FE2_ARG_BYTE,
    FE2_ARG_WORD,
    FE2_ARG_DWORD,
    FE2_ARG_STRING,     /* pointer to a NUL-terminated name */
    FE2_ARG_SUBJECT,    /* pointer to an object; text uses its name */
    FE2_ARG_OBJECT,     /* pointer to an object record */
    FE2_ARG_TEMPLATE,   /* pointer to a native record template */
    FE2_ARG_OBJECTS     /* pointer to the system object list */
} fe2_arg_kind;

typedef struct fe2_call_arg {
    fe2_arg_kind kind;
    uint32_t value;
    const void *ref;
} fe2_call_arg;

typedef struct fe2_call_args {
    fe2_call_arg arg[FE2_CALL_ARGS];
} fe2_call_args;

void fe2_args_clear(fe2_call_args *args);
/* Byte and word stores into a numeric parameter keep its other bytes. */
void fe2_args_byte(fe2_call_args *args, unsigned n, uint8_t value);
void fe2_args_word(fe2_call_args *args, unsigned n, uint16_t value);
void fe2_args_dword(fe2_call_args *args, unsigned n, uint32_t value);
void fe2_args_string(fe2_call_args *args, unsigned n, const char *text);
/* A subject named only by text: the original points at a scratch object
 * whose name field holds `name`. */
void fe2_args_subject(fe2_call_args *args, unsigned n, const char *name);
void fe2_args_object(fe2_call_args *args, unsigned n, const fe2_object *object);
void fe2_args_template(fe2_call_args *args, unsigned n, const void *record);
void fe2_args_objects(fe2_call_args *args, unsigned n, const fe2_object_list *objects);

#endif
