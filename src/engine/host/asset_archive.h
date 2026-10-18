/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_ASSET_ARCHIVE_H
#define FE2_ASSET_ARCHIVE_H

#include <stddef.h>
#include <stdint.h>

/* FE2DATA1: little-endian count, then records sorted by name.
 * Each record is a name length, the name, a little-endian size and the bytes.
 * Names use forward slashes and contain no drive, parent segment or slash prefix. */

enum { FE2_ASSET_MAX_RECORDS = 4096, FE2_ASSET_MAX_BYTES = 32 * 1024 * 1024 };

typedef struct fe2_asset_entry {
    const uint8_t *name;
    uint8_t name_len;
    const uint8_t *data;
    uint32_t count;
} fe2_asset_entry;

typedef struct fe2_asset_archive {
    uint8_t *bytes;
    size_t size;
    fe2_asset_entry *entries;
    size_t count;
} fe2_asset_archive;

typedef struct fe2_asset_part {
    const char *name;
    const uint8_t *data;
    uint32_t count;
} fe2_asset_part;

/* 0 on success. The archive keeps the file bytes; entry pointers use that memory. */
int fe2_asset_archive_load(fe2_asset_archive *archive, const char *path);
void fe2_asset_archive_unload(fe2_asset_archive *archive);
const fe2_asset_entry *fe2_asset_archive_find(const fe2_asset_archive *archive, const char *name);
/* Writes records in name order. The caller keeps `parts` and their bytes. */
int fe2_asset_archive_save(const char *path, const fe2_asset_part *parts, size_t count);

#endif
