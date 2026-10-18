/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "inflate.h"

#include <stdlib.h>
#include <string.h>

typedef struct bit_reader {
    const uint8_t *data;
    size_t count, at;
    unsigned bits, hold;
} bit_reader;

static int pull(bit_reader *reader, unsigned width, unsigned *out)
{
    while (reader->bits < width) {
        if (reader->at >= reader->count) return -1;
        reader->hold |= (unsigned)reader->data[reader->at++] << reader->bits;
        reader->bits += 8;
    }
    *out = reader->hold & ((1u << width) - 1u);
    reader->hold >>= width;
    reader->bits -= width;
    return 0;
}

static int stored(bit_reader *reader, uint8_t **out, size_t *count, size_t *cap)
{
    unsigned length, complement;
    reader->bits = 0;
    reader->hold = 0;
    if (reader->at + 4 > reader->count) return -1;
    length = reader->data[reader->at] | ((unsigned)reader->data[reader->at + 1] << 8);
    complement = reader->data[reader->at + 2] | ((unsigned)reader->data[reader->at + 3] << 8);
    reader->at += 4;
    if ((length ^ 0xffffu) != complement || reader->at + length > reader->count) return -1;
    if (*count + length > *cap) {
        size_t next = *cap ? *cap : 64;
        uint8_t *grown;
        while (next < *count + length) next *= 2;
        grown = realloc(*out, next);
        if (!grown) return -1;
        *out = grown;
        *cap = next;
    }
    memcpy(*out + *count, reader->data + reader->at, length);
    *count += length;
    reader->at += length;
    return 0;
}

typedef struct huff_symbol { unsigned code, length, symbol; } huff_symbol;

static unsigned build_huff(const unsigned *lengths, unsigned count, huff_symbol *table)
{
    unsigned bl_count[16], next_code[16], code = 0, len, i, n = 0;
    memset(bl_count, 0, sizeof bl_count);
    for (i = 0; i < count; ++i)
        if (lengths[i]) ++bl_count[lengths[i]];
    for (len = 1; len <= 15; ++len) {
        code = (code + bl_count[len - 1]) << 1;
        next_code[len] = code;
    }
    for (i = 0; i < count; ++i) {
        if (!lengths[i]) continue;
        table[n].code = next_code[lengths[i]]++;
        table[n].length = lengths[i];
        table[n].symbol = i;
        ++n;
    }
    return n;
}

static int decode_symbol(bit_reader *reader, const huff_symbol *table, unsigned count, unsigned *symbol)
{
    unsigned code = 0, length, i;
    for (length = 1; length <= 15; ++length) {
        unsigned bit;
        if (pull(reader, 1, &bit)) return -1;
        code = (code << 1) | bit;
        for (i = 0; i < count; ++i)
            if (table[i].length == length && table[i].code == code) {
                *symbol = table[i].symbol;
                return 0;
            }
    }
    return -1;
}

static int inflate_codes(bit_reader *reader, const unsigned *lit_lengths, unsigned lit_count,
                         const unsigned *dist_lengths, unsigned dist_count, uint8_t **out, size_t *count, size_t *cap)
{
    huff_symbol lit_table[288], dist_table[32];
    unsigned lit_n = build_huff(lit_lengths, lit_count, lit_table);
    unsigned dist_n = build_huff(dist_lengths, dist_count, dist_table);
    static const unsigned len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const unsigned len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const unsigned dist_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const unsigned dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        unsigned symbol, length, distance, extra, i;
        if (decode_symbol(reader, lit_table, lit_n, &symbol)) return -1;
        if (symbol < 256) {
            if (*count + 1 > *cap) {
                size_t next = *cap ? *cap * 2 : 64;
                uint8_t *grown = realloc(*out, next);
                if (!grown) return -1;
                *out = grown;
                *cap = next;
            }
            (*out)[(*count)++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 256) return 0;
        if (symbol > 285) return -1;
        symbol -= 257;
        if (pull(reader, len_extra[symbol], &extra)) return -1;
        length = len_base[symbol] + extra;
        if (decode_symbol(reader, dist_table, dist_n, &symbol) || symbol >= 30) return -1;
        if (pull(reader, dist_extra[symbol], &extra)) return -1;
        distance = dist_base[symbol] + extra;
        if (!distance || distance > *count) return -1;
        if (*count + length > *cap) {
            size_t next = *cap ? *cap : 64;
            uint8_t *grown;
            while (next < *count + length) next *= 2;
            grown = realloc(*out, next);
            if (!grown) return -1;
            *out = grown;
            *cap = next;
        }
        for (i = 0; i < length; ++i) (*out)[*count + i] = (*out)[*count - distance + i];
        *count += length;
    }
}

static void fixed_lengths(unsigned *lit, unsigned *dist)
{
    unsigned i;
    for (i = 0; i < 144; ++i) lit[i] = 8;
    for (i = 144; i < 256; ++i) lit[i] = 9;
    for (i = 256; i < 280; ++i) lit[i] = 7;
    for (i = 280; i < 288; ++i) lit[i] = 8;
    for (i = 0; i < 32; ++i) dist[i] = 5;
}

static int dynamic(bit_reader *reader, unsigned *lit, unsigned *dist, unsigned *lit_count, unsigned *dist_count)
{
    static const unsigned order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    unsigned hlit, hdist, hclen, i;
    unsigned code_lengths[19], lengths[320];
    huff_symbol code_table[19];
    unsigned code_n;
    if (pull(reader, 5, &hlit) || pull(reader, 5, &hdist) || pull(reader, 4, &hclen)) return -1;
    hlit += 257;
    hdist += 1;
    hclen += 4;
    if (hlit > 286 || hdist > 32) return -1;
    memset(code_lengths, 0, sizeof code_lengths);
    for (i = 0; i < hclen; ++i)
        if (pull(reader, 3, &code_lengths[order[i]])) return -1;
    memset(lengths, 0, sizeof lengths);
    code_n = build_huff(code_lengths, 19, code_table);
    i = 0;
    while (i < hlit + hdist) {
        unsigned symbol, repeat, value;
        if (decode_symbol(reader, code_table, code_n, &symbol)) return -1;
        if (symbol < 16) {
            lengths[i++] = symbol;
            continue;
        }
        if (symbol == 16) {
            if (!i || pull(reader, 2, &repeat)) return -1;
            value = lengths[i - 1];
            repeat += 3;
        } else if (symbol == 17) {
            if (pull(reader, 3, &repeat)) return -1;
            value = 0;
            repeat += 3;
        } else if (symbol == 18) {
            if (pull(reader, 7, &repeat)) return -1;
            value = 0;
            repeat += 11;
        } else return -1;
        if (i + repeat > hlit + hdist) return -1;
        while (repeat--) lengths[i++] = value;
    }
    memcpy(lit, lengths, hlit * sizeof *lit);
    memcpy(dist, lengths + hlit, hdist * sizeof *dist);
    *lit_count = hlit;
    *dist_count = hdist;
    return 0;
}

int fe2_inflate(const uint8_t *data, size_t count, uint8_t **out, size_t *out_count)
{
    bit_reader reader;
    uint8_t *decoded = NULL;
    size_t used = 0, cap = 0;
    unsigned final = 0;
    if (!data || !out || !out_count) return -1;
    memset(&reader, 0, sizeof reader);
    reader.data = data;
    reader.count = count;
    do {
        unsigned type;
        unsigned lit[288], dist[32], lit_count, dist_count;
        if (pull(&reader, 1, &final) || pull(&reader, 2, &type)) { free(decoded); return -1; }
        if (type == 0) {
            if (stored(&reader, &decoded, &used, &cap)) { free(decoded); return -1; }
        } else if (type == 1 || type == 2) {
            if (type == 1) {
                fixed_lengths(lit, dist);
                lit_count = 288;
                dist_count = 32;
            } else if (dynamic(&reader, lit, dist, &lit_count, &dist_count)) {
                free(decoded);
                return -1;
            }
            if (inflate_codes(&reader, lit, lit_count, dist, dist_count, &decoded, &used, &cap)) {
                free(decoded);
                return -1;
            }
        } else {
            free(decoded);
            return -1;
        }
    } while (!final);
    *out = decoded ? decoded : calloc(1, 1);
    *out_count = used;
    return *out ? 0 : -1;
}

uint32_t fe2_adler32(const uint8_t *data, size_t count)
{
    uint32_t a = 1, b = 0;
    size_t i;
    for (i = 0; i < count; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

int fe2_inflate_zlib(const uint8_t *data, size_t count, uint8_t **out, size_t *out_count)
{
    uint32_t have, expect;
    if (count < 6) return -1;
    if (fe2_inflate(data + 2, count - 6, out, out_count)) return -1;
    have = fe2_adler32(*out, *out_count);
    expect = ((uint32_t)data[count - 4] << 24) | ((uint32_t)data[count - 3] << 16) |
             ((uint32_t)data[count - 2] << 8) | data[count - 1];
    if (have != expect) { free(*out); *out = NULL; return -1; }
    return 0;
}
