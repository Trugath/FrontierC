/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the commander file codec. */
#include <string.h>

#include "commander_internal.h"

enum { MARKER = 0x0011, OVERLAY_RECORD_BYTES = 8, KEY_LOW = 0x5678, KEY_HIGH = 0x1234 };

/*
 * Zero runs: nonzero bytes copy; a zero is followed by the count of further
 * zeros. A run reaching 256 zeros writes 0xff and starts again with that
 * zero as a literal. A final zero has no count.
 */
static const uint8_t *encode_zero_runs(const uint8_t *from, uint8_t **to, uint16_t count)
{
    uint8_t *out = *to, byte, run = 0;
    int in_run = 0;
    for (;;) {
        if (!in_run) {
            byte = *from++;
            *out++ = byte;
            if (--count == 0) break;
            if (byte != 0) continue;
            in_run = 1;
            run = 0;
            continue;
        }
        byte = *from++;
        if (byte == 0) {
            if (++run != 0) {
                if (--count == 0) { *out++ = run; break; }
                continue;
            }
            run = 0xff;
            if (--count == 0) { *out++ = run; break; }
        }
        *out++ = run;
        *out++ = byte;
        if (byte == 0) { run = 0; continue; }
        in_run = 0;
        if (--count == 0) break;
    }
    *to = out;
    return from;
}

/* A zero's run of further zeros is not written when it exactly ends the
 * count. A stream that runs past its region or the file is damaged (the
 * original decodes on, across memory); it returns null. */
static const uint8_t *decode_zero_runs(const uint8_t *from, const uint8_t *end, uint8_t *to, uint16_t count)
{
    for (;;) {
        uint8_t byte, run;
        if (from >= end) return 0;
        byte = *from++;
        *to++ = byte;
        if (byte == 0) {
            if (from >= end) return 0;
            run = *from++;
            if (run > count) return 0;
            count = (uint16_t)(count - run);
            if (count == 0) break;
            memset(to, 0, run);
            to += run;
        }
        if (--count == 0) break;
    }
    return from;
}

static uint16_t get16(const uint8_t *at) { return (uint16_t)(at[0] | at[1] << 8); }
static void put16(uint8_t *at, uint16_t value) { at[0] = (uint8_t)value; at[1] = (uint8_t)(value >> 8); }

/* The key starts at 0x12345678 + the buffer limit; each plaintext word
 * (sign-extended) is added and the key rotated left one bit. Encoding XORs
 * each word with the key's low word before the update; decoding first. */
static uint32_t transform(uint8_t *words, uint16_t bytes, uint16_t buffer_limit, int encode)
{
    uint32_t key = ((uint32_t)KEY_HIGH << 16 | KEY_LOW) + buffer_limit;
    uint16_t count = (uint16_t)(bytes >> 1);
    do {
        uint16_t plain = get16(words);
        if (!encode) plain = (uint16_t)(plain ^ (uint16_t)key);
        put16(words, (uint16_t)(get16(words) ^ (uint16_t)key));
        words += 2;
        key += (uint32_t)(int32_t)(int16_t)plain;
        key = key << 1 | key >> 31;
    } while (--count != 0);
    return key;
}

uint16_t fe2_commander_encode(const fe2_save_image *image, uint16_t buffer_limit, uint8_t *file)
{
    uint8_t *out = file + 2;
    uint16_t length;
    uint32_t key;
    unsigned i;
    put16(file, MARKER);
    encode_zero_runs(image->objects, &out, FE2_SAVE_OBJECT_BYTES);
    for (i = 0; i < FE2_SAVE_OVERLAY_RECORDS; ++i, out += 4)
        memcpy(out, image->overlay_records + OVERLAY_RECORD_BYTES * i + 4, 4);
    memcpy(out, image->table, 4 * FE2_SAVE_TABLE_DWORDS);
    out += 4 * FE2_SAVE_TABLE_DWORDS;
    encode_zero_runs(image->resident, &out, FE2_SAVE_RESIDENT_END - FE2_SAVE_RESIDENT_START);
    encode_zero_runs(image->stack, &out, FE2_SAVE_STACK_END - FE2_SAVE_STACK_START);
    length = (uint16_t)(out - (file + 2));
    /* An odd length is padded; the original leaves the pad byte as it was
     * (stack memory below the caller's frame). */
    if (length & 1) file[2 + length++] = 0;
    key = transform(file + 2, length, buffer_limit, 1);
    put16(file + 2 + length, (uint16_t)key);
    put16(file + 4 + length, (uint16_t)(key >> 16));
    return (uint16_t)(length + 6);
}

/* A failed checksum leaves the payload decoded in the buffer. A damaged
 * stream that passes the (weak) checksum is rejected part way. */
int fe2_commander_decode(fe2_save_image *image, uint16_t buffer_limit, uint8_t *file, uint16_t length)
{
    uint16_t payload = (uint16_t)(length - 6);
    const uint8_t *in = file + 2, *end = file + 2 + payload;
    uint32_t key;
    unsigned i;
    if (get16(file) != MARKER) return 0;
    key = transform(file + 2, payload, buffer_limit, 0);
    if (key != ((uint32_t)get16(file + 4 + payload) << 16 | get16(file + 2 + payload))) return 0;
    in = decode_zero_runs(in, end, image->objects, FE2_SAVE_OBJECT_BYTES);
    if (!in || end - in < 8 * FE2_SAVE_OVERLAY_RECORDS) return 0;
    for (i = 0; i < FE2_SAVE_OVERLAY_RECORDS; ++i, in += 4)
        memcpy(image->overlay_records + OVERLAY_RECORD_BYTES * i + 4, in, 4);
    memcpy(image->table, in, 4 * FE2_SAVE_TABLE_DWORDS);
    in += 4 * FE2_SAVE_TABLE_DWORDS;
    in = decode_zero_runs(in, end, image->resident, FE2_SAVE_RESIDENT_END - FE2_SAVE_RESIDENT_START);
    if (!in || !decode_zero_runs(in, end, image->stack, FE2_SAVE_STACK_END - FE2_SAVE_STACK_START)) return 0;
    return 1;
}
