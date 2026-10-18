/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "call_args.h"

#include <string.h>

static fe2_call_arg *slot(fe2_call_args *args, unsigned n)
{
    return n < FE2_CALL_ARGS ? &args->arg[n] : 0;
}

static int numeric(const fe2_call_arg *arg)
{
    return arg->kind == FE2_ARG_BYTE || arg->kind == FE2_ARG_WORD ||
           arg->kind == FE2_ARG_DWORD;
}

static void set(fe2_call_args *args, unsigned n, fe2_arg_kind kind, uint32_t value,
                const void *ref)
{
    fe2_call_arg *arg = slot(args, n);
    if (arg == 0) return;
    arg->kind = kind;
    arg->value = value;
    arg->ref = ref;
}

void fe2_args_clear(fe2_call_args *args)
{
    memset(args, 0, sizeof *args);
}

void fe2_args_byte(fe2_call_args *args, unsigned n, uint8_t value)
{
    fe2_call_arg *arg = slot(args, n);
    if (arg == 0) return;
    if (numeric(arg)) arg->value = (arg->value & ~UINT32_C(0xff)) | value;
    else set(args, n, FE2_ARG_BYTE, value, 0);
}

void fe2_args_word(fe2_call_args *args, unsigned n, uint16_t value)
{
    fe2_call_arg *arg = slot(args, n);
    if (arg == 0) return;
    if (arg->kind == FE2_ARG_DWORD) arg->value = (arg->value & ~UINT32_C(0xffff)) | value;
    else set(args, n, FE2_ARG_WORD, value, 0);
}

void fe2_args_dword(fe2_call_args *args, unsigned n, uint32_t value)
{
    set(args, n, FE2_ARG_DWORD, value, 0);
}

void fe2_args_string(fe2_call_args *args, unsigned n, const char *text)
{
    set(args, n, FE2_ARG_STRING, 0, text);
}

void fe2_args_subject(fe2_call_args *args, unsigned n, const char *name)
{
    set(args, n, FE2_ARG_SUBJECT, 0, name);
}

void fe2_args_object(fe2_call_args *args, unsigned n, const fe2_object *object)
{
    set(args, n, FE2_ARG_OBJECT, 0, object);
}

void fe2_args_template(fe2_call_args *args, unsigned n, const void *record)
{
    set(args, n, FE2_ARG_TEMPLATE, 0, record);
}

void fe2_args_objects(fe2_call_args *args, unsigned n, const fe2_object_list *objects)
{
    set(args, n, FE2_ARG_OBJECTS, 0, objects);
}
