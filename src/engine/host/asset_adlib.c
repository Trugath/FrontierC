/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "asset_adlib.h"

#include "asset_files.h"
#include "asset_ini.h"
#include "asset_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    ADLIB_ROWS = 16384
};

typedef struct adlib_row {
    unsigned offset;
    char kind[12];
    char operands[96];
    char description[80];
} adlib_row;

static unsigned ru16(const uint8_t *image, unsigned addr)
{
    return image[addr] | ((unsigned)image[addr + 1] << 8);
}

static int width_of(unsigned opcode)
{
    switch (opcode) {
    case 0x91: return 4;
    case 0x95: return 5;
    case 0x81: case 0x84: case 0x85: case 0xac: case 0xad: case 0xb5: return 3;
    case 0x86: case 0x88: case 0x92: case 0xbb: return 1;
    case 0x80: case 0x82: case 0x83: case 0x87: case 0x89: case 0x90:
    case 0x93: case 0x9e: case 0xa0: case 0xa9: case 0xab: case 0xb3:
        return 2;
    default: return 0;
    }
}

static const char *symbol_of(unsigned opcode)
{
    switch (opcode) {
    case 0x80: return "ADLIB_LOOP_COUNT_80";
    case 0x81: return "ADLIB_LOOP_END_81";
    case 0x82: return "ADLIB_START_EVENT_82";
    case 0x83: return "ADLIB_RELEASE_THRESHOLD_83";
    case 0x84: return "ADLIB_JUMP_84";
    case 0x85: return "ADLIB_CALL_85";
    case 0x86: return "ADLIB_RETURN_86";
    case 0x87: return "ADLIB_OCTAVE_BIAS_87";
    case 0x88: return "ADLIB_STOP_88";
    case 0x89: return "ADLIB_REST_89";
    case 0x90: return "ADLIB_INSTRUMENT_90";
    case 0x91: return "ADLIB_PITCH_SLIDE_91";
    case 0x92: return "ADLIB_DISABLE_SLIDE_92";
    case 0x93: return "ADLIB_FREQUENCY_BIAS_93";
    case 0x95: return "ADLIB_PITCH_MODULATION_95";
    case 0x9e: return "ADLIB_LEVEL_9E";
    case 0xa0: return "ADLIB_DELAY_A0";
    case 0xa9: return "ADLIB_CHANNEL_TEMPO_A9";
    case 0xab: return "ADLIB_LEVEL_BIAS_AB";
    case 0xac: return "ADLIB_SET_CHANNEL_LEVEL_AC";
    case 0xad: return "ADLIB_ADD_CHANNEL_LEVEL_AD";
    case 0xb3: return "ADLIB_CLEAR_CHANNEL_B3";
    case 0xb5: return "ADLIB_RANDOM_FREQUENCY_OFFSET_B5";
    case 0xbb: return "ADLIB_NOP_BB";
    default: return NULL;
    }
}

static const char *short_of(unsigned opcode)
{
    switch (opcode) {
    case 0x80: return "loop_count";
    case 0x81: return "loop_end";
    case 0x82: return "start_event";
    case 0x83: return "channel_clock_divider";
    case 0x84: return "jump";
    case 0x85: return "call";
    case 0x86: return "return";
    case 0x87: return "octave_bias";
    case 0x88: return "stop";
    case 0x89: return "rest";
    case 0x90: return "instrument";
    case 0x91: return "pitch_slide";
    case 0x92: return "disable_slide";
    case 0x93: return "frequency_bias";
    case 0x95: return "pitch_modulation";
    case 0x9e: return "level";
    case 0xa0: return "delay";
    case 0xa9: return "channel_tempo";
    case 0xab: return "level_bias";
    case 0xac: return "set_channel_level";
    case 0xad: return "add_channel_level";
    case 0xb3: return "clear_channel";
    case 0xb5: return "random_frequency_offset";
    case 0xbb: return "nop";
    default: return NULL;
    }
}

static int is_branch(unsigned opcode)
{
    return opcode == 0x81 || opcode == 0x84 || opcode == 0x85;
}

static int no_fall(unsigned opcode)
{
    return opcode == 0x84 || opcode == 0x86 || opcode == 0x88;
}

static int add_row(adlib_row *rows, size_t *count, unsigned offset, const char *kind,
                   const char *operands, const char *description)
{
    adlib_row *row;
    if (*count >= ADLIB_ROWS) return -1;
    row = &rows[(*count)++];
    row->offset = offset;
    if (snprintf(row->kind, sizeof row->kind, "%s", kind) >= (int)sizeof row->kind ||
        snprintf(row->operands, sizeof row->operands, "%s", operands) >= (int)sizeof row->operands ||
        snprintf(row->description, sizeof row->description, "%s", description) >= (int)sizeof row->description)
        return -1;
    return 0;
}

static void target_name(char *out, size_t cap, unsigned target, unsigned event_at, const uint8_t *header,
                        unsigned ambient_pitch, unsigned ambient_level)
{
    if (target == event_at) snprintf(out, cap, "event_record_pointers");
    else if (header[target]) snprintf(out, cap, "event_record_%04x", target);
    else if (target == ambient_pitch) snprintf(out, cap, "ambient_pitch_parameter");
    else if (target == ambient_level) snprintf(out, cap, "ambient_level_parameter");
    else snprintf(out, cap, "stream_%04x", target);
}

int fe2_extract_adlib(const uint8_t *image, size_t image_n, const char *data_root)
{
    char path[4096];
    fe2_ini ini;
    fe2_ini_section *bank, *pitch, *ambient;
    unsigned origin, end, table, curves, steps, ambient_pitch, ambient_level, event_at, instrument_at;
    unsigned instrument_end, instrument_count, event_end, addr, min_patch, min_event;
    unsigned *patches = NULL, *events = NULL, *pending = NULL;
    uint8_t *occupied = NULL, *covered = NULL, *starts = NULL, *headers = NULL, *branches = NULL;
    adlib_row *rows = NULL, **content = NULL;
    char **label = NULL, **label_desc = NULL;
    uint8_t *label_owned = NULL;
    uint8_t *claimed = NULL;
    size_t patch_n = 0, event_n = 0, pending_n = 0, row_n = 0, i;
    int status = -1;
    fe2_buf patches_out, records_out, curves_out;
    memset(&patches_out, 0, sizeof patches_out);
    memset(&records_out, 0, sizeof records_out);
    memset(&curves_out, 0, sizeof curves_out);
    if (snprintf(path, sizeof path, "%s/adlib/bank.ini", data_root) >= (int)sizeof path) return -1;
    if (fe2_ini_load(&ini, path)) return fe2_asset_fail("This project is missing adlib/bank.ini");
    bank = fe2_ini_section_find(&ini, "bank");
    pitch = fe2_ini_section_find(&ini, "pitch");
    ambient = fe2_ini_section_find(&ini, "ambient");
    if (!bank || !pitch || !ambient || !fe2_ini_get(bank, "origin") || !fe2_ini_get(bank, "end") ||
        !fe2_ini_get(pitch, "table") || !fe2_ini_get(pitch, "curves") || !fe2_ini_get(pitch, "steps") ||
        !fe2_ini_get(ambient, "pitch") || !fe2_ini_get(ambient, "level")) {
        fe2_asset_fail("This project is missing adlib/bank.ini");
        goto done;
    }
    origin = (unsigned)strtoul(fe2_ini_get(bank, "origin"), NULL, 0);
    end = (unsigned)strtoul(fe2_ini_get(bank, "end"), NULL, 0);
    table = (unsigned)strtoul(fe2_ini_get(pitch, "table"), NULL, 0);
    curves = (unsigned)strtoul(fe2_ini_get(pitch, "curves"), NULL, 0);
    steps = (unsigned)strtoul(fe2_ini_get(pitch, "steps"), NULL, 0);
    ambient_pitch = (unsigned)strtoul(fe2_ini_get(ambient, "pitch"), NULL, 0);
    ambient_level = (unsigned)strtoul(fe2_ini_get(ambient, "level"), NULL, 0);
    if (!(origin < end && end <= image_n && curves == 14 && steps == 32)) {
        fe2_asset_fail("Unable to extract this Frontier install: EL2ADLIB.OVL");
        goto done;
    }
    occupied = (uint8_t *)calloc(end, 1);
    covered = (uint8_t *)calloc(end, 1);
    starts = (uint8_t *)calloc(end, 1);
    headers = (uint8_t *)calloc(end, 1);
    branches = (uint8_t *)calloc(end, 1);
    claimed = (uint8_t *)calloc(end, 1);
    patches = (unsigned *)calloc(256, sizeof *patches);
    events = (unsigned *)calloc(256, sizeof *events);
    pending = (unsigned *)calloc(8192, sizeof *pending);
    rows = (adlib_row *)calloc(ADLIB_ROWS, sizeof *rows);
    content = (adlib_row **)calloc(end, sizeof *content);
    label = (char **)calloc(end, sizeof *label);
    label_desc = (char **)calloc(end, sizeof *label_desc);
    label_owned = (uint8_t *)calloc(end, 1);
    if (!occupied || !covered || !starts || !headers || !branches || !claimed || !patches || !events ||
        !pending || !rows || !content || !label || !label_desc || !label_owned) goto done;
    event_at = ru16(image, origin);
    if (!(origin < event_at && event_at < end)) goto fail_bank;
    instrument_at = origin + 2;
    min_patch = end;
    addr = instrument_at;
    while (addr + 1 < end) {
        unsigned value = ru16(image, addr);
        addr += 2;
        if (origin <= value && value < end && value < min_patch) min_patch = value;
        if (min_patch != end && addr >= min_patch) break;
    }
    instrument_end = addr;
    if (min_patch == end || ((instrument_end - instrument_at) & 1u)) goto fail_bank;
    instrument_count = (instrument_end - instrument_at) / 2u;
    for (i = 0; i < instrument_count; ++i) {
        unsigned value = ru16(image, instrument_at + (unsigned)i * 2u);
        size_t p;
        if (!(origin <= value && value < end)) continue;
        for (p = 0; p < patch_n; ++p) if (patches[p] == value) break;
        if (p == patch_n) {
            if (patch_n == 256) goto fail_bank;
            patches[patch_n++] = value;
        }
    }
    for (i = 1; i < patch_n; ++i) {
        unsigned key = patches[i];
        size_t j = i;
        while (j && patches[j - 1] > key) {
            patches[j] = patches[j - 1];
            --j;
        }
        patches[j] = key;
    }
    if (!patch_n || patches[0] != min_patch) goto fail_bank;
    for (i = 0; i + 1 < patch_n; ++i) if (patches[i + 1] - patches[i] < 11) goto fail_bank;
    if (patches[patch_n - 1] + 11 > event_at) goto fail_bank;
    min_event = end;
    addr = event_at;
    while (addr + 1 < end && event_n < 256) {
        unsigned value = ru16(image, addr);
        events[event_n++] = value;
        addr += 2;
        if (origin <= value && value < end && value < min_event) min_event = value;
        if (min_event != end && addr >= min_event) break;
    }
    event_end = addr;
    if (!event_n || event_end != min_event) goto fail_bank;
    for (i = 0; i < event_n; ++i) if (origin <= events[i] && events[i] < end) headers[events[i]] = 1;
    for (addr = origin; addr < instrument_end; ++addr) occupied[addr] = 1;
    for (addr = event_at; addr < event_end; ++addr) occupied[addr] = 1;
    for (i = 0; i < patch_n; ++i) for (addr = patches[i]; addr < patches[i] + 11; ++addr) occupied[addr] = 1;
    for (addr = 0; addr + 1 < end; ++addr) if (headers[addr]) { occupied[addr] = 1; occupied[addr + 1] = 1; }
    for (i = 0; i < event_n; ++i) if (headers[events[i]]) pending[pending_n++] = events[i] + 2;
    while (pending_n) {
        addr = pending[--pending_n];
        while (addr < end && !starts[addr]) {
            unsigned op, width, target;
            char operands[96], name[64];
            const char *symbol, *brief;
            if (occupied[addr] || covered[addr]) {
                if (covered[addr] && !starts[addr]) goto fail_bank;
                break;
            }
            op = image[addr];
            if (op < 0x80) {
                if (addr + 1 >= end || occupied[addr + 1]) goto fail_bank;
                snprintf(operands, sizeof operands, "0x%02x, 0x%02x", op, image[addr + 1]);
                snprintf(name, sizeof name, "note, image+0x%04x", addr);
                if (add_row(rows, &row_n, addr, "note", operands, name)) goto done;
                starts[addr] = 1;
                covered[addr] = covered[addr + 1] = 1;
                addr += 2;
                continue;
            }
            if (addr + 1 < end && (addr + 1 == ambient_pitch || addr + 1 == ambient_level)) {
                if (op != 0xab && op != 0x93) goto fail_bank;
                snprintf(operands, sizeof operands, "0x%02x", op);
                if (add_row(rows, &row_n, addr, "byte", operands, "Ambient stream command opcode")) goto done;
                snprintf(operands, sizeof operands, "0x%02x", image[addr + 1]);
                if (add_row(rows, &row_n, addr + 1, "byte", operands, "Mutable ambient command operand")) goto done;
                starts[addr] = 1;
                covered[addr] = covered[addr + 1] = 1;
                addr += 2;
                continue;
            }
            symbol = symbol_of(op);
            brief = short_of(op);
            width = (unsigned)width_of(op);
            if (!symbol || !brief || !width) goto fail_bank;
            if (is_branch(op)) {
                if (addr + 2 >= end) goto fail_bank;
                target = ru16(image, addr + 1);
                snprintf(name, sizeof name, "%s, image+0x%04x", brief, addr);
                if (add_row(rows, &row_n, addr, "byte", symbol, name)) goto done;
                if (target < origin || target >= end) {
                    snprintf(operands, sizeof operands, "0x%04x", target);
                    if (add_row(rows, &row_n, addr + 1, "word", operands, "Unclassified bank data")) goto done;
                    starts[addr] = 1;
                    covered[addr] = covered[addr + 1] = covered[addr + 2] = 1;
                    break;
                }
                target_name(operands, sizeof operands, target, event_at, headers, ambient_pitch, ambient_level);
                if (add_row(rows, &row_n, addr + 1, "pointer", operands, "")) goto done;
                starts[addr] = 1;
                covered[addr] = covered[addr + 1] = covered[addr + 2] = 1;
                branches[target] = 1;
                if (!headers[target] && target != event_at) {
                    if (pending_n >= 8192) goto fail_bank;
                    pending[pending_n++] = target;
                }
                if (op == 0x81 && addr + 3 < end) branches[addr + 3] = 1;
                if (no_fall(op)) break;
                addr += 3;
                continue;
            }
            if (addr + width > end) goto fail_bank;
            for (i = 0; i < width; ++i) if (occupied[addr + (unsigned)i]) goto fail_bank;
            operands[0] = 0;
            snprintf(operands, sizeof operands, "%s", symbol);
            for (i = 1; i < width; ++i) {
                char piece[8];
                snprintf(piece, sizeof piece, ", 0x%02x", image[addr + (unsigned)i]);
                if (strlen(operands) + strlen(piece) >= sizeof operands) goto done;
                strcat(operands, piece);
            }
            snprintf(name, sizeof name, "%s, image+0x%04x", brief, addr);
            if (add_row(rows, &row_n, addr, "command", operands, name)) goto done;
            starts[addr] = 1;
            for (i = 0; i < width; ++i) covered[addr + (unsigned)i] = 1;
            if (no_fall(op)) break;
            addr += width;
        }
    }
    if (add_row(rows, &row_n, origin, "pointer", "event_record_pointers", "image+1871, sequencer bank address")) goto done;
    for (i = 0; i < instrument_count; ++i) {
        unsigned at = instrument_at + (unsigned)i * 2u;
        unsigned value = ru16(image, at);
        char operands[64], desc[64];
        if (origin <= value && value < end) {
            snprintf(operands, sizeof operands, "opl_patch_%04x", value);
            snprintf(desc, sizeof desc, "instrument %u", (unsigned)i);
            if (add_row(rows, &row_n, at, "pointer", operands, desc)) goto done;
        } else {
            snprintf(operands, sizeof operands, "0x%04x", value);
            snprintf(desc, sizeof desc, "instrument %u (unclassified address)", (unsigned)i);
            if (add_row(rows, &row_n, at, "word", operands, desc)) goto done;
        }
    }
    for (i = 0; i < patch_n; ++i) {
        char operands[32];
        snprintf(operands, sizeof operands, "opl_patch_%04x", patches[i]);
        if (add_row(rows, &row_n, patches[i], "patch", operands, "")) goto done;
    }
    for (i = 0; i < event_n; ++i) {
        unsigned at = event_at + (unsigned)i * 2u;
        char operands[64], desc[32];
        snprintf(desc, sizeof desc, "event %u", (unsigned)i);
        if (origin <= events[i] && events[i] < end) {
            target_name(operands, sizeof operands, events[i], event_at, headers, ambient_pitch, ambient_level);
            if (add_row(rows, &row_n, at, "pointer", operands, desc)) goto done;
        } else {
            snprintf(operands, sizeof operands, "0x%04x", events[i]);
            if (add_row(rows, &row_n, at, "word", operands, desc)) goto done;
        }
    }
    for (addr = 0; addr < end; ++addr) if (headers[addr]) {
        char operands[32];
        int priority = image[addr + 1] < 128 ? image[addr + 1] : (int)image[addr + 1] - 256;
        snprintf(operands, sizeof operands, "%u, %d", image[addr], priority);
        if (add_row(rows, &row_n, addr, "event", operands, "channel, signed priority byte")) goto done;
    }
    for (i = 0; i < row_n; ++i) {
        if (content[rows[i].offset]) goto fail_bank;
        content[rows[i].offset] = &rows[i];
    }
    for (addr = origin; addr < end; ++addr) if (content[addr]) {
        unsigned width = 1;
        const char *kind = content[addr]->kind;
        if (!strcmp(kind, "patch")) width = 11;
        else if (!strcmp(kind, "pointer") || !strcmp(kind, "word") || !strcmp(kind, "event") || !strcmp(kind, "note")) width = 2;
        if (!strcmp(kind, "command")) {
            unsigned commas = 0;
            const char *cursor = content[addr]->operands;
            while ((cursor = strchr(cursor, ',')) != NULL) { ++commas; ++cursor; }
            width = 1 + commas;
        }
        if (addr + width > end) goto fail_bank;
        for (i = 0; i < width; ++i) {
            if (claimed[addr + (unsigned)i]) goto fail_bank;
            claimed[addr + (unsigned)i] = 1;
        }
    }
    {
        unsigned raw_start = 0;
        int open = 0;
        for (addr = origin; addr < end; ++addr) {
            if (claimed[addr]) { open = 0; continue; }
            if (!open) { raw_start = addr; open = 1; }
            if (addr + 1 == end || claimed[addr + 1]) {
                unsigned chunk;
                for (chunk = raw_start; chunk <= addr; chunk += 12) {
                    unsigned stop = chunk + 12;
                    char operands[96];
                    unsigned n = 0;
                    if (stop > addr + 1) stop = addr + 1;
                    operands[0] = 0;
                    for (i = chunk; i < stop; ++i) {
                        char piece[8];
                        snprintf(piece, sizeof piece, "%s0x%02x", i == chunk ? "" : ", ", image[i]);
                        n += (unsigned)strlen(piece);
                        if (n >= sizeof operands) goto done;
                        strcat(operands, piece);
                    }
                    if (add_row(rows, &row_n, chunk, "raw", operands, "Unclassified bank data")) goto done;
                    content[chunk] = &rows[row_n - 1];
                }
            }
        }
    }
    for (addr = 0; addr < end; ++addr) { label[addr] = NULL; label_desc[addr] = NULL; }
    label[origin] = (char *)"resource_bank";
    label[instrument_at] = (char *)"instrument_pointers";
    label[event_at] = (char *)"event_record_pointers";
    label[ambient_pitch] = (char *)"ambient_pitch_parameter";
    label[ambient_level] = (char *)"ambient_level_parameter";
    label_desc[ambient_pitch] = (char *)"Mutable ambient command operand";
    label_desc[ambient_level] = (char *)"Mutable ambient command operand";
    for (i = 0; i < patch_n; ++i) {
        char *stored = (char *)malloc(24);
        if (!stored) goto done;
        snprintf(stored, 24, "opl_patch_%04x", patches[i]);
        label[patches[i]] = stored;
        label_owned[patches[i]] = 1;
    }
    for (addr = 0; addr < end; ++addr) if (headers[addr]) {
        char *stored = (char *)malloc(24);
        char *stream = (char *)malloc(24);
        if (!stored || !stream) { free(stored); free(stream); goto done; }
        snprintf(stored, 24, "event_record_%04x", addr);
        snprintf(stream, 24, "stream_%04x", addr + 2);
        if (label_owned[addr]) free(label[addr]);
        label[addr] = stored;
        label_owned[addr] = 1;
        if (addr + 2 < end && !label[addr + 2]) {
            label[addr + 2] = stream;
            label_owned[addr + 2] = 1;
        } else free(stream);
    }
    for (addr = 0; addr < end; ++addr) if (branches[addr] && !headers[addr] && addr != event_at &&
                                           addr != ambient_pitch && addr != ambient_level) {
        char *stored;
        if (label[addr]) {
            if (addr + 1 == ambient_pitch || addr + 1 == ambient_level)
                label_desc[addr] = (char *)"Ambient stream command opcode";
            continue;
        }
        stored = (char *)malloc(24);
        if (!stored) goto done;
        snprintf(stored, 24, "stream_%04x", addr);
        label[addr] = stored;
        label_owned[addr] = 1;
        if (addr + 1 == ambient_pitch || addr + 1 == ambient_level)
            label_desc[addr] = (char *)"Ambient stream command opcode";
    }
    {
        const char *header_line = "name,modulator_characteristics_20,carrier_characteristics_23,feedback_connection_c0,modulator_waveform_e0,carrier_waveform_e3,modulator_level_40,carrier_level_43,modulator_attack_decay_60,carrier_attack_decay_63,modulator_sustain_release_80,carrier_sustain_release_83\n";
        if (fe2_buf_add(&patches_out, header_line, strlen(header_line))) goto done;
    }
    for (i = 0; i < patch_n; ++i) {
        char line[160];
        unsigned at = patches[i];
        snprintf(line, sizeof line, "opl_patch_%04x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x\n",
                 at, image[at], image[at+1], image[at+2], image[at+3], image[at+4], image[at+5],
                 image[at+6], image[at+7], image[at+8], image[at+9], image[at+10]);
        if (fe2_buf_add(&patches_out, line, strlen(line))) goto done;
    }
    {
        const char *header_line = "offset\tkind\toperands\tdescription\n";
        if (fe2_buf_add(&records_out, header_line, strlen(header_line))) goto done;
    }
    for (addr = origin; addr < end; ++addr) {
        char line[256];
        char image_desc[32];
        if (label[addr]) {
            const char *desc = label_desc[addr];
            if (!desc) {
                snprintf(image_desc, sizeof image_desc, "image+0x%04x", addr);
                desc = image_desc;
            }
            snprintf(line, sizeof line, "0x%04x\tlabel\t%s\t%s\n", addr, label[addr], desc);
            if (fe2_buf_add(&records_out, line, strlen(line))) goto done;
        }
        if (content[addr]) {
            snprintf(line, sizeof line, "0x%04x\t%s\t%s\t%s\n", addr, content[addr]->kind,
                     content[addr]->operands, content[addr]->description);
            if (fe2_buf_add(&records_out, line, strlen(line))) goto done;
        }
    }
    {
        const char *header_line = "curve,bend_00,bend_01,bend_02,bend_03,bend_04,bend_05,bend_06,bend_07,bend_08,bend_09,bend_10,bend_11,bend_12,bend_13,bend_14,bend_15,bend_16,bend_17,bend_18,bend_19,bend_20,bend_21,bend_22,bend_23,bend_24,bend_25,bend_26,bend_27,bend_28,bend_29,bend_30,bend_31\n";
        if (fe2_buf_add(&curves_out, header_line, strlen(header_line))) goto done;
    }
    for (i = 0; i < curves; ++i) {
        unsigned pointer = ru16(image, table + (unsigned)i * 2u);
        unsigned expected = table + curves * 2u + (unsigned)i * steps;
        char line[256];
        unsigned n, at;
        if (pointer != expected || expected + steps > image_n) goto fail_bank;
        n = (unsigned)snprintf(line, sizeof line, "%u", (unsigned)i);
        for (at = 0; at < steps; ++at)
            n += (unsigned)snprintf(line + n, sizeof line - n, ",%u", image[expected + at]);
        if (n >= sizeof line || fe2_buf_add(&curves_out, line, n) || fe2_buf_byte(&curves_out, '\n')) goto done;
    }
    if (snprintf(path, sizeof path, "%s/adlib/patches.csv", data_root) >= (int)sizeof path ||
        fe2_write_bytes(path, patches_out.data, patches_out.count)) goto done;
    if (snprintf(path, sizeof path, "%s/adlib/records.tsv", data_root) >= (int)sizeof path ||
        fe2_write_bytes(path, records_out.data, records_out.count)) goto done;
    if (snprintf(path, sizeof path, "%s/adlib/pitch_curves.csv", data_root) >= (int)sizeof path ||
        fe2_write_bytes(path, curves_out.data, curves_out.count)) goto done;
    status = 0;
    goto done;
fail_bank:
    fe2_asset_fail("Unable to extract this Frontier install: EL2ADLIB.OVL");
done:
    if (label && label_owned) {
        for (addr = 0; addr < end; ++addr) if (label_owned[addr]) free(label[addr]);
    }
    free(label_owned);
    free(label); free(label_desc); free(content); free(rows); free(pending); free(events); free(patches);
    free(claimed); free(branches); free(headers); free(starts); free(covered); free(occupied);
    fe2_buf_free(&patches_out); fe2_buf_free(&records_out); fe2_buf_free(&curves_out);
    fe2_ini_free(&ini);
    return status;
}

static const uint8_t adlib_command_width[0x4a] = {
    2,3,2,2,3,3,1,2,1,2,3,3,2,6,2,1,2,4,1,2,1,5,1,1,1,1,2,1,2,2,2,1,2,2,1,1,2,1,
    2,1,1,2,1,2,3,3,2,2,2,1,1,2,1,3,1,1,1,2,1,1,2,2,1,10,10,10,2,1,3,3,3,1,2,3
};

static int adlib_number(const char *text, unsigned *value, unsigned bits)
{
    char *end;
    unsigned long parsed;
    while (*text == ' ' || *text == '\t') ++text;
    if (!*text) return -1;
    parsed = strtoul(text, &end, 0);
    if (end == text || *end || parsed >= (1ul << bits)) return -1;
    *value = (unsigned)parsed;
    return 0;
}

static int adlib_key(const char *line, const char *key, char *value, size_t value_n)
{
    size_t n = strlen(key);
    const char *cursor;
    if (strncmp(line, key, n) != 0) return 0;
    cursor = line + n;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (*cursor != '=') return 0;
    ++cursor;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (strlen(cursor) >= value_n) return -1;
    memcpy(value, cursor, strlen(cursor) + 1);
    return 1;
}

int fe2_compile_adlib_image(const char *data_root, uint8_t **out, size_t *out_n)
{
    enum { SYMBOLS = 64, PATCHES = 256, RECORDS = 16384 };
    char path[4096], line[512], patches_name[64], records_name[64];
    FILE *file;
    unsigned origin = 0, end = 0, pitch_at = 0x382, curve_count = 14, steps = 32;
    unsigned symbol_n = 0, patch_n = 0, record_n = 0, produced = 0, i, curve = 0;
    int status = -1, section = 0;
    typedef struct { char name[64]; unsigned value; } adlib_symbol;
    typedef struct { char name[64]; uint8_t byte[11]; } adlib_patch;
    typedef struct { unsigned offset; char kind; char operands[192]; } adlib_record;
    adlib_symbol *symbol = NULL;
    adlib_patch *patch = NULL;
    adlib_record *record = NULL;
    uint8_t *image = NULL;
    static const uint16_t frequencies[] = {
        0x134, 0x147, 0x15a, 0x16f, 0x184, 0x19c, 0x1b4, 0x1ce, 0x1e9, 0x207, 0x225, 0x246
    };
    patches_name[0] = records_name[0] = 0;
    if (!data_root || !out || !out_n) return -1;
    *out = NULL;
    *out_n = 0;
    symbol = (adlib_symbol *)calloc(SYMBOLS, sizeof *symbol);
    patch = (adlib_patch *)calloc(PATCHES, sizeof *patch);
    record = (adlib_record *)calloc(RECORDS, sizeof *record);
    if (!symbol || !patch || !record) goto done;
    if (snprintf(path, sizeof path, "%s/adlib/bank.ini", data_root) >= (int)sizeof path) goto done;
    file = fopen(path, "rb");
    if (!file) { fe2_asset_fail("Could not read %s", path); goto done; }
    while (fgets(line, sizeof line, file)) {
        char *cursor = line, value[64];
        int found;
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n') line[--n] = 0;
        if (n && line[n - 1] == '\r') line[--n] = 0;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (!*cursor || *cursor == '#' || *cursor == ';') continue;
        if (*cursor == '[') {
            section = strcmp(cursor, "[symbols]") == 0 ? 2 : strcmp(cursor, "[pitch]") == 0 ? 3 : 1;
            continue;
        }
        if (section == 2) {
            char *eq = strchr(cursor, '=');
            unsigned number;
            if (!eq || symbol_n == SYMBOLS) { fclose(file); fe2_asset_fail("Invalid AdLib symbols"); goto done; }
            {
                char *value_text = eq + 1;
                *eq = 0;
                while (eq > cursor && (eq[-1] == ' ' || eq[-1] == '\t')) *--eq = 0;
                while (*value_text == ' ' || *value_text == '\t') ++value_text;
                if (adlib_number(value_text, &number, 16) || strlen(cursor) >= sizeof symbol->name) {
                    fclose(file); fe2_asset_fail("Invalid AdLib symbol %s", cursor); goto done;
                }
            }
            memcpy(symbol[symbol_n].name, cursor, strlen(cursor) + 1);
            symbol[symbol_n].value = number;
            ++symbol_n;
            continue;
        }
        found = adlib_key(cursor, "origin", value, sizeof value);
        if (found < 0) { fclose(file); goto done; }
        if (found) { if (adlib_number(value, &origin, 16)) { fclose(file); goto done; } continue; }
        found = adlib_key(cursor, "end", value, sizeof value);
        if (found) { if (adlib_number(value, &end, 16)) { fclose(file); goto done; } continue; }
        found = adlib_key(cursor, "patches", value, sizeof value);
        if (found) { memcpy(patches_name, value, strlen(value) + 1); continue; }
        found = adlib_key(cursor, "records", value, sizeof value);
        if (found) { memcpy(records_name, value, strlen(value) + 1); continue; }
        found = adlib_key(cursor, "table", value, sizeof value);
        if (found && section == 3) { if (adlib_number(value, &pitch_at, 16)) { fclose(file); goto done; } continue; }
        found = adlib_key(cursor, "curves", value, sizeof value);
        if (found && section == 3) { if (adlib_number(value, &curve_count, 8)) { fclose(file); goto done; } continue; }
        found = adlib_key(cursor, "steps", value, sizeof value);
        if (found && section == 3) { if (adlib_number(value, &steps, 8)) { fclose(file); goto done; } continue; }
    }
    fclose(file);
    if (!origin || !end || end > 0x10000 || !patches_name[0] || !records_name[0] || curve_count != 14 || steps != 32) {
        fe2_asset_fail("Invalid AdLib bank.ini"); goto done;
    }
    if (snprintf(path, sizeof path, "%s/adlib/%s", data_root, patches_name) >= (int)sizeof path) goto done;
    file = fopen(path, "rb");
    if (!file) { fe2_asset_fail("Could not read %s", path); goto done; }
    if (!fgets(line, sizeof line, file)) { fclose(file); goto done; }
    while (fgets(line, sizeof line, file)) {
        char *token, *save = line;
        unsigned field;
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n') line[n - 1] = 0;
        if (!line[0]) continue;
        if (patch_n == PATCHES) { fclose(file); goto done; }
        token = strtok(save, ",");
        if (!token || strlen(token) >= sizeof patch->name) { fclose(file); goto done; }
        memcpy(patch[patch_n].name, token, strlen(token) + 1);
        for (field = 0; field < 11; ++field) {
            unsigned number;
            token = strtok(NULL, ",\r\n");
            if (!token || adlib_number(token, &number, 8)) { fclose(file); goto done; }
            patch[patch_n].byte[field] = (uint8_t)number;
        }
        ++patch_n;
    }
    fclose(file);
    if (snprintf(path, sizeof path, "%s/adlib/%s", data_root, records_name) >= (int)sizeof path) goto done;
    file = fopen(path, "rb");
    if (!file) { fe2_asset_fail("Could not read %s", path); goto done; }
    if (!fgets(line, sizeof line, file)) { fclose(file); goto done; }
    while (fgets(line, sizeof line, file)) {
        char *offset, *kind, *operands, *tab;
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n') line[--n] = 0;
        if (n && line[n - 1] == '\r') line[--n] = 0;
        if (!line[0]) continue;
        if (record_n == RECORDS) { fclose(file); goto done; }
        offset = line;
        tab = strchr(offset, '\t');
        if (!tab) { fclose(file); goto done; }
        *tab = 0;
        kind = tab + 1;
        tab = strchr(kind, '\t');
        if (!tab) { fclose(file); goto done; }
        *tab = 0;
        operands = tab + 1;
        tab = strchr(operands, '\t');
        if (tab) *tab = 0;
        if (adlib_number(offset, &record[record_n].offset, 16)) { fclose(file); goto done; }
        if (!strcmp(kind, "label")) record[record_n].kind = 'L';
        else if (!strcmp(kind, "pointer")) record[record_n].kind = 'P';
        else if (!strcmp(kind, "word")) record[record_n].kind = 'W';
        else if (!strcmp(kind, "raw")) record[record_n].kind = 'R';
        else if (!strcmp(kind, "byte")) record[record_n].kind = 'B';
        else if (!strcmp(kind, "patch")) record[record_n].kind = 'A';
        else if (!strcmp(kind, "event")) record[record_n].kind = 'E';
        else if (!strcmp(kind, "command")) record[record_n].kind = 'C';
        else if (!strcmp(kind, "note")) record[record_n].kind = 'N';
        else { fclose(file); fe2_asset_fail("Unknown AdLib record"); goto done; }
        if (strlen(operands) >= sizeof record->operands) { fclose(file); goto done; }
        memcpy(record[record_n].operands, operands, strlen(operands) + 1);
        ++record_n;
    }
    fclose(file);
    image = (uint8_t *)calloc(end ? end : 1, 1);
    if (!image) goto done;
    if (snprintf(path, sizeof path, "%s/adlib/pitch_curves.csv", data_root) >= (int)sizeof path) goto done;
    file = fopen(path, "rb");
    if (!file) { fe2_asset_fail("Could not read %s", path); goto done; }
    if (!fgets(line, sizeof line, file)) { fclose(file); goto done; }
    while (fgets(line, sizeof line, file)) {
        char *cursor = line;
        unsigned index, step;
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n') line[--n] = 0;
        if (n && line[n - 1] == '\r') line[--n] = 0;
        if (n >= 3 && (unsigned char)line[0] == 0xef) cursor += 3;
        if (!*cursor) continue;
        {
            char *comma = strchr(cursor, ',');
            if (!comma) { fclose(file); goto done; }
            *comma = 0;
            if (adlib_number(cursor, &index, 8) || index != curve || curve == curve_count) {
                fclose(file); fe2_asset_fail("Pitch curves must be ordered and contiguous from 0 through 13"); goto done;
            }
            cursor = comma + 1;
        }
        for (step = 0; step < steps; ++step) {
            char *comma;
            unsigned bend;
            if (!cursor || !*cursor) { fclose(file); goto done; }
            comma = strchr(cursor, ',');
            if (comma) *comma = 0;
            if (adlib_number(cursor, &bend, 8)) { fclose(file); goto done; }
            image[pitch_at + curve_count * 2 + curve * steps + step] = (uint8_t)bend;
            cursor = comma ? comma + 1 : cursor + strlen(cursor);
        }
        image[pitch_at + curve * 2] = (uint8_t)(pitch_at + curve_count * 2 + curve * steps);
        image[pitch_at + curve * 2 + 1] = (uint8_t)((pitch_at + curve_count * 2 + curve * steps) >> 8);
        ++curve;
    }
    fclose(file);
    if (curve != curve_count) { fe2_asset_fail("Expected 14 pitch curves"); goto done; }
    {
        static const uint8_t operators[] = {0x00, 0x01, 0x02, 0x08, 0x09, 0x0a, 0x10, 0x11, 0x12};
        memcpy(image + 0x85e, operators, sizeof operators);
    }
    for (i = 0; i < 12; ++i) {
        image[0x867 + 2 * i] = (uint8_t)frequencies[i];
        image[0x867 + 2 * i + 1] = (uint8_t)(frequencies[i] >> 8);
    }
    for (i = 0; i < record_n; ++i) {
        char kind = record[i].kind;
        char operands[192], *token;
        unsigned count = 0, values[16], t;
        if (record[i].offset != origin + produced) {
            fe2_asset_fail("AdLib records are not contiguous"); goto done;
        }
        if (kind == 'L') continue;
        if (kind == 'A') {
            for (t = 0; t < patch_n; ++t) if (strcmp(patch[t].name, record[i].operands) == 0) break;
            if (t == patch_n) { fe2_asset_fail("Missing AdLib patch"); goto done; }
            if (origin + produced + 11 > end) goto done;
            memcpy(image + origin + produced, patch[t].byte, 11);
            produced += 11;
            continue;
        }
        memcpy(operands, record[i].operands, sizeof operands);
        token = operands;
        while (token && *token && count < 16) {
            char *comma = strchr(token, ',');
            unsigned number = 0, found = 0;
            if (comma) *comma = 0;
            while (*token == ' ' || *token == '\t') ++token;
            {
                char *tail = token + strlen(token);
                while (tail > token && (tail[-1] == ' ' || tail[-1] == '\t')) *--tail = 0;
            }
            if (!*token) { fe2_asset_fail("Invalid AdLib operand"); goto done; }
            for (t = 0; t < record_n; ++t) if (record[t].kind == 'L' && strcmp(record[t].operands, token) == 0) {
                number = record[t].offset; found = 1; break;
            }
            if (!found) for (t = 0; t < symbol_n; ++t) if (strcmp(symbol[t].name, token) == 0) {
                number = symbol[t].value; found = 1; break;
            }
            if (!found && adlib_number(token, &number, 16)) { fe2_asset_fail("Invalid AdLib operand"); goto done; }
            values[count++] = number;
            token = comma ? comma + 1 : NULL;
        }
        if (kind == 'C') {
            if (!count || values[0] < 0x80 || values[0] > 0xc9 ||
                count != adlib_command_width[values[0] - 0x80]) {
                fe2_asset_fail("Invalid AdLib command"); goto done;
            }
        }
        {
            unsigned width = (kind == 'P' || kind == 'W') ? 2 : 1;
            if (kind == 'P') {
                token = record[i].operands;
                while (*token) {
                    char *comma = strchr(token, ',');
                    size_t len = comma ? (size_t)(comma - token) : strlen(token);
                    int labeled = 0;
                    while (len && token[len - 1] == ' ') --len;
                    for (t = 0; t < record_n; ++t) if (record[t].kind == 'L' && strlen(record[t].operands) == len &&
                        memcmp(record[t].operands, token, len) == 0) { labeled = 1; break; }
                    if (!labeled) { fe2_asset_fail("Pointer must reference a bank label"); goto done; }
                    token = comma ? comma + 1 : token + strlen(token);
                    while (*token == ' ') ++token;
                    if (!comma) break;
                }
            }
            for (t = 0; t < count; ++t) {
                if (values[t] >= (1u << (8 * width)) || origin + produced + width > end) goto done;
                image[origin + produced] = (uint8_t)values[t];
                if (width == 2) image[origin + produced + 1] = (uint8_t)(values[t] >> 8);
                produced += width;
            }
        }
    }
    if (origin + produced != end) { fe2_asset_fail("AdLib bank extent changed"); goto done; }
    *out = image;
    *out_n = end;
    image = NULL;
    status = 0;
done:
    free(symbol); free(patch); free(record); free(image);
    return status;
}
