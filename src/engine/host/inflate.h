/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_INFLATE_H
#define FE2_INFLATE_H

#include <stddef.h>
#include <stdint.h>

/* Inflate a raw DEFLATE stream or a zlib wrapper. Caller frees *out. */
int fe2_inflate(const uint8_t *data, size_t count, uint8_t **out, size_t *out_count);
int fe2_inflate_zlib(const uint8_t *data, size_t count, uint8_t **out, size_t *out_count);
uint32_t fe2_adler32(const uint8_t *data, size_t count);

#endif
