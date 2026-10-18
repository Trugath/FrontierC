/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "adlib.h"
#include "adlib_image.h"
#include <string.h>

/* Callback identifiers stored in the channel words at 0x29 and 0x2b. */
enum { CB_NOOP = 0, CB_MODULATE = 1, CB_SWEEP = 2, CB_SLIDE = 3 };
enum { FLOW_DECODE = 0, FLOW_CALLBACKS = 1, FLOW_ADVANCE = 2 };
enum { STREAM_STEPS = 256 };

static const uint8_t effect_map[40] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0x09, 0x09, 0x0a, 0x09, 0x0a, 0x12, 0x13,
    0x1c, 0x1b, 0x1d, 0x18, 0x0b, 0x0d, 0x15, 0x04, 0x01, 0x1a, 0x10, 0x03,
    0xff, 0x07, 0x08, 0x06, 0x17, 0x05, 0x11, 0x0a, 0x14, 0x0f, 0xff, 0x12,
    0x13, 0xff, 0x16, 0x02
};
static const uint8_t level_duration[15] = {
    0x32, 0x32, 0x32, 0x31, 0x2f, 0x2d, 0x28, 0x1e, 0x14, 0x0a, 0x05, 0x03, 0x02, 0x01, 0x01
};

static int flag(int (*fn)(void *), void *opaque, int fallback)
{
    return fn ? fn(opaque) : fallback;
}
static uint8_t img8(const fe2_adlib *a, unsigned offset)
{
    return offset < FE2_ADLIB_IMAGE_BYTES ? a->image[offset] : 0;
}
static uint16_t img16(const fe2_adlib *a, unsigned offset)
{
    return (uint16_t)(img8(a, offset) | (img8(a, offset + 1) << 8));
}
static unsigned at(unsigned channel, unsigned offset) { return channel * 67u + offset; }
static uint8_t raw(const fe2_adlib *a, unsigned channel, unsigned offset)
{
    unsigned index = at(channel, offset);
    return index < sizeof a->channels ? a->channels[index] : 0;
}
static void put(fe2_adlib *a, unsigned channel, unsigned offset, uint8_t value)
{
    unsigned index = at(channel, offset);
    if (index < sizeof a->channels) a->channels[index] = value;
}
static uint8_t b(const fe2_adlib *a, unsigned offset) { return raw(a, a->di, offset); }
static void setb(fe2_adlib *a, unsigned offset, uint8_t value) { put(a, a->di, offset, value); }
static uint16_t getw(const fe2_adlib *a, unsigned offset)
{
    return (uint16_t)(b(a, offset) | (b(a, offset + 1) << 8));
}
static void setw(fe2_adlib *a, unsigned offset, uint16_t value)
{
    setb(a, offset, (uint8_t)value);
    setb(a, offset + 1, (uint8_t)(value >> 8));
}
static void opl(fe2_adlib *a, uint8_t reg, uint8_t value)
{
    if (a->game.write_opl) a->game.write_opl(a->game.opaque, reg, value);
}
static void publish_music(fe2_adlib *a)
{
    if (a->game.set_current_music) a->game.set_current_music(a->game.opaque, a->current_music);
}
static uint8_t lodsb(fe2_adlib *a)
{
    uint8_t value = img8(a, a->stream);
    a->stream = (uint16_t)(a->stream + 1);
    return value;
}
static uint16_t lodsw(fe2_adlib *a)
{
    uint16_t value = img16(a, a->stream);
    a->stream = (uint16_t)(a->stream + 2);
    return value;
}
static uint16_t next_random(fe2_adlib *a)
{
    uint16_t value = (uint16_t)(a->random_state + 0x9248u);
    value = (uint16_t)((value >> 1) | (value << 15));
    value = (uint16_t)((value >> 1) | (value << 15));
    value = (uint16_t)((value >> 1) | (value << 15));
    a->random_state = value;
    return value;
}
static uint16_t draw_bound(fe2_adlib *a, uint16_t bound)
{
    if (!bound) return 0;
    if (a->game.random_bound) return a->game.random_bound(a->game.opaque, bound);
    return (uint16_t)(next_random(a) % bound);
}
static uint8_t clamp_level(unsigned value)
{
    if ((int16_t)value < 0) return 0;
    if ((int16_t)value > 0x3f) return 0x3f;
    return (uint8_t)value;
}
static uint8_t add8(uint8_t left, uint8_t right) { return (uint8_t)(left + right); }
static uint8_t saturate_level(uint8_t value, uint8_t patch)
{
    if (value > 0x3f) value = ((int8_t)value < 0) ? 0 : 0x3f;
    return (uint8_t)(value | (patch & 0xc0u));
}
static uint8_t scale_down(uint8_t value, uint8_t shift, uint8_t input)
{
    unsigned count = (unsigned)(shift + 1u) & 31u;
    uint16_t product = (uint16_t)((uint32_t)input << count);
    return (uint8_t)(value - (uint8_t)(product >> 8));
}
static uint8_t carrier_level(fe2_adlib *a)
{
    uint8_t value = b(a, 0x2f) & 0x3fu;
    value = add8(value, b(a, 0x20));
    value = add8(value, b(a, 0));
    value = add8(value, b(a, 0x30));
    if (b(a, 0x33)) value = scale_down(value, b(a, 0x33), b(a, 0x34));
    return saturate_level(value, b(a, 0x2f));
}
static uint8_t modulator_level(fe2_adlib *a)
{
    uint8_t value = b(a, 0x2e) & 0x3fu;
    if (b(a, 0x31)) {
        value = add8(value, b(a, 0x20));
        value = add8(value, b(a, 0));
        value = add8(value, b(a, 0x30));
    }
    if (b(a, 0x32)) value = scale_down(value, b(a, 0x32), b(a, 0x34));
    return saturate_level(value, b(a, 0x2e));
}
static void apply_levels(fe2_adlib *a)
{
    uint8_t op, value;
    if (a->channel_index > 9) return;
    op = img8(a, 0x85eu + a->channel_index);
    value = carrier_level(a);
    opl(a, (uint8_t)(op + 0x43u), value);
    if (!b(a, 0x31)) return;
    value = modulator_level(a);
    opl(a, (uint8_t)(op + 0x40u), value);
}
static void load_instrument(fe2_adlib *a, uint8_t op)
{
    uint8_t value;
    opl(a, (uint8_t)(0x20u + op), lodsb(a));
    opl(a, (uint8_t)(0x23u + op), lodsb(a));
    value = lodsb(a);
    opl(a, (uint8_t)(0xc0u + a->channel_index), value);
    setb(a, 0x31, (uint8_t)(value & 1u));
    opl(a, (uint8_t)(0xe0u + op), lodsb(a));
    opl(a, (uint8_t)(0xe3u + op), lodsb(a));
    setb(a, 0x2e, lodsb(a));
    opl(a, (uint8_t)(0x40u + op), modulator_level(a));
    setb(a, 0x2f, lodsb(a));
    opl(a, (uint8_t)(0x43u + op), carrier_level(a));
    opl(a, (uint8_t)(0x60u + op), lodsb(a));
    opl(a, (uint8_t)(0x63u + op), lodsb(a));
    opl(a, (uint8_t)(0x80u + op), lodsb(a));
    opl(a, (uint8_t)(0x83u + op), lodsb(a));
}
static void initialize_channel(fe2_adlib *a)
{
    unsigned offset;
    for (offset = 3; offset < 67; ++offset) setb(a, offset, 0);
    setb(a, 0x25, 0xff);
    setb(a, 8, 0);
    setw(a, 0x29, CB_NOOP);
    setw(a, 0x2b, CB_NOOP);
    setb(a, 0x35, 1);
}
static void silence_channel(fe2_adlib *a, uint8_t channel)
{
    uint8_t op;
    if (a->rhythm_mode && channel >= 6) return;
    op = img8(a, 0x85eu + channel);
    opl(a, (uint8_t)(0x60u + op), 0xff);
    opl(a, (uint8_t)(0x63u + op), 0xff);
    opl(a, (uint8_t)(0x80u + op), 0xff);
    opl(a, (uint8_t)(0x83u + op), 0xff);
    opl(a, (uint8_t)(0xb0u + channel), 0);
    opl(a, (uint8_t)(0xb0u + channel), 0x20);
}
static void release_note(fe2_adlib *a)
{
    uint8_t high;
    if (a->rhythm_mode && a->channel_index >= 6) return;
    if (a->channel_index == 9) return;
    high = (uint8_t)(b(a, 0x28) & 0xdfu);
    setb(a, 0x28, high);
    opl(a, (uint8_t)(0xb0u + a->channel_index), high);
}
static void publish_frequency(fe2_adlib *a, uint8_t low, uint8_t high)
{
    uint8_t block = (uint8_t)(high | (b(a, 0x28) & 0x20u));
    setb(a, 0x28, block);
    setb(a, 0x27, low);
    opl(a, (uint8_t)(0xa0u + a->channel_index), low);
    opl(a, (uint8_t)(0xb0u + a->channel_index), block);
}
static uint16_t bend_delta(fe2_adlib *a, unsigned table, uint8_t bend)
{
    uint16_t si = img16(a, 0x382u + table * 2u);
    return img8(a, (unsigned)si + bend);
}
static void calculate_pitch(fe2_adlib *a)
{
    uint8_t note = a->al, block, semitone, bend;
    uint16_t frequency, cx;
    setb(a, 0x41, note);
    block = (uint8_t)((note & 0xf0u) + b(a, 7));
    semitone = (uint8_t)((note & 0x0fu) + b(a, 0x12));
    if ((int8_t)semitone >= 12) {
        semitone = (uint8_t)(semitone - 12u);
        block = (uint8_t)(block + 0x10u);
    } else if ((int8_t)semitone < 0) {
        semitone = (uint8_t)(semitone + 12u);
        block = (uint8_t)(block - 0x10u);
    }
    frequency = (uint16_t)(img16(a, 0x867u + (unsigned)semitone * 2u) + b(a, 0x24));
    block = (uint8_t)(((block >> 2) & 0x1cu) | (frequency >> 8));
    cx = (uint16_t)((uint8_t)frequency | (block << 8));
    bend = b(a, 0x42);
    if ((int8_t)bend > 0) cx = (uint16_t)(cx + bend_delta(a, (note & 0x0fu) + 2u, bend));
    else if ((int8_t)bend < 0) cx = (uint16_t)(cx - bend_delta(a, note & 0x0fu, (uint8_t)((bend ^ 0xffu) + 1u)));
    publish_frequency(a, (uint8_t)cx, (uint8_t)(cx >> 8));
}
static void key_on(fe2_adlib *a)
{
    uint8_t high = (uint8_t)(b(a, 0x28) | 0x20u);
    uint8_t shift = (uint8_t)(9u - b(a, 0x19));
    uint16_t step;
    setb(a, 0x28, high);
    opl(a, (uint8_t)(0xb0u + a->channel_index), high);
    step = (uint16_t)((getw(a, 0x27) & 0x3ffu) >> (shift & 31u));
    setw(a, 0x17, (uint16_t)(step & 0xffu));
    setb(a, 0x1f, b(a, 0x1c));
}
static void set_duration(fe2_adlib *a)
{
    uint8_t duration = a->ah, piece, sum = 0;
    unsigned count;
    a->last_duration = duration;
    if (b(a, 0x36)) {
        setb(a, 5, add8(duration, (uint8_t)(next_random(a) & b(a, 0x36))));
        return;
    }
    if (b(a, 0x2d)) {
        piece = (uint8_t)(duration >> 3);
        for (count = b(a, 0x2d); count; --count) sum = add8(sum, piece);
        setb(a, 0x21, sum);
    }
    setb(a, 5, duration);
}
static void modulate_pitch(fe2_adlib *a)
{
    uint8_t phase;
    uint16_t step, frequency;
    if (b(a, 0x1f)) { setb(a, 0x1f, (uint8_t)(b(a, 0x1f) - 1u)); return; }
    phase = (uint8_t)(b(a, 0x1e) + b(a, 0x1d));
    setb(a, 0x1e, phase);
    if (phase >= b(a, 0x1d)) return;
    step = getw(a, 0x17);
    setb(a, 0x1a, (uint8_t)(b(a, 0x1a) - 1u));
    if (!b(a, 0x1a)) {
        step = (uint16_t)(-(int16_t)step);
        setw(a, 0x17, step);
        setb(a, 0x1a, b(a, 0x1b));
    }
    frequency = (uint16_t)((getw(a, 0x27) & 0x3ffu) + step);
    setb(a, 0x27, (uint8_t)frequency);
    setb(a, 0x28, (uint8_t)((b(a, 0x28) & 0xfcu) | (frequency >> 8)));
    opl(a, (uint8_t)(0xa0u + a->channel_index), b(a, 0x27));
    opl(a, (uint8_t)(0xb0u + a->channel_index), b(a, 0x28));
}
static void sweep_register(fe2_adlib *a)
{
    uint8_t phase = (uint8_t)(b(a, 0x3a) + b(a, 0x39));
    uint8_t index;
    setb(a, 0x3a, phase);
    if (phase >= b(a, 0x39)) return;
    index = (uint8_t)(b(a, 0x3c) - 1u);
    if ((int8_t)index < 0) index = b(a, 0x3b);
    setb(a, 0x3c, index);
    opl(a, (uint8_t)(b(a, 0x3d) + a->operator_offset), img8(a, getw(a, 0x3e) + index));
}
/* The masked-zero block change pops the saved frequency and then the near
 * return address. There is no return address here; that second pop reads 0,
 * which discards the inc or dec done between the two pops. */
static uint16_t slide_boundary(uint16_t shifted)
{
    return (shifted & 0x3ffu) ? shifted : 0;
}
static void slide_pitch(fe2_adlib *a)
{
    uint8_t phase = (uint8_t)(b(a, 0x14) + b(a, 0x13));
    uint16_t frequency, step;
    uint8_t block, key;
    int16_t signed_step;
    setb(a, 0x14, phase);
    if (phase >= b(a, 0x13)) return;
    frequency = (uint16_t)(b(a, 0x27) | ((b(a, 0x28) & 3u) << 8));
    block = (uint8_t)(b(a, 0x28) & 0x1cu);
    key = (uint8_t)(b(a, 0x28) & 0x20u);
    step = getw(a, 0x15);
    signed_step = (int16_t)step;
    frequency = (uint16_t)(frequency + step);
    if (signed_step >= 0) {
        if ((int16_t)frequency >= 0x2de) {
            frequency = (uint16_t)(frequency >> 1);
            frequency = slide_boundary(frequency);
            block = (uint8_t)((block + 4u) & 0x1cu);
        }
    } else if ((int16_t)frequency <= 0x184) {
        frequency = (uint16_t)(frequency << 1);
        frequency = slide_boundary(frequency);
        block = (uint8_t)((block - 4u) & 0x1cu);
    }
    frequency &= 0x3ffu;
    opl(a, (uint8_t)(0xa0u + a->channel_index), (uint8_t)frequency);
    setb(a, 0x27, (uint8_t)frequency);
    setb(a, 0x28, (uint8_t)((frequency >> 8) | block | key));
    opl(a, (uint8_t)(0xb0u + a->channel_index), b(a, 0x28));
}
static void run_callback(fe2_adlib *a, uint16_t which)
{
    if (which == CB_MODULATE) modulate_pitch(a);
    else if (which == CB_SWEEP) sweep_register(a);
    else if (which == CB_SLIDE) slide_pitch(a);
}
static void run_callbacks(fe2_adlib *a)
{
    run_callback(a, getw(a, 0x29));
    run_callback(a, getw(a, 0x2b));
}
static void queue_event(fe2_adlib *a, uint8_t event)
{
    a->queue[a->queue_write & 15u] = event;
    a->queue_write = (uint16_t)((a->queue_write + 1u) & 15u);
}
static int accept_event(fe2_adlib *a, uint8_t event, int silence_melodic)
{
    uint16_t record = img16(a, a->bank_pointer + (unsigned)event * 2u);
    uint16_t header = img16(a, record);
    uint8_t channel = (uint8_t)header, priority = (uint8_t)(header >> 8);
    unsigned saved = a->di;
    if (channel >= FE2_ADLIB_CHANNELS) return 0;
    a->di = channel;
    if ((int8_t)priority < (int8_t)b(a, 8)) { a->di = saved; return 0; }
    initialize_channel(a);
    setb(a, 8, priority);
    setw(a, 3, (uint16_t)(record + 2u));
    setb(a, 0x25, 0xff);
    setb(a, 0x26, 0xff);
    setb(a, 5, 1);
    a->voices = 1;
    if (silence_melodic || channel != 9) silence_channel(a, channel);
    a->di = saved;
    return 1;
}
static void drain_queue(fe2_adlib *a)
{
    while (a->queue_read != a->queue_write) {
        uint8_t event = a->queue[a->queue_read & 15u];
        a->queue_read = (uint16_t)((a->queue_read + 1u) & 15u);
        accept_event(a, event, 0);
    }
}
static void reset_opl(fe2_adlib *a)
{
    unsigned channel;
    a->voices = 0;
    a->random_state = 0x1234;
    opl(a, 0x01, 0x20);
    opl(a, 0x08, 0x00);
    opl(a, 0xbd, 0x00);
    for (channel = 10; channel > 0; --channel) {
        unsigned index = channel - 1u;
        a->di = index;
        if (index != 9) {
            uint8_t op = img8(a, 0x85eu + index);
            opl(a, (uint8_t)(op + 0x40u), 0x3f);
            opl(a, (uint8_t)(op + 0x43u), 0x3f);
        }
        initialize_channel(a);
    }
}
static void queue_effect(fe2_adlib *a, uint16_t event)
{
    if (event == 0x18) {
        if (a->hold_18) { a->hold_18 = 0x190; return; }
        a->hold_18 = 0x190;
    } else if (event == 0x0d) {
        if (a->hold_0d) { a->hold_0d = 0x258; return; }
        a->hold_0d = 0x258;
    } else if (event == 0x0b) {
        if (a->hold_0b) { a->hold_0b = 0x258; return; }
        a->hold_0b = 0x258;
    } else if (event == 0x1d) {
        if (a->hold_1d) { a->hold_1d = 0x190; return; }
        a->hold_1d = 0x190;
    }
    queue_event(a, (uint8_t)event);
}
static void clear_holds(fe2_adlib *a)
{
    queue_event(a, 0);
    a->hold_18 = a->hold_0d = a->hold_0b = a->hold_1d = 0;
    a->hold_ambient = 1;
}
static void select_candidate(fe2_adlib *a, uint16_t track)
{
    a->current_music = (int16_t)track;
    publish_music(a);
    queue_event(a, (uint8_t)(track + 0x32u));
}

static void jump_stream(fe2_adlib *a)
{
    uint8_t high = lodsb(a);
    a->stream = (uint16_t)(a->ah | (high << 8));
}
static int finish_duration(fe2_adlib *a)
{
    if (!a->last_duration) return FLOW_DECODE;
    setw(a, 3, a->stream);
    return FLOW_CALLBACKS;
}
static int command_stop(fe2_adlib *a)
{
    setb(a, 8, 0);
    setw(a, 3, 0);
    if (a->channel_index != 9) release_note(a);
    if (a->channel_index == 2) {
        a->current_music = -1;
        publish_music(a);
        if (flag(a->game.auto_music, a->game.opaque, 0)) fe2_adlib_select_music(a, -1);
    }
    return FLOW_ADVANCE;
}
static void with_channel(fe2_adlib *a, uint8_t channel, void (*body)(fe2_adlib *))
{
    unsigned saved_di = a->di;
    uint8_t saved = a->channel_index;
    if (channel >= FE2_ADLIB_CHANNELS) return;
    a->di = channel;
    a->channel_index = channel;
    body(a);
    a->di = saved_di;
    a->channel_index = saved;
}
static void store_level_base(fe2_adlib *a) { setb(a, 0, a->al); apply_levels(a); }
static void add_level_base(fe2_adlib *a) { setb(a, 0, add8(b(a, 0), a->al)); apply_levels(a); }
static void clear_one(fe2_adlib *a)
{
    uint8_t op;
    setb(a, 5, 0);
    setb(a, 8, 0);
    setw(a, 3, 0);
    setb(a, 0, 0);
    if (a->channel_index == 9) return;
    op = img8(a, 0x85eu + a->channel_index);
    opl(a, (uint8_t)(0xc0u + a->channel_index), 0);
    opl(a, (uint8_t)(op + 0x43u), 0x3f);
    opl(a, (uint8_t)(op + 0x83u), 0xff);
    opl(a, (uint8_t)(0xb0u + a->channel_index), 0);
}
static void rhythm_voice(fe2_adlib *a, uint8_t channel, uint8_t patch, uint8_t *first, uint8_t *second)
{
    unsigned pointer = img16(a, 0x1873u + (unsigned)patch * 2u);
    unsigned saved = a->stream;
    uint8_t op = img8(a, 0x85eu + channel);
    if (first) *first = img8(a, pointer + (second ? 5u : 6u));
    if (second) *second = img8(a, pointer + 6u);
    a->channel_index = channel;
    a->operator_offset = op;
    a->stream = (uint16_t)pointer;
    load_instrument(a, op);
    a->stream = (uint16_t)saved;
}
/* mode 0 stores the operand as bias and adds it again. mode 1 caches the
 * clamped base+level+bias+operand sum. mode 2 caches the operand and outputs
 * base+bias+operand. */
static void drum(fe2_adlib *a, uint8_t value, int mode, uint8_t *base, uint8_t *level, uint8_t *bias, uint8_t reg)
{
    uint8_t sum = add8(value, *base);
    if (mode == 0) {
        *bias = value;
        sum = add8(add8(sum, *level), *bias);
    } else if (mode == 1) {
        sum = add8(add8(sum, *level), *bias);
        *level = clamp_level(sum);
    } else {
        *level = value;
        sum = add8(sum, *bias);
    }
    opl(a, reg, mode == 1 ? *level : clamp_level(sum));
}
static void write_rhythm(fe2_adlib *a, uint8_t mask, int mode)
{
    uint8_t value = lodsb(a);
    if (mask & 1u) drum(a, value, mode, &a->base_hihat, &a->level_hihat, &a->bias_hihat, 0x51);
    if (mask & 2u) drum(a, value, mode, &a->base_cymbal, &a->level_cymbal, &a->bias_cymbal, 0x55);
    if (mask & 4u) drum(a, value, mode, &a->base_tom, &a->level_tom, &a->bias_tom, 0x52);
    if (mask & 8u) drum(a, value, mode, &a->base_snare, &a->level_snare, &a->bias_snare, 0x54);
    if (mask & 0x10u) drum(a, value, mode, &a->base_bass, &a->level_bass, &a->bias_bass, 0x53);
}
static int command(fe2_adlib *a)
{
    uint8_t opcode = (uint8_t)(a->al & 0x7fu);
    uint16_t word;
    unsigned saved;
    uint8_t saved_op, channel;
    /* 0xca..0xf0 index past the dispatch table. 0xf1..0xff stop by the range check. */
    if (opcode > 0x49u) return command_stop(a);
    switch (opcode) {
    case 0x00: setb(a, 6, a->ah); return FLOW_DECODE;
    case 0x01:
        setb(a, 6, (uint8_t)(b(a, 6) - 1u));
        if (!b(a, 6)) { a->stream = (uint16_t)(a->stream + 1u); return FLOW_DECODE; }
        jump_stream(a);
        return FLOW_DECODE;
    case 0x04:
        jump_stream(a);
        return FLOW_DECODE;
    case 0x02:
        if (a->ah != 0xff) accept_event(a, a->ah, 1);
        return FLOW_DECODE;
    case 0x03: setb(a, 0x35, a->ah); return FLOW_DECODE;
    case 0x05:
        a->stream = (uint16_t)(a->stream - 1u);
        channel = b(a, 9);
        word = (uint16_t)(a->stream + 2u);
        setw(a, 0x0au + ((unsigned)channel * 2u & 0xffu), word);
        setb(a, 9, (uint8_t)(channel + 1u));
        a->stream = lodsw(a);
        return FLOW_DECODE;
    case 0x06:
        a->stream = (uint16_t)(a->stream - 1u);
        channel = (uint8_t)(b(a, 9) - 1u);
        setb(a, 9, channel);
        a->stream = getw(a, 0x0au + ((unsigned)channel * 2u & 0xffu));
        return FLOW_DECODE;
    case 0x07: setb(a, 7, a->ah); return FLOW_DECODE;
    case 0x09:
        set_duration(a);
        setw(a, 3, a->stream);
        if (a->channel_index != 9) release_note(a);
        return finish_duration(a);
    case 0x0a: opl(a, a->ah, lodsb(a)); return FLOW_DECODE;
    case 0x0b:
        a->al = a->ah;
        calculate_pitch(a);
        a->ah = lodsb(a);
        set_duration(a);
        return finish_duration(a);
    case 0x0c: setb(a, 0x12, a->ah); return FLOW_DECODE;
    case 0x0d:
        setb(a, 0x3a, a->ah);
        setb(a, 0x39, a->ah);
        setb(a, 0x3b, lodsb(a));
        setb(a, 0x3c, b(a, 0x3b));
        setb(a, 0x3d, lodsb(a));
        setw(a, 0x3e, lodsw(a));
        setw(a, 0x2b, CB_SWEEP);
        return FLOW_DECODE;
    case 0x0e:
        saved = a->di;
        channel = a->channel_index;
        if (a->ah < FE2_ADLIB_CHANNELS) {
            a->di = a->ah;
            setb(a, 5, 0);
            setb(a, 8, 0);
            setw(a, 3, 0);
        }
        a->di = saved;
        a->channel_index = channel;
        return FLOW_DECODE;
    case 0x10:
        saved = a->stream;
        a->stream = img16(a, 0x1873u + (unsigned)a->ah * 2u);
        load_instrument(a, a->operator_offset);
        a->stream = (uint16_t)saved;
        return FLOW_DECODE;
    case 0x11:
        setb(a, 0x13, a->ah);
        a->ah = lodsb(a);
        a->al = lodsb(a);
        setw(a, 0x15, (uint16_t)(a->al | (a->ah << 8)));
        setw(a, 0x29, CB_SLIDE);
        setb(a, 0x14, 0xff);
        return FLOW_DECODE;
    case 0x12:
        a->stream = (uint16_t)(a->stream - 1u);
        setw(a, 0x29, CB_NOOP);
        setw(a, 0x15, 0);
        return FLOW_DECODE;
    case 0x13: setb(a, 0x24, a->ah); return FLOW_DECODE;
    case 0x15:
        setb(a, 0x1d, a->ah);
        setb(a, 0x19, lodsb(a));
        a->al = lodsb(a);
        setb(a, 0x1a, (uint8_t)(a->al + 1u));
        setb(a, 0x1b, (uint8_t)(a->al << 1));
        setb(a, 0x1c, lodsb(a));
        setw(a, 0x29, CB_MODULATE);
        return FLOW_DECODE;
    case 0x1a: setb(a, 8, a->ah); return FLOW_DECODE;
    case 0x1c:
        a->level_reload = (uint8_t)(a->ah >> 1);
        a->level_remaining = a->level_reload;
        a->level_phase = 0xff;
        a->level_value = 0;
        a->gate = 0;
        return FLOW_DECODE;
    case 0x1d:
        if (!a->gate) {
            if (a->ah & a->level_value) {
                a->stream = (uint16_t)(a->stream - 2u);
                setb(a, 5, 1);
                setw(a, 3, a->stream);
                return FLOW_ADVANCE;
            }
            a->gate = 1;
            a->stream = (uint16_t)(a->stream - 2u);
            setb(a, 5, 1);
            setw(a, 3, a->stream);
            return FLOW_ADVANCE;
        }
        if (!(a->ah & a->level_value)) {
            a->stream = (uint16_t)(a->stream - 2u);
            setb(a, 5, 1);
            setw(a, 3, a->stream);
            return FLOW_ADVANCE;
        }
        a->gate = 0;
        return FLOW_DECODE;
    case 0x1e: setb(a, 0x20, a->ah); apply_levels(a); return FLOW_DECODE;
    case 0x20: set_duration(a); return finish_duration(a);
    case 0x21:
        set_duration(a);
        key_on(a);
        return finish_duration(a);
    case 0x24: setb(a, 0x2d, (uint8_t)(a->ah & 7u)); return FLOW_DECODE;
    case 0x26: a->tempo = a->ah; return FLOW_DECODE;
    case 0x27:
        a->stream = (uint16_t)(a->stream - 1u);
        setw(a, 0x2b, CB_NOOP);
        return FLOW_DECODE;
    case 0x29: setb(a, 0x25, a->ah); return FLOW_DECODE;
    case 0x2b: setb(a, 0x30, a->ah); return FLOW_DECODE;
    case 0x2c:
        a->al = lodsb(a);
        with_channel(a, a->ah, store_level_base);
        return FLOW_DECODE;
    case 0x2d:
        a->al = lodsb(a);
        with_channel(a, a->ah, add_level_base);
        return FLOW_DECODE;
    case 0x2e:
        a->opl_bd = (uint8_t)((a->opl_bd & 0x7fu) | ((a->ah & 1u) << 7));
        opl(a, 0xbd, a->opl_bd);
        return FLOW_DECODE;
    case 0x2f:
        a->opl_bd = (uint8_t)((a->opl_bd & 0xbfu) | ((a->ah & 1u) << 6));
        opl(a, 0xbd, a->opl_bd);
        return FLOW_DECODE;
    case 0x30: setb(a, 0x20, add8(b(a, 0x20), a->ah)); apply_levels(a); return FLOW_DECODE;
    case 0x33: with_channel(a, a->ah, clear_one); return FLOW_DECODE;
    case 0x35: {
        uint16_t mask = (uint16_t)(lodsb(a) | (a->ah << 8));
        uint16_t mixed = (uint16_t)(next_random(a) & mask);
        uint8_t key = (uint8_t)(b(a, 0x28) & 0x20u);
        uint16_t frequency = (uint16_t)(b(a, 0x27) | ((b(a, 0x28) & 0x1fu) << 8));
        frequency = (uint16_t)(frequency + mixed);
        opl(a, (uint8_t)(0xa0u + a->channel_index), (uint8_t)frequency);
        opl(a, (uint8_t)(0xb0u + a->channel_index), (uint8_t)((frequency >> 8) | key));
        return FLOW_DECODE;
    }
    case 0x36:
        a->stream = (uint16_t)(a->stream - 1u);
        setw(a, 0x29, CB_NOOP);
        return FLOW_DECODE;
    case 0x39:
        setb(a, 0x42, a->ah);
        a->al = b(a, 0x41);
        calculate_pitch(a);
        return FLOW_DECODE;
    case 0x3a:
        a->stream = (uint16_t)(a->stream - 1u);
        setb(a, 0x25, a->tempo);
        return FLOW_DECODE;
    case 0x3b: a->stream = (uint16_t)(a->stream - 1u); return FLOW_DECODE;
    case 0x3c: setb(a, 0x36, a->ah); return FLOW_DECODE;
    case 0x3d:
        if ((int8_t)a->ah >= 0) {
            uint8_t sum = add8(a->ah, b(a, 0x25));
            setb(a, 0x25, (uint8_t)((sum < a->ah) ? 0xff : sum));
        } else {
            uint8_t old = b(a, 0x25), sum = add8(a->ah, old);
            setb(a, 0x25, sum < old ? sum : 1);
        }
        return FLOW_DECODE;
    case 0x3f:
    case 0x40:
    case 0x41:
        saved = a->di;
        saved_op = a->operator_offset;
        channel = a->channel_index;
        rhythm_voice(a, 6, a->ah, &a->base_bass, NULL);
        rhythm_voice(a, 7, lodsb(a), &a->base_hihat, &a->base_snare);
        rhythm_voice(a, 8, lodsb(a), &a->base_tom, &a->base_cymbal);
        a->di = saved;
        a->al = (uint8_t)(lodsb(a) & 0x2fu);
        put(a, 6, 0x28, a->al);
        opl(a, 0xb6, a->al);
        opl(a, 0xa6, lodsb(a));
        a->al = (uint8_t)(lodsb(a) & 0x2fu);
        put(a, 7, 0x28, a->al);
        opl(a, 0xb7, a->al);
        opl(a, 0xa7, lodsb(a));
        a->al = (uint8_t)(lodsb(a) & 0x2fu);
        put(a, 8, 0x28, a->al);
        opl(a, 0xb8, a->al);
        opl(a, 0xa8, lodsb(a));
        a->rhythm_mode = 0x20;
        a->operator_offset = saved_op;
        a->channel_index = channel;
        a->di = saved;
        return FLOW_DECODE;
    case 0x42: {
        uint8_t mask = a->ah;
        uint8_t first = (uint8_t)((((mask & 0x1fu) ^ 0xffu) & a->rhythm_mode) | 0x20u);
        opl(a, 0xbd, first);
        a->rhythm_mode = (uint8_t)(a->rhythm_mode | mask);
        opl(a, 0xbd, (uint8_t)(a->rhythm_mode | a->opl_bd | 0x20u));
        return FLOW_DECODE;
    }
    case 0x43:
        a->stream = (uint16_t)(a->stream - 1u);
        a->rhythm_mode = 0;
        opl(a, 0xbd, (uint8_t)(a->opl_bd & 0xc0u));
        return FLOW_DECODE;
    case 0x44: write_rhythm(a, a->ah, 0); return FLOW_DECODE;
    case 0x45: write_rhythm(a, a->ah, 1); return FLOW_DECODE;
    case 0x46: write_rhythm(a, a->ah, 2); return FLOW_DECODE;
    case 0x48: setb(a, 0x40, a->ah); return FLOW_DECODE;
    case 0x49:
        setb(a, 0x32, a->ah);
        setb(a, 0x33, lodsb(a));
        return FLOW_DECODE;
    default: return command_stop(a);
    }
}
static int decode_note(fe2_adlib *a)
{
    uint8_t op, value;
    calculate_pitch(a);
    key_on(a);
    set_duration(a);
    if (getw(a, 0x32) || getw(a, 0x33)) {
        setb(a, 0x34, lodsb(a));
        op = a->operator_offset;
        value = carrier_level(a);
        opl(a, (uint8_t)(op + 0x43u), value);
        value = modulator_level(a);
        opl(a, (uint8_t)(op + 0x40u), value);
    }
    return finish_duration(a);
}
static void service_channel(fe2_adlib *a)
{
    uint8_t phase, duration;
    unsigned steps;
    int flow;
    if (!b(a, 5)) return;
    a->operator_offset = img8(a, 0x85eu + a->channel_index);
    if (b(a, 0x40)) setb(a, 0x25, a->tempo);
    phase = (uint8_t)(b(a, 0x26) + b(a, 0x25));
    setb(a, 0x26, phase);
    if (phase >= b(a, 0x25)) { run_callbacks(a); return; }
    setb(a, 5, (uint8_t)(b(a, 5) - 1u));
    duration = b(a, 5);
    if (duration) {
        if (duration == b(a, 0x21)) release_note(a);
        if (duration == b(a, 0x35) && a->channel_index != 9) release_note(a);
        run_callbacks(a);
        return;
    }
    a->stream = getw(a, 3);
    for (steps = 0; steps < STREAM_STEPS; ++steps) {
        uint16_t word = lodsw(a);
        a->al = (uint8_t)word;
        a->ah = (uint8_t)(word >> 8);
        flow = (a->al & 0x80u) ? command(a) : decode_note(a);
        if (flow == FLOW_DECODE) continue;
        if (flow == FLOW_CALLBACKS) run_callbacks(a);
        return;
    }
    setb(a, 5, 0);
}
static void update_voices(fe2_adlib *a)
{
    int channel;
    if (!a->voices) return;
    for (channel = 9; channel >= 0; --channel) {
        a->channel_index = (uint8_t)channel;
        a->di = (unsigned)channel;
        service_channel(a);
    }
}
static void update_level_counter(fe2_adlib *a)
{
    uint8_t phase = (uint8_t)(a->level_phase + a->tempo);
    a->level_phase = phase;
    if (phase >= a->tempo) return;
    a->level_remaining--;
    if (a->level_remaining) return;
    a->level_remaining = a->level_reload;
    a->level_value++;
}

void fe2_adlib_init(fe2_adlib *a, const fe2_adlib_game *game)
{
    fe2_adlib_game copy = game ? *game : (fe2_adlib_game){0};
    if (!a) return;
    memset(a, 0, sizeof *a);
    a->game = copy;
    if (!fe2_adlib_image_template || fe2_adlib_image_bytes != sizeof a->image) return;
    memcpy(a->image, fe2_adlib_image_template, sizeof a->image);
    a->bank_pointer = img16(a, 0x1871);
    reset_opl(a);
    a->current_music = -1;
    publish_music(a);
    if (a->game.set_active) a->game.set_active(a->game.opaque, 1);
    clear_holds(a);
}
void fe2_adlib_shutdown(fe2_adlib *a)
{
    if (!a) return;
    reset_opl(a);
    if (a->game.set_active) a->game.set_active(a->game.opaque, 0);
}
static int track_enabled(fe2_adlib *a, uint16_t track)
{
    return a->game.music_mask ? a->game.music_mask(a->game.opaque, track) : 1;
}
void fe2_adlib_select_music(fe2_adlib *a, int16_t index)
{
    uint16_t track = (uint16_t)index;
    unsigned tries;
    if (!a) return;
    if ((track & 0x8000u) == 0) {
        if (!flag(a->game.music_enabled, a->game.opaque, 1)) return;
    } else if (a->current_music != -1) return;
    else track = draw_bound(a, (uint16_t)((index & 1) ? 7u : 6u));
    for (tries = 0x28; ; --tries) {
        if (track_enabled(a, track)) { select_candidate(a, track); return; }
        if (tries == 1) break;
        track = draw_bound(a, 7);
    }
    for (track = 0; track < 8; ++track)
        if (track_enabled(a, track)) { select_candidate(a, track); return; }
}
void fe2_adlib_check_music(fe2_adlib *a)
{
    if (!a) return;
    if (!flag(a->game.auto_music, a->game.opaque, 0)) fe2_adlib_reset_music(a);
}
void fe2_adlib_reset_music(fe2_adlib *a)
{
    if (!a) return;
    queue_event(a, 0x23);
    a->current_music = -1;
    publish_music(a);
}
/* Effects 40..49 read the ten hold bytes that follow the 40-byte map. */
static uint8_t mapped_event(const fe2_adlib *a, unsigned effect)
{
    uint16_t hold = 0;
    if (effect < 40) return effect_map[effect];
    if (effect >= 50) return 0xff;
    if (effect < 42) hold = a->hold_18;
    else if (effect < 44) hold = a->hold_0d;
    else if (effect < 46) hold = a->hold_0b;
    else if (effect < 48) hold = a->hold_ambient;
    else hold = a->hold_1d;
    return (effect & 1u) ? (uint8_t)(hold >> 8) : (uint8_t)hold;
}
void fe2_adlib_play_effect(fe2_adlib *a, uint16_t effect)
{
    uint8_t event;
    if (!a || !flag(a->game.effects_enabled, a->game.opaque, 1)) return;
    if (effect == 0x14 && !flag(a->game.ui_effect_enabled, a->game.opaque, 1)) return;
    event = mapped_event(a, effect);
    if (event & 0x80u) return;
    queue_effect(a, event);
}
void fe2_adlib_play_duration(fe2_adlib *a, uint16_t effect, uint16_t duration)
{
    uint8_t event;
    if (!a || !flag(a->game.effects_enabled, a->game.opaque, 1) || !duration) return;
    (void)(duration >> 4);
    event = mapped_event(a, effect);
    if (event & 0x80u) return;
    queue_effect(a, event);
}
void fe2_adlib_play_level(fe2_adlib *a, uint16_t effect, uint8_t level)
{
    if (!a || !flag(a->game.effects_enabled, a->game.opaque, 1)) return;
    if (level >= 15 || !flag(a->game.time_running, a->game.opaque, 1)) return;
    fe2_adlib_play_duration(a, effect, level_duration[level]);
}
void fe2_adlib_update(fe2_adlib *a)
{
    uint16_t next;
    if (!a || a->guard) return;
    a->guard++;
    if (a->hold_ambient) {
        a->hold_ambient--;
        if (!a->hold_ambient) queue_event(a, 0x22);
    }
    if (a->hold_18) { a->hold_18--; if (!a->hold_18) queue_event(a, 0x19); }
    if (a->hold_0d) { a->hold_0d--; if (!a->hold_0d) queue_event(a, 0x0e); }
    if (a->hold_0b) { a->hold_0b--; if (!a->hold_0b) queue_event(a, 0x0c); }
    if (a->hold_1d) { a->hold_1d--; if (!a->hold_1d) queue_event(a, 0x1e); }
    next = (uint16_t)(a->accumulator - 327u);
    if (a->accumulator < 327u) {
        a->accumulator = (uint16_t)(next + 910u);
        drain_queue(a);
        update_voices(a);
        update_level_counter(a);
    } else a->accumulator = next;
    a->guard--;
}
void fe2_adlib_stop_effects(fe2_adlib *a)
{
    if (!a) return;
    clear_holds(a);
}
void fe2_adlib_ambient(fe2_adlib *a, uint16_t level, uint16_t pitch)
{
    uint8_t scaled, distance;
    if (!a || !flag(a->game.effects_enabled, a->game.opaque, 1) || a->current_music >= 0) return;
    a->guard++;
    if (level > 0x3f) level = 0x3f;
    scaled = (uint8_t)(((uint16_t)(pitch - 0xaeu)) >> 2);
    distance = (uint8_t)(0x3fu - scaled);
    if (distance > 0x3f) distance = 0x3f;
    a->image[FE2_ADLIB_AMBIENT_LEVEL] = (uint8_t)(distance << 2);
    a->image[FE2_ADLIB_AMBIENT_PITCH] = (uint8_t)(((uint8_t)(~(uint8_t)level)) & 0x3fu);
    if (!a->hold_ambient) queue_event(a, 0x21);
    a->hold_ambient = 0xc8;
    a->guard--;
}
int16_t fe2_adlib_current_music(const fe2_adlib *a) { return a ? a->current_music : -1; }
