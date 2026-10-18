/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "asset_image.h"

#include "asset_files.h"
#include "asset_util.h"
#include "inflate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t fe2_crc32(const uint8_t *data, size_t count)
{
    uint32_t crc = 0xffffffffu;
    size_t i;
    unsigned bit;
    for (i = 0; i < count; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int fe2_read_png(const char *path, fe2_image *image)
{
    FILE *file;
    long length;
    uint8_t *data = NULL;
    size_t size, at, idat_cap = 0, idat_n = 0;
    uint8_t *idat = NULL, *raw = NULL;
    size_t raw_n = 0;
    unsigned width = 0, height = 0, depth = 0, color = 0;
    unsigned compression = 1, filtering = 1, interlace = 1;
    size_t palette = 0, packed, expected, y, x;
    int status = -1;
    memset(image, 0, sizeof *image);
    file = fopen(path, "rb");
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return -1; }
    length = ftell(file);
    if (length < 8 || fseek(file, 0, SEEK_SET)) { fclose(file); return -1; }
    size = (size_t)length;
    data = (uint8_t *)malloc(size);
    if (!data || fread(data, 1, size, file) != size) { free(data); fclose(file); return -1; }
    fclose(file);
    if (memcmp(data, "\x89PNG\r\n\x1a\n", 8) != 0) goto done;
    at = 8;
    while (at + 12 <= size) {
        uint32_t chunk = read_be32(data + at);
        const uint8_t *tag = data + at + 4;
        const uint8_t *payload;
        uint32_t crc;
        if (at + 12 + chunk > size) goto done;
        payload = data + at + 8;
        crc = read_be32(data + at + 8 + chunk);
        if (crc != fe2_crc32(tag, 4 + chunk)) goto done;
        if (memcmp(tag, "IHDR", 4) == 0 && chunk == 13) {
            width = read_be32(payload);
            height = read_be32(payload + 4);
            depth = payload[8];
            color = payload[9];
            compression = payload[10];
            filtering = payload[11];
            interlace = payload[12];
        } else if (memcmp(tag, "PLTE", 4) == 0) {
            palette = chunk / 3;
        } else if (memcmp(tag, "IDAT", 4) == 0) {
            uint8_t *grown = (uint8_t *)realloc(idat, idat_n + chunk);
            if (!grown) goto done;
            idat = grown;
            memcpy(idat + idat_n, payload, chunk);
            idat_n += chunk;
            idat_cap = idat_n;
        } else if (memcmp(tag, "IEND", 4) == 0) {
            at += 12 + chunk;
            break;
        }
        at += 12 + chunk;
    }
    packed = (width * depth + 7) / 8;
    expected = (packed + 1) * height;
    if (!width || width > 65535 || !height || height > 255 || width * height > 65535 ||
        compression || filtering || interlace ||
        !((color == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8)) ||
          (color == 0 && depth == 8)) ||
        (color == 3 && (palette == 0 || palette > 256)) ||
        fe2_inflate_zlib(idat, idat_n, &raw, &raw_n) || raw_n != expected) goto done;
    image->width = width;
    image->height = height;
    image->pixels = (uint8_t *)malloc((size_t)width * height);
    if (!image->pixels) goto done;
    {
        uint8_t *previous = (uint8_t *)calloc(packed ? packed : 1, 1);
        uint8_t *row = (uint8_t *)malloc(packed ? packed : 1);
        if (!previous || !row) { free(previous); free(row); goto done; }
        for (y = 0; y < height; ++y) {
            unsigned filter = raw[y * (packed + 1)];
            memcpy(row, raw + y * (packed + 1) + 1, packed);
            if (filter > 4) { free(previous); free(row); goto done; }
            for (x = 0; x < packed; ++x) {
                unsigned left = x ? row[x - 1] : 0;
                unsigned above = previous[x];
                unsigned corner = x ? previous[x - 1] : 0;
                unsigned value = row[x];
                if (filter == 1) value += left;
                else if (filter == 2) value += above;
                else if (filter == 3) value += (left + above) / 2;
                else if (filter == 4) {
                    int p = (int)left + (int)above - (int)corner;
                    int dl = abs(p - (int)left), da = abs(p - (int)above), dc = abs(p - (int)corner);
                    unsigned pred = dl <= da && dl <= dc ? left : (da <= dc ? above : corner);
                    value += pred;
                }
                row[x] = (uint8_t)value;
            }
            for (x = 0; x < width; ++x) {
                unsigned index = (row[x * depth / 8] >> (8 - depth - (x * depth % 8))) & ((1u << depth) - 1u);
                if (color == 3 && index >= palette) { free(previous); free(row); goto done; }
                image->pixels[y * width + x] = (uint8_t)index;
            }
            memcpy(previous, row, packed);
        }
        free(previous);
        free(row);
    }
    status = 0;
done:
    free(data);
    free(idat);
    free(raw);
    if (status) { free(image->pixels); image->pixels = NULL; }
    (void)idat_cap;
    return status;
}

void fe2_image_free(fe2_image *image)
{
    if (!image) return;
    free(image->pixels);
    memset(image, 0, sizeof *image);
}

int fe2_compress_sprite(const uint8_t *data, size_t count, fe2_buf *out)
{
    size_t position = 0, literals = 0;
    while (position < count) {
        size_t best_count = 0, best_distance = 0, distance, limit;
        limit = position < 2047 ? position : 2047;
        for (distance = 1; distance <= limit; ++distance) {
            size_t run = 0, room = count - position;
            if (room > 16) room = 16;
            while (run < room && data[position + run] == data[position + run - distance]) ++run;
            if (run > best_count) {
                best_count = run;
                best_distance = distance;
                if (run == 16) break;
            }
        }
        if (best_count >= 3) {
            size_t i;
            if (literals) {
                if (fe2_buf_byte(out, (uint8_t)literals) || fe2_buf_add(out, data + position - literals, literals)) return -1;
                literals = 0;
            }
            if (fe2_buf_byte(out, (uint8_t)(((-(int)best_count << 3) & 255) | (best_distance >> 8))) ||
                fe2_buf_byte(out, (uint8_t)best_distance)) return -1;
            for (i = 0; i < best_count; ++i) (void)i;
            position += best_count;
        } else {
            ++literals;
            ++position;
            if (literals == 127) {
                if (fe2_buf_byte(out, 127) || fe2_buf_add(out, data + position - 127, 127)) return -1;
                literals = 0;
            }
        }
    }
    if (literals && (fe2_buf_byte(out, (uint8_t)literals) || fe2_buf_add(out, data + count - literals, literals))) return -1;
    return 0;
}

static int decode_tokens(const uint8_t *stream, size_t stream_n, size_t *used, const uint8_t *history, size_t history_n,
                         size_t count, uint8_t **pixels, char **plan)
{
    fe2_buf decoded, text;
    size_t at = 0;
    memset(&decoded, 0, sizeof decoded);
    memset(&text, 0, sizeof text);
    if (history_n && fe2_buf_add(&decoded, history, history_n)) goto fail;
    {
        const char *header = "# Original encoding hints. Changed pixels may invalidate these and trigger recompression.\n";
        if (fe2_buf_add(&text, header, strlen(header))) goto fail;
    }
    while (decoded.count - history_n < count) {
        unsigned command, copies, distance;
        if (at >= stream_n) goto fail;
        command = stream[at++];
        if (command < 128) {
            char line[32];
            if (at + (command ? command : 256) > stream_n) goto fail;
            if (!command) command = 256;
            snprintf(line, sizeof line, "literal %u\n", command);
            if (fe2_buf_add(&text, line, strlen(line)) || fe2_buf_add(&decoded, stream + at, command)) goto fail;
            at += command;
        } else {
            char line[40];
            copies = ((256u - command) + 7u) >> 3;
            if (at >= stream_n) goto fail;
            distance = ((command & 7u) << 8) | stream[at++];
            if (!copies || distance < 1 || distance > decoded.count) goto fail;
            snprintf(line, sizeof line, "copy %u %u\n", copies, distance);
            if (fe2_buf_add(&text, line, strlen(line))) goto fail;
            while (copies--) {
                uint8_t byte = decoded.data[decoded.count - distance];
                if (fe2_buf_byte(&decoded, byte)) goto fail;
            }
        }
        if (decoded.count - history_n > count) goto fail;
    }
    *used = at;
    *pixels = (uint8_t *)malloc(count ? count : 1);
    if (!*pixels) goto fail;
    if (count) memcpy(*pixels, decoded.data + history_n, count);
    *plan = (char *)text.data;
    fe2_buf_free(&decoded);
    return 0;
fail:
    fe2_buf_free(&decoded);
    fe2_buf_free(&text);
    return -1;
}

int fe2_decode_grf(const uint8_t *data, size_t count, fe2_sprite **sprites, size_t *sprite_count)
{
    size_t position = 0, n = 0, extra = 0;
    fe2_sprite *records = NULL;
    *sprites = NULL;
    *sprite_count = 0;
    while (position < count) {
        unsigned size, flags, width, height;
        int ox, oy;
        fe2_sprite *grown;
        uint8_t header[8];
        if (position + 2 > count) goto fail;
        size = data[position] | ((unsigned)data[position + 1] << 8);
        position += 2;
        if (!size) {
            if (position != count) goto fail;
            break;
        }
        if (position >= count) goto fail;
        flags = data[position];
        if (flags == 255 || size < 8 || position + 8 > count || ((flags & 2u) && position + size > count)) goto fail;
        memcpy(header, data + position, 8);
        position += 8;
        height = header[1];
        width = header[2] | ((unsigned)header[3] << 8);
        ox = (int)(int16_t)(header[4] | ((unsigned)header[5] << 8));
        oy = (int)(int16_t)(header[6] | ((unsigned)header[7] << 8));
        grown = (fe2_sprite *)realloc(records, (n + 1) * sizeof *grown);
        if (!grown) goto fail;
        records = grown;
        memset(&records[n], 0, sizeof records[n]);
        extra = 1;
        records[n].flags = flags;
        records[n].width = width;
        records[n].height = height;
        records[n].offset_x = ox;
        records[n].offset_y = oy;
        if (flags & 2u) {
            size_t packed = size - 8, used = 0;
            if (position + packed > count || decode_tokens(data + position, packed, &used, NULL, 0,
                    (size_t)width * height, &records[n].pixels, &records[n].plan) || used != packed) goto fail;
            position += packed;
        } else {
            size_t used = 0;
            if (size != 8u + width * height ||
                decode_tokens(data + position, count - position, &used, header, 8, (size_t)width * height,
                              &records[n].pixels, &records[n].plan)) goto fail;
            position += used;
        }
        ++n;
        extra = 0;
    }
    *sprites = records;
    *sprite_count = n;
    return 0;
fail:
    fe2_sprites_free(records, n + extra);
    return fe2_asset_fail("Unable to extract this Frontier install: EL2G.GRF");
}

void fe2_sprites_free(fe2_sprite *sprites, size_t count)
{
    size_t i;
    if (!sprites) return;
    for (i = 0; i < count; ++i) { free(sprites[i].pixels); free(sprites[i].plan); }
    free(sprites);
}

static int png_chunk(fe2_buf *out, const char *tag, const uint8_t *data, size_t count)
{
    uint8_t head[8];
    uint32_t crc;
    uint8_t *signed_bytes;
    head[0] = (uint8_t)(count >> 24); head[1] = (uint8_t)(count >> 16);
    head[2] = (uint8_t)(count >> 8); head[3] = (uint8_t)count;
    memcpy(head + 4, tag, 4);
    signed_bytes = (uint8_t *)malloc(4 + count);
    if (!signed_bytes) return -1;
    memcpy(signed_bytes, tag, 4);
    if (count) memcpy(signed_bytes + 4, data, count);
    crc = fe2_crc32(signed_bytes, 4 + count);
    free(signed_bytes);
    if (fe2_buf_add(out, head, 8) || fe2_buf_add(out, data, count)) return -1;
    head[0] = (uint8_t)(crc >> 24); head[1] = (uint8_t)(crc >> 16);
    head[2] = (uint8_t)(crc >> 8); head[3] = (uint8_t)crc;
    return fe2_buf_add(out, head, 4);
}

static int stored_deflate(const uint8_t *data, size_t count, fe2_buf *out)
{
    size_t offset = 0;
    uint32_t adler = fe2_adler32(data, count);
    uint8_t tail[4];
    if (fe2_buf_byte(out, 0x78) || fe2_buf_byte(out, 0x01)) return -1;
    do {
        size_t block = count - offset;
        uint8_t header[5];
        int last;
        if (block > 65535) block = 65535;
        last = offset + block == count;
        header[0] = (uint8_t)(last ? 1 : 0);
        header[1] = (uint8_t)block;
        header[2] = (uint8_t)(block >> 8);
        header[3] = (uint8_t)~header[1];
        header[4] = (uint8_t)~header[2];
        if (fe2_buf_add(out, header, 5) || fe2_buf_add(out, data + offset, block)) return -1;
        offset += block;
        if (!count) break;
    } while (offset < count);
    tail[0] = (uint8_t)(adler >> 24); tail[1] = (uint8_t)(adler >> 16);
    tail[2] = (uint8_t)(adler >> 8); tail[3] = (uint8_t)adler;
    return fe2_buf_add(out, tail, 4);
}

int fe2_write_indexed_png(const char *path, unsigned width, unsigned height, const uint8_t *pixels,
                          int transparent_zero, const uint8_t *rgb6, size_t colors)
{
    fe2_buf file, raw, idat, palette;
    uint8_t ihdr[13];
    size_t y, x;
    int status = -1;
    memset(&file, 0, sizeof file);
    memset(&raw, 0, sizeof raw);
    memset(&idat, 0, sizeof idat);
    memset(&palette, 0, sizeof palette);
    for (y = 0; y < height; ++y) {
        if (fe2_buf_byte(&raw, 0) || fe2_buf_add(&raw, pixels + y * width, width)) goto done;
    }
    if (stored_deflate(raw.data, raw.count, &idat)) goto done;
    ihdr[0] = (uint8_t)(width >> 24); ihdr[1] = (uint8_t)(width >> 16);
    ihdr[2] = (uint8_t)(width >> 8); ihdr[3] = (uint8_t)width;
    ihdr[4] = (uint8_t)(height >> 24); ihdr[5] = (uint8_t)(height >> 16);
    ihdr[6] = (uint8_t)(height >> 8); ihdr[7] = (uint8_t)height;
    ihdr[8] = 8; ihdr[9] = 3; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    for (x = 0; x < 256; ++x) {
        unsigned r, g, b;
        if (rgb6 && x < colors) {
            r = (rgb6[x * 3] * 255u + 31u) / 63u;
            g = (rgb6[x * 3 + 1] * 255u + 31u) / 63u;
            b = (rgb6[x * 3 + 2] * 255u + 31u) / 63u;
        } else r = g = b = x;
        if (fe2_buf_byte(&palette, (uint8_t)r) || fe2_buf_byte(&palette, (uint8_t)g) || fe2_buf_byte(&palette, (uint8_t)b))
            goto done;
    }
    if (fe2_buf_add(&file, "\x89PNG\r\n\x1a\n", 8) || png_chunk(&file, "IHDR", ihdr, 13) ||
        png_chunk(&file, "PLTE", palette.data, palette.count) ||
        (transparent_zero && png_chunk(&file, "tRNS", (const uint8_t *)"\0", 1)) ||
        png_chunk(&file, "IDAT", idat.data, idat.count) || png_chunk(&file, "IEND", NULL, 0) ||
        fe2_write_bytes(path, file.data, file.count)) goto done;
    status = 0;
done:
    fe2_buf_free(&file);
    fe2_buf_free(&raw);
    fe2_buf_free(&idat);
    fe2_buf_free(&palette);
    return status;
}
