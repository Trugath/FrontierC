/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "asset_files.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

static char error_text[512];

const char *fe2_asset_error(void)
{
    return error_text;
}

int fe2_asset_fail(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(error_text, sizeof error_text, fmt, args);
    va_end(args);
    return -1;
}

int fe2_path_join(char *out, size_t cap, const char *base, const char *rel)
{
    const char *scan;
    int wrote;
    if (!out || !base || !rel || !rel[0]) return fe2_asset_fail("Asset path is empty");
    if (rel[0] == '/' || rel[0] == '\\' || strchr(rel, ':'))
        return fe2_asset_fail("Asset path leaves its source directory");
    for (scan = rel; *scan; ++scan) {
        if (*scan == '\\') return fe2_asset_fail("Asset path leaves its source directory");
        if (scan[0] == '.' && scan[1] == '.' && (scan == rel || scan[-1] == '/') &&
            (scan[2] == '/' || scan[2] == 0))
            return fe2_asset_fail("Asset path leaves its source directory");
    }
    wrote = snprintf(out, cap, "%s/%s", base, rel);
    if (wrote < 0 || (size_t)wrote >= cap) return fe2_asset_fail("Asset path is too long");
    return 0;
}

char *fe2_read_text(const char *path, size_t *length)
{
    FILE *file;
    long size;
    char *text;
    size_t i;
    file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return NULL; }
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    text = (char *)malloc((size_t)size + 1);
    if (!text) { fclose(file); return NULL; }
    if (size && fread(text, 1, (size_t)size, file) != (size_t)size) { free(text); fclose(file); return NULL; }
    fclose(file);
    text[size] = 0;
    for (i = 0; i < (size_t)size; ++i) {
        if (!text[i]) { free(text); return NULL; }
    }
    if (length) *length = (size_t)size;
    return text;
}

int fe2_write_bytes(const char *path, const void *data, size_t count)
{
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    if (count && fwrite(data, 1, count, file) != count) { fclose(file); return -1; }
    if (fclose(file)) return -1;
    return 0;
}

int fe2_make_dirs(const char *path)
{
    char copy[4096];
    size_t i, n;
    if (!path || snprintf(copy, sizeof copy, "%s", path) >= (int)sizeof copy) return -1;
    n = strlen(copy);
    for (i = 0; i < n; ++i) {
        if (copy[i] != '/' && copy[i] != '\\') continue;
        if (i == 0 || (i == 2 && copy[1] == ':')) continue;
        copy[i] = 0;
#ifdef _WIN32
        if (!CreateDirectoryA(copy, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return -1;
#else
        if (mkdir(copy, 0755) != 0 && errno != EEXIST) return -1;
#endif
        copy[i] = '/';
    }
#ifdef _WIN32
    if (!CreateDirectoryA(copy, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return -1;
#else
    if (mkdir(copy, 0755) != 0 && errno != EEXIST) return -1;
#endif
    return 0;
}

int fe2_file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}
