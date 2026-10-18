/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "text_format.h"

typedef struct output {
    const fe2_text_resources *r;
    uint8_t *data;
    size_t at, capacity;
    unsigned depth;
} output;
static int put(output *o, unsigned c)
{
    if (o->at >= o->capacity) return -1;
    o->data[o->at++] = (uint8_t)c;
    return 0;
}
static int expand(output *o, uint16_t selector);
static int template(output *o, fe2_render_bytes bytes, size_t at)
{
    if (!bytes.data) return -1;
    while (at < bytes.count) {
        unsigned c = bytes.data[at++];
        if (!c) return 0;
        if (c == 255) {
            uint16_t selector;
            if (bytes.count - at < 2) return -1;
            selector = (uint16_t)((unsigned)bytes.data[at] * 256u + bytes.data[at + 1]);
            at += 2;
            if (expand(o, selector)) return -1;
        } else if (c == 253 || c == 254) {
            unsigned n = c == 253 ? 1 : 2;
            if (bytes.count - at < n || put(o, c == 253 ? 30 : 31)) return -1;
            while (n--) if (put(o, bytes.data[at++])) return -1;
        } else if (put(o, c)) return -1;
    }
    return -1;
}
static int directory(output *o, const fe2_text_directory *d, unsigned index)
{
    size_t at = (size_t)index * 2;
    if (!d->bytes.data || index >= d->entries || at + 2 > d->bytes.count) return -1;
    return template(o, d->bytes, (unsigned)d->bytes.data[at] + (unsigned)d->bytes.data[at + 1] * 256u);
}
static int decimal(output *o, uint32_t bits, unsigned fraction)
{
    uint8_t digits[10];
    unsigned n = 0, i;
    uint32_t magnitude = bits;
    if (bits & UINT32_C(0x80000000)) {
        if (put(o, '-')) return -1;
        magnitude = 0u - bits;
    }
    do { digits[n++] = (uint8_t)('0' + magnitude % 10u); magnitude /= 10u; } while (magnitude);
    while (n <= fraction) digits[n++] = '0';
    for (i = n; i; --i) {
        if (fraction && i == fraction && put(o, '.')) return -1;
        if (put(o, digits[i - 1])) return -1;
    }
    return 0;
}
static uint32_t sar(uint32_t n, unsigned shift)
{
    return n >> shift | (n & UINT32_C(0x80000000) ? UINT32_MAX << (32 - shift) : 0);
}
static int date(output *o, uint32_t days, int year)
{
    static const uint8_t months[12][3] = {
        {'J','a','n'},{'F','e','b'},{'M','a','r'},{'A','p','r'},
        {'M','a','y'},{'J','u','n'},{'J','u','l'},{'A','u','g'},
        {'S','e','p'},{'O','c','t'},{'N','o','v'},{'D','e','c'}};
    static const unsigned lengths[12] = {31,29,31,30,31,30,31,31,30,31,30,31};
    unsigned day = days % 1461u, y, m = 0, i;
    if (days / 1461u > 65535u) return -1;
    y = (unsigned)(days / 1461u) * 4;
    if (day >= 366) {
        day -= 366; ++y;
        while (day >= 365) { day -= 365; ++y; }
        if (day >= 59) ++day;
    }
    while (day >= lengths[m]) day -= lengths[m++];
    if (decimal(o, day + 1, 0) || put(o, '-')) return -1;
    for (i = 0; i < 3; ++i) if (put(o, months[m][i])) return -1;
    return year ? (put(o, '-') || decimal(o, y, 0) ? -1 : 0) : 0;
}
int fe2_text_wrap(uint8_t *text, size_t length, const uint8_t widths[256], unsigned width)
{
    size_t at = 0, line = 0;
    int remaining = (int)width;
    if (!text || !widths || width > 32767) return -1;
    if (!width) return 0;
    while (at < length) {
        unsigned c = text[at++], w = widths[c];
        if (!c) return 0;
        if (w & 128u) { remaining = (int)width; line = at; continue; }
        remaining -= (int)w;
        if (remaining >= 0) continue;
        if (c == 13) { remaining = (int)width; line = at; continue; }
        while (at > line && text[at - 1] != ' ') --at;
        if (at <= line) return -1;
        text[at - 1] = 13;
        line = at; remaining = (int)width;
    }
    return -1;
}
static int numeric(output *o, unsigned n)
{
    uint32_t v;
    unsigned i;
    size_t begin;
    if (!n) return 0;
    if (n <= 12) return decimal(o, o->r->value[(n - 1) % 3], (n - 1) / 3);
    if (n <= 15) return date(o, o->r->value[n - 13], 1);
    if (n <= 18) {
        unsigned seconds = fe2_math_divide_unsigned(o->r->value[n - 16], 49710u).quotient;
        unsigned hours = seconds / 3600, minutes = seconds / 60 % 60;
        if (hours > 32767 || decimal(o, hours, 0) || put(o, ':') ||
            put(o, '0' + minutes / 10) || put(o, '0' + minutes % 10) || put(o, ':') ||
            put(o, '0' + seconds % 60 / 10) || put(o, '0' + seconds % 10)) return -1;
        return 0;
    }
    if (n <= 21) {
        v = o->r->value[n - 19];
        for (i = 0; i < 8; ++i) {
            unsigned digit = v >> 28;
            if (put(o, digit + (digit < 10 ? 48 : 129))) return -1;
            v <<= 4;
        }
        return 0;
    }
    if (n <= 23) return template(o, o->r->pointer[n - 22], 0);
    if (n <= 26) return expand(o, (uint16_t)o->r->value[n - 24]);
    if (n <= 28) return expand(o, (uint16_t)o->r->pointer_value[n - 27]);
    if (n <= 30) return decimal(o, o->r->pointer_value[n - 29], 0);
    if (n <= 33) {
        v = o->r->value[n - 31];
        return v ? (decimal(o, v, 0) || put(o, 't') ? -1 : 0) : put(o, '-');
    }
    if (n == 34) {
        begin = o->at;
        if (template(o, o->r->pointer[0], 0) || put(o, 0)) return -1;
        if (fe2_text_wrap(o->data + begin, o->at - begin, o->r->widths, 40)) return -1;
        --o->at; return 0;
    }
    if (n <= 37) {
        v = o->r->value[n - 35];
        if (!(v & UINT32_C(0x80000000)) && v > 0x4705du) {
            v = sar(v, 7);
            if (decimal(o, v + sar(v, 2) - sar(v, 5), 2)) return -1;
            i = '|';
        } else {
            if (decimal(o, v * 3u + sar(v, 1) - sar(v, 4), 1)) return -1;
            i = '{';
        }
        return put(o, ' ') || put(o, 'k') || put(o, 'm') || put(o, i) ? -1 : 0;
    }
    if (n == 38) return date(o, o->r->day_month, 0);
    if (n <= 40) return date(o, o->r->value[n - 38], 0);
    if (n == 41) return date(o, o->r->current_day, 1);
    return -1;
}
static int expand(output *o, uint16_t selector)
{
    int result;
    if (++o->depth > 32) { --o->depth; return -1; }
    if (selector < 0x3000) result = directory(o, &o->r->builtin, selector);
    else if (selector < 0x4000) result = numeric(o, selector - 0x3000u);
    else if (selector < 0x8000) {
        /* Registered module texts (intro / overlay bank) take 0x4000..0x7fff;
         * otherwise the main external directory. Index low 14 bits; module
         * banks are small (fe2-intro masks 0x7f). */
        if (o->r->module.entries)
            result = directory(o, &o->r->module, selector & 0x3fffu);
        else
            result = directory(o, &o->r->external, selector & 0x3fffu);
    } else {
        unsigned slot = (selector >> 9) & 63u;
        const fe2_text_directory *dir = &o->r->overlay[slot];
        uint16_t mask = o->r->overlay_mask[slot];
        /* The market price list is one selector and runs to several hundred bytes. */
        char text[1024];
        if (o->r->overlay_text &&
            !o->r->overlay_text(o->r->overlay_context, o->r, slot, (uint16_t)(selector << 1), text, sizeof text)) {
            const char *c;
            result = 0;
            for (c = text; *c && !result; ++c) result = put(o, (uint8_t)*c);
            --o->depth;
            return result;
        }
        /* Station interaction strings (98xxh) are the interaction catalog. Overlay
         * 12 has no text bank of its own. */
        if (!dir->entries && slot == 12 && o->r->overlay[18].entries) {
            dir = &o->r->overlay[18];
            mask = o->r->overlay_mask[18];
        }
        result = directory(o, dir, selector & mask);
    }
    --o->depth;
    return result;
}
int fe2_text_expand(const fe2_text_resources *resources, uint16_t selector,
    uint8_t *data, size_t capacity, size_t *length)
{
    output o;
    if (!resources || !data || !capacity) return -1;
    o.r = resources; o.data = data; o.at = 0; o.capacity = capacity; o.depth = 0;
    if (expand(&o, selector) || put(&o, 0)) return -1;
    if (length) *length = o.at;
    return 0;
}
