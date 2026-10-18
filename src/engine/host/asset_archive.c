/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "asset_archive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int write_u32(FILE *file, uint32_t value)
{
    uint8_t bytes[4];
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
    return fwrite(bytes, 1, 4, file) == 4 ? 0 : -1;
}

static int valid_name(const char *name, size_t length)
{
    size_t i;
    if (!name || length == 0 || length > 255 || name[0] == '/') return 0;
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32 || c > 126 || c == '\\' || c == ':') return 0;
    }
    for (i = 0; i + 1 < length; ++i)
        if (name[i] == '.' && name[i + 1] == '.') return 0;
    return 1;
}

void fe2_asset_archive_unload(fe2_asset_archive *archive)
{
    if (!archive) return;
    free(archive->bytes);
    free(archive->entries);
    memset(archive, 0, sizeof *archive);
}

int fe2_asset_archive_load(fe2_asset_archive *archive, const char *path)
{
    FILE *file;
    long length;
    size_t size, at, i;
    uint32_t count;
    uint8_t *bytes;
    fe2_asset_entry *entries;
    if (!archive || !path) return -1;
    fe2_asset_archive_unload(archive);
    file = fopen(path, "rb");
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return -1; }
    length = ftell(file);
    if (length < 12 || length > FE2_ASSET_MAX_BYTES) { fclose(file); return -1; }
    if (fseek(file, 0, SEEK_SET)) { fclose(file); return -1; }
    size = (size_t)length;
    bytes = malloc(size);
    if (!bytes) { fclose(file); return -1; }
    if (fread(bytes, 1, size, file) != size) { free(bytes); fclose(file); return -1; }
    fclose(file);
    if (memcmp(bytes, "FE2DATA1", 8) != 0) { free(bytes); return -1; }
    count = read_u32(bytes + 8);
    if (count > FE2_ASSET_MAX_RECORDS) { free(bytes); return -1; }
    entries = count ? calloc(count, sizeof *entries) : NULL;
    if (count && !entries) { free(bytes); return -1; }
    if (count == 0) {
        if (12 != size) { free(bytes); return -1; }
        archive->bytes = bytes;
        archive->size = size;
        return 0;
    }
    at = 12;
    for (i = 0; i < count; ++i) {
        uint8_t name_len;
        uint32_t record;
        size_t j;
        if (at >= size) { free(bytes); free(entries); return -1; }
        name_len = bytes[at++];
        if (name_len == 0 || (size_t)name_len + 4 > size - at) { free(bytes); free(entries); return -1; }
        if (!valid_name((const char *)(bytes + at), name_len)) { free(bytes); free(entries); return -1; }
        for (j = 0; j < i; ++j) {
            if (entries[j].name_len == name_len && memcmp(entries[j].name, bytes + at, name_len) == 0) {
                free(bytes); free(entries); return -1;
            }
        }
        entries[i].name = bytes + at;
        entries[i].name_len = name_len;
        at += name_len;
        record = read_u32(bytes + at);
        at += 4;
        if ((size_t)record > size - at) { free(bytes); free(entries); return -1; }
        entries[i].data = bytes + at;
        entries[i].count = record;
        at += record;
    }
    if (at != size) { free(bytes); free(entries); return -1; }
    archive->bytes = bytes;
    archive->size = size;
    archive->entries = entries;
    archive->count = count;
    return 0;
}

const fe2_asset_entry *fe2_asset_archive_find(const fe2_asset_archive *archive, const char *name)
{
    size_t length, i;
    if (!archive || !name) return NULL;
    length = strlen(name);
    if (length == 0 || length > 255) return NULL;
    for (i = 0; i < archive->count; ++i) {
        const fe2_asset_entry *entry = &archive->entries[i];
        if (entry->name_len == length && memcmp(entry->name, name, length) == 0) return entry;
    }
    return NULL;
}

static int compare_parts(const void *left, const void *right)
{
    const fe2_asset_part *a = left;
    const fe2_asset_part *b = right;
    return strcmp(a->name, b->name);
}

int fe2_asset_archive_save(const char *path, const fe2_asset_part *parts, size_t count)
{
    fe2_asset_part *ordered;
    FILE *file;
    size_t i;
    int failed = 0;
    if (!path || (!parts && count) || count > FE2_ASSET_MAX_RECORDS) return -1;
    for (i = 0; i < count; ++i)
        if (!parts[i].name) return -1;
    ordered = count ? malloc(count * sizeof *ordered) : NULL;
    if (count && !ordered) return -1;
    if (count) memcpy(ordered, parts, count * sizeof *ordered);
    if (count > 1) qsort(ordered, count, sizeof *ordered, compare_parts);
    for (i = 0; i < count; ++i) {
        if (!valid_name(ordered[i].name, strlen(ordered[i].name))) { free(ordered); return -1; }
        if (i && strcmp(ordered[i - 1].name, ordered[i].name) == 0) { free(ordered); return -1; }
        if (ordered[i].count && !ordered[i].data) { free(ordered); return -1; }
    }
    file = fopen(path, "wb");
    if (!file) { free(ordered); return -1; }
    if (fwrite("FE2DATA1", 1, 8, file) != 8 || write_u32(file, (uint32_t)count)) failed = 1;
    for (i = 0; !failed && i < count; ++i) {
        size_t length = strlen(ordered[i].name);
        uint8_t name_len = (uint8_t)length;
        if (fwrite(&name_len, 1, 1, file) != 1 || fwrite(ordered[i].name, 1, length, file) != length ||
            write_u32(file, ordered[i].count)) failed = 1;
        else if (ordered[i].count && fwrite(ordered[i].data, 1, ordered[i].count, file) != ordered[i].count) failed = 1;
    }
    if (fclose(file)) failed = 1;
    free(ordered);
    return failed ? -1 : 0;
}
