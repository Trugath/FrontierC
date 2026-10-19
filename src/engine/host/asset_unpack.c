/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "asset_adlib.h"
#include "asset_unpack.h"

#include "asset_files.h"
#include "asset_image.h"
#include "asset_ini.h"
#include "asset_model.h"
#include "asset_program.h"
#include "asset_seed.h"
#include "asset_util.h"
#include "inflate.h"
#include "sha256.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

enum { FE2_NEEDED = 26 };

static const char *needed_names[FE2_NEEDED] = {
    "el2g.grf", "el2mcga3.ovl", "el2m13.ovl", "frontier.exe", "el2m08.ovl",
    "babayaga.rol", "barem.rol", "bluedan.rol", "elitet.rol", "ggofkiev.rol",
    "hallking.rol", "intro.rol", "jupalt.rol", "valkries.rol",
    "el2m00.ovl", "el2m02.ovl", "el2m03.ovl", "el2m09.ovl", "el2m10.ovl",
    "el2m14.ovl", "el2m15.ovl", "el2m17.ovl", "el2m18.ovl", "el2m19.ovl",
    "el2adlib.ovl", "el2setup.ovl"
};

static const char *version_names[FE2_NEEDED] = {
    "babayaga.rol", "barem.rol", "bluedan.rol", "el2adlib.ovl", "el2g.grf",
    "el2m00.ovl", "el2m02.ovl", "el2m03.ovl", "el2m08.ovl", "el2m09.ovl", "el2m10.ovl",
    "el2m13.ovl", "el2m14.ovl", "el2m15.ovl", "el2m17.ovl", "el2m18.ovl", "el2m19.ovl",
    "el2mcga3.ovl", "el2setup.ovl", "elitet.rol", "frontier.exe", "ggofkiev.rol", "hallking.rol",
    "intro.rol", "jupalt.rol", "valkries.rol"
};

static const char *version_hex[FE2_NEEDED] = {
    "a0e575d9c9adb9f38aa415a705e98daeb91a0722035d1800e89d35cc8d20cc5b",
    "1cf990d45823ca2545a696a23487a06382a2c627d6c132e9dbead561c27edcb2",
    "6218208f5b52735b281a9f7c2234703a7bea32ab32de14620b6b4cfb17adec7b",
    "82a388beadd1b7038bf39586762b604d289870a78b362269af6e160bdd170bfb",
    "5ee7a6afcf2be3456cad77a471ebb68f5eb3fa527e961657a00cf02533272257",
    "498ec72901e1e976deb40ccf239689721aa2d065e7e7230a4105175d01520644",
    "5983e32f81f3d87718fb9f755470a9139c79b2fe96e35faf1be1dd51e40e95d4",
    "9c7291fb7dd325df438a210ca5251024064cb0e759f49f2282e94959f30263b7",
    "9a54088bf20fe6a0d11860a84392772a83914c4fffc75e089ca2ac96aa7fad45",
    "9c78faa549fbd60fe6d5252ac8e86d90147db3cf625d20e711fb3290269531dc",
    "47d6c8ddf5864a1ee57677b55f215d67d5f004bee5bcdc71439d2a8fee7ba7fa",
    "0e74df1d4125353946560b2fb8c55dde139a311cfe7b1d1a6f727d8a8634229d",
    "3e300b9c0ca16b8c76a79a53273ef3d330bbf863fdcd24a3280c7649c97c426d",
    "265741fa94ecadbd73296f66e85f4908f6de23abec26863c66f6bfc6e226a6ab",
    "0278fb19894dfee22f78bcf341cf58762a2f8947d4cff24ca420f1f77eb1ab81",
    "b7af80f723a553f499f51483764a1f3b161231ebb90948db1480dac8a9b4ef7c",
    "a33c7573c493fb7cad8ad4cc0cde478ea116681b5166884177a36080a55c8c3c",
    "34bdc63da0dfee171f1dc86c92d0ebd5dd3b2fa697a6d725851abfdea7bff291",
    "2ee4e914f1c464a338889caae3bc65e27640654072bf4add1d278b9f833852a0",
    "68b8814f045d14902854ce0144f2ef9797e98866ac9a3f15b31954cfd520a646",
    "0b7c477eb68f43785194f1d489277c7bb10bdeb743a3b9a0a4092b1692456982",
    "93c40e207ffdfcb6c8e4e062dbb50b08ecdb8dc0dbe698f2ea77f6d818c342ad",
    "074f7b9e09f816a989adbd39185515f5d94414f8b9294cc710cc71c0701b848f",
    "61c8ab1022c2b18173278051d0d3ce158f76169c5d43cd3e1b323ebc6ad84419",
    "244567d4dae0747b3137fada92b9149152a3332604e67cdb4a4ccfa1d4eec62a",
    "99a93721a0e2d9798d91444d625b9cb4138c1d7c5a83c2ec3809bb5242dad44f"
};

typedef struct fe2_blob {
    uint8_t *data;
    size_t count;
} fe2_blob;

static unsigned ru16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static unsigned ru32(const uint8_t *p)
{
    return p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static int rs16(const uint8_t *p)
{
    unsigned value = ru16(p);
    return value & 0x8000u ? (int)value - 65536 : (int)value;
}

static int wanted(const char *name)
{
    int i;
    for (i = 0; i < FE2_NEEDED; ++i) if (!strcmp(name, needed_names[i])) return i;
    return -1;
}

static void release_blobs(fe2_blob *files)
{
    int i;
    for (i = 0; i < FE2_NEEDED; ++i) free(files[i].data);
    memset(files, 0, sizeof(fe2_blob) * FE2_NEEDED);
}

static int keep_blob(fe2_blob *files, const char *name, const uint8_t *data, size_t count)
{
    int slot = wanted(name);
    uint8_t *copy;
    if (slot < 0 || files[slot].data) return 0;
    copy = (uint8_t *)malloc(count ? count : 1);
    if (!copy) return -1;
    if (count) memcpy(copy, data, count);
    files[slot].data = copy;
    files[slot].count = count;
    return 0;
}

static int read_whole(const char *path, uint8_t **data, size_t *count)
{
    FILE *file = fopen(path, "rb");
    long length;
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return -1; }
    length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET)) { fclose(file); return -1; }
    *data = (uint8_t *)malloc((size_t)length ? (size_t)length : 1);
    if (!*data || ((size_t)length && fread(*data, 1, (size_t)length, file) != (size_t)length)) {
        free(*data);
        *data = NULL;
        fclose(file);
        return -1;
    }
    fclose(file);
    *count = (size_t)length;
    return 0;
}

static int walk_dir(const char *root, fe2_blob *files)
{
#ifdef _WIN32
    char pattern[4096];
    WIN32_FIND_DATAA found;
    HANDLE handle;
    if (snprintf(pattern, sizeof pattern, "%s\\*", root) >= (int)sizeof pattern) return -1;
    handle = FindFirstFileA(pattern, &found);
    if (handle == INVALID_HANDLE_VALUE) return -1;
    do {
        char path[4096], lower[64];
        size_t n, i;
        if (!strcmp(found.cFileName, ".") || !strcmp(found.cFileName, "..")) continue;
        if (snprintf(path, sizeof path, "%s/%s", root, found.cFileName) >= (int)sizeof path) { FindClose(handle); return -1; }
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (walk_dir(path, files)) { FindClose(handle); return -1; }
            continue;
        }
        n = strlen(found.cFileName);
        if (n >= sizeof lower) continue;
        for (i = 0; i < n; ++i) lower[i] = (char)tolower((unsigned char)found.cFileName[i]);
        lower[n] = 0;
        if (wanted(lower) >= 0) {
            uint8_t *data = NULL;
            size_t count = 0;
            if (read_whole(path, &data, &count) || keep_blob(files, lower, data, count)) { free(data); FindClose(handle); return -1; }
            free(data);
        }
    } while (FindNextFileA(handle, &found));
    FindClose(handle);
    return 0;
#else
    DIR *dir = opendir(root);
    struct dirent *entry;
    if (!dir) return -1;
    while ((entry = readdir(dir)) != NULL) {
        char path[4096], lower[64];
        struct stat info;
        size_t n, i;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (snprintf(path, sizeof path, "%s/%s", root, entry->d_name) >= (int)sizeof path) { closedir(dir); return -1; }
        if (stat(path, &info)) continue;
        if (S_ISDIR(info.st_mode)) { if (walk_dir(path, files)) { closedir(dir); return -1; } continue; }
        n = strlen(entry->d_name);
        if (n >= sizeof lower) continue;
        for (i = 0; i < n; ++i) lower[i] = (char)tolower((unsigned char)entry->d_name[i]);
        lower[n] = 0;
        if (wanted(lower) >= 0) {
            uint8_t *data = NULL;
            size_t count = 0;
            if (read_whole(path, &data, &count) || keep_blob(files, lower, data, count)) { free(data); closedir(dir); return -1; }
            free(data);
        }
    }
    closedir(dir);
    return 0;
#endif
}

static int load_zip(const uint8_t *zip, size_t count, fe2_blob *files)
{
    size_t eocd = 0, central, i;
    unsigned entries;
    int found = 0;
    if (count < 22) return -1;
    for (i = count - 22; ; --i) {
        if (ru32(zip + i) == 0x06054b50u) { eocd = i; found = 1; break; }
        if (!i || count - i > 65557) break;
    }
    if (!found) return -1;
    entries = ru16(zip + eocd + 10);
    central = ru32(zip + eocd + 16);
    for (i = 0; i < entries; ++i) {
        unsigned method, name_n, extra_n, comment_n, comp, local_off, local_name, local_extra;
        char name[256], base[64];
        const char *slash;
        size_t n, c;
        if (central + 46 > count || ru32(zip + central) != 0x02014b50u) return -1;
        method = ru16(zip + central + 10);
        comp = ru32(zip + central + 20);
        name_n = ru16(zip + central + 28);
        extra_n = ru16(zip + central + 30);
        comment_n = ru16(zip + central + 32);
        local_off = ru32(zip + central + 42);
        if (name_n >= sizeof name || central + 46 + name_n > count) return -1;
        memcpy(name, zip + central + 46, name_n);
        name[name_n] = 0;
        slash = strrchr(name, '/');
        if (!slash) slash = strrchr(name, '\\');
        slash = slash ? slash + 1 : name;
        n = strlen(slash);
        if (n && n < sizeof base && slash[n - 1] != '/') {
            for (c = 0; c < n; ++c) base[c] = (char)tolower((unsigned char)slash[c]);
            base[n] = 0;
            if (wanted(base) >= 0 && local_off + 30 <= count && ru32(zip + local_off) == 0x04034b50u) {
                local_name = ru16(zip + local_off + 26);
                local_extra = ru16(zip + local_off + 28);
                if (method > 8 || local_off + 30 + local_name + local_extra + comp > count) return -1;
                if (method == 0) {
                    if (keep_blob(files, base, zip + local_off + 30 + local_name + local_extra, comp)) return -1;
                } else if (method == 8) {
                    uint8_t *plain = NULL;
                    size_t plain_n = 0;
                    if (fe2_inflate(zip + local_off + 30 + local_name + local_extra, comp, &plain, &plain_n) ||
                        keep_blob(files, base, plain, plain_n)) { free(plain); return -1; }
                    free(plain);
                }
            }
        }
        central += 46 + name_n + extra_n + comment_n;
    }
    return 0;
}

static int load_install(const char *source, fe2_blob *files)
{
    uint8_t *data = NULL;
    size_t count = 0;
    memset(files, 0, sizeof(fe2_blob) * FE2_NEEDED);
#ifdef _WIN32
    {
        DWORD attr = GetFileAttributesA(source);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return walk_dir(source, files);
    }
#else
    {
        struct stat info;
        if (!stat(source, &info) && S_ISDIR(info.st_mode)) return walk_dir(source, files);
    }
#endif
    if (read_whole(source, &data, &count)) return fe2_asset_fail("Expected a zip of Frontier binaries or a directory of those files");
    if (count >= 4 && ru32(data) == 0x04034b50u) {
        int status = load_zip(data, count, files);
        free(data);
        if (status) return fe2_asset_fail("Expected a zip of Frontier binaries or a directory of those files");
        return 0;
    }
    free(data);
    return fe2_asset_fail("Expected a zip of Frontier binaries or a directory of those files");
}

static int check_install(fe2_blob *files)
{
    char missing[1024] = "", wrong[1024] = "";
    int i, any_missing = 0, any_wrong = 0;
    for (i = 0; i < FE2_NEEDED; ++i) if (!files[i].data) {
        if (any_missing && strlen(missing) + 2 < sizeof missing) strcat(missing, ", ");
        if (strlen(missing) + strlen(needed_names[i]) < sizeof missing) strcat(missing, needed_names[i]);
        any_missing = 1;
    }
    if (any_missing) return fe2_asset_fail("This Frontier install is missing: %s", missing);
    for (i = 0; i < FE2_NEEDED; ++i) {
        int slot = wanted(version_names[i]);
        uint8_t digest[32];
        char hex[65];
        int byte;
        fe2_sha256(files[slot].data, files[slot].count, digest);
        for (byte = 0; byte < 32; ++byte) sprintf(hex + 2 * byte, "%02x", digest[byte]);
        if (strcmp(hex, version_hex[i]) != 0) {
            if (any_wrong && strlen(wrong) + 2 < sizeof wrong) strcat(wrong, ", ");
            if (strlen(wrong) + strlen(version_names[i]) < sizeof wrong) strcat(wrong, version_names[i]);
            any_wrong = 1;
        }
    }
    if (any_wrong) return fe2_asset_fail("Unable to extract %s: not the Frontier: Elite II version this tool supports.", wrong);
    return 0;
}

static int parent_dirs(const char *file)
{
    char copy[4096];
    char *slash;
    if (snprintf(copy, sizeof copy, "%s", file) >= (int)sizeof copy) return -1;
    slash = strrchr(copy, '/');
    if (!slash) slash = strrchr(copy, '\\');
    if (!slash) return 0;
    *slash = 0;
    return fe2_make_dirs(copy);
}

static int seed_files(const char *root)
{
    size_t i;
    if (!fe2_authored_count) return fe2_asset_fail("This project is missing authored templates");
    for (i = 0; i < fe2_authored_count; ++i) {
        char path[4096];
        if (snprintf(path, sizeof path, "%s/%s", root, fe2_authored_files[i].path) >= (int)sizeof path) return -1;
        if (fe2_file_exists(path)) continue;
        if (parent_dirs(path) || fe2_write_bytes(path, fe2_authored_files[i].data, fe2_authored_files[i].count))
            return fe2_asset_fail("Could not write %s", path);
    }
    return 0;
}

static int program_image(const fe2_blob *blob, const uint8_t **image, size_t *count)
{
    unsigned header;
    if (blob->count < 64 || blob->data[0] != 'M' || blob->data[1] != 'Z')
        return fe2_asset_fail("Unable to extract this Frontier install: Expected an MZ executable");
    header = ru16(blob->data + 8) * 16u;
    if (header < 64 || header >= blob->count) return fe2_asset_fail("Unable to extract this Frontier install: Invalid MZ header");
    *image = blob->data + header;
    *count = blob->count - header;
    return 0;
}

static int write_gpl(const char *path, const uint8_t *rgb6, size_t colors, const char *name)
{
    fe2_buf text;
    size_t i;
    char line[160];
    memset(&text, 0, sizeof text);
    snprintf(line, sizeof line, "GIMP Palette\nName: %s\nColumns: 16\n# RGB8 display values; the build quantizes to VGA RGB6.\n", name);
    if (fe2_buf_add(&text, line, strlen(line))) return -1;
    for (i = 0; i < colors; ++i) {
        unsigned r = (rgb6[i * 3] * 255u + 31u) / 63u;
        unsigned g = (rgb6[i * 3 + 1] * 255u + 31u) / 63u;
        unsigned b = (rgb6[i * 3 + 2] * 255u + 31u) / 63u;
        snprintf(line, sizeof line, "%3u %3u %3u  color_%03u\n", r, g, b, (unsigned)i);
        if (fe2_buf_add(&text, line, strlen(line))) { fe2_buf_free(&text); return -1; }
    }
    if (parent_dirs(path) || fe2_write_bytes(path, text.data, text.count)) { fe2_buf_free(&text); return -1; }
    fe2_buf_free(&text);
    return 0;
}

static int write_font(const char *path, const uint8_t *glyphs)
{
    fe2_buf text;
    int index, row;
    char line[80];
    memset(&text, 0, sizeof text);
    {
        const char *header = "STARTFONT 2.1\nFONT fe2-recovered\nSIZE 9 75 75\nFONTBOUNDINGBOX 8 9 0 0\n"
            "COMMENT ENCODING values are game glyph IDs, not Unicode characters\n"
            "STARTPROPERTIES 2\nFONT_ASCENT 9\nFONT_DESCENT 0\nENDPROPERTIES\nCHARS 115\n";
        if (fe2_buf_add(&text, header, strlen(header))) return -1;
    }
    for (index = 0; index < 115; ++index) {
        const uint8_t *glyph = glyphs + index * 10;
        unsigned advance = glyph[9];
        snprintf(line, sizeof line, "STARTCHAR glyph_%03d\nENCODING %d\nSWIDTH %u 0\nDWIDTH %u 0\nBBX 8 9 0 0\nBITMAP\n",
                 index, index, advance * 8000u / 9u, advance);
        if (fe2_buf_add(&text, line, strlen(line))) goto fail;
        for (row = 0; row < 9; ++row) {
            snprintf(line, sizeof line, "%02X\n", glyph[row]);
            if (fe2_buf_add(&text, line, strlen(line))) goto fail;
        }
        if (fe2_buf_add(&text, "ENDCHAR\n", 8)) goto fail;
    }
    if (fe2_buf_add(&text, "ENDFONT\n", 8) || parent_dirs(path) || fe2_write_bytes(path, text.data, text.count)) goto fail;
    fe2_buf_free(&text);
    return 0;
fail:
    fe2_buf_free(&text);
    return -1;
}

static int extract_mcga(const fe2_blob *blob, const char *video, uint8_t **preview, size_t *preview_colors)
{
    const uint8_t *image;
    size_t count, i;
    uint8_t palette[127 * 3], mixed[768];
    char path[4096];
    if (program_image(blob, &image, &count)) return -1;
    if (count < 0x5f64 + 115 * 10) 
        return fe2_asset_fail("Unable to extract this Frontier install: EL2MCGA3.OVL does not contain the font and boot palette");
    {
        size_t at;
        int marked = 0;
        for (at = 0x5d4b; at + 8 <= 0x5de7; ++at) if (!memcmp(image + at, "EL2G.GRF", 8)) marked = 1;
        if (!marked)
            return fe2_asset_fail("Unable to extract this Frontier install: EL2MCGA3.OVL does not contain the font and boot palette");
    }
    memcpy(palette, image + 0x5de7, sizeof palette);
    for (i = 0; i < sizeof palette; ++i) if (palette[i] > 63)
        return fe2_asset_fail("Unable to extract this Frontier install: Boot palette is not 127 VGA colors");
    if (snprintf(path, sizeof path, "%s/default.gpl", video) >= (int)sizeof path ||
        write_gpl(path, palette, 127, "FE2 MCGA canonical palette")) return -1;
    if (snprintf(path, sizeof path, "%s/font.bdf", video) >= (int)sizeof path || write_font(path, image + 0x5f64)) return -1;
    memset(mixed, 0, sizeof mixed);
    memcpy(mixed, palette, sizeof palette);
    memcpy(mixed + sizeof palette, image + 0x5f64, sizeof mixed - sizeof palette);
    for (i = 0; i < 768; ++i) mixed[i] &= 63;
    *preview = (uint8_t *)malloc(768);
    if (!*preview) return -1;
    memcpy(*preview, mixed, 768);
    *preview_colors = 256;
    return 0;
}

static int extract_portrait(const fe2_blob *blob, const char *palette_dir, uint8_t *rgb6)
{
    const uint8_t *image;
    size_t count, i;
    char path[4096];
    if (program_image(blob, &image, &count) || count < 0x680)
        return fe2_asset_fail("Unable to extract this Frontier install: EL2M13.OVL does not contain the portrait palette");
    for (i = 0xb0; i < 0x680; ++i) if (image[i] > 63)
        return fe2_asset_fail("Unable to extract this Frontier install: Portrait palette is not 496 VGA colors");
    memcpy(rgb6, image + 0xb0, 496 * 3);
    if (snprintf(path, sizeof path, "%s/composites.gpl", palette_dir) >= (int)sizeof path) return -1;
    return write_gpl(path, rgb6, 496, "FE2 Composite palette");
}

/* Sprites 109..111 and 120..207 are the station portraits. Their PNG colors
 * from index 128 are the composite upload for an identity whose selectors
 * are all zero, so an editor shows the face instead of the font-byte preview. */
static int portrait_sprite(unsigned index)
{
    return (index >= 109 && index <= 111) || (index >= 120 && index <= 207);
}

static int parse_count(const char *text, int *out)
{
    char *end;
    long value;
    if (!text || !*text) return -1;
    value = strtol(text, &end, 10);
    if (end == text || *end || value < 0 || value > 65535) return -1;
    *out = (int)value;
    return 0;
}

static int first_palette_view(const char *path, char *name, size_t cap)
{
    FILE *file = fopen(path, "rb");
    char line[128];
    int row = 0;
    if (!file) return -1;
    while (fgets(line, sizeof line, file)) {
        size_t length = strlen(line);
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = 0;
        if (!length) continue;
        if (row++ == 0) {
            if (strcmp(line, "view") != 0) { fclose(file); return -1; }
            continue;
        }
        if (snprintf(name, cap, "%s", line) >= (int)cap) { fclose(file); return -1; }
        fclose(file);
        return 0;
    }
    fclose(file);
    return -1;
}

static int palette_view(fe2_ini *views, const char *name, int *first, int *count)
{
    char section[32];
    fe2_ini_section *found;
    if (strncmp(name, "view_", 5) != 0) return -1;
    if (snprintf(section, sizeof section, "view %s", name + 5) >= (int)sizeof section) return -1;
    found = fe2_ini_section_find(views, section);
    if (!found || parse_count(fe2_ini_get(found, "first_color"), first) ||
        parse_count(fe2_ini_get(found, "color_count"), count)) return -1;
    return 0;
}

static int copy_view_colors(uint8_t *dest, int at, const uint8_t *bank, int first, int count, int expect, int take)
{
    if (count != expect || first < 0 || at < 0 || at > 256 - take || first > 496 - take) return -1;
    memcpy(dest + (size_t)at * 3, bank + (size_t)first * 3, (size_t)take * 3);
    return 0;
}

static int portrait_preview_colors(const char *data_root, const uint8_t *boot, const uint8_t *bank, uint8_t *out)
{
    static const struct { int at; const char *file; } slots[] = {
        {0xd8, "feature_palette_pointers.csv"},
        {0xe0, "detail_palette_pointers.csv"},
        {0xe8, "secondary_palette_pointers.csv"},
        {0xf0, "feature_palette_pointers.csv"},
        {0xf8, "feature_palette_pointers.csv"}
    };
    fe2_ini views;
    char path[4096], name[64];
    int first, count, slot;
    memset(&views, 0, sizeof views);
    memcpy(out, boot, 768);
    if (snprintf(path, sizeof path, "%s/palettes/composites.ini", data_root) >= (int)sizeof path ||
        fe2_ini_load(&views, path)) goto fail;
    if (snprintf(path, sizeof path, "%s/composites/base_palette_pointers.csv", data_root) >= (int)sizeof path ||
        first_palette_view(path, name, sizeof name) || palette_view(&views, name, &first, &count) ||
        copy_view_colors(out, 0x80, bank, first, count, 44, 88)) goto fail;
    for (slot = 0; slot < 5; ++slot) {
        if (snprintf(path, sizeof path, "%s/composites/%s", data_root, slots[slot].file) >= (int)sizeof path ||
            first_palette_view(path, name, sizeof name) || palette_view(&views, name, &first, &count) ||
            copy_view_colors(out, slots[slot].at, bank, first, count, 8, 8)) goto fail;
    }
    fe2_ini_free(&views);
    return 0;
fail:
    fe2_ini_free(&views);
    return fe2_asset_fail("Portrait sprites need the composite palette views");
}

static int read_display_color(char **cursor, int *out)
{
    char *end;
    long value;
    while (**cursor == ' ' || **cursor == '\t') ++*cursor;
    value = strtol(*cursor, &end, 10);
    if (end == *cursor || value < 0 || value > 255) return -1;
    *cursor = end;
    *out = (int)value;
    return 0;
}

/* The system chart uploads these 128 colors at palette index 128. */
static int read_chart_palette(const char *path, uint8_t *rgb6)
{
    FILE *file = fopen(path, "rb");
    char line[256];
    size_t found = 0;
    int header = 0;
    if (!file) return -1;
    while (fgets(line, sizeof line, file)) {
        char *cursor = line;
        int red, green, blue;
        size_t length = strlen(line);
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = 0;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (!*cursor || *cursor == '#') continue;
        if (!header) {
            if (strncmp(cursor, "GIMP Palette", 12) != 0) { fclose(file); return -1; }
            header = 1;
            continue;
        }
        if (!strncmp(cursor, "Name:", 5) || !strncmp(cursor, "Columns:", 8)) continue;
        if (read_display_color(&cursor, &red) || read_display_color(&cursor, &green) ||
            read_display_color(&cursor, &blue) || found >= 128) {
            fclose(file);
            return -1;
        }
        rgb6[found * 3] = (uint8_t)((red * 63 + 127) / 255);
        rgb6[found * 3 + 1] = (uint8_t)((green * 63 + 127) / 255);
        rgb6[found * 3 + 2] = (uint8_t)((blue * 63 + 127) / 255);
        ++found;
    }
    fclose(file);
    return found == 128 ? 0 : -1;
}

static int read_map_sprites(const char *path, unsigned char *ids)
{
    FILE *file = fopen(path, "rb");
    char line[64];
    int any = 0;
    if (!file) return -1;
    memset(ids, 0, 225);
    while (fgets(line, sizeof line, file)) {
        char *end;
        long value;
        size_t length = strlen(line);
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = 0;
        if (!length) continue;
        value = strtol(line, &end, 10);
        if (end == line || *end || value < 0 || value >= 225) { fclose(file); return -1; }
        ids[value] = 1;
        any = 1;
    }
    fclose(file);
    return any ? 0 : -1;
}

static int map_preview_colors(const char *data_root, const uint8_t *boot, uint8_t *out, unsigned char *ids)
{
    char path[4096];
    uint8_t bank[128 * 3];
    memcpy(out, boot, 768);
    if (snprintf(path, sizeof path, "%s/maps/hierarchy_sprite_selectors.csv", data_root) >= (int)sizeof path ||
        read_map_sprites(path, ids)) goto fail;
    if (snprintf(path, sizeof path, "%s/maps/system_view_palette.gpl", data_root) >= (int)sizeof path ||
        read_chart_palette(path, bank)) goto fail;
    memcpy(out + 128 * 3, bank, sizeof bank);
    return 0;
fail:
    return fe2_asset_fail("Map sprites need the system chart palette");
}

static int extract_sprites(const fe2_blob *blob, const char *graphics, const uint8_t *preview,
                           const uint8_t *portraits, const uint8_t *map_colors, const unsigned char *map_ids)
{
    fe2_sprite *sprites = NULL;
    size_t count = 0, i;
    fe2_ini ini;
    fe2_buf text;
    char path[4096];
    memset(&ini, 0, sizeof ini);
    memset(&text, 0, sizeof text);
    if (snprintf(path, sizeof path, "%s/el2g.ini", graphics) < (int)sizeof path) fe2_ini_load(&ini, path);
    if (fe2_decode_grf(blob->data, blob->count, &sprites, &count) || count != 225) {
        fe2_sprites_free(sprites, count);
        fe2_ini_free(&ini);
        return fe2_asset_fail("Unable to extract this Frontier install: EL2G.GRF");
    }
    {
        const char *header = "# PNG pixel indices are authoritative. Do not reorder palette entries.\n"
            "# Indices 0..127 preview the boot palette. Portrait indices 128..255 preview\n"
            "# the identity-zero composite upload. Map body indices 128..255 preview the\n"
            "# system chart palette. Refresh: python tools/sprite_previews.py\n"
            "# Compression files preserve original token choices.\n\n";
        if (fe2_buf_add(&text, header, strlen(header))) goto fail;
    }
    for (i = 0; i < count; ++i) {
        char section[32], image_rel[64], plan_rel[64], line[160];
        const char *image = NULL, *plan = NULL;
        snprintf(section, sizeof section, "sprite %03u", (unsigned)i);
        {
            fe2_ini_section *existing = fe2_ini_section_find(&ini, section);
            if (existing) {
                image = fe2_ini_get(existing, "image");
                plan = fe2_ini_get(existing, "compression");
            }
        }
        snprintf(image_rel, sizeof image_rel, "%s", image ? image : "");
        snprintf(plan_rel, sizeof plan_rel, "%s", plan ? plan : "");
        if (!image_rel[0]) snprintf(image_rel, sizeof image_rel, "sprites/%03u.png", (unsigned)i);
        if (!plan_rel[0]) snprintf(plan_rel, sizeof plan_rel, "compression/%03u.lz", (unsigned)i);
        {
            const uint8_t *colors = preview;
            if (portraits && portrait_sprite((unsigned)i)) colors = portraits;
            else if (map_colors && map_ids && map_ids[i]) colors = map_colors;
            if (snprintf(path, sizeof path, "%s/%s", graphics, image_rel) >= (int)sizeof path || parent_dirs(path) ||
                fe2_write_indexed_png(path, sprites[i].width, sprites[i].height, sprites[i].pixels,
                                      sprites[i].flags & 1, colors, colors ? 256 : 0)) goto fail;
        }
        if (snprintf(path, sizeof path, "%s/%s", graphics, plan_rel) >= (int)sizeof path || parent_dirs(path) ||
            fe2_write_bytes(path, sprites[i].plan, strlen(sprites[i].plan))) goto fail;
        snprintf(line, sizeof line, "[%s]\nimage = %s\ncompression = %s\nflags = %u\noffset_x = %d\noffset_y = %d\n\n",
                 section, image_rel, plan_rel, sprites[i].flags, sprites[i].offset_x, sprites[i].offset_y);
        if (fe2_buf_add(&text, line, strlen(line))) goto fail;
    }
    if (snprintf(path, sizeof path, "%s/el2g.ini", graphics) >= (int)sizeof path || fe2_write_bytes(path, text.data, text.count))
        goto fail;
    fe2_buf_free(&text);
    fe2_sprites_free(sprites, count);
    fe2_ini_free(&ini);
    return 0;
fail:
    fe2_buf_free(&text);
    fe2_sprites_free(sprites, count);
    fe2_ini_free(&ini);
    return fe2_asset_fail("Unable to extract this Frontier install: EL2G.GRF");
}

static int find_bytes(const uint8_t *hay, size_t hay_n, const uint8_t *needle, size_t needle_n, size_t *at)
{
    size_t i, hits = 0, found = 0;
    if (!needle_n || needle_n > hay_n) return -1;
    for (i = 0; i + needle_n <= hay_n; ++i) if (!memcmp(hay + i, needle, needle_n)) { found = i; ++hits; }
    if (hits != 1) return -1;
    *at = found;
    return 0;
}

static int write_one_model(const char *name, const uint8_t *record, size_t record_n, const char *bank,
                           const char *csv, const char *names, int allow_contact)
{
    unsigned program_at, vertex_at, vertex_work, class_at, class_work, header[3];
    size_t vertex_len, class_len, extension_n, program_n, contact_at = 0, contact_end = 0, render_n;
    const uint8_t *extension, *vertices, *classification, *program;
    uint8_t *render = NULL;
    char *source = NULL, *contact = NULL, *offsets = NULL;
    int absent, status = -1;
    unsigned entries[256];
    size_t entry_n = 0;
    if (record_n < 16) return fe2_asset_fail("Unable to extract this Frontier install: %s", name);
    program_at = ru16(record); vertex_at = ru16(record + 2); vertex_work = ru16(record + 4);
    class_at = ru16(record + 6); class_work = ru16(record + 8);
    header[0] = ru16(record + 10); header[1] = ru16(record + 12); header[2] = ru16(record + 14);
    if (vertex_at < 16 || vertex_at > record_n || vertex_work % 16) return fe2_asset_fail("Unable to extract this Frontier install: %s", name);
    vertex_len = vertex_work / 16;
    extension = record + 16;
    extension_n = vertex_at - 16;
    vertices = record + vertex_at;
    if (vertex_at + vertex_len > record_n) return fe2_asset_fail("Unable to extract this Frontier install: %s", name);
    if (!class_at && !class_work) {
        absent = 1; classification = NULL; class_len = 0;
        if (program_at != vertex_at + vertex_len) return fe2_asset_fail("Unable to extract this Frontier install: %s", name);
    } else {
        absent = 0;
        if (class_work < 4 || class_at + (class_work - 4) > record_n || program_at != class_at + (class_work - 4))
            return fe2_asset_fail("Unable to extract this Frontier install: %s", name);
        class_len = class_work - 4;
        classification = record + class_at;
    }
    program = record + program_at;
    program_n = record_n - program_at;
    render_n = program_n;
    render = (uint8_t *)malloc(program_n ? program_n : 1);
    if (!render) return -1;
    memcpy(render, program, program_n);
    if (allow_contact && extension_n >= 12) {
        unsigned pointer = extension[10] | ((unsigned)extension[11] << 8);
        if (pointer) {
            size_t pos;
            if (pointer < program_at || pointer - program_at >= program_n) goto done;
            contact_at = pointer - program_at;
            if (fe2_decode_contact(program, program_n, contact_at, &contact, &contact_end)) goto done;
            pos = contact_at;
            while (pos >= 2) {
                if (!rs16(program + pos - 2)) break;
                if (pos < 4) { pos = contact_at; break; }
                pos -= 4;
            }
            if (pos < 2) pos = contact_at;
            if (pos != contact_at && fe2_decode_offsets(program + pos, contact_at - pos, &offsets)) {
                free(offsets); offsets = NULL; pos = contact_at;
            }
            render_n = pos + (program_n - contact_end);
            memcpy(render, program, pos);
            memcpy(render + pos, program + contact_end, program_n - contact_end);
            contact_at = pos;
        }
    }
    if (extension_n == 118 || extension_n == 198) {
        size_t base = 16 + extension_n + vertex_len + class_len, index;
        for (index = 16; index < extension_n; index += 2) {
            unsigned address = extension[index] | ((unsigned)extension[index + 1] << 8);
            if (address < base) { entry_n = 0; break; }
            entries[entry_n++] = address - (unsigned)base;
        }
    }
    if (fe2_decode_program(render, render_n, entry_n ? entries : NULL, entry_n, &source)) goto done;
    if (csv) {
        fe2_buf needle;
        size_t at = 0;
        memset(&needle, 0, sizeof needle);
        if (!fe2_ship_record(csv, name, names, &needle) && !find_bytes(render, render_n, needle.data, needle.count, &at)) {
            char *rewritten = (char *)malloc(strlen(source) + 128);
            char *cursor, *line;
            size_t used = 0, filled = 0;
            int skipping = 0, ok = rewritten != NULL;
            if (rewritten) rewritten[0] = 0;
            cursor = source;
            while (ok && cursor && *cursor) {
                char *eol = strchr(cursor, '\n');
                fe2_buf chunk;
                if (eol) *eol = 0;
                line = cursor;
                memset(&chunk, 0, sizeof chunk);
                if (fe2_compile_program(line, NULL, NULL, &chunk)) ok = 0;
                else if (!skipping && used == at) {
                    char directive[96];
                    snprintf(directive, sizeof directive, "ship_parameters file=ship_parameters.csv model=%s\n", name);
                    if (filled + strlen(directive) + 1 > strlen(source) + 128) ok = 0;
                    else { memcpy(rewritten + filled, directive, strlen(directive) + 1); filled += strlen(directive); }
                    skipping = 1;
                }
                if (ok && skipping) {
                    used += chunk.count;
                    if (used == at + needle.count) skipping = 0;
                    else if (used > at + needle.count) ok = 0;
                } else if (ok) {
                    size_t n = strlen(line);
                    if (filled + n + 2 > strlen(source) + 128) ok = 0;
                    else { memcpy(rewritten + filled, line, n); filled += n; rewritten[filled++] = '\n'; rewritten[filled] = 0; }
                    used += chunk.count;
                }
                fe2_buf_free(&chunk);
                if (!eol) break;
                *eol = '\n';
                cursor = eol + 1;
            }
            if (ok && !skipping && used == render_n) { free(source); source = rewritten; rewritten = NULL; }
            free(rewritten);
        }
        fe2_buf_free(&needle);
    }
    {
        char *out = (char *)malloc((source ? strlen(source) : 0) + 4096);
        char *cursor;
        size_t filled = 0;
        unsigned used_offsets[64], used_n = 0;
        if (!out) goto done;
        out[0] = 0;
        cursor = source;
        while (cursor && *cursor) {
            char *eol = strchr(cursor, '\n');
            char *payload;
            if (eol) *eol = 0;
            payload = strstr(cursor, " payload=");
            if (!strncmp(cursor, "geometry_pipeline ", 18) && payload && strcmp(payload + 9, "-") != 0) {
                const char *hex = payload + 9;
                const char *comment = strstr(hex, " # ");
                size_t hex_n = comment ? (size_t)(comment - hex) : strlen(hex);
                uint8_t *bytes = (uint8_t *)malloc(hex_n / 2 + 1);
                size_t n = 0, h;
                char *geometry = NULL;
                char relative[96];
                unsigned stamp = used_n;
                if (comment && !strncmp(comment, " # program +0x", 14)) stamp = (unsigned)strtoul(comment + 14, NULL, 16);
                while (stamp < 0x10000) {
                    size_t u;
                    int clash = 0;
                    for (u = 0; u < used_n; ++u) if (used_offsets[u] == stamp) clash = 1;
                    if (!clash) break;
                    ++stamp;
                }
                if (used_n < 64) used_offsets[used_n++] = stamp;
                for (h = 0; h + 1 < hex_n; h += 2) {
                    unsigned value;
                    if (sscanf(hex + h, "%2x", &value) != 1) break;
                    bytes[n++] = (uint8_t)value;
                }
                if (fe2_decode_geometry(bytes, n, &geometry)) {
                    size_t nline = strlen(cursor);
                    memcpy(out + filled, cursor, nline);
                    filled += nline;
                    out[filled++] = '\n';
                } else {
                    char dest[4096];
                    snprintf(relative, sizeof relative, "geometry/%s_%04x.geometry", name, stamp);
                    snprintf(dest, sizeof dest, "%s/%s", bank, relative);
                    if (parent_dirs(dest) || fe2_write_bytes(dest, geometry, strlen(geometry))) { free(bytes); free(geometry); free(out); goto done; }
                    payload[1] = 0;
                    filled += (size_t)sprintf(out + filled, "%sfile=%s%s\n", cursor, relative, comment ? comment : "");
                }
                free(bytes);
                free(geometry);
            } else {
                size_t nline = strlen(cursor);
                memcpy(out + filled, cursor, nline);
                filled += nline;
                out[filled++] = '\n';
                out[filled] = 0;
            }
            if (!eol) break;
            *eol = '\n';
            cursor = eol + 1;
        }
        free(source);
        source = out;
    }
    {
        fe2_buf model;
        char path[4096], line[128];
        size_t i;
        const uint8_t *prefix = extension;
        size_t prefix_n = (entry_n ? 16 : extension_n);
        memset(&model, 0, sizeof model);
        if (fe2_buf_add(&model, "[header]\n", 9)) goto model_fail;
        for (i = 0; i < 3; ++i) {
            const char *keys[] = {"scale_shift", "reserved_0c", "culling_radius"};
            snprintf(line, sizeof line, "%s = 0x%04x\n", keys[i], header[i]);
            if (fe2_buf_add(&model, line, strlen(line))) goto model_fail;
        }
        {
            const char *extension_header = "\n[extension]\nbytes =\n";
            if (fe2_buf_add(&model, extension_header, strlen(extension_header))) goto model_fail;
        }
        for (i = 0; i < prefix_n; i += 16) {
            size_t c, end = i + 16 < prefix_n ? i + 16 : prefix_n;
            if (fe2_buf_add(&model, "    ", 4)) goto model_fail;
            for (c = i; c < end; ++c) {
                snprintf(line, sizeof line, "%s0x%02x", c == i ? "" : ", ", prefix[c]);
                if (fe2_buf_add(&model, line, strlen(line))) goto model_fail;
            }
            if (fe2_buf_byte(&model, '\n')) goto model_fail;
        }
        if (entry_n && fe2_buf_add(&model, "instance_programs =\n", 20)) goto model_fail;
        for (i = 0; i < entry_n; ++i) {
            snprintf(line, sizeof line, "    0x%02x, 0x%04x\n", (unsigned)(0x20 + i), entries[i]);
            if (fe2_buf_add(&model, line, strlen(line))) goto model_fail;
        }
        if (fe2_buf_add(&model, "\n[vertices]\nrecords =\n", 22)) goto model_fail;
        for (i = 0; i < vertex_len; i += 4) {
            snprintf(line, sizeof line, "    0x%02x, 0x%02x, 0x%02x, 0x%02x\n", vertices[i], vertices[i+1], vertices[i+2], vertices[i+3]);
            if (fe2_buf_add(&model, line, strlen(line))) goto model_fail;
        }
        if (fe2_buf_add(&model, "\n[classification]\nrecords =\n", 28)) goto model_fail;
        for (i = 0; i < class_len; i += 4) {
            snprintf(line, sizeof line, "    0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
                     classification[i], classification[i+1], classification[i+2], classification[i+3]);
            if (fe2_buf_add(&model, line, strlen(line))) goto model_fail;
        }
        if (fe2_buf_add(&model, "\n[program]\ninstructions =\n", 26)) goto model_fail;
        if (source) {
            char *line_cursor = source;
            while (*line_cursor) {
                char *eol = strchr(line_cursor, '\n');
                size_t n;
                if (eol) *eol = 0;
                n = strlen(line_cursor);
                while (n && (line_cursor[0] == ' ' || line_cursor[0] == '\t')) { ++line_cursor; --n; }
                if (n && (fe2_buf_add(&model, "    ", 4) || fe2_buf_add(&model, line_cursor, n) || fe2_buf_byte(&model, '\n')))
                    goto model_fail;
                if (!eol) break;
                line_cursor = eol + 1;
            }
        }
        if (contact) {
            snprintf(path, sizeof path, "%s/contacts/%s.contact", bank, name);
            if (parent_dirs(path) || fe2_write_bytes(path, contact, strlen(contact))) goto model_fail;
            snprintf(line, sizeof line, "\n[contact]\nfile = contacts/%s.contact\nprogram_offset = %u\n", name, (unsigned)contact_at);
            if (fe2_buf_add(&model, line, strlen(line))) goto model_fail;
            if (offsets) {
                snprintf(path, sizeof path, "%s/contact_offsets/%s.basis_offsets", bank, name);
                if (parent_dirs(path) || fe2_write_bytes(path, offsets, strlen(offsets))) goto model_fail;
                snprintf(line, sizeof line, "\n[contact_offsets]\nfile = contact_offsets/%s.basis_offsets\nprogram_offset = %u\n",
                         name, (unsigned)contact_at);
                if (fe2_buf_add(&model, line, strlen(line))) goto model_fail;
            }
        }
        snprintf(path, sizeof path, "%s/models/%s.model", bank, name);
        if (parent_dirs(path) || fe2_write_bytes(path, model.data, model.count)) goto model_fail;
        fe2_buf_free(&model);
        status = 0;
        goto done;
    model_fail:
        fe2_buf_free(&model);
    }
done:
    free(render);
    free(source);
    free(contact);
    free(offsets);
    (void)absent;
    return status;
}

static int extract_bank(const uint8_t *image, size_t image_n, const char *ini_path, const char *names, int allow_contact)
{
    fe2_ini ini;
    char base[4096], csv_path[4096], *csv = NULL;
    size_t s, slash;
    if (fe2_ini_load(&ini, ini_path)) return fe2_asset_fail("Could not read %s", ini_path);
    slash = strlen(ini_path);
    while (slash && ini_path[slash - 1] != '/' && ini_path[slash - 1] != '\\') --slash;
    if (!slash || slash >= sizeof base) { fe2_ini_free(&ini); return -1; }
    memcpy(base, ini_path, slash - 1);
    base[slash - 1] = 0;
    if (snprintf(csv_path, sizeof csv_path, "%s/ship_parameters.csv", base) < (int)sizeof csv_path)
        csv = fe2_read_text(csv_path, NULL);
    for (s = 0; s < ini.count; ++s) {
        fe2_ini_section *section = &ini.sections[s];
        long long start, end;
        if (!strcmp(section->name, "bank") || !strcmp(section->name, "trailer")) continue;
        if (!fe2_ini_get(section, "start") || !fe2_ini_get(section, "end")) continue;
        start = strtoll(fe2_ini_get(section, "start"), NULL, 0);
        end = strtoll(fe2_ini_get(section, "end"), NULL, 0);
        if (start < 0 || end <= start || (size_t)end > image_n) {
            free(csv); fe2_ini_free(&ini);
            return fe2_asset_fail("Unable to extract this Frontier install: %s", section->name);
        }
        if (write_one_model(section->name, image + start, (size_t)(end - start), base, csv, names, allow_contact)) {
            free(csv); fe2_ini_free(&ini);
            if (!fe2_asset_error()[0]) fe2_asset_fail("Unable to extract this Frontier install: %s", section->name);
            return -1;
        }
    }
    free(csv);
    fe2_ini_free(&ini);
    return 0;
}

static int append_escaped(fe2_buf *out, const uint8_t *bytes, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        char hex[5];
        if (bytes[i] == '\\') {
            if (fe2_buf_add(out, "\\\\", 2)) return -1;
        } else if (bytes[i] >= 32 && bytes[i] < 127) {
            if (fe2_buf_byte(out, bytes[i])) return -1;
        } else {
            sprintf(hex, "\\x%02X", bytes[i]);
            if (fe2_buf_add(out, hex, 4)) return -1;
        }
    }
    return 0;
}

static int extract_text_catalog(const uint8_t *image, size_t image_n, const char *data_root, const char *module)
{
    char ini_path[4096], names_path[4096], out_path[4096];
    fe2_ini ini;
    fe2_ini_section *section;
    const char *start_text, *end_text, *count_text;
    char *names, *line, *next;
    long long start, end, count, index, cursor;
    const uint8_t *block;
    fe2_buf out = {0};
    int status = -1;
    if (snprintf(ini_path, sizeof ini_path, "%s/text/%s/catalog.ini", data_root, module) >= (int)sizeof ini_path ||
        snprintf(names_path, sizeof names_path, "%s/text/%s/identifiers.txt", data_root, module) >= (int)sizeof names_path ||
        snprintf(out_path, sizeof out_path, "%s/text/%s/strings.tsv", data_root, module) >= (int)sizeof out_path)
        return -1;
    if (fe2_ini_load(&ini, ini_path)) return fe2_asset_fail("Could not read %s", ini_path);
    section = fe2_ini_section_find(&ini, "strings");
    start_text = section ? fe2_ini_get(section, "directory_offset") : NULL;
    end_text = section ? fe2_ini_get(section, "end_offset") : NULL;
    count_text = section ? fe2_ini_get(section, "count") : NULL;
    names = fe2_read_text(names_path, NULL);
    if (!start_text || !end_text || !count_text || !names) {
        fe2_asset_fail("This project is missing text/%s identifiers", module);
        goto done;
    }
    start = strtoll(start_text, NULL, 0);
    end = strtoll(end_text, NULL, 0);
    count = strtoll(count_text, NULL, 0);
    if (start < 0 || end <= start || count <= 0 || (size_t)end > image_n || (size_t)(end - start) < (size_t)(2 * count)) {
        fe2_asset_fail("Unable to extract this Frontier install: %s text does not match its catalog", module);
        goto done;
    }
    block = image + start;
    line = names;
    cursor = 2 * count;
    for (index = 0; index < count; ++index) {
        unsigned relative = ru16(block + 2 * index);
        size_t length = 0;
        char *tab = strpbrk(line, "\r\n");
        next = tab ? tab + 1 : line + strlen(line);
        if (tab && *tab == '\r' && *next == '\n') ++next;
        if (tab) *tab = 0;
        if (!line[0] || relative != (unsigned)cursor) {
            fe2_asset_fail("Unable to extract this Frontier install: %s text directory is not contiguous", module);
            goto done;
        }
        while (cursor + (long long)length < end - start && block[cursor + (long long)length] != 0) ++length;
        if (cursor + (long long)length >= end - start) {
            fe2_asset_fail("Unable to extract this Frontier install: %s text directory is not contiguous", module);
            goto done;
        }
        if (fe2_buf_add(&out, line, strlen(line)) || fe2_buf_byte(&out, '\t') ||
            append_escaped(&out, block + cursor, length) || fe2_buf_byte(&out, '\n')) goto done;
        cursor += (long long)length + 1;
        line = next;
    }
    if (cursor != end - start || line[0]) {
        fe2_asset_fail("Unable to extract this Frontier install: %s text does not fill its catalog span", module);
        goto done;
    }
    if (fe2_write_bytes(out_path, out.data, out.count)) {
        fe2_asset_fail("Could not write %s", out_path);
        goto done;
    }
    status = 0;
done:
    fe2_buf_free(&out);
    free(names);
    fe2_ini_free(&ini);
    return status;
}

static int append_json(fe2_buf *out, const char *text, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        char escaped[3] = {'\\', 0, 0};
        if (text[i] == '\r') escaped[1] = 'r';
        else if (text[i] == '\n') escaped[1] = 'n';
        else if (text[i] == '\\' || text[i] == '"') escaped[1] = text[i];
        else if ((unsigned char)text[i] < 32 || (unsigned char)text[i] >= 127) return -1;
        else {
            if (fe2_buf_byte(out, (uint8_t)text[i])) return -1;
            continue;
        }
        if (fe2_buf_add(out, escaped, 2)) return -1;
    }
    return 0;
}

static int extract_setup_messages(const fe2_blob *blob, const char *data_root)
{
    static const char *keys[] = {
        "dos_version_message", "cpu_required_message", "base_memory_message",
        "ems_missing_message", "ems_memory_message", "sound_menu_message", "ems_device_name"
    };
    const uint8_t *image;
    size_t image_n, cursor, index;
    fe2_buf out = {0};
    char path[4096];
    int status = -1;
    if (program_image(blob, &image, &image_n) || image_n < 0x6a) return -1;
    if (snprintf(path, sizeof path, "%s/setup/messages.json", data_root) >= (int)sizeof path) return -1;
    if (parent_dirs(path) || fe2_buf_add(&out, "{\n", 2)) goto done;
    cursor = 0x6a;
    for (index = 0; index < 7; ++index) {
        size_t end = cursor;
        int last = index == 6;
        if (fe2_buf_add(&out, "  \"", 3) || fe2_buf_add(&out, keys[index], strlen(keys[index])) ||
            fe2_buf_add(&out, "\": \"", 4)) goto done;
        if (last) {
            while (end < image_n && image[end] != 0) ++end;
        } else {
            while (end < image_n && image[end] != '$') ++end;
        }
        if (end >= image_n || append_json(&out, (const char *)image + cursor, end - cursor) ||
            fe2_buf_add(&out, last ? "\"\n" : "\",\n", last ? 2 : 3)) {
            fe2_asset_fail("Unable to extract this Frontier install: EL2SETUP.OVL does not contain the setup messages");
            goto done;
        }
        cursor = end + (last ? 0 : 1);
    }
    if (fe2_buf_add(&out, "}\n", 2) || fe2_write_bytes(path, out.data, out.count)) goto done;
    status = 0;
done:
    fe2_buf_free(&out);
    return status;
}

/* One 286-byte intro record, in the same text the Python unpacker writes. */
static int write_intro_object(const char *path, const uint8_t *raw)
{
    unsigned selector = ru16(raw + 0x5a), offset, state = 0;
    char line[512];
    fe2_buf out = {0};
    int status = -1;
    if (selector % 2 || selector / 2 >= 240) return -1;
    snprintf(line, sizeof line,
        "[orientation]\nrows = 0x%04x,0x%04x,0x%04x\n"
        "    0x%04x,0x%04x,0x%04x\n"
        "    0x%04x,0x%04x,0x%04x\n\n"
        "[render]\nword_12 = 0x%04x\n"
        "offset_x = 0x%08x\noffset_y = 0x%08x\noffset_z = 0x%08x\n"
        "model_slot = %u\nphase = 0x%04x\n\n[state]\n",
        ru16(raw), ru16(raw + 2), ru16(raw + 4),
        ru16(raw + 6), ru16(raw + 8), ru16(raw + 10),
        ru16(raw + 12), ru16(raw + 14), ru16(raw + 16),
        ru16(raw + 0x12),
        ru16(raw + 0x14) | (ru16(raw + 0x16) << 16),
        ru16(raw + 0x18) | (ru16(raw + 0x1a) << 16),
        ru16(raw + 0x1c) | (ru16(raw + 0x1e) << 16),
        selector / 2, ru16(raw + 0x72));
    if (fe2_buf_add(&out, line, strlen(line))) goto done;
    for (offset = 0x20; offset < 0x11e; offset += 2) {
        unsigned value;
        if (offset == 0x5a || offset == 0x72) continue;
        value = ru16(raw + offset);
        if (!value) continue;
        snprintf(line, sizeof line, "%s0x%04x,0x%04x\n", state ? "    " : "words = ", offset, value);
        if (fe2_buf_add(&out, line, strlen(line))) goto done;
        ++state;
    }
    if (!state && fe2_buf_add(&out, "words =\n", 8)) goto done;
    if (parent_dirs(path) || fe2_write_bytes(path, out.data, out.count)) goto done;
    status = 0;
done:
    fe2_buf_free(&out);
    return status;
}

static int extract_intro_objects(const uint8_t *image, size_t image_n, const char *data_root)
{
    fe2_ini ini;
    char ini_path[4096];
    size_t s, written = 0;
    if (snprintf(ini_path, sizeof ini_path, "%s/intro/objects.ini", data_root) >= (int)sizeof ini_path) return -1;
    if (fe2_ini_load(&ini, ini_path)) return fe2_asset_fail("Could not read %s", ini_path);
    for (s = 0; s < ini.count; ++s) {
        fe2_ini_section *section = &ini.sections[s];
        const char *file = fe2_ini_get(section, "file");
        const char *start_text = fe2_ini_get(section, "start");
        const char *end_text = fe2_ini_get(section, "end");
        long long start, end;
        char path[4096];
        if (!file || !start_text || !end_text) continue;
        start = strtoll(start_text, NULL, 0);
        end = strtoll(end_text, NULL, 0);
        if (start < 0 || end - start != 286 || (size_t)end > image_n ||
            snprintf(path, sizeof path, "%s/intro/%s", data_root, file) >= (int)sizeof path ||
            write_intro_object(path, image + start)) {
            fe2_ini_free(&ini);
            if (!fe2_asset_error()[0])
                fe2_asset_fail("Unable to extract this Frontier install: %s leaves the intro overlay", section->name);
            return -1;
        }
        ++written;
    }
    fe2_ini_free(&ini);
    if (written == 23) return 0;
    return fe2_asset_fail("Unable to extract this Frontier install: intro objects");
}

static int extract_density(const uint8_t *image, size_t image_n, const char *data_root)
{
    char ini_path[4096], out_path[4096], line[1024];
    fe2_ini ini;
    fe2_ini_section *section;
    long long start, end, width, height, x, y;
    fe2_buf out = {0};
    int status = -1;
    if (snprintf(ini_path, sizeof ini_path, "%s/galaxy/tables.ini", data_root) >= (int)sizeof ini_path ||
        snprintf(out_path, sizeof out_path, "%s/galaxy/density.pgm", data_root) >= (int)sizeof out_path)
        return -1;
    if (fe2_ini_load(&ini, ini_path)) return fe2_asset_fail("This project is missing galaxy/tables.ini");
    section = fe2_ini_section_find(&ini, "galaxy_density_data");
    if (!section || !fe2_ini_get(section, "start") || !fe2_ini_get(section, "end") ||
        !fe2_ini_get(section, "width") || !fe2_ini_get(section, "height")) {
        fe2_asset_fail("This project is missing galaxy/tables.ini");
        goto done;
    }
    start = strtoll(fe2_ini_get(section, "start"), NULL, 0);
    end = strtoll(fe2_ini_get(section, "end"), NULL, 0);
    width = strtoll(fe2_ini_get(section, "width"), NULL, 0);
    height = strtoll(fe2_ini_get(section, "height"), NULL, 0);
    if (start < 0 || width != 128 || height != 129 || end - start != width * height || (size_t)end > image_n) {
        fe2_asset_fail("Unable to extract this Frontier install: EL2M19.OVL does not contain the galaxy density grid");
        goto done;
    }
    {
        const char *header = "P2\n# Frontier galaxy density samples; preserve raw byte intensities.\n128 129\n255\n";
        if (fe2_buf_add(&out, header, strlen(header))) goto done;
    }
    for (y = 0; y < height; ++y) {
        line[0] = 0;
        for (x = 0; x < width; ++x) {
            char piece[8];
            snprintf(piece, sizeof piece, "%s%u", x ? " " : "", image[start + y * width + x]);
            if (strlen(line) + strlen(piece) >= sizeof line) goto done;
            strcat(line, piece);
        }
        if (fe2_buf_add(&out, line, strlen(line)) || fe2_buf_byte(&out, '\n')) goto done;
    }
    if (fe2_write_bytes(out_path, out.data, out.count)) goto done;
    status = 0;
done:
    fe2_buf_free(&out);
    fe2_ini_free(&ini);
    return status;
}

int fe2_unpack_game(const char *source, const char *data_root)
{
    fe2_blob files[FE2_NEEDED];
    const uint8_t *image;
    size_t image_n;
    uint8_t *preview = NULL;
    size_t preview_colors = 0;
    uint8_t composite[496 * 3], portraits[768], map_colors[768];
    unsigned char map_ids[225];
    const uint8_t *portrait_colors = NULL, *chart_colors = NULL;
    char path[4096];
    int status = -1;
    int slot;
    if (load_install(source, files) || check_install(files) || seed_files(data_root)) goto done;
    if (snprintf(path, sizeof path, "%s/video", data_root) >= (int)sizeof path) goto done;
    slot = wanted("el2mcga3.ovl");
    if (extract_mcga(&files[slot], path, &preview, &preview_colors)) goto done;
    if (snprintf(path, sizeof path, "%s/palettes", data_root) >= (int)sizeof path ||
        extract_portrait(&files[wanted("el2m13.ovl")], path, composite)) goto done;
    if (snprintf(path, sizeof path, "%s/palettes/composites.ini", data_root) >= (int)sizeof path) goto done;
    if (fe2_file_exists(path)) {
        if (!preview || portrait_preview_colors(data_root, preview, composite, portraits)) goto done;
        portrait_colors = portraits;
    }
    if (snprintf(path, sizeof path, "%s/maps/system_view_palette.gpl", data_root) >= (int)sizeof path) goto done;
    if (fe2_file_exists(path)) {
        if (!preview || map_preview_colors(data_root, preview, map_colors, map_ids)) goto done;
        chart_colors = map_colors;
    }
    if (snprintf(path, sizeof path, "%s/graphics", data_root) >= (int)sizeof path ||
        extract_sprites(&files[wanted("el2g.grf")], path, preview, portrait_colors, chart_colors, map_ids)) goto done;
    if (program_image(&files[wanted("frontier.exe")], &image, &image_n) ||
        extract_text_catalog(image, image_n, data_root, "frontier")) goto done;
    {
        char names[4096], ini[4096];
        snprintf(names, sizeof names, "%s/text/frontier/strings.tsv", data_root);
        snprintf(ini, sizeof ini, "%s/models/frontier/models.ini", data_root);
        if (extract_bank(image, image_n, ini, names, 1)) goto done;
        snprintf(ini, sizeof ini, "%s/models/frontier_auxiliary/models.ini", data_root);
        if (extract_bank(image, image_n, ini, names, 1)) goto done;
        if (program_image(&files[wanted("el2m08.ovl")], &image, &image_n)) goto done;
        snprintf(ini, sizeof ini, "%s/intro/models.ini", data_root);
        if (extract_bank(image, image_n, ini, names, 0) ||
            extract_intro_objects(image, image_n, data_root)) goto done;
    }
    {
        static const char *modules[] = {
            "frontier", "intro", "systems", "chart", "flight", "names", "commander",
            "status", "offences", "mining", "interaction"
        };
        static const char *overlays[] = {
            "frontier.exe", "el2m08.ovl", "el2m00.ovl", "el2m02.ovl", "el2m03.ovl", "el2m09.ovl",
            "el2m10.ovl", "el2m14.ovl", "el2m15.ovl", "el2m17.ovl", "el2m18.ovl"
        };
        int prose;
        for (prose = 0; prose < 11; ++prose) {
            if (program_image(&files[wanted(overlays[prose])], &image, &image_n) ||
                extract_text_catalog(image, image_n, data_root, modules[prose])) goto done;
        }
        if (program_image(&files[wanted("el2m19.ovl")], &image, &image_n) ||
            extract_density(image, image_n, data_root)) goto done;
        if (program_image(&files[wanted("el2adlib.ovl")], &image, &image_n) ||
            fe2_extract_adlib(image, image_n, data_root)) goto done;
        if (extract_setup_messages(&files[wanted("el2setup.ovl")], data_root)) goto done;
    }
    for (slot = 0; slot < FE2_NEEDED; ++slot) {
        const char *dot;
        if (!strstr(needed_names[slot], ".rol")) continue;
        dot = strchr(needed_names[slot], '.');
        if (snprintf(path, sizeof path, "%s/music/%.*s.mid", data_root, (int)(dot - needed_names[slot]), needed_names[slot]) >= (int)sizeof path ||
            parent_dirs(path) || fe2_write_bytes(path, files[slot].data, files[slot].count)) goto done;
    }
    status = 0;
done:
    free(preview);
    release_blobs(files);
    return status;
}
