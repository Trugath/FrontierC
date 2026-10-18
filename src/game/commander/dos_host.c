/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The host DOS platform (see dos_host.h). */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "dos_host.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
typedef struct host_search { HANDLE handle; WIN32_FIND_DATAA data; int first; } host_search;
#else
#include <dirent.h>
#include <unistd.h>
#endif

enum {
    DOS_ERROR_NOT_FOUND = 2, DOS_ERROR_PATH = 3, DOS_ERROR_HANDLES = 4, DOS_ERROR_DENIED = 5, DOS_ERROR_HANDLE = 6,
    DOS_ERROR_NO_SPACE = 8, DOS_ERROR_DRIVE = 15, DOS_ERROR_NO_MORE = 18,
    FIRST_HANDLE = 5, ATTRIBUTE_DIRECTORY = 0x10
};

static int host_error(void)
{
    if (errno == ENOENT) return DOS_ERROR_NOT_FOUND;
    if (errno == ENOSPC) return DOS_ERROR_NO_SPACE;
    if (errno == EMFILE || errno == ENFILE) return DOS_ERROR_HANDLES;
    return DOS_ERROR_DENIED;
}

int fe2_dos_host_init(fe2_dos_host *host, const char *root, char drive)
{
    size_t length;
    if (!host || !root || !*root) return -1;
    length = strlen(root);
    if (length >= sizeof host->root - 1) return -1;
    drive = (char)toupper((unsigned char)drive);
    if (drive < 'A' || drive > 'Z') return -1;
    memset(host, 0, sizeof *host);
    memcpy(host->root, root, length + 1);
    host->drive = drive;
    return 0;
}

static void end_search(fe2_dos_host *host)
{
#ifdef _WIN32
    if (host->search) {
        host_search *search = host->search;
        FindClose(search->handle); free(search);
    }
#else
    if (host->search) closedir((DIR *)host->search);
#endif
    host->search = 0;
}

void fe2_dos_host_close(fe2_dos_host *host)
{
    unsigned i;
    for (i = 0; i < FE2_DOS_HOST_HANDLES; ++i)
        if (host->files[i]) fclose(host->files[i]), host->files[i] = 0;
    end_search(host);
}

static int same_name(const char *a, const char *b)
{
    for (; *a || *b; ++a, ++b)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
    return 1;
}

/* Append one component to the host path at `out`: the host's spelling of
 * an entry that matches without case, else upper case as DOS stores it. */
static void append_component(char *path, size_t out, const char *name, size_t length)
{
    size_t i;
    path[out] = '/';
    for (i = 0; i < length; ++i) path[out + 1 + i] = (char)toupper((unsigned char)name[i]);
    path[out + 1 + length] = 0;
#ifndef _WIN32
    {
        struct stat info;
        DIR *directory;
        struct dirent *entry;
        if (stat(path, &info) == 0) return;
        path[out] = 0;
        directory = opendir(path);
        path[out] = '/';
        if (!directory) return;
        while ((entry = readdir(directory)) != 0)
            if (strlen(entry->d_name) == length && same_name(entry->d_name, path + out + 1)) {
                memcpy(path + out + 1, entry->d_name, length);
                break;
            }
        closedir(directory);
    }
#endif
}

/* A DOS path on the drive to a host path; returns 0 or an error. A path
 * without a drive is relative to the root (the current directory). */
static int map_path(const fe2_dos_host *host, const char *dos, char *path)
{
    size_t n = strlen(dos), out, start, i;
    if (n >= FE2_DOS_HOST_PATH_BYTES) return DOS_ERROR_PATH;
    if (n >= 2 && dos[1] == ':') {
        if (toupper((unsigned char)dos[0]) != host->drive) return DOS_ERROR_DRIVE;
        if (n < 3 || (dos[2] != '\\' && dos[2] != '/')) return DOS_ERROR_PATH;
        start = 3;
    } else {
        start = 0;
    }
    out = strlen(host->root);
    memcpy(path, host->root, out + 1);
    while (start < n) {
        size_t end = start;
        while (end < n && dos[end] != '\\' && dos[end] != '/') ++end;
        if (end == start && end == n) break;    /* a trailing separator */
        if (end == start || (end - start == 1 && dos[start] == '.') ||
            (end - start == 2 && dos[start] == '.' && dos[start + 1] == '.'))
            return DOS_ERROR_PATH;
        if (out + 1 + end - start >= FE2_DOS_HOST_PATH_BYTES) return DOS_ERROR_PATH;
        for (i = start; i < end; ++i) {
            unsigned char c = (unsigned char)dos[i];
            if (c < 32 || strchr(":*?\"<>|", c)) return DOS_ERROR_PATH;
        }
        append_component(path, out, dos + start, end - start);
        out += 1 + end - start;
        start = end + 1;
    }
    return 0;
}

static int current_directory(void *opaque, uint8_t drive, char *path)
{
    fe2_dos_host *host = (fe2_dos_host *)opaque;
    if (drive != 0 && drive != (uint8_t)(host->drive - '@')) return DOS_ERROR_DRIVE;
    memcpy(path, host->current, sizeof host->current);
    return 0;
}

/* DOS searches expose only names that fit 8.3 on either platform. */
static int fits_83(const char *name)
{
    const char *dot = strchr(name, '.');
    size_t base = dot ? (size_t)(dot - name) : strlen(name);
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 1;
    return base >= 1 && base <= 8 && (!dot || (strlen(dot + 1) <= 3 && !strchr(dot + 1, '.')));
}

static int matches(const char *pattern, const char *name)
{
    return strcmp(pattern, "*.*") == 0 || strcmp(pattern, "*") == 0 || same_name(pattern, name);
}

static void search_begin(void *opaque) { end_search((fe2_dos_host *)opaque); }

static int search_next(void *opaque, fe2_dos_found *found)
{
    fe2_dos_host *host = (fe2_dos_host *)opaque;
#ifndef _WIN32
    struct dirent *entry;
    size_t i;
    if (!host->search) return DOS_ERROR_NO_MORE;
    while ((entry = readdir((DIR *)host->search)) != 0) {
        char path[FE2_DOS_HOST_PATH_BYTES + FE2_DOS_NAME_BYTES + 2];
        struct stat info;
        int directory;
        if (!fits_83(entry->d_name) || !matches(host->search_name, entry->d_name)) continue;
        snprintf(path, sizeof path, "%s/%s", host->search_directory, entry->d_name);
        if (stat(path, &info) != 0) continue;
        directory = S_ISDIR(info.st_mode);
        if (directory && !(host->search_attributes & ATTRIBUTE_DIRECTORY)) continue;
        if (!directory && !S_ISREG(info.st_mode)) continue;
        found->attributes = (uint8_t)(directory ? ATTRIBUTE_DIRECTORY : 0x20);
        found->size = directory ? 0 : (uint32_t)info.st_size;
        memset(found->name, 0, sizeof found->name);
        for (i = 0; entry->d_name[i] && i < sizeof found->name - 1; ++i)
            found->name[i] = (char)toupper((unsigned char)entry->d_name[i]);
        return 0;
    }
#else
    host_search *search = host->search;
    size_t i;
    if (!search) return DOS_ERROR_NO_MORE;
    while (search->first || FindNextFileA(search->handle, &search->data)) {
        const WIN32_FIND_DATAA *entry = &search->data;
        int directory = (entry->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        search->first = 0;
        if (!fits_83(entry->cFileName) || !matches(host->search_name, entry->cFileName)) continue;
        if (directory && !(host->search_attributes & ATTRIBUTE_DIRECTORY)) continue;
        if (entry->nFileSizeHigh) continue;
        found->attributes = (uint8_t)(directory ? ATTRIBUTE_DIRECTORY : 0x20);
        found->size = directory ? 0 : entry->nFileSizeLow;
        memset(found->name, 0, sizeof found->name);
        for (i = 0; entry->cFileName[i] && i < sizeof found->name - 1; ++i)
            found->name[i] = (char)toupper((unsigned char)entry->cFileName[i]);
        return 0;
    }
#endif
    end_search(host);
    return DOS_ERROR_NO_MORE;
}

/* "D:\\DIR\\PATTERN": the directory is searched for PATTERN (*.* or one
 * name, without case). */
static int search_first(void *opaque, const char *pattern, uint16_t attributes, fe2_dos_found *found)
{
    fe2_dos_host *host = (fe2_dos_host *)opaque;
    char directory[FE2_DOS_HOST_PATH_BYTES];
    const char *name = strrchr(pattern, '\\');
    size_t length = name ? (size_t)(name - pattern) + 1 : 0;
    int error;
    end_search(host);
    if (length >= sizeof directory) return DOS_ERROR_PATH;
    memcpy(directory, pattern, length);
    directory[length] = 0;
    if ((error = map_path(host, directory, host->search_directory)) != 0) return error;
    name = pattern + length;
    if (strlen(name) >= sizeof host->search_name) return DOS_ERROR_PATH;
    strcpy(host->search_name, name);
    host->search_attributes = attributes;
#ifndef _WIN32
    host->search = opendir(host->search_directory);
    if (!host->search) return DOS_ERROR_PATH;
#else
    {
        char wildcard[FE2_DOS_HOST_PATH_BYTES + 3];
        host_search *search = malloc(sizeof *search);
        if (!search) return DOS_ERROR_NO_SPACE;
        snprintf(wildcard, sizeof wildcard, "%s/*", host->search_directory);
        search->handle = FindFirstFileA(wildcard, &search->data);
        if (search->handle == INVALID_HANDLE_VALUE) { free(search); return DOS_ERROR_PATH; }
        search->first = 1; host->search = search;
    }
#endif
    return search_next(host, found);
}

static void search_end(void *opaque) { end_search((fe2_dos_host *)opaque); }

static int open_mode(fe2_dos_host *host, const char *dos, const char *mode, uint16_t *handle)
{
    char path[FE2_DOS_HOST_PATH_BYTES];
    unsigned i;
    int error = map_path(host, dos, path);
    if (error) return error;
    for (i = 0; i < FE2_DOS_HOST_HANDLES && host->files[i]; ++i) {}
    if (i == FE2_DOS_HOST_HANDLES) return DOS_ERROR_HANDLES;
    if ((host->files[i] = fopen(path, mode)) == 0) return host_error();
    *handle = (uint16_t)(FIRST_HANDLE + i);
    return 0;
}

static FILE *file_of(fe2_dos_host *host, uint16_t handle)
{
    unsigned i = (unsigned)(handle - FIRST_HANDLE);
    return handle >= FIRST_HANDLE && i < FE2_DOS_HOST_HANDLES ? host->files[i] : 0;
}

static int open_file(void *opaque, const char *path, uint8_t mode, uint16_t *handle)
{
    return open_mode((fe2_dos_host *)opaque, path, (mode & 3) == 0 ? "rb" : "r+b", handle);
}

static int create_file(void *opaque, const char *path, uint16_t attributes, uint16_t *handle)
{
    (void)attributes;
    return open_mode((fe2_dos_host *)opaque, path, "wb", handle);
}

static int read_file(void *opaque, uint16_t handle, uint8_t *buffer, uint16_t count, uint16_t *done)
{
    FILE *file = file_of((fe2_dos_host *)opaque, handle);
    if (!file) return DOS_ERROR_HANDLE;
    *done = (uint16_t)fread(buffer, 1, count, file);
    return ferror(file) ? DOS_ERROR_DENIED : 0;
}

static int write_file(void *opaque, uint16_t handle, const uint8_t *buffer, uint16_t count, uint16_t *done)
{
    FILE *file = file_of((fe2_dos_host *)opaque, handle);
    if (!file) return DOS_ERROR_HANDLE;
    *done = (uint16_t)fwrite(buffer, 1, count, file);
    return ferror(file) ? host_error() : 0;
}

static void close_file(void *opaque, uint16_t handle)
{
    fe2_dos_host *host = (fe2_dos_host *)opaque;
    FILE *file = file_of(host, handle);
    if (!file) return;
    fclose(file);
    host->files[handle - FIRST_HANDLE] = 0;
}

static int delete_file(void *opaque, const char *dos)
{
    char path[FE2_DOS_HOST_PATH_BYTES];
    int error = map_path((fe2_dos_host *)opaque, dos, path);
    if (error) return error;
    return remove(path) == 0 ? 0 : host_error();
}

fe2_dos_platform fe2_dos_host_platform(fe2_dos_host *host)
{
    fe2_dos_platform platform;
    platform.opaque = host;
    platform.current_directory = current_directory;
    platform.search_begin = search_begin;
    platform.search_first = search_first;
    platform.search_next = search_next;
    platform.search_end = search_end;
    platform.open_file = open_file;
    platform.create_file = create_file;
    platform.read_file = read_file;
    platform.write_file = write_file;
    platform.close_file = close_file;
    platform.delete_file = delete_file;
    return platform;
}
