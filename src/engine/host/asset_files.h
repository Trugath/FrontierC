/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_ASSET_FILES_H
#define FE2_ASSET_FILES_H

#include <stdarg.h>
#include <stddef.h>

const char *fe2_asset_error(void);
int fe2_asset_fail(const char *fmt, ...);

/* Relative path under base. Rejects absolute paths and parent segments. */
int fe2_path_join(char *out, size_t cap, const char *base, const char *rel);
char *fe2_read_text(const char *path, size_t *length);
int fe2_write_bytes(const char *path, const void *data, size_t count);
int fe2_make_dirs(const char *path);
int fe2_file_exists(const char *path);

#endif
