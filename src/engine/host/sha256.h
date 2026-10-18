/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SHA256_H
#define FE2_SHA256_H

#include <stddef.h>
#include <stdint.h>

void fe2_sha256(const uint8_t *data, size_t count, uint8_t out[32]);

#endif
