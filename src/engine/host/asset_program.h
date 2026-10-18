/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_ASSET_PROGRAM_H
#define FE2_ASSET_PROGRAM_H

#include "asset_util.h"

/* Compile the editable model sources. base is the directory of models.ini. */
int fe2_compile_geometry(const char *text, fe2_buf *out);
int fe2_compile_contact(const char *text, fe2_buf *out);
int fe2_compile_offsets(const char *text, fe2_buf *out);
int fe2_compile_program(const char *text, const char *base, const char *names_path, fe2_buf *out);
int fe2_ship_record(const char *csv, const char *model, const char *names_path, fe2_buf *out);
int fe2_operand_count(unsigned token, const unsigned *words, size_t count, int *out);
/* Decode a renderer program to instruction text. entries may be NULL. */
int fe2_decode_program(const uint8_t *data, size_t count, const unsigned *entries, size_t entry_count, char **text);
int fe2_decode_geometry(const uint8_t *data, size_t count, char **text);
int fe2_decode_contact(const uint8_t *data, size_t count, size_t start, char **text, size_t *next);
int fe2_decode_offsets(const uint8_t *data, size_t count, char **text);

#endif
