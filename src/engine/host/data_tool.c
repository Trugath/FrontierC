/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Pack and unpack frontier.dat.
 * pack reads an editable data tree, or assets.txt in a record directory, and writes the dat.
 * unpack writes a dat back to that record folder, or extracts a Frontier install into a data tree.
 * repack extracts an install in a temporary tree and writes the dat from it. */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "asset_archive.h"
#include "asset_files.h"
#include "asset_tree.h"
#include "asset_unpack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static int make_dir(const char *path)
{
#ifdef _WIN32
    if (CreateDirectoryA(path, NULL)) return 0;
    return GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
#else
    if (mkdir(path, 0755) == 0 || errno == EEXIST) return 0;
    return -1;
#endif
}

static int join_record(char *out, size_t cap, const char *root, const char *name)
{
    int wrote = snprintf(out, cap, "%s/%s", root, name);
    if (wrote < 0 || (size_t)wrote >= cap) return -1;
    return 0;
}

static int ensure_record_dirs(const char *root, const char *name)
{
    char path[4096];
    size_t root_len, i, n;
    if (make_dir(root)) return -1;
    if (join_record(path, sizeof path, root, name)) return -1;
    root_len = strlen(root);
    n = strlen(path);
    for (i = root_len + 1; i < n; ++i) {
        if (path[i] != '/') continue;
        path[i] = 0;
        if (make_dir(path)) return -1;
        path[i] = '/';
    }
    return 0;
}

static uint8_t *read_file(const char *path, uint32_t *count)
{
    FILE *file;
    long length;
    uint8_t *bytes;
    file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return NULL; }
    length = ftell(file);
    if (length < 0 || length > FE2_ASSET_MAX_BYTES) { fclose(file); return NULL; }
    if (fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    bytes = malloc((size_t)length + 1u);
    if (!bytes) { fclose(file); return NULL; }
    if (length && fread(bytes, 1, (size_t)length, file) != (size_t)length) { free(bytes); fclose(file); return NULL; }
    fclose(file);
    *count = (uint32_t)length;
    return bytes;
}

static int pack_records(const char *root, const char *dat)
{
    char manifest[4096], line[512];
    FILE *file;
    fe2_asset_part *parts = NULL;
    char **names = NULL;
    uint8_t **blobs = NULL;
    size_t count = 0, i;
    int status = -1;
    if (snprintf(manifest, sizeof manifest, "%s/assets.txt", root) >= (int)sizeof manifest) return -1;
    file = fopen(manifest, "rb");
    if (!file) { fprintf(stderr, "Could not read %s\n", manifest); return -1; }
    while (fgets(line, sizeof line, file)) {
        size_t length = strlen(line);
        char path[4096];
        uint32_t bytes = 0;
        uint8_t *blob;
        char **next_names;
        uint8_t **next_blobs;
        fe2_asset_part *next_parts;
        if (length && line[length - 1] != '\n' && !feof(file)) { fprintf(stderr, "Asset name is too long\n"); goto done; }
        if (length && line[length - 1] == '\n') line[--length] = 0;
        if (length && line[length - 1] == '\r') line[--length] = 0;
        if (!length) continue;
        if (count >= FE2_ASSET_MAX_RECORDS) { fprintf(stderr, "Too many asset records\n"); goto done; }
        if (join_record(path, sizeof path, root, line)) goto done;
        blob = read_file(path, &bytes);
        if (!blob) { fprintf(stderr, "Could not read %s\n", path); goto done; }
        next_names = realloc(names, (count + 1) * sizeof *names);
        if (!next_names) { free(blob); goto done; }
        names = next_names;
        next_blobs = realloc(blobs, (count + 1) * sizeof *blobs);
        if (!next_blobs) { free(blob); goto done; }
        blobs = next_blobs;
        next_parts = realloc(parts, (count + 1) * sizeof *parts);
        if (!next_parts) { free(blob); goto done; }
        parts = next_parts;
        names[count] = malloc(length + 1);
        if (!names[count]) { free(blob); goto done; }
        memcpy(names[count], line, length + 1);
        blobs[count] = blob;
        parts[count].name = names[count];
        parts[count].data = blob;
        parts[count].count = bytes;
        ++count;
    }
    if (ferror(file)) goto done;
    if (fe2_asset_archive_save(dat, parts, count)) { fprintf(stderr, "Could not write %s\n", dat); goto done; }
    printf("%s\n", dat);
    status = 0;
done:
    if (file) fclose(file);
    for (i = 0; i < count; ++i) {
        free(names[i]);
        free(blobs[i]);
    }
    free(names);
    free(blobs);
    free(parts);
    return status;
}

static int compare_names(const void *left, const void *right)
{
    return strcmp(left, right);
}

static int unpack_records(const char *dat, const char *root)
{
    fe2_asset_archive archive;
    char (*names)[256] = NULL;
    FILE *manifest = NULL;
    char list[4096];
    size_t i;
    int status = -1;
    memset(&archive, 0, sizeof archive);
    if (fe2_asset_archive_load(&archive, dat)) { fprintf(stderr, "Could not read %s\n", dat); return -1; }
    if (make_dir(root)) { fprintf(stderr, "Could not create %s\n", root); goto done; }
    names = calloc(archive.count ? archive.count : 1, sizeof *names);
    if (!names) goto done;
    for (i = 0; i < archive.count; ++i) {
        const fe2_asset_entry *entry = &archive.entries[i];
        char path[4096];
        FILE *out;
        memcpy(names[i], entry->name, entry->name_len);
        names[i][entry->name_len] = 0;
        if (ensure_record_dirs(root, names[i])) goto done;
        if (join_record(path, sizeof path, root, names[i])) goto done;
        out = fopen(path, "wb");
        if (!out) { fprintf(stderr, "Could not write %s\n", path); goto done; }
        if (entry->count && fwrite(entry->data, 1, entry->count, out) != entry->count) { fclose(out); goto done; }
        if (fclose(out)) goto done;
    }
    if (archive.count > 1) qsort(names, archive.count, sizeof *names, compare_names);
    if (snprintf(list, sizeof list, "%s/assets.txt", root) >= (int)sizeof list) goto done;
    manifest = fopen(list, "wb");
    if (!manifest) { fprintf(stderr, "Could not write %s\n", list); goto done; }
    for (i = 0; i < archive.count; ++i)
        if (fprintf(manifest, "%s\n", names[i]) < 0) goto done;
    if (fclose(manifest)) { manifest = NULL; goto done; }
    manifest = NULL;
    printf("%s\n", root);
    status = 0;
done:
    if (manifest) fclose(manifest);
    free(names);
    fe2_asset_archive_unload(&archive);
    return status;
}

static int is_editable_tree(const char *root)
{
    char path[4096];
    if (snprintf(path, sizeof path, "%s/models/frontier/models.ini", root) >= (int)sizeof path) return 0;
    if (fe2_file_exists(path)) return 1;
    if (snprintf(path, sizeof path, "%s/graphics/el2g.ini", root) >= (int)sizeof path) return 0;
    return fe2_file_exists(path);
}

static int remove_tree(const char *root)
{
#ifdef _WIN32
    char pattern[4096], path[4096];
    WIN32_FIND_DATAA found;
    HANDLE handle;
    if (snprintf(pattern, sizeof pattern, "%s\\*", root) >= (int)sizeof pattern) return -1;
    handle = FindFirstFileA(pattern, &found);
    if (handle == INVALID_HANDLE_VALUE) return RemoveDirectoryA(root) ? 0 : -1;
    do {
        if (strcmp(found.cFileName, ".") == 0 || strcmp(found.cFileName, "..") == 0) continue;
        if (snprintf(path, sizeof path, "%s\\%s", root, found.cFileName) >= (int)sizeof path) {
            FindClose(handle);
            return -1;
        }
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? remove_tree(path) : !DeleteFileA(path)) {
            FindClose(handle);
            return -1;
        }
    } while (FindNextFileA(handle, &found));
    {
        DWORD error = GetLastError();
        FindClose(handle);
        if (error != ERROR_NO_MORE_FILES) return -1;
    }
    return RemoveDirectoryA(root) ? 0 : -1;
#else
    DIR *dir = opendir(root);
    struct dirent *entry;
    if (!dir) return -1;
    while ((entry = readdir(dir))) {
        char path[4096];
        struct stat info;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (snprintf(path, sizeof path, "%s/%s", root, entry->d_name) >= (int)sizeof path) { closedir(dir); return -1; }
        if (lstat(path, &info)) { closedir(dir); return -1; }
        if (S_ISDIR(info.st_mode) ? remove_tree(path) : unlink(path)) { closedir(dir); return -1; }
    }
    closedir(dir);
    return rmdir(root);
#endif
}

static int make_temp_tree(char *out, size_t cap)
{
#ifdef _WIN32
    char base[MAX_PATH], file[MAX_PATH];
    if (!GetTempPathA((DWORD)sizeof base, base) || !GetTempFileNameA(base, "fe2", 0, file)) return -1;
    DeleteFileA(file);
    if (!CreateDirectoryA(file, NULL) || strlen(file) >= cap) {
        RemoveDirectoryA(file);
        return -1;
    }
    memcpy(out, file, strlen(file) + 1);
    return 0;
#else
    char path[] = "/tmp/fe2-repack-XXXXXX";
    if (!mkdtemp(path)) return -1;
    if (strlen(path) >= cap) { rmdir(path); return -1; }
    memcpy(out, path, strlen(path) + 1);
    return 0;
#endif
}

static int pack_tree(const char *root, const char *dat);

static int repack_install(const char *source, const char *dat)
{
    char tree[4096];
    int unpacked;
    if (make_temp_tree(tree, sizeof tree)) {
        fprintf(stderr, "Could not create a temporary directory\n");
        return 1;
    }
    unpacked = fe2_unpack_game(source, tree);
    if (unpacked) {
        fprintf(stderr, "%s\n", fe2_asset_error()[0] ? fe2_asset_error() : "Unable to extract this Frontier install");
        if (remove_tree(tree)) fprintf(stderr, "Could not remove %s\n", tree);
        return 2;
    }
    unpacked = pack_tree(tree, dat);
    if (remove_tree(tree)) {
        fprintf(stderr, "Could not remove %s\n", tree);
        return 1;
    }
    return unpacked ? 1 : 0;
}

static int pack_tree(const char *root, const char *dat)
{
    fe2_tree_archive tree;
    memset(&tree, 0, sizeof tree);
    if (fe2_pack_tree(root, &tree)) {
        fprintf(stderr, "%s\n", fe2_asset_error()[0] ? fe2_asset_error() : "Unable to pack this asset tree");
        return -1;
    }
    if (fe2_asset_archive_save(dat, tree.parts, tree.count)) {
        fprintf(stderr, "Could not write %s\n", dat);
        fe2_tree_free(&tree);
        return -1;
    }
    fe2_tree_free(&tree);
    printf("%s\n", dat);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "pack") == 0) {
        return (is_editable_tree(argv[2]) ? pack_tree(argv[2], argv[3]) : pack_records(argv[2], argv[3])) ? 1 : 0;
    }
    if (argc == 4 && strcmp(argv[1], "unpack") == 0) {
        FILE *dat = fopen(argv[2], "rb");
        char magic[8];
        int archive = dat && fread(magic, 1, 8, dat) == 8 && memcmp(magic, "FE2DATA1", 8) == 0;
        if (dat) fclose(dat);
        if (archive) return unpack_records(argv[2], argv[3]) ? 1 : 0;
        if (fe2_unpack_game(argv[2], argv[3])) {
            fprintf(stderr, "%s\n", fe2_asset_error()[0] ? fe2_asset_error() : "Unable to extract this Frontier install");
            return 2;
        }
        printf("Unpacked Frontier assets into %s\n", argv[3]);
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "repack") == 0) return repack_install(argv[2], argv[3]);
    fprintf(stderr, "usage:\n  frontier-data pack <data-or-record-directory> <frontier.dat>\n  frontier-data unpack <frontier-install-or-dat> <directory>\n  frontier-data repack <frontier.zip> <frontier.dat>\n");
    return 2;
}
