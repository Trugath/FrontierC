/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "opl.h"
#include <string.h>

/* Native C YM3812 synthesizer ported from the assembly by Elliot Stirling. */
static const uint16_t logsin[] = {
		0x859,0x6c3,0x607,0x58b,0x52e,0x4e4,0x4a6,0x471,0x443,0x41a,0x3f5,0x3d3,0x3b5,0x398,0x37e,0x365,
		0x34e,0x339,0x324,0x311,0x2ff,0x2ed,0x2dc,0x2cd,0x2bd,0x2af,0x2a0,0x293,0x286,0x279,0x26d,0x261,
		0x256,0x24b,0x240,0x236,0x22c,0x222,0x218,0x20f,0x206,0x1fd,0x1f5,0x1ec,0x1e4,0x1dc,0x1d4,0x1cd,
		0x1c5,0x1be,0x1b7,0x1b0,0x1a9,0x1a2,0x19b,0x195,0x18f,0x188,0x182,0x17c,0x177,0x171,0x16b,0x166,
		0x160,0x15b,0x155,0x150,0x14b,0x146,0x141,0x13c,0x137,0x133,0x12e,0x129,0x125,0x121,0x11c,0x118,
		0x114,0x10f,0x10b,0x107,0x103,0x0ff,0x0fb,0x0f8,0x0f4,0x0f0,0x0ec,0x0e9,0x0e5,0x0e2,0x0de,0x0db,
		0x0d7,0x0d4,0x0d1,0x0cd,0x0ca,0x0c7,0x0c4,0x0c1,0x0be,0x0bb,0x0b8,0x0b5,0x0b2,0x0af,0x0ac,0x0a9,
		0x0a7,0x0a4,0x0a1,0x09f,0x09c,0x099,0x097,0x094,0x092,0x08f,0x08d,0x08a,0x088,0x086,0x083,0x081,
		0x07f,0x07d,0x07a,0x078,0x076,0x074,0x072,0x070,0x06e,0x06c,0x06a,0x068,0x066,0x064,0x062,0x060,
		0x05e,0x05c,0x05b,0x059,0x057,0x055,0x053,0x052,0x050,0x04e,0x04d,0x04b,0x04a,0x048,0x046,0x045,
		0x043,0x042,0x040,0x03f,0x03e,0x03c,0x03b,0x039,0x038,0x037,0x035,0x034,0x033,0x031,0x030,0x02f,
		0x02e,0x02d,0x02b,0x02a,0x029,0x028,0x027,0x026,0x025,0x024,0x023,0x022,0x021,0x020,0x01f,0x01e,
		0x01d,0x01c,0x01b,0x01a,0x019,0x018,0x017,0x017,0x016,0x015,0x014,0x014,0x013,0x012,0x011,0x011,
		0x010,0x00f,0x00f,0x00e,0x00d,0x00d,0x00c,0x00c,0x00b,0x00a,0x00a,0x009,0x009,0x008,0x008,0x007,
		0x007,0x007,0x006,0x006,0x005,0x005,0x005,0x004,0x004,0x004,0x003,0x003,0x003,0x002,0x002,0x002,
		0x002,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000

};

static const uint16_t exptab[] = {
		8168,8148,8124,8104,8080,8060,8040,8016,
		7996,7972,7952,7932,7908,7888,7864,7844,
		7824,7804,7780,7760,7740,7720,7696,7676,
		7656,7636,7616,7592,7572,7552,7532,7512,
		7492,7472,7452,7432,7412,7392,7372,7352,
		7332,7312,7292,7272,7252,7232,7212,7192,
		7176,7156,7136,7116,7096,7076,7060,7040,
		7020,7000,6984,6964,6944,6928,6908,6888,
		6868,6852,6832,6816,6796,6776,6760,6740,
		6724,6704,6688,6668,6652,6632,6616,6596,
		6580,6560,6544,6524,6508,6492,6472,6456,
		6436,6420,6404,6384,6368,6352,6336,6316,
		6300,6284,6264,6248,6232,6216,6200,6180,
		6164,6148,6132,6116,6100,6080,6064,6048,
		6032,6016,6000,5984,5968,5952,5936,5920,
		5904,5888,5872,5856,5840,5824,5808,5792,
		5776,5760,5744,5732,5716,5700,5684,5668,
		5652,5636,5624,5608,5592,5576,5564,5548,
		5532,5516,5504,5488,5472,5456,5444,5428,
		5412,5400,5384,5368,5356,5340,5328,5312,
		5296,5284,5268,5256,5240,5228,5212,5200,
		5184,5168,5156,5144,5128,5116,5100,5088,
		5072,5060,5044,5032,5020,5004,4992,4976,
		4964,4952,4936,4924,4912,4896,4884,4872,
		4856,4844,4832,4820,4804,4792,4780,4768,
		4752,4740,4728,4716,4704,4688,4676,4664,
		4652,4640,4628,4616,4600,4588,4576,4564,
		4552,4540,4528,4516,4504,4492,4480,4468,
		4456,4444,4432,4420,4408,4396,4384,4372,
		4360,4348,4336,4324,4312,4300,4288,4276,
		4264,4256,4244,4232,4220,4208,4196,4184,
		4176,4164,4152,4140,4128,4120,4108,4096

};

static const uint32_t increments[] = {
		0x00000000, 0x00000000, 0x10101010, 0x10101010,  // 0-3    (0x00-0x03)
		0x10101010, 0x10101010, 0x11101110, 0x11101110,  // 4-7    (0x04-0x07)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 8-11   (0x08-0x0B)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 12-15  (0x0C-0x0F)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 16-19  (0x10-0x13)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 20-23  (0x14-0x17)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 24-27  (0x18-0x1B)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 28-31  (0x1C-0x1F)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 32-35  (0x20-0x23)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 36-39  (0x24-0x27)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 40-43  (0x28-0x2B)
		0x10101010, 0x10111010, 0x11101110, 0x11111110,  // 44-47  (0x2C-0x2F)
		0x11111111, 0x21112111, 0x21212121, 0x22212221,  // 48-51  (0x30-0x33)
		0x22222222, 0x42224222, 0x42424242, 0x44424442,  // 52-55  (0x34-0x37)
		0x44444444, 0x84448444, 0x84848484, 0x88848884,  // 56-59  (0x38-0x3B)
		0x88888888, 0x88888888, 0x88888888, 0x88888888   // 60-63  (0x3C-0x3F)

};

static const uint8_t slot_of[9] = {0,1,2,8,9,10,16,17,18};
static const uint8_t multiple[16] = {1,2,4,6,8,10,12,14,16,18,20,20,24,24,30,30};

/* Arithmetic shift without depending on the host's signed right shift. */
static int sar(int value, unsigned bits)
{
    return value >= 0 ? value / (int)(1u << bits) : -1 - (-1 - value) / (int)(1u << bits);
}

void fe2_opl_init(fe2_opl *chip)
{
    unsigned slot;
    if (!chip) return;
    memset(chip, 0, sizeof *chip);
    chip->noise = 1;
    for (slot = 0; slot < 22; ++slot) { chip->env[slot] = 1023; chip->stage[slot] = 3; }
}

static void keys(fe2_opl *chip)
{
    static const uint8_t rhythm_slot[6] = {16,19,17,20,18,21};
    static const uint8_t rhythm_mask[6] = {16,16,1,8,4,2};
    unsigned channel, n;
    for (channel = 0; channel < 9; ++channel) {
        uint8_t on = (chip->reg[0xb0u + channel] >> 5) & 1u;
        chip->key[slot_of[channel]] = chip->key[slot_of[channel] + 3] = on;
    }
    if (chip->reg[0xbd] & 0x20u) for (n = 0; n < 6; ++n)
        chip->key[rhythm_slot[n]] |= (chip->reg[0xbd] & rhythm_mask[n]) != 0;
}

void fe2_opl_write(fe2_opl *chip, uint8_t reg, uint8_t value)
{
    if (!chip) return;
    chip->reg[reg] = value;
    if ((reg >= 0xb0 && reg <= 0xb8) || reg == 0xbd) keys(chip);
}

static unsigned effective_rate(unsigned rate, unsigned scale)
{
    if (!rate) return 0;
    rate = rate * 4u + scale;
    return rate > 63 ? 63 : rate;
}

static void envelope(fe2_opl *chip, unsigned slot, unsigned scale)
{
    unsigned adsr = chip->reg[0x60u + slot], sr = chip->reg[0x80u + slot];
    unsigned sustain = (sr >> 4) == 15 ? 992u : (sr >> 4) * 32u;
    unsigned rate, shift, index, increment;
    uint32_t counter;
    if (chip->key[slot] != chip->applied_key[slot]) {
        chip->applied_key[slot] = chip->key[slot];
        if (chip->key[slot]) {
            chip->stage[slot] = 0;
            chip->phase[slot] = 0;
            if (effective_rate(adsr >> 4, scale) >= 62) chip->env[slot] = 0;
        } else chip->stage[slot] = 3;
    }
    if (chip->stage[slot] == 0 && !chip->env[slot]) chip->stage[slot] = 1;
    if (chip->stage[slot] == 1 && chip->env[slot] >= (int)sustain) chip->stage[slot] = 2;
    rate = chip->stage[slot] == 0 ? adsr >> 4 : chip->stage[slot] == 1 ? adsr & 15u :
        chip->stage[slot] == 2 && (chip->reg[0x20u + slot] & 0x20u) ? 0 : sr & 15u;
    rate = effective_rate(rate, scale);
    shift = rate >> 2;
    counter = chip->clock << shift;
    if (counter & 2047u) return;
    index = (counter >> (shift <= 11 ? 11 : shift)) & 7u;
    increment = (increments[rate] >> (index * 4u)) & 15u;
    if (chip->stage[slot] == 0) {
        if (rate < 62) chip->env[slot] += sar((-1 - chip->env[slot]) * (int)increment, 4);
    } else {
        chip->env[slot] += (int)increment;
        if (chip->env[slot] > 1023) chip->env[slot] = 1023;
    }
}

static unsigned advance(fe2_opl *chip, unsigned slot, unsigned fnum, unsigned block, int pm, unsigned am)
{
    static const uint8_t key_level[16] = {0,24,32,37,40,43,45,47,48,50,51,52,53,54,55,56};
    unsigned character = chip->reg[0x20u + slot];
    unsigned scale = block * 2u + ((fnum >> (9u - ((chip->reg[8] >> 6) & 1u))) & 1u);
    unsigned frequency = fnum * 4u, ksl = chip->reg[0x40u + slot] >> 6;
    unsigned attenuation;
    int level = (int)key_level[fnum >> 6] - (int)(8u * (7u - block));
    if (!(character & 0x10u)) scale >>= 2;
    envelope(chip, slot, scale);
    if (character & 0x40u) frequency = (frequency + (unsigned)sar(pm * (int)(fnum >> 7), 1)) & 4095u;
    chip->phase[slot] += (((frequency << block) >> 2) * multiple[character & 15u]) >> 1;
    attenuation = (unsigned)chip->env[slot] + (chip->reg[0x40u + slot] & 63u) * 8u;
    if (ksl && level > 0) attenuation += (unsigned)level << (ksl == 1 ? 2 : ksl == 2 ? 1 : 3);
    if (character & 0x80u) attenuation += am;
    return (attenuation > 1023 ? 1023 : attenuation) * 4u;
}

static int output(const fe2_opl *chip, unsigned slot, unsigned phase, unsigned attenuation)
{
    unsigned shape = chip->reg[1] & 0x20u ? chip->reg[0xe0u + slot] & 3u : 0;
    unsigned quarter;
    int negative;
    if (chip->env[slot] > 896) return 0;
    phase &= 1023u;
    negative = (phase & 512u) != 0;
    quarter = phase & 255u;
    if (phase & 256u) quarter = 255u - quarter;
    if ((shape == 1 && negative) || (shape == 3 && (phase & 256u))) quarter = 0;
    attenuation += logsin[quarter];
    {
        int sample = exptab[attenuation & 255u] >> (attenuation >> 8);
        return shape == 0 && negative ? -sample : sample;
    }
}

/* One chip sample at master clock / 72. Also exposed for chip-reference
 * comparisons, before the host's 44.1 kHz sample-rate conversion. */
int16_t fe2_opl_clock(fe2_opl *chip)
{
    static const int8_t pm_scale[8] = {8,4,0,-4,-8,-4,0,4};
    unsigned attenuation[22] = {0}, channel, phase7, phase8, select, noise;
    unsigned am_counter = chip->am_counter++, pm_counter = chip->pm_counter++;
    unsigned am = (am_counter < 6720u ? am_counter : 13503u - am_counter) >>
        ((chip->reg[0xbd] & 0x80u) ? 7 : 9);
    int pm = sar(pm_scale[(pm_counter >> 10) & 7u], (chip->reg[0xbd] & 0x40u) ? 0 : 1);
    int mix = 0;
    uint32_t shifted;
    if (chip->am_counter == 13440) chip->am_counter = 0;
    ++chip->clock;
    shifted = chip->noise << 1;
    chip->noise = shifted | (((shifted >> 23) ^ (shifted >> 9) ^ (shifted >> 8) ^ (shifted >> 1)) & 1u);
    for (channel = 0; channel < 9; ++channel) {
        unsigned block = (chip->reg[0xb0u + channel] >> 2) & 7u;
        unsigned fnum = chip->reg[0xa0u + channel] | ((chip->reg[0xb0u + channel] & 3u) << 8);
        unsigned slot = slot_of[channel];
        attenuation[slot] = advance(chip, slot, fnum, block, pm, am);
        attenuation[slot + 3] = advance(chip, slot + 3, fnum, block, pm, am);
    }
    phase7 = chip->phase[17] >> 10; phase8 = chip->phase[21] >> 10;
    select = (((phase7 >> 2) ^ (phase7 >> 7)) | (phase7 >> 3) | ((phase8 >> 5) ^ (phase8 >> 3))) & 1u;
    noise = (chip->noise >> 23) & 1u;
    for (channel = 0; channel < 9; ++channel) {
        unsigned slot = slot_of[channel], connection = chip->reg[0xc0u + channel];
        unsigned feedback = (connection >> 1) & 7u;
        int previous = chip->feedback[channel], modulator, carrier;
        int mod = feedback ? sar(previous + chip->feedback_previous[channel], 10u - feedback) : 0;
        chip->feedback_previous[channel] = previous;
        if ((chip->reg[0xbd] & 0x20u) && channel == 7) {
            unsigned hat = (select << 9) | (0xd0u >> (2u * (noise ^ select)));
            unsigned snare = (0x100u << ((phase7 >> 8) & 1u)) ^ (noise << 8);
            mix += 2 * (sar(output(chip, 17, hat, attenuation[17]), 1) + sar(output(chip, 20, snare, attenuation[20]), 1));
            continue;
        }
        if ((chip->reg[0xbd] & 0x20u) && channel == 8) {
            mix += 2 * (sar(output(chip, 18, chip->phase[18] >> 10, attenuation[18]), 1) +
                sar(output(chip, 21, 0x100u | select << 9, attenuation[21]), 1));
            continue;
        }
        modulator = output(chip, slot, (chip->phase[slot] >> 10) + (unsigned)mod, attenuation[slot]);
        chip->feedback[channel] = modulator;
        if ((chip->reg[0xbd] & 0x20u) && channel == 6) {
            carrier = output(chip, slot + 3, (chip->phase[slot + 3] >> 10) + (unsigned)((connection & 1u) ? 0 : sar(modulator, 1)), attenuation[slot + 3]);
            mix += 2 * sar(carrier, 1);
        } else {
            carrier = output(chip, slot + 3, (chip->phase[slot + 3] >> 10) + (unsigned)((connection & 1u) ? 0 : sar(previous, 1)), attenuation[slot + 3]);
            mix += sar(carrier, 1) + ((connection & 1u) ? sar(previous, 1) : 0);
        }
    }
    if (mix < -32768) return -32768;
    if (mix > 32767) return 32767;
    /* YM3014's ten-bit mantissa discards progressively more low bits. */
    {
        unsigned magnitude = (unsigned)(mix < 0 ? -1 - mix : mix), shift = 0;
        while (magnitude >= 512u) { magnitude >>= 1; ++shift; }
        return (int16_t)(sar(mix, shift) * (int)(1u << shift));
    }
}

void fe2_opl_render(fe2_opl *chip, int16_t *samples, unsigned frames)
{
    unsigned frame;
    if (!chip || !samples) return;
    for (frame = 0; frame < frames; ++frame) {
        chip->sample_clock += 3579545u;
        while (chip->sample_clock >= 72u * FE2_OPL_RATE) {
            chip->sample_clock -= 72u * FE2_OPL_RATE;
            chip->sample = fe2_opl_clock(chip);
        }
        samples[frame] = chip->sample;
    }
}
