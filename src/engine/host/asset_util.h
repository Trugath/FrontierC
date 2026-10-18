/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_ASSET_UTIL_H
#define FE2_ASSET_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct fe2_buf {
    uint8_t *data;
    size_t count, cap;
} fe2_buf;

static inline int fe2_buf_add(fe2_buf *buf, const void *bytes, size_t count)
{
    if (buf->count + count > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 64;
        uint8_t *grown;
        while (cap < buf->count + count) cap *= 2;
        grown = (uint8_t *)realloc(buf->data, cap);
        if (!grown) return -1;
        buf->data = grown;
        buf->cap = cap;
    }
    if (count && bytes) memcpy(buf->data + buf->count, bytes, count);
    buf->count += count;
    return 0;
}

static inline int fe2_buf_byte(fe2_buf *buf, uint8_t value)
{
    return fe2_buf_add(buf, &value, 1);
}

static inline int fe2_buf_u16(fe2_buf *buf, unsigned value)
{
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    return fe2_buf_add(buf, bytes, 2);
}

static inline void fe2_buf_free(fe2_buf *buf)
{
    free(buf->data);
    memset(buf, 0, sizeof *buf);
}

#endif
